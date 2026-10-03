"""Fallback for kinds without a parser yet: the payload as one file. Lossless but not useful for editing."""
KIND = "raw"


def dump(payload, out):
    (out / "payload.bin").write_bytes(payload)


def rebuild(src):
    return (src / "payload.bin").read_bytes()
