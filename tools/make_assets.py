# -*- coding: utf-8 -*-
"""
生成桌宠素材：从两张原图得到透明 PNG（assets/），以及程序图标（assets/pet.ico）。
用法：python tools/make_assets.py
"""
import os
import sys
from collections import deque

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ASSETS = os.path.join(ROOT, "assets")
DEBUG = os.path.join(ROOT, "build", "debug")
os.makedirs(ASSETS, exist_ok=True)
os.makedirs(DEBUG, exist_ok=True)

# 原始输入图（用户提供）：图1=常态, 图2=顶锅哭哭
SRC_DIR = os.environ.get("PET_SRC_DIR", HERE)
SRC1 = os.path.join(SRC_DIR, "img1_normal.webp")
SRC2 = os.path.join(SRC_DIR, "img2_low.jpg")

TARGET_H = 320
report = {}


# ---------------------------------------------------------------- 工具
def bfs(mask, seeds):
    h, w = mask.shape
    out = np.zeros((h, w), bool)
    dq = deque()
    for (y, x) in seeds:
        if 0 <= y < h and 0 <= x < w and mask[y, x] and not out[y, x]:
            out[y, x] = True
            dq.append((y, x))
    while dq:
        y, x = dq.popleft()
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            ny, nx = y + dy, x + dx
            if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not out[ny, nx]:
                out[ny, nx] = True
                dq.append((ny, nx))
    return out


def maxf(mask, size):
    return np.asarray(Image.fromarray((mask * 255).astype(np.uint8), "L")
                      .filter(ImageFilter.MaxFilter(size))) > 127


def minf(mask, size):
    return np.asarray(Image.fromarray((mask * 255).astype(np.uint8), "L")
                      .filter(ImageFilter.MinFilter(size))) > 127


def trim_resize(im, margin=2, target_h=TARGET_H):
    box_src = im.getchannel("A").point(lambda v: 255 if v > 8 else 0).getbbox()
    if box_src:
        box = (max(0, box_src[0] - margin), max(0, box_src[1] - margin),
               min(im.width, box_src[2] + margin), min(im.height, box_src[3] + margin))
        im = im.crop(box)
    sc = target_h / im.height
    return im.resize((max(1, int(round(im.width * sc))), target_h), Image.LANCZOS)


def checker(im, s=14):
    chk = Image.new("RGB", im.size, (255, 255, 255))
    dr = ImageDraw.Draw(chk)
    for yy in range(0, im.height, s):
        for xx in range(0, im.width, s):
            if ((xx // s) + (yy // s)) % 2:
                dr.rectangle([xx, yy, xx + s - 1, yy + s - 1], fill=(198, 198, 205))
    chk.paste(im, (0, 0), im)
    return chk


def save(im, name, extra=None):
    p = os.path.join(ASSETS, name)
    im.save(p)
    checker(im).save(os.path.join(DEBUG, "dbg_" + name))
    white = Image.new("RGB", im.size, (255, 255, 255))
    white.paste(im, (0, 0), im)
    white.save(os.path.join(DEBUG, "onwhite_" + name))
    info = {"out": p, "size": list(im.size),
            "transparent_pct": round(float((np.asarray(im.getchannel("A")) < 8).mean() * 100), 1)}
    if extra:
        info.update(extra)
    report[name] = info


# ---------------------------------------------------------------- 图1：自带透明通道
def build_normal():
    im = Image.open(SRC1).convert("RGBA")
    a = np.asarray(im)
    rgb = a[:, :, :3].copy()
    al = a[:, :, 3].copy()
    rgb[al < 8] = 255          # 透明区域的黑边清掉，避免缩放时出现暗边
    out = trim_resize(Image.fromarray(np.dstack([rgb, al]), "RGBA"))
    save(out, "pet_normal.png", {"src": os.path.basename(SRC1)})
    return out


# ---------------------------------------------------------------- 图2：照片抠图
def build_low():
    im = Image.open(SRC2).convert("RGB")
    W, H = im.size
    rgb = np.asarray(im).astype(np.float64)
    wall = np.median(rgb[0:5, :].reshape(-1, 3), axis=0)          # 背景墙
    floor = np.median(rgb[H - 5:H, :].reshape(-1, 3), axis=0)     # 地面

    d_wall = np.sqrt(((rgb - wall) ** 2).sum(2))
    d_floor = np.sqrt(((rgb - floor) ** 2).sum(2))

    # 边缘强度：防止从墙色区域"漏"进主体
    g = rgb.mean(2)
    edge = np.zeros((H, W), np.float32)
    edge[:, 1:-1] = np.abs(g[:, 2:] - g[:, :-2]) * 0.5
    edge[1:-1, :] += np.abs(g[2:, :] - g[:-2, :]) * 0.5

    # 阶段1：从画面四边漫水，识别墙色背景
    p1 = (d_wall <= 92.0) & ~((edge > 26.0) & (d_wall > 45.0))
    seeds = [(0, x) for x in range(W)] + [(H - 1, x) for x in range(W)]
    seeds += [(y, 0) for y in range(H)] + [(y, W - 1) for y in range(H)]
    bg = bfs(p1, seeds)

    # 阶段1b：被主体完全包围的洞（锅上的高光）算作主体
    outside = bfs(~bg, [(0, 0), (0, W - 1), (H - 1, 0), (H - 1, W - 1)])
    holes = (~bg) & (~outside)

    # 阶段2：上方墙色区域继续生长（锅/脸都在这里，用较松的墙色阈值）
    pot_bottom = int(H * 0.62)
    band = np.zeros((H, W), bool)
    band[:pot_bottom, :] = True
    for _ in range(5):
        cand = maxf(bg, 3) & ~bg & band & (d_wall <= 118.0)
        if cand.sum() < 20:
            break
        bg |= cand

    # 阶段3：下方地面区域生长，清掉脚下的地板
    foot_top = int(H * 0.72)
    band = np.zeros((H, W), bool)
    band[foot_top:, :] = True
    for _ in range(6):
        cand = maxf(bg, 3) & ~bg & band & (d_floor <= 150.0)
        if cand.sum() < 20:
            break
        bg |= cand

    bg = np.array(bg, dtype=bool)
    holes = np.array(holes, dtype=bool)

    # 阶段4：形态学清理 + 生成 alpha
    bg = minf(maxf(bg, 3), 3)
    alpha = np.clip((np.minimum(d_wall, d_floor) - 18.0) / (60.0 - 18.0), 0, 1)
    alpha = np.where(bg, alpha, 1.0)
    core = minf(~bg, 3)
    alpha = np.where(core, 1.0, alpha)
    alpha = np.where(bg & minf(bg, 3), 0.0, alpha)

    # 阶段5：边缘去污（用邻近主体颜色替换半透明像素的偏色）
    src = rgb.copy()
    op = (alpha >= 0.99)
    m = Image.fromarray((op * 255).astype(np.uint8), "L")
    c = Image.fromarray(src.astype(np.uint8), "RGB")
    for _ in range(4):
        m = m.filter(ImageFilter.MaxFilter(3))
        c = Image.merge("RGB", [ch.filter(ImageFilter.MaxFilter(3)) for ch in c.split()])
    covered = np.asarray(m) > 127
    carr = np.asarray(c).astype(np.float64)
    bnd = (alpha > 0.02) & (alpha < 0.99) & covered
    out_rgb = np.where(bnd[:, :, None], carr, src)

    al_img = Image.fromarray((alpha * 255).astype(np.uint8), "L").filter(ImageFilter.GaussianBlur(0.5))
    alpha_final = np.where(core, 255, np.asarray(al_img)).astype(np.uint8)
    out = np.dstack([np.clip(out_rgb, 0, 255), alpha_final.astype(np.float64)]).astype(np.uint8)
    sprite = trim_resize(Image.fromarray(out, "RGBA"))
    save(sprite, "pet_low.png", {"src": os.path.basename(SRC2),
                                 "wall": [round(float(v), 1) for v in wall],
                                 "floor": [round(float(v), 1) for v in floor]})
    return sprite


# ---------------------------------------------------------------- 图标
def build_icon(sprite):
    a = sprite.getchannel("A")
    bbox = a.point(lambda v: 255 if v > 8 else 0).getbbox()
    im = sprite.crop(bbox) if bbox else sprite
    side = max(im.size) + 8
    canvas = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    canvas.paste(im, ((side - im.width) // 2, (side - im.height) // 2), im)
    p = os.path.join(ASSETS, "pet.ico")
    canvas.resize((256, 256), Image.LANCZOS).save(
        p, sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    report["pet.ico"] = {"out": p}
    return p


def main():
    normal = build_normal()
    low = build_low()
    # 图标优先用动图第一帧（和桌宠实际形象一致）
    anim = os.path.join(ASSETS, "pet_wave.webp")
    icon_src = normal
    if os.path.isfile(anim):
        try:
            im = Image.open(anim)
            im.seek(0)
            icon_src = trim_resize(im.convert("RGBA"))
        except Exception:
            pass
    build_icon(icon_src)
    import json
    with open(os.path.join(DEBUG, "img_report.json"), "w", encoding="utf-8") as f:
        json.dump(report, f, ensure_ascii=False, indent=2)
    print(json.dumps(report, ensure_ascii=False, indent=2))
    # 最后重新导出动画帧（它也会重写 pet_normal.png / pet.ico，保持形象一致）
    anim_tool = os.path.join(HERE, "make_anim_frames.py")
    if os.path.isfile(anim_tool):
        import subprocess
        print("--- 导出动画帧 ---")
        subprocess.run([sys.executable, anim_tool], check=False)
        strip_tool = os.path.join(HERE, "make_strip.py")
        if os.path.isfile(strip_tool):
            print("--- 打包 strip 图 ---")
            subprocess.run([sys.executable, strip_tool], check=False)


if __name__ == "__main__":
    main()
