# -*- coding: utf-8 -*-
"""把 frames_normal 打包成一张竖排 strip 图（所有帧宽高一致），并校验素材完整性。

输出: assets/frames_normal.png   （宽度=帧宽，高度=帧高*帧数）
      assets/frames_normal.meta.json  {frame_ms, count, w, h}
"""
import json
import os
import sys

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ASSETS = os.path.join(ROOT, "assets")
SRC = os.path.join(ASSETS, "frames_normal")
OUT_PNG = os.path.join(ASSETS, "frames_normal.png")
OUT_META = os.path.join(ASSETS, "frames_normal.meta.json")


def main():
    files = sorted(f for f in os.listdir(SRC) if f.lower().endswith(".png"))
    if not files:
        print("没有帧文件")
        return 1

    frames = []
    bad = []
    for f in files:
        p = os.path.join(SRC, f)
        try:
            im = Image.open(p)
            im.load()
            frames.append((f, im.convert("RGBA")))
        except Exception as e:
            bad.append((f, repr(e)))
    print("读取成功 %d 帧，失败 %d" % (len(frames), len(bad)))
    for f, e in bad[:10]:
        print("  失败:", f, e)
    if not frames:
        return 1

    sizes = {(im.width, im.height) for _, im in frames}
    print("尺寸集合:", sizes)
    w, h = frames[0][1].size

    strip = Image.new("RGBA", (w, h * len(frames)), (0, 0, 0, 0))
    for i, (_, im) in enumerate(frames):
        if im.size != (w, h):
            im = im.resize((w, h), Image.LANCZOS)
        strip.paste(im, (0, i * h))
    strip.save(OUT_PNG)

    meta = {"count": len(frames), "w": w, "h": h, "frame_ms": 22}
    with open(OUT_META, "w", encoding="utf-8") as f:
        json.dump(meta, f, indent=2)
    print("写出:", OUT_PNG, strip.size, "%.2f MB" % (os.path.getsize(OUT_PNG) / 1048576))
    print("写出:", OUT_META, meta)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
