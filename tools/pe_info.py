"""Small structured summary of one PE file (recon steps 2-5).

Usage: python tools/pe_info.py <file.exe> [--json]

Reports: hash, linker version, timestamp, image base, entry point and the section it lands in, sections with
entropy, Rich header entries (if present), DRM markers, and imported DLLs with function counts.
Uses pefile when installed; falls back to a minimal parser for headers and sections.
"""
import argparse
import collections
import datetime
import hashlib
import json
import math
import struct
import sys


def entropy(data):
    if not data:
        return 0.0
    c = collections.Counter(data)
    n = len(data)
    return -sum(v / n * math.log2(v / n) for v in c.values())


def minimal(b):
    pe = struct.unpack_from("<I", b, 0x3C)[0]
    nsec = struct.unpack_from("<H", b, pe + 6)[0]
    ts = struct.unpack_from("<I", b, pe + 8)[0]
    osz = struct.unpack_from("<H", b, pe + 20)[0]
    opt = pe + 24
    info = {
        "linker": f"{b[opt + 2]}.{b[opt + 3]}",
        "timestamp": ts,
        "timestamp_utc": datetime.datetime.fromtimestamp(ts, datetime.timezone.utc).isoformat(),
        "entry_rva": struct.unpack_from("<I", b, opt + 16)[0],
        "image_base": struct.unpack_from("<I", b, opt + 28)[0],
        "sections": [],
    }
    s = opt + osz
    for i in range(nsec):
        o = s + i * 40
        name = b[o:o + 8].rstrip(b"\0").decode("latin1")
        vsize, va, rsize, rptr = struct.unpack_from("<IIII", b, o + 8)
        info["sections"].append({
            "name": name, "va": va, "vsize": vsize, "rsize": rsize,
            "entropy": round(entropy(b[rptr:rptr + rsize]), 2),
        })
    ep = info["entry_rva"]
    info["entry_section"] = next((x["name"] for x in info["sections"]
                                  if x["va"] <= ep < x["va"] + max(x["vsize"], x["rsize"])), None)
    return info


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    b = open(args.file, "rb").read()
    info = {"file": args.file, "size": len(b), "sha256": hashlib.sha256(b).hexdigest()}
    info.update(minimal(b))

    markers = [m.decode() for m in (b".bind", b"SecuROM", b".securom", b"steam_api", b"SteamService")
               if m in b]
    info["drm_markers"] = markers
    text = next((x for x in info["sections"] if x["name"] == ".text"), None)
    info["text_encrypted_guess"] = bool(text and text["entropy"] > 7.5)

    try:
        import pefile
        pe = pefile.PE(data=b)
        rich = pe.parse_rich_header()
        if rich:
            vals = rich["values"]
            info["rich"] = [{"prodid": vals[i] >> 16, "build": vals[i] & 0xFFFF, "count": vals[i + 1]}
                            for i in range(0, len(vals), 2)]
        else:
            info["rich"] = None
        imps = {}
        for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
            imps[entry.dll.decode()] = len(entry.imports)
        info["imports"] = imps
    except ImportError:
        info["rich"] = "pefile not installed"
    except Exception as e:  # imports may be unreadable while the file is still wrapped
        info["pefile_error"] = str(e)

    if args.json:
        print(json.dumps(info, indent=2))
        return
    print(f"{info['file']}\n  size {info['size']:,}  sha256 {info['sha256']}")
    print(f"  linker {info['linker']}  built {info['timestamp_utc']}  base 0x{info['image_base']:x}"
          f"  entry 0x{info['entry_rva']:x} in {info['entry_section']}")
    for x in info["sections"]:
        print(f"  {x['name']:<8} va 0x{x['va']:06x} vsize 0x{x['vsize']:06x} entropy {x['entropy']:.2f}")
    print(f"  drm markers: {markers or 'none'}  .text encrypted (guess): {info['text_encrypted_guess']}")
    if isinstance(info.get("rich"), list):
        for r in info["rich"]:
            print(f"  rich prodid {r['prodid']:3d} build {r['build']:5d} x{r['count']}")
    else:
        print(f"  rich: {info.get('rich')}")
    for dll, n in info.get("imports", {}).items():
        print(f"  import {dll}: {n}")


if __name__ == "__main__":
    sys.exit(main())
