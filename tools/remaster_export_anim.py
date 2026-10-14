"""Export one level's skeletons and animations to the remaster engine's format (engine/).

  python tools/remaster_export_anim.py lev_1_m1            -> work/remaster/lev_1_m1/{skeletons,anims}/
  python tools/remaster_export_anim.py lev_1_m1 --check    also writes work/remaster/anim_check/*.png (grunt_1)

Reads <level>_big05_skin.big (skins: bones, inverse bind matrices, palettes, blend indices/weights),
<level>_big03_anim.big (node trees and key blocks) and <level>_big07_obj.big (template slots). Formats and evidence:
docs/formats/big-anim.md (key blocks, evaluation) and docs/formats/big-skin.md (bones, palettes, blend data).

Output (game data: lives under work/, never committed). GAME space throughout: Z up, game units, column vectors
(p' = M p, translation in the 4th column), quaternions (x, y, z, w) w last, in the convention of Quat_ToMatrix
0x004e3260 (the rotation the game actually applies: key-block rotations are conjugated as fn_004ebf90 does).
  skeletons/<skin>.json  {"name", "reference_anim", "bones": [{"name", "parent" (-1 root), "t", "r", "s",
                         "skinned"}], "inverse_bind": [16 floats column-major per bone]}
  anims/<anim>.json      {"name", "source_name", "fps", "duration", "start_tick", "end_tick", "tracks": {node:
                         {"t": [[time, x, y, z]..], "r": [[time, x, y, z, w]..], "s": [[time, x, y, z]..]}},
                         "parents": {node: parent node or null}, "notes": [[time, text]..],
                         "cameras": {node: {"t", "r" (world), "fov": [[time, vertical radians]..], "near", "far"}}}
  anims/index.json       {template: {"slots": {slot: anim}, "skeleton": skin}} for templates whose model is skinned
  anims/by_prefix.json   {character prefix: [anim names]}
The model .bin/.json joints/weights are written by tools/remaster_export.py (write_model), which calls
skin_vertex_weights() and build_skeleton() from here.
"""
import argparse
import json
import math
import pathlib
import struct
import sys

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "formats"))
from big_kinds import obj, objchunk, skin  # noqa: E402
from big_kinds.objchunk import u16, u32  # noqa: E402

TICKS_PER_SECOND = 4800          # root +0x1c = 160 ticks per frame at root +0x20 = 30 fps in every anim
SLOT_ASPECT = 1.702128           # camera +0x154 default (docs/subsystems/camera.md): horizontal -> vertical FOV


def _payload(level, part):
    return (ROOT / "orig" / "pc" / "GameData" / level / f"{level}_{part}.big").read_bytes()[8:]


def f32(b, o, n=1):
    v = struct.unpack_from(f"<{n}f", b, o)
    return v if n > 1 else v[0]


def i32(b, o):
    return struct.unpack_from("<i", b, o)[0]


# ---------------------------------------------------------------------------------------------------------------
# quaternion and matrix helpers (the game's own definitions; addresses in docs/formats/big-anim.md 7)

def quat_unpack32(v):
    """Quat_Unpack32 0x004f1920: smallest-three. bits 0-1 largest component (set to +-1, sign bit 2), the others in
    table 0x0057e7f8 order get bits 3-12 and 13-22 (signed, /511) and 23-31 (signed, /255); then normalised."""
    big = v & 3
    order = ((1, 2, 3), (0, 2, 3), (0, 1, 3), (0, 1, 2))[big]
    q = [0.0] * 4
    q[big] = -1.0 if v & 4 else 1.0
    s = v - (1 << 32) if v & 0x80000000 else v
    e0 = ((v << 19) & 0xFFFFFFFF)
    e0 = (e0 - (1 << 32) if e0 & 0x80000000 else e0) >> 22
    e1 = ((v << 9) & 0xFFFFFFFF)
    e1 = (e1 - (1 << 32) if e1 & 0x80000000 else e1) >> 22
    e2 = s >> 23
    q[order[0]] = e0 * (1 / 511)
    q[order[1]] = e1 * (1 / 511)
    q[order[2]] = e2 * (1 / 255)
    n = math.sqrt(sum(x * x for x in q)) or 1.0
    return [x / n for x in q]


def slerp_raw(q0, q1, t):
    """Quat_SlerpRaw 0x004f15a0: from q0 to q1, no shortest-arc flip."""
    d = q0[0] * q1[0] + q0[1] * q1[1] + q0[2] * q1[2] + q0[3] * q1[3]
    if not d + 1.0 > 1e-4:
        return list(q1)
    if not 1.0 - d > 1e-4:
        w0, w1 = 1.0 - t, t
    else:
        sn = math.sqrt(1.0 - d * d)
        a = math.atan2(sn, d)
        w0, w1 = math.sin((1.0 - t) * a) / sn, math.sin(a * t) / sn
    return [w0 * x + w1 * y for x, y in zip(q0, q1)]


def squad(q, p, a, b, t):
    """Quat_Squad 0x004f16b0: slerp(slerp(q, p, t), slerp(a, b, t), 2t(1 - t))."""
    return slerp_raw(slerp_raw(q, p, t), slerp_raw(a, b, t), 2 * t * (1 - t))


def ease(t, a, b):
    """CurveParam_Ease 0x004eba50."""
    s = a + b
    if t == 0.0 or t == 1.0 or s == 0.0:
        return t
    if s > 1.0:
        a, b = a / s, b / s
    k = 1.0 / (2.0 - a - b)
    if t < a:
        return k / a * t * t
    if 1.0 - b > t:
        return (2 * t - a) * k
    return 1.0 - k / b * (1 - t) ** 2


def quat_to_mat3(q):
    """Quat_ToMatrix 0x004e3260 (column vectors)."""
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (z * z + x * x), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (y * y + x * x)]])


def mat3_to_quat(m):
    """Matrix_ToQuat 0x004e35a0 (same branch structure)."""
    tr = m[0, 0] + m[1, 1] + m[2, 2]
    if tr > 0:
        s = math.sqrt(tr + 1.0)
        r = 0.5 / s
        return [(m[2, 1] - m[1, 2]) * r, (m[0, 2] - m[2, 0]) * r, (m[1, 0] - m[0, 1]) * r, 0.5 * s]
    i = 0
    if m[1, 1] > m[0, 0]:
        i = 1
    if m[2, 2] > m[i, i]:
        i = 2
    j, k = (1, 2, 0)[i], (1, 2, 0)[(1, 2, 0)[i]]
    s = math.sqrt(m[i, i] - (m[k, k] + m[j, j]) + 1.0)
    q = [0.0] * 4
    q[i] = 0.5 * s
    r = 0.5 / s
    q[3] = (m[k, j] - m[j, k]) * r
    q[j] = (m[j, i] + m[i, j]) * r
    q[k] = (m[k, i] + m[i, k]) * r
    return q


def quat_mul_game(a, b):
    """Quat_Mul 0x004f1710(a, b) = b (x) a in Hamilton notation."""
    bx, by, bz, bw = b
    ax, ay, az, aw = a
    return [bw * ax + aw * bx + (by * az - bz * ay), bw * ay + aw * by + (bz * ax - bx * az),
            bw * az + aw * bz + (bx * ay - by * ax), aw * bw - (ax * bx + ay * by + az * bz)]


def trs_matrix(t, q, s):
    """Transform_ToMatrix 0x004e3370 with the flags already applied: M = T R S."""
    m = np.eye(4)
    m[:3, :3] = quat_to_mat3(q) * np.asarray(s)[None, :]
    m[:3, 3] = t
    return m


def decompose(m):
    """M = T R S -> (t, q, s); S may be negative (mirror) when det < 0. Returns the orthogonality residual too."""
    a = m[:3, :3]
    s = np.linalg.norm(a, axis=0)
    if np.linalg.det(a) < 0:
        s = -s
    r = a / s[None, :]
    resid = float(np.abs(r.T @ r - np.eye(3)).max())
    return list(m[:3, 3]), mat3_to_quat(r), list(s), resid


# ---------------------------------------------------------------------------------------------------------------
# key blocks (Reloc_FixupTypedBlock 0x004eeda0 layouts; samplers from the table 0x005cabe0)

class Track:
    def __init__(self, b, p):
        self.b = b
        self.type, self.pre, self.post, self.flags = b[p], b[p + 1], b[p + 2], b[p + 3]
        self.count = u32(b, p + 8)
        self.items = [u32(b, p + 0xC + 4 * i) for i in range(self.count)]
        self.ticks = [i32(b, it) for it in self.items]
        self.const = f32(b, p + 0xC) if self.count == 0 else None   # roll/fov constant without keys (fn_004eb710)

    def find(self, t):
        """AnimTrack_FindKeys 0x004eb780 -> (key A, key B, fraction)."""
        n, first, last = self.count, self.ticks[0], self.ticks[-1]
        rng = last - first
        if not (self.flags & 2) and rng != 0:
            def cmod(x, y):  # C remainder, x >= 0 here
                return x - y * int(x / y)
            if t < first:
                if self.pre in (2, 3):
                    t = last - cmod(last - t, rng)
                elif self.pre == 4:
                    q = int((last - t) / rng)
                    t = last - cmod(last - t, rng)
                    if q & 1:
                        t = last - t + first
            if t > last:
                if self.post in (2, 3):
                    t = cmod(t - first, rng) + first
                elif self.post == 4:
                    q = int((t - first) / rng)
                    t = cmod(t - first, rng) + first
                    if q & 1:
                        t = last - t + first
        hi, lo, idx, mid = n, 0, n, n >> 1
        if hi != mid:
            while True:
                idx = mid
                d = t - self.ticks[mid]
                if d == 0:
                    break
                if d < 0:
                    hi = mid
                else:
                    lo = mid
                mid = ((hi - lo) >> 1) + lo
                if idx == mid:
                    break
        if self.ticks[idx] > t:
            return self.items[-1], self.items[idx], 1.0
        if idx == n - 1:
            return self.items[idx], self.items[0], 0.0
        t0, t1 = self.ticks[idx], self.ticks[idx + 1]
        return self.items[idx], self.items[idx + 1], (t - t0) / (t1 - t0)

    def sample(self, t, span):
        b = self.b
        if self.count == 0:
            return self.const
        a, n, s = self.find(t)
        r = 1.0 - s
        typ = self.type
        if typ == 6:   # 0x004ebd70 linear vec3 {tick, xyz}
            return [f32(b, n + 4 + 4 * i) * s + f32(b, a + 4 + 4 * i) * r for i in range(3)]
        if typ == 3:   # 0x004ebc30 linear scalar {tick, value}
            return f32(b, a + 4) * r + f32(b, n + 4) * s
        if typ in (5, 2):   # 0x004ebd20 / 0x004ebbe0 -> CurveEval_Bezier 0x004eb930
            k = 3 if typ == 5 else 1
            o_p, o_out, o_in = (0x20, 0x14, 0x8) if typ == 5 else (0x10, 0xC, 0x8)
            p0, t0 = f32(b, a + o_p, k), f32(b, a + o_out, k)
            t1, p1 = f32(b, n + o_in, k), f32(b, n + o_p, k)
            if k == 1:
                p0, t0, t1, p1 = (p0,), (t0,), (t1,), (p1,)
            if (u32(b, a + 4) & 0x1C00) == 0x800 or (u32(b, n + 4) & 0x380) == 0x100:
                out = list(p1 if s == 1.0 else p0)
            else:
                dt = i32(b, n) - i32(b, a)
                if dt < 0:
                    dt += span
                h3 = dt / 3.0
                out = [p0[i] * r ** 3 + 3 * s * r * r * (p0[i] + h3 * t0[i]) +
                       3 * s * s * r * (p1[i] + h3 * t1[i]) + p1[i] * s ** 3 for i in range(k)]
            return out if k == 3 else out[0]
        if typ == 1:   # 0x004ebb50 scalar Hermite with ease
            e = ease(s, f32(b, a + 0x14), f32(b, n + 0x10))
            e2, e3 = e * e, e * e * e
            A = 2 * e3 - 3 * e2
            return ((e3 - e2) * f32(b, n + 0x1C) + (e - 2 * e2 + e3) * f32(b, a + 0x18) - A * f32(b, n + 0x20)
                    + (1 + A) * f32(b, a + 0x20))
        if typ == 4:   # 0x004ebc50 vec3 Hermite with ease
            e = ease(s, f32(b, a + 0x14), f32(b, n + 0x10))
            e2, e3 = e * e, e * e * e
            A = 2 * e3 - 3 * e2
            return [(e3 - e2) * f32(b, n + 0x24 + 4 * i) - A * f32(b, n + 0x30 + 4 * i)
                    + (e - 2 * e2 + e3) * f32(b, a + 0x18 + 4 * i) + (1 + A) * f32(b, a + 0x30 + 4 * i)
                    for i in range(3)]
        if typ == 14:  # 0x004ebf40 slerp of packed values {tick, packed}
            return slerp_raw(quat_unpack32(u32(b, a + 4)), quat_unpack32(u32(b, n + 4)), s)
        if typ == 13:  # 0x004ebed0 squad {tick, flags, packed tangent, packed value}
            return squad(quat_unpack32(u32(b, a + 0xC)), quat_unpack32(u32(b, n + 0xC)),
                         quat_unpack32(u32(b, a + 8)), quat_unpack32(u32(b, n + 8)), s)
        if typ == 12:  # 0x004ebe60 squad {tick, 5 dw, packed out-tangent, packed in-tangent, packed value}
            return squad(quat_unpack32(u32(b, a + 0x20)), quat_unpack32(u32(b, n + 0x20)),
                         quat_unpack32(u32(b, a + 0x18)), quat_unpack32(u32(b, n + 0x1C)), s)
        if typ == 9:   # 0x004ebe30 slerp of float quaternions {tick, xyzw}
            return slerp_raw(f32(b, a + 4, 4), f32(b, n + 4, 4), s)
        raise ValueError(f"key block type {typ} not supported")


# ---------------------------------------------------------------------------------------------------------------
# anims

class Node:
    pass


class Anim:
    """One ANM chunk: root (big-anim.md 3.1) and the default tree's node records (3.2)."""

    def __init__(self, ch):
        b, r = ch.data, ch.root
        self.chunk, self.b = ch, b
        self.name = ch.name
        self.start, self.end = u32(b, r + 0x14), u32(b, r + 0x18)
        self.tpf, self.fps = u32(b, r + 0x1C), u32(b, r + 0x20)
        self.span = self.end - self.start
        items = objchunk.dict_items(b, u32(b, r + 0x2C))
        self.nodes, by_ptr = [], {}
        for name, p, _ in items:
            n = Node()
            n.name, n.ptr = name, p
            n.kind, n.layout, n.flags = u16(b, p + 4), u16(b, p + 6), u16(b, p + 8)
            n.pos, n.quat, n.scale = list(f32(b, p + 0x1C, 3)), list(f32(b, p + 0x28, 4)), list(f32(b, p + 0x38, 3))
            n.xflags = u32(b, p + 0x44)
            n.tracks = {}
            for key, o in (("t", 0x48), ("r", 0x4C), ("s", 0x50)):
                q = u32(b, p + o)
                if q:
                    n.tracks[key] = Track(b, q)
            n.parent_ptr, n.target_ptr = u32(b, p + 0x10), u32(b, p + 0x14)
            if n.kind == 1:   # camera block (fn_004ef270, 0x004eb710): +0x9a fov kind, +0x9c fov, +0xa0/+0xa4 near/far
                n.fov_kind, n.fov = u16(b, p + 0x9A), f32(b, p + 0x9C)
                n.near, n.far = f32(b, p + 0xA0), f32(b, p + 0xA4)
                q = u32(b, p + 0xA8)
                n.fov_track = Track(b, q) if q else None
            by_ptr[p] = n
            self.nodes.append(n)
        for n in self.nodes:
            n.parent = by_ptr.get(n.parent_ptr)
            n.target = by_ptr.get(n.target_ptr)
        self.by_name = {n.name.lower(): n for n in self.nodes}
        self.notes = []
        nt = u32(b, r + 0x24)
        if nt and b[nt] == 11:   # note track: items {tick, ?, count, count x {u32, ptr, ptr -> text}}
            for i in range(u32(b, nt + 8)):
                it = u32(b, nt + 0xC + 4 * i)
                for j in range(u32(b, it + 8)):
                    txt = u32(b, it + 0x14 + 12 * j)
                    if txt:
                        self.notes.append((i32(b, it), objchunk.cstr(b, txt)))

    def local(self, n, tick):
        """fn_004ebf90 for layouts 0 and 2 (and the aim layout 3 via world()). Returns (t, q, s) as
        Transform_ToMatrix applies them (flag bits 0x10 / 1 / 0xa / 8 resolved)."""
        pos, quat, scale, f = list(n.pos), list(n.quat), list(n.scale), n.xflags
        if n.layout == 2:
            if "t" in n.tracks:
                pos = n.tracks["t"].sample(tick, self.span)
                f |= 0x10
            if "r" in n.tracks:
                q = n.tracks["r"].sample(tick, self.span)
                quat = [-q[0], -q[1], -q[2], q[3]]
                f |= 1
        elif n.layout == 1:
            raise ValueError(f"{self.name}/{n.name}: layout 1 (stepped transform) not supported")
        if n.layout in (2, 3) and "s" in n.tracks:
            scale = n.tracks["s"].sample(tick, self.span)
            f |= 2
        return self._resolve(pos, quat, scale, f)

    @staticmethod
    def _resolve(pos, quat, scale, f):
        t = pos if f & 0x10 else [0.0, 0.0, 0.0]
        q = quat if f & 1 else [0.0, 0.0, 0.0, 1.0]
        if f & 0xA:
            s = [-x for x in scale] if f & 8 else list(scale)
        else:
            s = [1.0, 1.0, 1.0]
        return t, q, s

    def world(self, n, tick, cache=None):
        cache = {} if cache is None else cache
        if id(n) in cache:
            return cache[id(n)]
        pw = self.world(n.parent, tick, cache) if n.parent else np.eye(4)
        if n.layout == 3:
            m = self._aim(n, tick, pw, cache)
        else:
            m = pw @ trs_matrix(*self.local(n, tick))
        cache[id(n)] = m
        return m

    def aim_local(self, n, tick, pw, cache):
        """fn_004ebf90 layout 3: look-at basis from (position - target), up = z column of the parent's inverse,
        times the roll about local z (Quat_FromAxisAngle of 0x005fb89c = (0, 0, 1, roll))."""
        tw = self.world(n.target, tick, cache) if n.target else np.eye(4)
        inv = np.linalg.inv(pw)
        tgt = (inv @ tw[:, 3])[:3]
        up = inv[:3, 2]
        pos = list(n.pos)
        if "t" in n.tracks:
            pos = n.tracks["t"].sample(tick, self.span)
        z = np.asarray(pos) - tgt
        z /= np.linalg.norm(z) or 1.0
        x = np.cross(up, z)
        x /= np.linalg.norm(x) or 1.0
        y = np.cross(z, x)
        q = mat3_to_quat(np.stack([x, y, z], axis=1))
        if "r" in n.tracks:
            tr = n.tracks["r"]
            roll = tr.sample(tick, self.span) if tr.count else tr.const
            h = roll * 0.5
            q = quat_mul_game([0.0, 0.0, math.sin(h), math.cos(h)], q)
        scale, f = list(n.scale), n.xflags | 0x11
        if "s" in n.tracks:
            scale = n.tracks["s"].sample(tick, self.span)
            f |= 2
        return self._resolve(pos, q, scale, f)

    def _aim(self, n, tick, pw, cache):
        return pw @ trs_matrix(*self.aim_local(n, tick, pw, cache))

    def fov(self, n, tick):
        """0x004eb710 + 0x0040a4d0: radians, converted to a vertical full angle."""
        v = n.fov
        if n.fov_track is not None:
            v = n.fov_track.sample(tick, self.span) if n.fov_track.count else n.fov_track.const
        if n.fov_kind == 0:
            return 2 * math.atan(math.tan(v / 2) / SLOT_ASPECT)
        if n.fov_kind == 1:
            return v
        return math.pi / 2

    def sample_ticks(self, n=None):
        ticks = set(range(self.start, self.end + 1, self.tpf)) | {self.end}
        if n is not None:
            for tr in n.tracks.values():
                ticks |= {t for t in tr.ticks if self.start <= t <= self.end}
        return sorted(ticks)


def _close(a, b, eps=1e-6):
    return all(abs(x - y) <= eps for x, y in zip(a, b))


def _rnd(v):
    return [float(f"{x:.7g}") for x in v]


def _track(samples):
    """[(time, vec)] -> keys; constant -> one key at 0."""
    if all(_close(v, samples[0][1]) for _, v in samples):
        return [[0.0] + _rnd(samples[0][1])]
    return [[round(t, 6)] + _rnd(v) for t, v in samples]


def export_anim(anim):
    secs = lambda t: (t - anim.start) / TICKS_PER_SECOND  # noqa: E731
    tracks, parents, cameras = {}, {}, {}
    for n in anim.nodes:
        parents[n.name] = n.parent.name if n.parent else None
        if n.kind == 1 or n.layout == 3:
            cam = {"t": [], "r": [], "fov": []}
            prev = None
            for tk in anim.sample_ticks(n):
                m = anim.world(n, tk)
                t, q, s, _ = decompose(m)
                if prev is not None and sum(a * b for a, b in zip(prev, q)) < 0:
                    q = [-x for x in q]
                prev = q
                cam["t"].append((secs(tk), t))
                cam["r"].append((secs(tk), q))
                if n.kind == 1:
                    cam["fov"].append((secs(tk), [anim.fov(n, tk)]))
            cam = {k: _track(v) for k, v in cam.items() if v}
            if n.kind == 1:
                cam["fov"] = [[k[0], k[1]] for k in cam["fov"]]
                cam["near"], cam["far"], cam["fov_kind"] = n.near, n.far, n.fov_kind
            cameras[n.name] = cam
            continue
        ts, rs, ss, prev = [], [], [], None
        for tk in anim.sample_ticks(n):
            t, q, s = anim.local(n, tk)
            if prev is not None and sum(a * b for a, b in zip(prev, q)) < 0:
                q = [-x for x in q]   # same rotation; keeps linear/slerp interpolation between samples short
            prev = q
            ts.append((secs(tk), t))
            rs.append((secs(tk), q))
            ss.append((secs(tk), s))
        tr = {"t": _track(ts), "r": _track(rs)}
        if not (len(ss) and all(_close(v, [1, 1, 1]) for _, v in ss)):
            tr["s"] = _track(ss)
        tracks[n.name] = tr
    return {"name": anim.name.lower(), "source_name": anim.name, "source_path": anim.chunk.path,
            "fps": anim.fps, "duration": anim.span / TICKS_PER_SECOND,
            "start_tick": anim.start, "end_tick": anim.end, "ticks_per_second": TICKS_PER_SECOND,
            "tracks": tracks, "parents": parents,
            "notes": [[secs(t), txt] for t, txt in anim.notes], "cameras": cameras}


# ---------------------------------------------------------------------------------------------------------------
# skins

MODEL_BONE = "__model"   # synthetic identity bone for blend index 0 (the unskinned base matrix)
M_BONE, M_NBONE, M_NPAL, M_PAL, M_GRP, M_NGRP, M_STREAMS = 0x8C, 0x88, 0x90, 0x94, 0x84, 0x80, 0x44


def skin_meshes(ch):
    """[(mesh name, mesh ptr)] in dictionary order (the order mesh_submeshes uses)."""
    b = ch.data
    return [(mn, m) for _, tree, _ in objchunk.dict_items(b, u32(b, ch.root + 0x1C))
            for mn, m, _ in objchunk.dict_items(b, tree)]


def skin_bones(ch):
    """{bone name: inverse bind matrix (4x4, column vectors)} over all meshes, plus conflicts."""
    b = ch.data
    names = {i: n for n, _, i in objchunk.dict_items(b, u32(b, ch.root + 0x18))}
    out, conflicts = {}, []
    for _, m in skin_meshes(ch):
        for k in range(u32(b, m + M_NBONE)):
            p = u32(b, m + M_BONE) + k * 0x58
            name = names[u16(b, p + 8)]
            mat = np.array(f32(b, p + 0xC, 16)).reshape(4, 4)
            if name in out and not np.allclose(out[name], mat, atol=1e-4):
                conflicts.append(name)
            out.setdefault(name, mat)
    return out, conflicts


def is_skinned(ch):
    b = ch.data
    for _, m in skin_meshes(ch):
        sb = u32(b, m + M_STREAMS)
        if sb and u32(b, m + M_NBONE) and b[sb + 0xAC + 4 * 13 + 2] and u32(b, sb + 0x70):
            return True
    return False


def uses_base(ch):
    """True when some weighted blend index is 0 (the base matrix, no bone)."""
    b = ch.data
    probe = {MODEL_BONE: 0}
    for _, m in skin_meshes(ch):
        idx = _AnyIndex(probe)
        jw = skin_vertex_weights(ch, m, idx)
        if jw and idx.base_used:
            return True
    return False


class _AnyIndex(dict):
    def __init__(self, d):
        super().__init__(d)
        self.base_used = False

    def __getitem__(self, k):
        if k == MODEL_BONE:
            self.base_used = True
        return 0


# Skins whose meshes carry UBYTE4 blend indices without weights (rigid: each vertex follows the bone of its first
# index) and whose bones the game moves at run time, exported skinned (weight 1 on that bone). Opt-in, because ~40
# lev_1_m1 skins have that layout (collision models, ui objects, the big red arrow) and the engine treats them as
# rigid models today. stig_mgun: base (stand, sandbags), center (pivot) and gun (barrel) by index 3 / 6 / 9; the
# turret's pivot-bone callback 0x00499670 / 0x00498ff0 turns center (docs/subsystems/turret.md).
RIGID_SKINNED = {"stig_mgun_0", "stig_mgun_9"}


def skin_vertex_weights(ch, mesh_ptr, bone_index):
    """Per render vertex of one mesh: (joints [4] skeleton bone indices, weights [4] summing to 1), or None when the
    mesh has no blend data. Blend index = palette slot * 3 (constant registers per matrix, 0x004feb90 uploads 3
    registers per entry from c45); slot -> draw group palette (mesh +0x94, or all mesh bones when the group's palette
    is -1, Skin_BuildPalette 0x004edbe0) -> mesh bone (+0x8c, 0x58 each) -> bone name -> skeleton bone."""
    b = ch.data
    m = mesh_ptr
    sb = u32(b, m + M_STREAMS)
    if not sb:
        return None
    di, dw = tuple(b[sb + 0xAC + 52:sb + 0xB0 + 52]), tuple(b[sb + 0xAC + 4:sb + 0xB0 + 4])
    rigid = di[2] == 5 and not dw[2] and getattr(ch, "name", "").lower() in RIGID_SKINNED
    if not di[2] or (not dw[2] and not rigid) or not u32(b, m + M_NBONE):
        return None
    names = {i: n for n, _, i in objchunk.dict_items(b, u32(b, ch.root + 0x18))}
    mesh_bones = [names[u16(b, u32(b, m + M_BONE) + k * 0x58 + 8)] for k in range(u32(b, m + M_NBONE))]
    pals = []
    for i in range(u32(b, m + M_NPAL)):
        p = u32(b, m + M_PAL) + i * 0x18
        pals.append([u16(b, u32(b, p + 0x14) + 2 * k) for k in range(u32(b, p + 0x10))])
    n = u32(b, sb + 0xC)
    streams = [(u32(b, sb + 0x70 + 20 * s), u32(b, sb + 0x74 + 20 * s)) for s in range(2)]
    vgroup = [None] * n
    for g in range(u32(b, m + M_NGRP)):
        gp = u32(b, m + M_GRP) + g * 0x2C
        first, cnt, pal = u32(b, gp + 0x14), u32(b, gp + 0x18), i32(b, gp + 0x1C)
        for v in range(first, min(first + cnt, n)):
            vgroup[v] = pal
    out = []
    for v in range(n):
        d, st = streams[di[0]]
        if di[2] == 7:      # SHORT4 indices + SHORT4 weights (characters)
            idx = struct.unpack_from("<4h", b, d + v * st + di[1])
        elif di[2] == 6:    # SHORT2 indices + SHORT2 weights
            idx = struct.unpack_from("<2h", b, d + v * st + di[1]) + (0, 0)
        elif di[2] == 5:    # UBYTE4, no weight slot: rigid, first index only (collision meshes)
            idx = tuple(b[d + v * st + di[1]:d + v * st + di[1] + 4])
        else:
            raise ValueError(f"blend index type {di[2]} not handled")
        if dw[2] in (7, 6):
            d, st = streams[dw[0]]
            w = struct.unpack_from(f"<{4 if dw[2] == 7 else 2}h", b, d + v * st + dw[1])
            w = tuple(w) + (0,) * (4 - len(w))
        else:
            w = (32767, 0, 0, 0)
        pal = vgroup[v]
        joints, weights = [], []
        for k in range(4):
            # index = (palette slot + 1) * 3: index 0 is the unskinned base matrix (c42, world * view, no bone),
            # slot s is uploaded at c45 + 3s by 0x004feb90
            slot = idx[k] // 3 - 1
            if not w[k]:
                joints.append(0)
            elif slot < 0:
                joints.append(bone_index[MODEL_BONE])
            else:
                mb = pals[pal][slot] if pal is not None and pal >= 0 else slot
                joints.append(bone_index[mesh_bones[mb]])
            weights.append(w[k] / 32767.0 if w[k] > 0 else 0.0)
        tot = sum(weights)
        weights = [x / tot for x in weights] if tot > 0 else [1.0, 0.0, 0.0, 0.0]
        out.append((joints, weights))
    return out


def rigid_vertex_bones(ch, mesh_ptr):
    """Per render vertex of a mesh whose blend data is UBYTE4 indices without a weight slot (rigid parts, docs/formats/
    big-skin.md 3.8): the skin bone name carrying it, or None for blend index 0 (the base matrix, no bone). None when
    the mesh is not of that kind. The first index byte only (the other three are 0 in every lev_1_m1 mesh of this
    kind); it decodes like the SHORT4 indices of skin_vertex_weights: (palette slot + 1) * 3 through the vertex's
    draw group palette. Checked on lev_1_m1 oilpump_1: every index a multiple of 3 (3..18), within its palette (group
    2's palette is all 6 mesh bones)."""
    b = ch.data
    m = mesh_ptr
    sb = u32(b, m + M_STREAMS)
    if not sb or not u32(b, m + M_NBONE):
        return None
    di, dw = tuple(b[sb + 0xAC + 52:sb + 0xB0 + 52]), tuple(b[sb + 0xAC + 4:sb + 0xB0 + 4])
    if di[2] != 5 or dw[2]:
        return None
    names = {i: n for n, _, i in objchunk.dict_items(b, u32(b, ch.root + 0x18))}
    mesh_bones = [names[u16(b, u32(b, m + M_BONE) + k * 0x58 + 8)] for k in range(u32(b, m + M_NBONE))]
    pals = []
    for i in range(u32(b, m + M_NPAL)):
        p = u32(b, m + M_PAL) + i * 0x18
        pals.append([u16(b, u32(b, p + 0x14) + 2 * k) for k in range(u32(b, p + 0x10))])
    n = u32(b, sb + 0xC)
    d, st = u32(b, sb + 0x70 + 20 * di[0]), u32(b, sb + 0x74 + 20 * di[0])
    vgroup = [None] * n
    for g in range(u32(b, m + M_NGRP)):
        gp = u32(b, m + M_GRP) + g * 0x2C
        first, cnt, pal = u32(b, gp + 0x14), u32(b, gp + 0x18), i32(b, gp + 0x1C)
        for v in range(first, min(first + cnt, n)):
            vgroup[v] = pal
    out = []
    for v in range(n):
        slot = b[d + v * st + di[1]] // 3 - 1
        if slot < 0:
            out.append(None)
            continue
        pal = vgroup[v]
        out.append(mesh_bones[pals[pal][slot] if pal is not None and pal >= 0 else slot])
    return out


# ---------------------------------------------------------------------------------------------------------------
# skeletons

def binds(anim, bone_names):
    """AnimSkinBinding_Create 0x004ed410 / SkinBinding_Create 0x004ed290: every skin bone name found in the tree."""
    return all(n.lower() in anim.by_name for n in bone_names)


def pick_reference(skin_name, inv_binds, anims, templates):
    """The anim whose node tree gives the hierarchy: a slot of a template using this skin that binds (a *_ref slot
    first), else any binding anim with the skin's prefix, else any binding anim."""
    cands = []
    for t in templates.values():
        if skin_name in t["lods"]:
            cands += [s for s in t["slots"] if s.endswith("_ref")] + t["slots"]
    prefix = skin_name.split("_")[0]
    cands += sorted(a for a in anims if a.startswith(prefix + "_") and a.endswith("_ref"))
    cands += sorted(a for a in anims if a.startswith(prefix + "_"))
    cands += sorted(anims)
    for c in cands:
        a = anims.get(c)
        if a is not None and binds(a, inv_binds):
            return a
    return None


def build_skeleton(ch, anims, templates):
    """-> skeleton dict (the skeletons/<skin>.json content) and {bone name: index}."""
    inv_binds, conflicts = skin_bones(ch)
    ref = pick_reference(ch.name.lower(), inv_binds, anims, templates)
    lower = {k.lower(): v for k, v in inv_binds.items()}
    order, parent_of = [], {}
    if ref is not None:
        nodes = [n for n in ref.nodes if n.kind != 1 and n.layout != 3]
        keep = {id(n) for n in nodes}
        for n in nodes:
            p = n.parent
            while p is not None and id(p) not in keep:   # a camera parent (never seen): skip it
                p = p.parent
            parent_of[n.name] = p.name if p is not None else None
        # parents before children
        done = set()

        def visit(n):
            if n.name in done:
                return
            if parent_of[n.name]:
                visit(ref.by_name[parent_of[n.name].lower()])
            done.add(n.name)
            order.append(n.name)
        for n in nodes:
            visit(n)
    for name in inv_binds:
        if name.lower() not in {o.lower() for o in order}:
            order.append(name)
            parent_of[name] = None
    if uses_base(ch):
        order.append(MODEL_BONE)
        parent_of[MODEL_BONE] = None
    index = {n: i for i, n in enumerate(order)}
    world, cache = {}, {}
    for name in order:
        if name.lower() in lower:
            world[name] = np.linalg.inv(lower[name.lower()])
        elif name == MODEL_BONE:
            world[name] = np.eye(4)
        else:
            n = ref.by_name[name.lower()]
            pw = world[parent_of[name]] if parent_of[name] else np.eye(4)
            world[name] = pw @ trs_matrix(*ref.local(n, ref.start))
    bones, inv, worst = [], [], 0.0
    for name in order:
        p = parent_of[name]
        local = (np.linalg.inv(world[p]) @ world[name]) if p else world[name]
        t, q, s, resid = decompose(local)
        worst = max(worst, resid)
        bones.append({"name": name, "parent": index[p] if p else -1, "t": _rnd(t), "r": _rnd(q), "s": _rnd(s),
                      "skinned": name.lower() in lower})
        ib = lower[name.lower()] if name.lower() in lower else np.linalg.inv(world[name])
        inv.append(_rnd(ib.T.reshape(-1)))   # column-major
    sk = {"name": ch.name.lower(), "reference_anim": ref.name.lower() if ref else None, "bones": bones,
          "inverse_bind": inv, "inverse_bind_conflicts": conflicts, "max_shear": worst}
    by_name = dict(index)
    by_name.update({k.lower(): v for k, v in index.items()})
    return sk, by_name


def load_level(level):
    _, achunks = objchunk.split_bank(_payload(level, "big03_anim"), padded=False)
    anims = {}
    for c in achunks:
        try:
            anims[c.name.lower()] = Anim(c)
        except (ValueError, struct.error) as e:
            print(f"anim {c.name}: {e}")
    _, schunks = objchunk.split_bank(_payload(level, "big05_skin"), padded=True)
    skins = {c.name.lower(): c for c in schunks}
    _, tlist = obj.parse(_payload(level, "big07_obj"))
    templates = {t["name"].decode("latin1").lower(): {"lods": [x.decode("latin1").lower() for x in t["lods"]],
                                                      "slots": [x.decode("latin1").lower() for x in t["slots"]]}
                 for t in tlist}
    return anims, skins, templates


_LEVEL_CACHE = {}


def skeleton_for(level, ch, out):
    """For tools/remaster_export.py: build, write skeletons/<skin>.json and return {bone name: index}, or None
    when the skin has no blend data."""
    if not is_skinned(ch):
        return None
    if level not in _LEVEL_CACHE:
        _LEVEL_CACHE[level] = load_level(level)
    anims, _, templates = _LEVEL_CACHE[level]
    sk, index = build_skeleton(ch, anims, templates)
    (out / "skeletons").mkdir(parents=True, exist_ok=True)
    (out / "skeletons" / f"{sk['name']}.json").write_text(objchunk.json_text(sk))
    return index


def prefix_of(name):
    parts = name.lower().split("_")
    if parts[0] == "mover" and len(parts) > 1:
        return parts[2] if parts[1] in ("q", "molmer") and len(parts) > 3 else parts[1]
    return parts[0]


# ---------------------------------------------------------------------------------------------------------------
# verification (CPU skinning of one skin at frame 0 of an anim, PNG with front and side projections)

def skin_positions(ch, sk, anim, tick):
    """Every render vertex of every mesh skinned with world(node) * inverse_bind (fn_004ed5d0), plus the bind
    positions. Bones the anim lacks keep the skeleton's rest local transform."""
    index = {bn["name"]: i for i, bn in enumerate(sk["bones"])}
    world = [None] * len(sk["bones"])
    cache = {}
    for i, bn in enumerate(sk["bones"]):
        n = anim.by_name.get(bn["name"].lower()) if anim else None
        if n is not None:
            local = trs_matrix(*anim.local(n, tick))
        else:
            local = trs_matrix(bn["t"], bn["r"], bn["s"])
        world[i] = (world[bn["parent"]] @ local) if bn["parent"] >= 0 else local
    del cache
    pal = [world[i] @ np.array(sk["inverse_bind"][i]).reshape(4, 4).T for i in range(len(world))]
    s = skin.decode_skin(ch)
    meshes = [m for t in s["trees"] for m in t["meshes"]]
    bind, posed = [], []
    lower = {k.lower(): v for k, v in index.items()}
    lower.update(index)
    for (mn, mp), m in zip(skin_meshes(ch), meshes):
        pos = m.get("_pos")
        jw = skin_vertex_weights(ch, mp, lower)
        if not pos or not jw:
            continue
        for p, (j, w) in zip(pos, jw):
            v = np.array([p[0], p[1], p[2], 1.0])
            acc = sum(wk * (pal[jk] @ v) for jk, wk in zip(j, w) if wk)
            bind.append(p)
            posed.append(acc[:3])
    return np.array(bind), np.array(posed)


def plot(path, title, sets):
    from PIL import Image, ImageDraw
    cols = len(sets) * 2
    cw, H, pad = 420, 640, 30
    W = cw * cols
    img = Image.new("RGB", (W, H), "white")
    dr = ImageDraw.Draw(img)
    allp = np.concatenate([p for _, p, _ in sets])
    lo, hi = allp.min(axis=0), allp.max(axis=0)
    sc = min((cw - 2 * pad) / max(hi[0] - lo[0], hi[1] - lo[1]), (H - 2 * pad - 30) / (hi[2] - lo[2]))
    for si, (label, p, colour) in enumerate(sets):
        for vi, (ax, axname) in enumerate(((0, "front: X right, Z up"), (1, "side: Y right, Z up"))):
            x0 = (si * 2 + vi) * cw
            dr.text((x0 + 5, 5), label, fill="black")
            dr.text((x0 + 5, 18), axname, fill="black")
            gz = H - pad - (0 - lo[2]) * sc
            dr.line([(x0, gz), (x0 + cw, gz)], fill=(200, 200, 200))
            for q in p:
                x = x0 + cw / 2 + (q[ax] - (lo[ax] + hi[ax]) / 2) * sc
                y = H - pad - (q[2] - lo[2]) * sc
                dr.point((x, y), fill=colour)
    dr.text((5, H - 15), title, fill="black")
    path.parent.mkdir(parents=True, exist_ok=True)
    img.save(path)


def check(level, anims, skins, templates, skin_name="grunt_1", anim_names=("grunt_ref", "grunt_run",
                                                                              "grunt_attention", "grunt_die_1")):
    ch = skins[skin_name]
    sk, _ = build_skeleton(ch, anims, templates)
    outdir = ROOT / "work" / "remaster" / "anim_check"
    report = {}
    bind, rest = skin_positions(ch, sk, None, 0)
    report["rest_vs_bind_max_err"] = float(np.abs(rest - bind).max())
    report["bind_bbox"] = [bind.min(axis=0).round(3).tolist(), bind.max(axis=0).round(3).tolist()]
    sets = [("bind", bind, (0, 0, 0))]
    for an in anim_names:
        a = anims.get(an)
        if not a:
            continue
        _, posed = skin_positions(ch, sk, a, a.start)
        d = np.linalg.norm(posed - bind, axis=1)
        report[an] = {"bbox": [posed.min(axis=0).round(3).tolist(), posed.max(axis=0).round(3).tolist()],
                      "max_vertex_move": float(d.max()), "median_vertex_move": float(np.median(d))}
        sets.append((f"{an} f0", posed, (200, 0, 0)))
    for i in range(1, len(sets), 2):
        chunk = [sets[0]] + sets[i:i + 2]
        plot(outdir / f"{skin_name}_{'_'.join(s[0].split()[0] for s in chunk[1:])}.png",
             f"{level} {skin_name}: black = bind pose (model space), red = CPU-skinned frame 0", chunk)
    return report


# ---------------------------------------------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("level")
    ap.add_argument("--out")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    level = a.level
    out = pathlib.Path(a.out) if a.out else ROOT / "work" / "remaster" / level
    anims, skins, templates = load_level(level)
    _LEVEL_CACHE[level] = (anims, skins, templates)
    (out / "skeletons").mkdir(parents=True, exist_ok=True)
    (out / "anims").mkdir(parents=True, exist_ok=True)
    sk_count = 0
    for name, ch in skins.items():
        if is_skinned(ch):
            sk, _ = build_skeleton(ch, anims, templates)
            (out / "skeletons" / f"{name}.json").write_text(objchunk.json_text(sk))
            sk_count += 1
            if sk["inverse_bind_conflicts"] or sk["reference_anim"] is None:
                print(f"skeleton {name}: reference {sk['reference_anim']}, conflicts {sk['inverse_bind_conflicts']}")
    failed = []
    for name, an in anims.items():
        try:
            (out / "anims" / f"{name}.json").write_text(json.dumps(export_anim(an), separators=(",", ":")))
        except (ValueError, ZeroDivisionError) as e:
            failed.append(f"{name}: {e}")
    index = {}
    for tname, t in templates.items():
        model = t["lods"][0] if t["lods"] else None
        if model in skins and is_skinned(skins[model]):
            index[tname] = {"slots": {s: s for s in t["slots"] if s in anims}, "skeleton": model}
    (out / "anims" / "index.json").write_text(json.dumps(index, indent=1))
    by_prefix = {}
    for name in sorted(anims):
        by_prefix.setdefault(prefix_of(name), []).append(name)
    (out / "anims" / "by_prefix.json").write_text(json.dumps(by_prefix, indent=1))
    print(f"skeletons {sk_count}, anims {len(anims) - len(failed)} of {len(anims)}, characters {len(index)}")
    for f in failed:
        print("  failed:", f)
    if a.check:
        print(json.dumps(check(level, anims, skins, templates), indent=1))


if __name__ == "__main__":
    main()
