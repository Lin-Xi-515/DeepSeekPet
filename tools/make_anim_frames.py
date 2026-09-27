# -*- coding: utf-8 -*-
"""
从动图 pet_wave.webp 导出逐帧透明 PNG（保持统一裁剪框，避免抖动）。

- 输出目录：assets/frames_normal/frame_000.png ...
- 元数据  ：assets/frames_normal/meta.json  {"frame_ms": 100, "count": 49, "size": [w, h]}

用法：python tools/make_anim_frames.py
"""
import json
import os

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ASSETS = os.path.join(ROOT, "assets")
SRC = os.environ.get("PET_ANIM_SRC", os.path.join(ASSETS, "pet_wave.webp"))
OUT = os.path.join(ASSETS, "frames_normal")

TARGET_H = 320


def main():
    if not os.path.isfile(SRC):
        print("找不到动图：", SRC)
        return 1

    im = Image.open(SRC)
    n = getattr(im, "n_frames", 1)
    durations = []
    frames = []
    union = None

    for i in range(n):
        im.seek(i)
        durations.append(int(im.info.get("duration") or 100))
        fr = im.convert("RGBA")
        frames.append(fr)
        a = np.asarray(fr.getchannel("A"))
        bbox = Image.fromarray(np.where(a > 8, 255, 0).astype(np.uint8), "L").getbbox()
        if bbox:
            union = bbox if union is None else (
                min(union[0], bbox[0]), min(union[1], bbox[1]),
                max(union[2], bbox[2]), max(union[3], bbox[3]))

    if union is None:
        union = (0, 0, im.width, im.height)
    m = 2
    box = (max(0, union[0] - m), max(0, union[1] - m),
           min(im.width, union[2] + m), min(im.height, union[3] + m))

    cw, ch = box[2] - box[0], box[3] - box[1]
    sc = TARGET_H / ch
    tw, th = max(1, int(round(cw * sc))), TARGET_H

    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        if f.lower().endswith(".png"):
            os.remove(os.path.join(OUT, f))

    for i, fr in enumerate(frames):
        crop = fr.crop(box)
        # 透明区域的黑边清掉
        arr = np.asarray(crop).copy()
        arr[arr[:, :, 3] < 8, 0:3] = 255
        crop = Image.fromarray(arr, "RGBA")
        crop = crop.resize((tw, th), Image.LANCZOS)
        crop.save(os.path.join(OUT, "frame_%03d.png" % i))

    avg = int(round(sum(durations) / len(durations))) if durations else 100
    meta = {"frame_ms": avg, "count": n, "size": [tw, th], "durations": durations}
    with open(os.path.join(OUT, "meta.json"), "w", encoding="utf-8") as f:
        json.dump(meta, f, ensure_ascii=False, indent=2)

    print(json.dumps({"frames": n, "avg_ms": avg, "crop": box, "out_size": [tw, th],
                      "total_ms": sum(durations)}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
