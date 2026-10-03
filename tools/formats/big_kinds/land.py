"""`land` payload (magic 0xFADEBACC): the level's landscape (docs/formats/big-land.md).

Loader: 0x00448470 Landscape_Load (src/loader/auto_00410ec0.cpp).

  u32 skin_size                       offset of the grid header inside the data below (= the skin's byte size)
  data:
    skin_size bytes                   an ANM_Skin chunk, loaded in place by 0x004eb140 (pointers are offsets
                                      from its own start; u32 +0 = its size, u32 +8 = offset of its 0x30-byte
                                      chunk header, header +0x20 = the skin object, object +0x28 = mesh count)
    grid:
      u32 rest_bytes                  bytes of the grid after this word (not read by the loader; derived)
      u32 nx, ny, nz                  cells per axis (landscape +0x08/+0x0c/+0x10)
      f32 min x, min y, min z         bounds (landscape +0x14/+0x1c/+0x24)
      f32 max x, max y, max z         bounds (landscape +0x18/+0x20/+0x28)
      f32 cell_size                   landscape +0x2c, passed to Grid_Init 0x0048e870
      per mesh (count from the skin object +0x28):
        nx*ny*nz x {u32 first, u32 count}   cell (x, y, z) at ((ny*z + y)*nx + x); first = running sum
        u32 indices[sum of counts]          primitive indices into that mesh (0x00448870)

Dump:
  landscape.anm   the ANM_Skin chunk, byte for byte (opaque here: the ANM serialisation is shared with skins)
  grid.json       dimensions, bounds, cell size and, per mesh, cells[z][y][x] = list of primitive indices
Rebuild recomputes skin_size, rest_bytes and every cell's first/count from the parts.
"""
import json
import struct

KIND = "land"
MAGIC = 0xFADEBACC

F32 = struct.Struct("<f")


def _f(u):
    return F32.unpack(struct.pack("<I", u))[0]


def skin_mesh_count(skin):
    """Mesh count of an ANM_Skin chunk: object (chunk header +0x20) +0x28. 0x00448470 reads it for the grids."""
    header = struct.unpack_from("<I", skin, 8)[0]
    obj = struct.unpack_from("<I", skin, header + 0x20)[0]
    return struct.unpack_from("<I", skin, obj + 0x28)[0]


def skin_name(skin):
    header = struct.unpack_from("<I", skin, 8)[0]
    obj = struct.unpack_from("<I", skin, header + 0x20)[0]
    raw = skin[obj + 0x34:obj + 0x34 + 64]
    return raw.split(b"\0", 1)[0].decode("latin-1")


def parse(payload):
    skin_size = struct.unpack_from("<I", payload, 0)[0]
    data = payload[4:]
    skin = data[:skin_size]
    g = skin_size
    rest_bytes, nx, ny, nz = struct.unpack_from("<4I", data, g)
    if rest_bytes != len(data) - g - 4:
        raise ValueError(f"grid size word {rest_bytes:#x} != {len(data) - g - 4:#x}")
    floats = struct.unpack_from("<7f", data, g + 16)
    cur = g + 44
    ncell = nx * ny * nz
    meshes = []
    for m in range(skin_mesh_count(skin)):
        pairs = struct.unpack_from(f"<{ncell * 2}I", data, cur)
        cur += ncell * 8
        total = pairs[-2] + pairs[-1] if ncell else 0
        idx = struct.unpack_from(f"<{total}I", data, cur)
        cur += total * 4
        cells = []
        run = 0
        for i in range(ncell):
            first, count = pairs[2 * i], pairs[2 * i + 1]
            if first != run:
                raise ValueError(f"mesh {m} cell {i}: first {first} != running sum {run}")
            cells.append(list(idx[first:first + count]))
            run += count
        if run != total:
            raise ValueError(f"mesh {m}: index count {total} != sum of cell counts {run}")
        meshes.append([[cells[(ny * z + y) * nx:(ny * z + y + 1) * nx] for y in range(ny)] for z in range(nz)])
    if cur != len(data):
        raise ValueError(f"{len(data) - cur} trailing bytes after the grids")
    grid = {
        "skin_name": skin_name(skin),
        "cells_x": nx, "cells_y": ny, "cells_z": nz,
        "min": list(floats[0:3]), "max": list(floats[3:6]), "cell_size": floats[6],
        "meshes": [{"cells": c} for c in meshes],
    }
    return skin, grid


def build(skin, grid):
    if struct.unpack_from("<I", skin, 0)[0] != len(skin):
        raise ValueError(f"landscape.anm: size word {struct.unpack_from('<I', skin, 0)[0]:#x} != file size "
                         f"{len(skin):#x}")
    nx, ny, nz = grid["cells_x"], grid["cells_y"], grid["cells_z"]
    meshes = grid["meshes"]
    want = skin_mesh_count(skin)
    if len(meshes) != want:
        raise ValueError(f"grid.json has {len(meshes)} meshes, the skin has {want} (object +0x28)")
    body = bytearray(struct.pack("<3I", nx, ny, nz))
    body += struct.pack("<7f", *grid["min"], *grid["max"], grid["cell_size"])
    for m, mesh in enumerate(meshes):
        cells = mesh["cells"]
        if len(cells) != nz or any(len(row) != ny for row in cells) or \
                any(len(r) != nx for row in cells for r in row):
            raise ValueError(f"mesh {m}: cells must be [{nz}][{ny}][{nx}] (z, y, x)")
        flat = [c for plane in cells for row in plane for c in row]
        run = 0
        idx = []
        for c in flat:
            body += struct.pack("<2I", run, len(c))
            run += len(c)
            idx.extend(c)
        body += struct.pack(f"<{len(idx)}I", *idx)
    return struct.pack("<I", len(skin)) + skin + struct.pack("<I", len(body)) + bytes(body)


def dump(payload, out):
    skin, grid = parse(payload)
    (out / "landscape.anm").write_bytes(skin)
    # One line per row of x cells keeps the file readable and diffable.
    (out / "grid.json").write_text(_compact(grid))


def _compact(grid):
    head = {k: v for k, v in grid.items() if k != "meshes"}
    lines = ["{"]
    for k, v in head.items():
        lines.append(f" {json.dumps(k)}: {json.dumps(v)},")
    lines.append(' "meshes": [')
    for mi, mesh in enumerate(grid["meshes"]):
        lines.append('  {"cells": [')
        planes = mesh["cells"]
        for zi, plane in enumerate(planes):
            lines.append("   [")
            for yi, row in enumerate(plane):
                sep = "," if yi < len(plane) - 1 else ""
                lines.append("    " + json.dumps(row, separators=(",", ":")) + sep)
            lines.append("   ]" + ("," if zi < len(planes) - 1 else ""))
        lines.append("  ]}" + ("," if mi < len(grid["meshes"]) - 1 else ""))
    lines.append(" ]")
    lines.append("}")
    return "\n".join(lines) + "\n"


def rebuild(src):
    skin = (src / "landscape.anm").read_bytes()
    grid = json.loads((src / "grid.json").read_text())
    return build(skin, grid)
