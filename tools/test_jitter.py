# -*- coding: utf-8 -*-
"""专门验证"光标进入图标时抖动一次"这个残留问题。

做法：
  1. 把光标放在图标外的远处（面板收起）
  2. 一步一步、每步 30ms 把光标移进图标中心（模拟真实的进入过程）
  3. 全过程以 20ms 采样窗口高度，统计"状态翻转次数"
  4. 翻转次数应 <= 1 次（收起→展开），不应出现 展开→收起→展开 这种抖动

另外对比：光标扫过图标边界时也不应抖动。
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
    u.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
except Exception:
    u.SetProcessDPIAware()

WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)
u.EnumWindows.argtypes = [WNDENUMPROC, wt.LPARAM]
u.GetWindowRect.argtypes = [wt.HWND, ctypes.POINTER(wt.RECT)]

CFGDIR = os.path.join(os.environ["LOCALAPPDATA"], "DeepSeekPet")
CFG = os.path.join(CFGDIR, "config.ini")
LOG = os.path.join(CFGDIR, "pet.log")
EXE = sys.argv[1] if len(sys.argv) > 1 else os.environ.get(
    "PET_EXE",
    r"D:\my\C&C++\C++\Arithmetic\DeepSeekPet_test\build\DeepSeekPet.exe")


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


def sample_flips(h, duration, step=0.02):
    """采样窗口高度，返回 (高度序列, 翻转次数)"""
    hs = []
    t0 = time.time()
    while time.time() - t0 < duration:
        hs.append(rect(h)[3])
        time.sleep(step)
    flips = 0
    for a, b in zip(hs, hs[1:]):
        if a != b:
            flips += 1
    return hs, flips


def main():
    kill()
    os.makedirs(CFGDIR, exist_ok=True)
    with open(CFG, "w", encoding="utf-8") as f:
        f.write("api_key=sk-dummy\nlow_threshold=1.00\nrefresh_sec=300\nscale=1.00\n"
                "window_x=760\nwindow_y=560\nauto_start=0\ndsh_command=\ndsh_args=\n")
    if os.path.exists(LOG):
        os.remove(LOG)

    p = subprocess.Popen([EXE])
    time.sleep(4.5)
    h = find_pet(p.pid)
    if not h:
        print("[失败] 程序没起来")
        return 1
    print(f"被测: {EXE}")

    l, t, w, hh = rect(h)
    print(f"桌宠窗口: ({l},{t}) {w}x{hh}")

    # 图标实际左边界（用命中测试找）
    probe = None
    for yy in range(t + 40, t + hh - 40, 6):
        for xx in range(l + 40, l + w - 40, 6):
            if u.WindowFromPoint(wt.POINT(xx, yy)) == h:
                probe = (xx, yy)
                break
        if probe:
            break
    cx, cy = probe
    print(f"图标内一点: ({cx},{cy})")

    # 起点：图标上方 200px（明确在外）
    start = (cx, t - 60)
    moveabs(*start)
    time.sleep(1.2)
    collapsed = rect(h)[3]
    print(f"起点在图标外，窗口高={collapsed}")

    # 逐步进入：每步 6px、间隔 25ms（比人手慢一点，模拟"光标进入"的过程）
    print("\n--- 逐步进入图标（每步 6px / 25ms）---")
    y = start[1]
    steps = 0
    while y < cy:
        y = min(y + 6, cy)
        moveabs(cx, y)
        steps += 1
        time.sleep(0.025)
    hs, flips = sample_flips(h, 1.6)
    print(f"  移动 {steps} 步；进入过程+停留 1.6s 内窗口高度翻转次数 = {flips}")
    print(f"  高度取值 = {sorted(set(hs))}")
    ok_entry = flips <= 1

    # 从图标内部退出去
    print("\n--- 逐步退出图标 ---")
    y = cy
    while y > t - 100:
        y -= 6
        moveabs(cx, y)
        time.sleep(0.025)
    hs2, flips2 = sample_flips(h, 1.6)
    print(f"  退出过程+停留 1.6s 内翻转次数 = {flips2}")
    print(f"  高度取值 = {sorted(set(hs2))}")
    ok_exit = flips2 <= 1

    # 静止在图标下方（之前抖动的场景）
    print("\n--- 停在图标正下方 6px ---")
    l2, t2, w2, h2 = rect(h)
    moveabs(l2 + w2 // 2, t2 + h2 - 2 + 6)
    time.sleep(1.0)
    hs3, flips3 = sample_flips(h, 2.5)
    print(f"  2.5s 内翻转次数 = {flips3}  高度取值 = {sorted(set(hs3))}")
    ok_below = flips3 == 0

    kill()
    print("\n========= 结论 =========")
    print(f"  进入图标不抖动 : {'通过' if ok_entry else '失败'} (翻转 {flips} 次)")
    print(f"  退出图标不抖动 : {'通过' if ok_exit else '失败'} (翻转 {flips2} 次)")
    print(f"  停在下方不抖动 : {'通过' if ok_below else '失败'} (翻转 {flips3} 次)")
    return 0 if (ok_entry and ok_exit and ok_below) else 1


if __name__ == "__main__":
    raise SystemExit(main())
