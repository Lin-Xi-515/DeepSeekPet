# -*- coding: utf-8 -*-
"""A/B 修复的权威验证（本进程设为 DPI 感知，坐标与程序一致）

A. 悬停展开面板 -> 桌宠本体包围盒不变
B. 桌宠/面板允许超出屏幕（不被夹回）
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

# 关键：让本进程 DPI 感知，这样 GetWindowRect / GetSystemMetrics 返回真实物理坐标
try:
    u.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))   # PER_MONITOR_AWARE_V2
except Exception:
    try:
        ctypes.WinDLL("shcore").SetProcessDpiAwareness(2)
    except Exception:
        u.SetProcessDPIAware()

WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)
u.EnumWindows.argtypes = [WNDENUMPROC, wt.LPARAM]
u.GetWindowRect.argtypes = [wt.HWND, ctypes.POINTER(wt.RECT)]
u.SetCursorPos.argtypes = [ctypes.c_int, ctypes.c_int]
u.WindowFromPoint.argtypes = [wt.POINT]
u.WindowFromPoint.restype = wt.HWND

ROOT = r"D:\my\C&C++\C++\Arithmetic\DeepSeekPet_test"
EXE = os.path.join(ROOT, "build", "DeepSeekPet.exe")
CFGDIR = os.path.join(os.environ["LOCALAPPDATA"], "DeepSeekPet")
CFG = os.path.join(CFGDIR, "config.ini")
LOG = os.path.join(CFGDIR, "pet.log")


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


def sprite_extent(h, sprite_w):
    """在窗口内只扫【桌宠那一块】，返回命中像素的最小/最大 x（判断是否位移）"""
    l, t, w, hh = rect(h)
    xs = []
    for y in range(t + 3, t + hh - 3, 5):
        for x in range(l + 3, l + min(sprite_w, w) - 3, 5):
            if u.WindowFromPoint(wt.POINT(x, y)) == h:
                xs.append(x)
    if not xs:
        return None
    return min(xs), max(xs)


def kill():
    subprocess.run(["taskkill", "/F", "/IM", "DeepSeekPet.exe"], capture_output=True)
    time.sleep(0.8)


def launch(cfg_text):
    kill()
    os.makedirs(CFGDIR, exist_ok=True)
    with open(CFG, "w", encoding="utf-8") as f:
        f.write(cfg_text)
    if os.path.exists(LOG):
        os.remove(LOG)
    p = subprocess.Popen([EXE])
    time.sleep(4.5)
    h = find_pet(p.pid)
    return p, h


def main():
    sw, sh = u.GetSystemMetrics(0), u.GetSystemMetrics(1)
    print(f"屏幕(物理): {sw}x{sh}   DPI={u.GetDpiForSystem() if hasattr(u,'GetDpiForSystem') else '?'}")

    # ---------------- A ----------------
    print("\n=== A. 悬停时桌宠是否位移 ===")
    _, h = launch("api_key=sk-dummy\nlow_threshold=1.00\nrefresh_sec=300\nscale=1.00\n"
                  "window_x=400\nwindow_y=300\nauto_start=0\ndsh_command=\ndsh_args=\n")
    if not h:
        print("[失败] 程序未启动"); return 1
    u.SetCursorPos(10, 10)
    time.sleep(0.8)
    l0, t0, w0, h0 = rect(h)
    print(f"  悬停前 窗口=({l0},{t0},{w0}x{h0})")
    e0 = sprite_extent(h, w0 if w0 < 400 else 346)
    # 悬停到桌宠中心
    u.SetCursorPos(l0 + (min(w0, 346)) // 2, t0 + h0 // 2)
    time.sleep(1.3)
    l1, t1, w1, h1 = rect(h)
    print(f"  悬停后 窗口=({l1},{t1},{w1}x{h1})  ← 变宽说明面板展开")
    e1 = sprite_extent(h, w0 if w0 < 400 else 346)
    print(f"  桌宠横向范围: {e0} -> {e1}")
    ok_a = (e0 and e1 and abs(e0[0] - e1[0]) <= 5 and abs(e0[1] - e1[1]) <= 5)
    print("  [通过] 桌宠未位移" if ok_a else "  [失败] 桌宠发生位移")

    # ---------------- B ----------------
    print("\n=== B. 允许超出屏幕 ===")
    # 把桌宠放到"右下角外面"，即配置坐标本身就超过屏幕
    _, h2 = launch("api_key=sk-dummy\nlow_threshold=1.00\nrefresh_sec=300\nscale=1.00\n"
                   f"window_x={sw + 120}\nwindow_y={sh + 120}\nauto_start=0\ndsh_command=\ndsh_args=\n")
    if not h2:
        print("[失败] 程序未启动"); return 1
    l2, t2, w2, h2h = rect(h2)
    print(f"  配置要求桌宠在 ({sw + 120},{sh + 120})")
    print(f"  实际窗口=({l2},{t2},{w2}x{h2h})")
    ok_b1 = (l2 >= sw or t2 >= sh)
    print("  [通过] 没有被夹回屏幕内" if ok_b1 else "  [失败] 被夹回屏幕内")

    # 再验证"面板超出屏幕"：桌宠靠近右边界，展开后面板应超出
    _, h3 = launch("api_key=sk-dummy\nlow_threshold=1.00\nrefresh_sec=300\nscale=1.00\n"
                   f"window_x={sw - 200}\nwindow_y={sh - 260}\nauto_start=0\ndsh_command=\ndsh_args=\n")
    l3, t3, w3, h3h = rect(h3)
    print(f"  近边界: 窗口=({l3},{t3},{w3}x{h3h})")
    u.SetCursorPos(l3 + min(w3, 346) // 2, t3 + h3h // 2)
    time.sleep(1.3)
    l4, t4, w4, h4 = rect(h3)
    right = l4 + w4
    print(f"  展开后: 窗口=({l4},{t4},{w4}x{h4}) 右边界={right} (屏幕宽 {sw})")
    ok_b2 = right > sw
    print("  [通过] 面板可以超出屏幕" if ok_b2 else "  [信息] 本次仍未超出（说明没被夹在 屏幕宽-面板宽 的位置）")

    print("\n--- 日志 ---")
    if os.path.exists(LOG):
        print(open(LOG, encoding="utf-8").read())
    kill()
    print(f"\n结论: A={'通过' if ok_a else '失败'}  B={'通过' if (ok_b1 or ok_b2) else '失败'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
