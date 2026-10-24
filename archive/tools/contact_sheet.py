"""Tile frames (work/visual/<name>/*.bmp) into one PNG contact sheet with frame numbers, for quick review.

Usage: .venv/Scripts/python.exe archive/tools/contact_sheet.py work/visual/walk [--cols 4] [--every 2] [--scale 0.5]
"""
import argparse
import os

from PIL import Image, ImageDraw


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("folder")
    ap.add_argument("--cols", type=int, default=4)
    ap.add_argument("--every", type=int, default=1)
    ap.add_argument("--scale", type=float, default=0.5)
    ap.add_argument("--first", type=int, default=0)
    ap.add_argument("--last", type=int, default=10 ** 9)
    ap.add_argument("--out")
    a = ap.parse_args()
    names = sorted(n for n in os.listdir(a.folder) if n.endswith(".bmp") and a.first <= int(n[:-4]) <= a.last)
    names = names[::a.every]
    imgs = [Image.open(os.path.join(a.folder, n)).convert("RGB") for n in names]
    w, h = int(imgs[0].width * a.scale), int(imgs[0].height * a.scale)
    rows = (len(imgs) + a.cols - 1) // a.cols
    sheet = Image.new("RGB", (w * a.cols, h * rows))
    for i, (n, im) in enumerate(zip(names, imgs)):
        x, y = (i % a.cols) * w, (i // a.cols) * h
        sheet.paste(im.resize((w, h)), (x, y))
        ImageDraw.Draw(sheet).text((x + 4, y + 4), n[:-4], fill=(255, 255, 0))
    out = a.out or os.path.join(a.folder, "sheet.png")
    sheet.save(out)
    print(out, len(imgs), "frames")


if __name__ == "__main__":
    main()
