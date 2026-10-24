"""BC1 / BC3 block compression (numpy, vectorised) for tools/remaster_textures.py.

Pillow's DDS encoder (until 2026-10-25) put a third of a dark texture's blocks in BC1's 3-colour mode and fitted
endpoints loosely: smooth dark gradients came out posterised with green / teal steps (fx_wind_spray: mean error 1.77,
chroma 1.27), and the lev_5 storm's spray dome, added over a dark scene, showed them as teal blotches with stair-step
edges (findings B1, work/texfix/). This encoder always uses the 4-colour mode, takes the endpoints from the block's
principal axis and refines them by least squares for the chosen indices (fx_wind_spray: 1.15, chroma 0.88).

  encode_bc1(rgb (H, W, 3) uint8) -> bytes      8 bytes per 4x4 block, row-major blocks
  encode_bc3(rgba (H, W, 4) uint8) -> bytes     16 bytes per block: the alpha block (8 interpolated values), then BC1
"""
import numpy as np

# weight of the second endpoint for indices 0..3 in the 4-colour mode (index 2 = (2 c0 + c1) / 3)
_W = np.array([0.0, 1.0, 1 / 3, 2 / 3])


def _to565(c):
    c = np.clip(np.rint(c), 0, 255).astype(np.int32)
    return ((c[..., 0] * 31 + 127) // 255) << 11 | ((c[..., 1] * 63 + 127) // 255) << 5 | (c[..., 2] * 31 + 127) // 255


def _from565(v):
    return np.stack([((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31], -1).astype(np.float64)


def _fit(px, c0, c1):
    """The best index per texel for endpoints c0, c1 (as the decoder will see them) and the block's squared error."""
    e0, e1 = _from565(_to565(c0)), _from565(_to565(c1))
    pal = np.floor(e0[:, None, :] * (1 - _W[None, :, None]) + e1[:, None, :] * _W[None, :, None])
    d = ((px[:, :, None, :] - pal[:, None, :, :]) ** 2).sum(-1)
    idx = d.argmin(-1)
    return idx, np.take_along_axis(d, idx[..., None], -1)[..., 0].sum(-1)


def _colour_blocks(px):
    """px (n, 16, 3) float -> c0, c1 (n,) 565 with c0 >= c1, indices (n, 16)."""
    mean = px.mean(1, keepdims=True)
    x = px - mean
    cov = np.einsum("nki,nkj->nij", x, x)
    v = np.full((len(px), 3), 1 / np.sqrt(3))
    for _ in range(8):  # the principal axis by power iteration
        v = np.einsum("nij,nj->ni", cov, v)
        v /= np.maximum(np.linalg.norm(v, axis=1, keepdims=True), 1e-9)
    proj = np.einsum("nki,ni->nk", x, v)
    best = (mean[:, 0] + proj.max(1)[:, None] * v, mean[:, 0] + proj.min(1)[:, None] * v)
    best_idx, best_err = _fit(px, *best)
    for _ in range(2):  # least squares: px ~ (1 - w) c0 + w c1 for the chosen indices' weights
        w = _W[best_idx]
        a, b, c = ((1 - w) ** 2).sum(1), ((1 - w) * w).sum(1), (w ** 2).sum(1)
        r0, r1 = ((1 - w)[..., None] * px).sum(1), (w[..., None] * px).sum(1)
        det = a * c - b * b
        ok = (np.abs(det) > 1e-9)[:, None]
        dd = np.where(ok, det[:, None], 1.0)
        n0 = np.where(ok, (c[:, None] * r0 - b[:, None] * r1) / dd, best[0])
        n1 = np.where(ok, (a[:, None] * r1 - b[:, None] * r0) / dd, best[1])
        idx, err = _fit(px, n0, n1)
        better = err < best_err
        best = (np.where(better[:, None], n0, best[0]), np.where(better[:, None], n1, best[1]))
        best_idx = np.where(better[:, None], idx, best_idx)
        best_err = np.where(better, err, best_err)
    q0, q1 = _to565(best[0]), _to565(best[1])
    # the 4-colour mode needs c0 > c1: swap the endpoints (and the indices) where needed; equal endpoints: index 0
    swap = q0 < q1
    idx = np.where(swap[:, None], np.array([1, 0, 3, 2])[best_idx], best_idx)
    q0, q1 = np.where(swap, q1, q0), np.where(swap, q0, q1)
    idx = np.where((q0 == q1)[:, None], 0, idx)
    return q0, q1, idx


def _blocks(img):
    """(H, W, C) -> (n, 16, C) float, edge-padded to whole blocks."""
    h, w, ch = img.shape
    H, W = (h + 3) // 4 * 4, (w + 3) // 4 * 4
    p = np.pad(img.astype(np.float64), ((0, H - h), (0, W - w), (0, 0)), mode="edge")
    return p.reshape(H // 4, 4, W // 4, 4, ch).transpose(0, 2, 1, 3, 4).reshape(-1, 16, ch)


def _colour_bytes(px):
    c0, c1, idx = _colour_blocks(px)
    bits = np.zeros(len(px), np.uint32)
    for k in range(16):
        bits |= idx[:, k].astype(np.uint32) << np.uint32(2 * k)
    out = np.zeros((len(px), 8), np.uint8)
    out[:, 0], out[:, 1], out[:, 2], out[:, 3] = c0 & 255, c0 >> 8, c1 & 255, c1 >> 8
    for j in range(4):
        out[:, 4 + j] = (bits >> np.uint32(8 * j)) & 255
    return out


def _alpha_bytes(a):
    """a (n, 16) float -> BC3 alpha blocks (8-value mode: a0 > a1; a flat block a0 = a1, index 0)."""
    hi, lo = np.rint(a.max(1)).astype(np.int64), np.rint(a.min(1)).astype(np.int64)
    pal = np.stack([hi, lo] + [((7 - j) * hi + j * lo) // 7 for j in range(1, 7)], 1).astype(np.float64)
    idx = np.abs(a[:, :, None] - pal[:, None, :]).argmin(-1)
    idx = np.where((hi == lo)[:, None], 0, idx)
    bits = np.zeros(len(a), np.uint64)
    for k in range(16):
        bits |= idx[:, k].astype(np.uint64) << np.uint64(3 * k)
    out = np.zeros((len(a), 8), np.uint8)
    out[:, 0], out[:, 1] = hi, lo
    for j in range(6):
        out[:, 2 + j] = (bits >> np.uint64(8 * j)) & np.uint64(255)
    return out


def encode_bc1(rgb):
    return _colour_bytes(_blocks(np.asarray(rgb)[..., :3])).tobytes()


def encode_bc3(rgba):
    b = _blocks(np.asarray(rgba))
    return np.concatenate([_alpha_bytes(b[..., 3]), _colour_bytes(b[..., :3])], 1).tobytes()
