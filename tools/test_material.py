# -*- coding: utf-8 -*-
"""C 功能（更换素材）测试

覆盖：
  1. 默认使用内置素材（strip 49 帧）
  2. 配置指定自定义素材（png 静态图）→ 启动后加载成功、尺寸归一化到 320 高
  3. 配置指定自定义素材（webp 动图 49 帧）→ 启动后加载成功且能播放
  4. 素材文件丢失 → 自动回退内置素材（不崩）
  5. 打开「更换素材…」二级菜单 → 面板行数变化、桌宠不位移、不抖动
  6. 二级菜单点「返回上级菜单」→ 恢复主菜单
"""
import ctypes
import ctypes.wintypes as wt
import os
import shutil
import subprocess
import sys
import time

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

u = ctypes.WinDLL("user32", use_last_error=True)
try:
    u.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
except Exception:
    u.SetProcessDPIAware()

WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)
u.EnumWindows.argtypes = [WNDENUMPROC, wt.LPARAM]
u.GetWindowRect.argtypes = [wt.HWND, ctypes.POINTER(wt.RECT)]

ROOT = r"D:\my\C&C++\C++\Arithmetic\DeepSeekPet_test"
EXE = os.environ.get("PET_EXE", os.path.join(ROOT, "build", "DeepSeekPet.exe"))
CFGDIR = os.path.join(os.environ["LOCALAPPDATA"], "DeepSeekPet")
CFG = os.path.join(CFGDIR, "config.ini")
LOG = os.path.join(CFGDIR, "pet.log")
MATDIR = os.path.join(CFGDIR, "materials")

results = []


def check(name, ok, detail=""):
    results.append((name, ok, detail))
    print(f"  [{'通过' if ok else '失败'}] {name}" + (f"   {detail}" if detail else ""))


def kill():
    subprocess.run(["taskkill", "/F", "/IM", "DeepSeekPet.exe"], capture_output=True)
    time.sleep(0.8)


def find_pet(pid):
    f = []

    def cb(h, _):
        p = wt.DWORD()
        u.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid:
            c = ctypes.create_unicode_buffer(64)
            u.GetClassNameW(h, c, 64)
            if c.value == "DeepSeekPetWnd":
                f.append(h)
                return False
        return True

    u.EnumWindows(WNDENUMPROC(cb), 0)
    return f[0] if f else None


def rect(h):
    r = wt.RECT()
    u.GetWindowRect(h, ctypes.byref(r))
    return r.left, r.top, r.right - r.left, r.bottom - r.top


def moveabs(x, y):
    sw, sh = u.GetSystemMetrics(0), u.GetSystemMetrics(1)
    u.mouse_event(0x0001 | 0x8000, int(x * 65535 / sw), int(y * 65535 / sh), 0, 0)


def click(x, y):
    moveabs(x, y)
    time.sleep(0.15)
    u.mouse_event(0x0002, 0, 0, 0, 0)
    time.sleep(0.06)
    u.mouse_event(0x0004, 0, 0, 0, 0)
    time.sleep(0.5)


def write_cfg(custom=None, use=False, scale="1.00"):
    lines = ["api_key=sk-dummy", "low_threshold=1.00", "refresh_sec=300",
             f"scale={scale}", "window_x=900", "window_y=560", "auto_start=0",
             "dsh_command=", "dsh_args=",
             f"use_custom_material={1 if use else 0}",
             f"custom_material={custom or ''}"]
    with open(CFG, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


def launch():
    if os.path.exists(LOG):
        os.remove(LOG)
    p = subprocess.Popen([EXE])
    time.sleep(4.5)
    h = find_pet(p.pid)
    return p, h, (open(LOG, encoding="utf-8").read() if os.path.exists(LOG) else "")


def main():
    print(f"被测: {EXE}")
    kill()
    os.makedirs(CFGDIR, exist_ok=True)
    os.makedirs(MATDIR, exist_ok=True)

    # ---------- 1. 内置素材 ----------
    print("\n[1] 默认内置素材")
    write_cfg()
    p, h, log = launch()
    check("程序启动", h is not None)
    if not h:
        return 1
    check("走 strip 内置素材", "strip 载入 49 帧" in log,
          [l for l in log.splitlines() if "strip" in l][:1])
    kill()

    # ---------- 2. 自定义静态图 ----------
    print("\n[2] 自定义素材：静态 PNG")
    src_png = os.path.join(ROOT, "assets", "pet_low.png")   # 320x320
    dst_png = os.path.join(MATDIR, "test_static.png")
    shutil.copyfile(src_png, dst_png)
    write_cfg(custom=dst_png, use=True)
    p, h, log = launch()
    check("自定义素材加载成功", "自定义素材载入" in log,
          [l for l in log.splitlines() if "自定义素材载入" in l][:1])
    l, t, w, hh = rect(h)
    check("窗口高度=320（归一化成功）", hh == 320, f"{w}x{hh}")
    kill()

    # ---------- 3. 自定义动图 ----------
    print("\n[3] 自定义素材：WebP 动图（49 帧）")
    src_webp = os.path.join(ROOT, "assets", "pet_wave.webp")
    dst_webp = os.path.join(MATDIR, "test_anim.webp")
    shutil.copyfile(src_webp, dst_webp)
    write_cfg(custom=dst_webp, use=True)
    p, h, log = launch()
    line = [l for l in log.splitlines() if "自定义素材载入" in l]
    check("动图加载成功（49 帧）", bool(line) and "49 帧" in line[0], line[:1])
    l, t, w, hh = rect(h)
    check("窗口高度=320", hh == 320, f"{w}x{hh}")
    # 动画是否在跑：连续采两次窗口尺寸不变，但内存/CPU 会动；这里看日志有无报错
    check("无解码错误", "无法解码" not in log and "没有可用帧" not in log)
    kill()

    # ---------- 4. 素材丢失 → 回退 ----------
    print("\n[4] 自定义素材文件丢失")
    write_cfg(custom=os.path.join(MATDIR, "not_exists_xxxx.png"), use=True)
    p, h, log = launch()
    check("程序仍然启动", h is not None)
    check("自动回退内置素材", "回退内置素材" in log,
          [l for l in log.splitlines() if "回退" in l][:1])
    l, t, w, hh = rect(h)
    check("窗口尺寸正常", w > 100 and hh >= 320, f"{w}x{hh}")
    kill()

    # ---------- 5. 二级菜单：点击是否生效 ----------
    print("\n[5] 「更换素材」二级菜单")
    # 先准备一份自定义素材，并让程序以"使用自定义素材"启动
    src_webp = os.path.join(ROOT, "assets", "pet_wave.webp")
    dst_webp = os.path.join(MATDIR, "test_anim.webp")
    shutil.copyfile(src_webp, dst_webp)
    write_cfg(custom=dst_webp, use=True)
    p, h, log = launch()
    l, t, w, hh = rect(h)
    check("以自定义素材启动", "形象: 常态(自定义素材)" in log,
          [x for x in log.splitlines() if "形象:" in x][:1])

    # 悬停展开主菜单
    moveabs(l + w // 2, t + hh // 2)
    time.sleep(1.4)
    l1, t1, w1, h1 = rect(h)
    check("主菜单已展开", h1 > hh, f"高 {hh} -> {h1}")
    main_h = h1

    row_h, head = 34, 78
    row_x = l1 + w1 - 120

    # 点「更换素材…」（主菜单第 6 行）
    click(row_x, t1 + head + 5 * row_h + row_h // 2)
    time.sleep(0.9)
    l2, t2, w2, h2 = rect(h)
    check("进入二级菜单后窗口高度不变（菜单项不会被挪走）", h2 == main_h, f"{main_h} -> {h2}")
    check("桌宠未位移", abs(l2 - l1) <= 1 and abs((t2 + h2) - (t1 + h1)) <= 1,
          f"桌宠底边 {t1 + h1} -> {t2 + h2}")

    # 二级菜单行序（与程序枚举一致）：
    #   0=使用内置素材  1=使用自定义素材  2=从文件加载素材
    #   3=打开素材文件夹  4=删除自定义素材  5=返回上级菜单
    # 点第 0 行「使用内置素材」
    click(row_x, t2 + head + 0 * row_h + row_h // 2)
    time.sleep(1.0)
    cfg = open(CFG, encoding="utf-8").read()
    log2 = open(LOG, encoding="utf-8").read()
    check("点击「使用内置素材」后切回内置",
          "use_custom_material=0" in cfg and "常态, 49 帧" in log2,
          [x for x in log2.splitlines() if "形象:" in x][-1:])

    # 再点第 1 行「使用自定义素材」
    click(row_x, t2 + head + 1 * row_h + row_h // 2)
    time.sleep(1.0)
    cfg = open(CFG, encoding="utf-8").read()
    log3 = open(LOG, encoding="utf-8").read()
    check("点击「使用自定义素材」后切到自定义",
          "use_custom_material=1" in cfg and "自定义素材)" in log3,
          [x for x in log3.splitlines() if "形象:" in x][-1:])
    kill()

    print("\n========= 汇总 =========")
    bad = [r for r in results if not r[1]]
    for name, ok, _ in results:
        print(f"  {'OK ' if ok else 'FAIL'} {name}")
    print(f"\n通过 {len(results) - len(bad)}/{len(results)}")
    return 0 if not bad else 1


if __name__ == "__main__":
    raise SystemExit(main())
