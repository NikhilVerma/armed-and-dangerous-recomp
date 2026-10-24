"""Client for the player's local Media Lab API (C:\\video-lab, http://127.0.0.1:8191, OpenAPI at /openapi.json).

Queues one job (POST /api/generate, multipart), waits for it (GET /api/jobs/{id}) and downloads its outputs
(GET /outputs/{subfolder}/{filename}). Standard library only.

Usage: .venv/Scripts/python.exe tools/retex/medialab.py ENGINE OUT_DIR IMAGE [IMAGE...] [--prompt P] [--negative N]
           [--set key=value ...]
   e.g. tools/retex/medialab.py upscale-faithful work/retex/c_realesrgan work/retex/AnD_title_orig.png
           --set upscale_model=RealESRGAN_x4plus.pth --set upscale_factor=4
Prints the downloaded file paths.
"""
import argparse
import json
import mimetypes
import os
import sys
import time
import urllib.parse
import urllib.request
import uuid

BASE = os.environ.get("MEDIALAB_URL", "http://127.0.0.1:8191")


def _multipart(fields, files):
    boundary = uuid.uuid4().hex
    out = []
    for k, v in fields:
        out.append(f"--{boundary}\r\nContent-Disposition: form-data; name=\"{k}\"\r\n\r\n{v}\r\n".encode())
    for k, path in files:
        ctype = mimetypes.guess_type(path)[0] or "application/octet-stream"
        head = (f"--{boundary}\r\nContent-Disposition: form-data; name=\"{k}\"; "
                f"filename=\"{os.path.basename(path)}\"\r\nContent-Type: {ctype}\r\n\r\n").encode()
        with open(path, "rb") as f:
            out.append(head + f.read() + b"\r\n")
    out.append(f"--{boundary}--\r\n".encode())
    return b"".join(out), f"multipart/form-data; boundary={boundary}"


def submit(engine, images, prompt="", negative="", extra=None):
    fields = [("engine", engine)]
    if prompt:
        fields.append(("prompt", prompt))
    if negative:
        fields.append(("negative", negative))
    for k, v in (extra or {}).items():
        fields.append((k, str(v)))
    body, ctype = _multipart(fields, [("images", p) for p in images])
    req = urllib.request.Request(BASE + "/api/generate", data=body, headers={"Content-Type": ctype}, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=120) as r:
            return json.load(r)
    except urllib.error.HTTPError as e:
        raise SystemExit(f"generate failed: {e.code} {e.read().decode(errors='replace')[:500]}")


def wait(job_id, timeout=3600, poll=2.0):
    end = time.time() + timeout
    while time.time() < end:
        with urllib.request.urlopen(f"{BASE}/api/jobs/{job_id}", timeout=60) as r:
            job = json.load(r)
        if job["status"] in ("complete", "partial", "error", "canceled"):
            return job
        time.sleep(poll)
    raise SystemExit(f"job {job_id} timed out")


def download(job, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    paths = []
    for o in job.get("outputs", []):
        rel = "/".join([*o["subfolder"].replace("\\", "/").split("/"), o["filename"]])
        url = f"{BASE}/outputs/{urllib.parse.quote(rel)}"
        dst = os.path.join(out_dir, o["filename"])
        urllib.request.urlretrieve(url, dst)
        paths.append(dst)
    return paths


def run(engine, images, out_dir, prompt="", negative="", extra=None):
    job = submit(engine, images, prompt, negative, extra)
    job = wait(job["id"])
    if job["status"] not in ("complete", "partial"):
        raise SystemExit(f"job {job['id']} {job['status']}: {job.get('error', '')}")
    paths = download(job, out_dir)
    with open(os.path.join(out_dir, "job.json"), "w") as f:
        json.dump(job, f, indent=1)
    return paths


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("engine")
    ap.add_argument("out")
    ap.add_argument("images", nargs="+")
    ap.add_argument("--prompt", default="")
    ap.add_argument("--negative", default="")
    ap.add_argument("--set", action="append", default=[])
    a = ap.parse_args()
    extra = dict(s.split("=", 1) for s in a.set)
    for p in run(a.engine, a.images, a.out, a.prompt, a.negative, extra):
        print(p)


if __name__ == "__main__":
    sys.exit(main())
