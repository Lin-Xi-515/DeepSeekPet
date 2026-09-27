# -*- coding: utf-8 -*-
"""桌宠完整回归测试

逐项验证（全部用真实鼠标输入，避免 SetCursorPos 被窗口重定位带跑）：
  1. 启动与动画
  2. 悬停展开面板：稳定不抖动，桌宠本体不位移
  3. 光标停在图标下方：不抖动
  4. 移开：收起且回到原位
  5. 滚轮缩放：桌宠锚点不跳、比例写盘、面板保持
  6. 允许超出屏幕：桌宠与面板都能出屏，不被夹回

用法: python test_all.py [exe路径]        默认用测试副本
"""
import ctypes
import ctypes.wintypes as wt
import os
import subprocess
import sys
import time

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

u = ctypes.WinDLL("user32", use_last_error=True)
try:
    u.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))   # PerMonitorV2
except Exception:
    try:
        u.SetProcessDPIAware()
    except Exception:
        pass

WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)
u.EnumWindows.argtypes = [WNDENUMPROC, wt.LPARAM]
u.GetWindowRect.argtypes = [wt.HWND, ctypes.POINTER(wt.RECT)]
u.WindowFromPoint.argtypes = [wt.POINT]
u.WindowFromPoint.restype = wt.HWND
u.PostMessageW.argtypes = [wt.HWND, ctypes.c_uint, ctypes.c_size_t, ctypes.c_size_t]

WM_MOUSEWHEEL = 0x020A
CFGDIR = os.path.join(os.environ["LOCALAPPDATA"], "DeepSeekPet")
CFG = os.path.join(CFGDIR, "config.ini")
LOG = os.path.join(CFGDIR, "pet.log")

DEFAULT_EXE = os.environ.get(
    "PET_EXE",
    r"D:\my\C&C++\C++\Arithmetic\DeepSeekPet_test\build\DeepSeekPet.exe")

results = []


def check(name, ok, detail=""):
    results.append((name, ok, detail))
    print(f"  [{'通过' if ok else '失败'}] {name}" + (f"   {detail}" if detail else ""))


def kill():
    subprocess.run(["taskkill", "/F", "/IM", "DeepSeekPet.exe"], capture_output=True)
    time.sleep(0.8)


def find_pet(pid):
    found = []

    def cb(h, _):
        p = wt.DWORD()
        u.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid:
            cls = ctypes.create_unicode_buffer(64)
            u.GetClassNameW(h, cls, 64)
            if cls.value == "DeepSeekPetWnd":
                found.append(h)
                return False
        return True

    u.EnumWindows(WNDENUMPROC(cb), 0)
    return found[0] if found else None


def rect(h):
    r = wt.RECT()
    u.GetWindowRect(h, ctypes.byref(r))
    return r.left, r.top, r.right - r.left, r.bottom - r.top


def moveabs(x, y):
    sw, sh = u.GetSystemMetrics(0), u.GetSystemMetrics(1)
    u.mouse_event(0x0001 | 0x8000, int(x * 65535 / sw), int(y * 65535 / sh), 0, 0)


def stable_height(h, seconds=2.0, step=0.12):
    hs = set()
    t0 = time.time()
    while time.time() - t0 < seconds:
        hs.add(rect(h)[3])
        time.sleep(step)
    return hs


def launch(exe, cfg_text=None):
    kill()
    os.makedirs(CFGDIR, exist_ok=True)
    if cfg_text is not None:
        with open(CFG, "w", encoding="utf-8") as f:
            f.write(cfg_text)
    if os.path.exists(LOG):
        os.remove(LOG)
    p = subprocess.Popen([exe])
    time.sleep(4.5)
    h = find_pet(p.pid)
    return p, h


def sprite_probe_point(h):
    l, t, w, hh = rect(h)
    for yy in range(t + 40, t + hh - 40, 8):
        for xx in range(l + 40, l + w - 40, 8):
            if u.WindowFromPoint(wt.POINT(xx, yy)) == h:
                return xx, yy
    return l + w // 2, t + hh // 2


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_EXE
    if not os.path.exists(exe):
        print("可执行文件不存在:", exe)
        return 1
    print(f"被测程序: {exe}")
    print(f"屏幕: {u.GetSystemMetrics(0)}x{u.GetSystemMetrics(1)}")

    base_cfg = ("api_key=sk-dummy\nlow_threshold=1.00\nrefresh_sec=300\nscale=1.00\n"
                "window_x=900\nwindow_y=500\nauto_start=0\ndsh_command=\ndsh_args=\n")

    # ---------- 1. 启动与动画 ----------
    print("\n[1] 启动与动画")
    p, h = launch(exe, base_cfg)
    check("程序启动", h is not None)
    if not h:
        return 1
    log = open(LOG, encoding="utf-8").read() if os.path.exists(LOG) else ""
    check("素材加载（49 帧 strip）", "49 帧" in log, [l for l in log.splitlines() if "strip" in l][:1])
    l, t, w, hh = rect(h)
    check("窗口尺寸合理", w > 100 and hh > 100, f"{w}x{hh}")

    # ---------- 2. 悬停展开：稳定 + 不位移 ----------
    print("\n[2] 悬停展开面板")
    moveabs(10, 300)
    time.sleep(0.8)
    l0, t0, w0, h0 = rect(h)
    px, py = sprite_probe_point(h)
    moveabs(px, py)
    time.sleep(1.5)
    l1, t1, w1, h1 = rect(h)
    check("面板展开（窗口变大）", w1 > w0, f"{w0}x{h0} -> {w1}x{h1}")
    # 桌宠本体左上角（面板在右侧，桌宠贴窗口底部）
    sx1, sy1 = l1, t1 + (h1 - 320)
    check("桌宠未位移", abs(sx1 - l0) <= 1 and abs(sy1 - t0) <= 1,
          f"({l0},{t0}) -> ({sx1},{sy1})")
    hs = stable_height(h, 2.0)
    check("展开状态稳定（不抖动）", len(hs) == 1, f"高度取值={sorted(hs)}")

    # ---------- 3. 光标停在图标正下方 ----------
    print("\n[3] 光标停在图标正下方")
    moveabs(l1 + min(w1, 346) // 2, t1 + (h1 - 320) + 320 + 6)
    time.sleep(1.0)
    hs = stable_height(h, 3.0)
    check("不抖动", len(hs) == 1, f"高度取值={sorted(hs)}")

    # ---------- 4. 移开：收起并回到原位 ----------
    print("\n[4] 移开光标")
    moveabs(10, u.GetSystemMetrics(1) - 40)
    time.sleep(1.5)
    l2, t2, w2, h2 = rect(h)
    check("面板收起", w2 == w0 and h2 == h0, f"{w2}x{h2}")
    check("回到原位", abs(l2 - l0) <= 1 and abs(t2 - t0) <= 1, f"({l2},{t2})")
    hs = stable_height(h, 2.0)
    check("收起状态稳定", len(hs) == 1, f"高度取值={sorted(hs)}")

    # ---------- 5. 滚轮缩放 ----------
    print("\n[5] 滚轮缩放")
    import re

    def read_zoom_pairs():
        """从日志里取每次缩放的 (中心x, 底边y, w, h)

        程序每次缩放会写一行：
          缩放 -> 145% (502x464) 桌宠中心x=1073 底边y=820
        若锚点不变，那么所有行的 中心x / 底边y 都应该一样。
        """
        if not os.path.exists(LOG):
            return []
        pat = re.compile(r"缩放 -> (\d+)% \((\d+)x(\d+)\) 桌宠中心x=(-?\d+) 底边y=(-?\d+)")
        out = []
        for line in open(LOG, encoding="utf-8"):
            m = pat.search(line)
            if m:
                pct, w, h, cx, by = (int(x) for x in m.groups())
                out.append((cx, by, w, h, pct))
        return out

    def wheel(up):
        d = 120 if up else -120
        u.PostMessageW(h, WM_MOUSEWHEEL, (d & 0xFFFF) << 16,
                       ((py & 0xFFFF) << 16) | (px & 0xFFFF))

    # 放大 3 格、再缩小 2 格，覆盖两个方向
    for _ in range(3):
        wheel(True)
        time.sleep(0.5)
    time.sleep(0.8)
    for _ in range(2):
        wheel(False)
        time.sleep(0.5)
    time.sleep(1.0)

    log = open(LOG, encoding="utf-8").read() if os.path.exists(LOG) else ""
    check("出现缩放日志", "缩放 ->" in log,
          [l for l in log.splitlines() if "缩放 ->" in l][-1:])

    seq = read_zoom_pairs()
    check("记录到多次缩放", len(seq) >= 3, f"{len(seq)} 次")
    detail = [f"{pct}% {w}x{h} 中心{cx} 底边{by}" for cx, by, w, h, pct in seq[-4:]]
    if seq:
        cxs = {s[0] for s in seq}
        bys = {s[1] for s in seq}
        sizes = {(s[2], s[3]) for s in seq}
        ok_anchor = len(cxs) == 1 and len(bys) == 1
        check("缩放锚点不动（中心x / 底边y）", ok_anchor,
              f"中心x取值={sorted(cxs)} 底边y取值={sorted(bys)} 尺寸数={len(sizes)}")
    else:
        check("缩放锚点不动（中心x / 底边y）", False, "取不到日志")

    # 缩放比例写盘（延迟 0.5s 保存）
    time.sleep(1.2)
    cfg = open(CFG, encoding="utf-8").read() if os.path.exists(CFG) else ""
    check("缩放比例已写盘", any(l.startswith("scale=") and l != "scale=1.00"
                                for l in cfg.splitlines()),
          [l for l in cfg.splitlines() if l.startswith("scale")])

    # ---------- 6. 允许超出屏幕 ----------
    print("\n[6] 允许超出屏幕")
    sw, sh = u.GetSystemMetrics(0), u.GetSystemMetrics(1)
    p2, h2w = launch(exe, base_cfg.replace("window_x=900", f"window_x={sw + 150}")
                              .replace("window_y=500", f"window_y={sh + 150}"))
    if h2w:
        l5, t5, w5, h5 = rect(h2w)
        check("桌宠可位于屏幕外", l5 >= sw or t5 >= sh, f"窗口=({l5},{t5})")
    else:
        check("桌宠可位于屏幕外", False, "未启动")

    # 近右边界：展开后面板应出屏
    p3, h3w = launch(exe, base_cfg.replace("window_x=900", f"window_x={sw - 200}")
                              .replace("window_y=500", f"window_y={sh - 300}"))
    if h3w:
        px, py = sprite_probe_point(h3w)
        moveabs(px, py)
        time.sleep(1.5)
        l6, t6, w6, h6 = rect(h3w)
        check("面板可超出屏幕", l6 + w6 > sw, f"右边界={l6 + w6} 屏幕宽={sw}")
    else:
        check("面板可超出屏幕", False, "未启动")

    kill()

    print("\n========= 汇总 =========")
    bad = [r for r in results if not r[1]]
    for name, ok, detail in results:
        print(f"  {'✔' if ok else '�’'} {name}")
    print(f"\n通过 {len(results) - len(bad)}/{len(results)}")
    return 0 if not bad else 1


if __name__ == "__main__":
    raise SystemExit(main())
