# -*- coding: utf-8 -*-
"""缩放 / 动画 / 面板 测试。"""
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
gdi = ctypes.WinDLL("gdi32", use_last_error=True)

WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)
u.EnumWindows.argtypes = [WNDENUMPROC, wt.LPARAM]
u.GetWindowRect.argtypes = [wt.HWND, ctypes.POINTER(wt.RECT)]
u.PostMessageW.argtypes = [wt.HWND, ctypes.c_uint, ctypes.c_size_t, ctypes.c_size_t]
u.SetCursorPos.argtypes = [ctypes.c_int, ctypes.c_int]
u.WindowFromPoint.argtypes = [wt.POINT]
u.WindowFromPoint.restype = wt.HWND

ROOT = r"D:\my\C&C++\C++\Arithmetic\DeepSeekPet"
EXE = os.path.join(ROOT, "build", "DeepSeekPet.exe")
CFG = os.path.join(os.environ["LOCALAPPDATA"], "DeepSeekPet", "config.ini")
LOG = os.path.join(os.environ["LOCALAPPDATA"], "DeepSeekPet", "pet.log")
WM_MOUSEWHEEL = 0x020A


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


def wheel(h, x, y, up):
    d = 120 if up else -120
    u.PostMessageW(h, WM_MOUSEWHEEL, (d & 0xFFFF) << 16, ((y & 0xFFFF) << 16) | (x & 0xFFFF))


def sprite_point(h, l, t, w, hh):
    """在窗口右侧找属于桌宠的像素"""
    hdc = u.GetDC(0)
    for y in range(t + 30, t + hh - 30, 8):
        for x in range(l + int(w * 0.45), l + w - 8, 8):
            c = gdi.GetPixel(hdc, x, y)
            if c == 0xFFFFFFFF or c < 0:
                continue
            rr, bb = c & 0xFF, (c >> 16) & 0xFF
            if rr < 170 and bb > 60:
                u.ReleaseDC(0, hdc)
                return x, y
    u.ReleaseDC(0, hdc)
    return l + w - 60, t + hh - 40


def main():
    subprocess.run(["taskkill", "/F", "/IM", "DeepSeekPet.exe"], capture_output=True)
    time.sleep(0.8)
    for p in (CFG, LOG):
        if os.path.exists(p):
            os.remove(p)
    with open(CFG, "w", encoding="utf-8") as f:
        f.write("api_key=sk-dummy\nlow_threshold=1.00\nrefresh_sec=300\nscale=1.00\n"
                "window_x=900\nwindow_y=500\nauto_start=0\ndsh_command=\ndsh_args=\n")

    proc = subprocess.Popen([EXE])
    time.sleep(4.0)
    h = find_pet(proc.pid)
    if not h:
        print("找不到桌宠窗口")
        return 1

    l, t, w, hh = rect(h)
    print(f"初始: 窗口 {w}x{hh} @ {l},{t}")
    px, py = sprite_point(h, l, t, w, hh)
    print(f"桌宠像素点: {px},{py}")
    u.SetCursorPos(px, py)
    time.sleep(0.5)
    print("  WindowFromPoint 命中:", u.WindowFromPoint(wt.POINT(px, py)) == h)

    for i in range(3):
        wheel(h, px, py, True)
        time.sleep(0.4)
    time.sleep(1.0)
    l2, t2, w2, h2 = rect(h)
    print(f"滚轮上滚 3 格后: {w2}x{h2}")

    for i in range(5):
        wheel(h, px, py, False)
        time.sleep(0.4)
    time.sleep(1.0)
    l3, t3, w3, h3 = rect(h)
    print(f"滚轮下滚 5 格后: {w3}x{h3}")

    # 悬停到桌宠上（缩放后窗口位置变了，重新定位）
    l5, t5, w5, h5 = rect(h)
    px2, py2 = sprite_point(h, l5, t5, w5, h5)
    u.SetCursorPos(px2, py2)
    time.sleep(1.3)
    l4, t4, w4, h4 = rect(h)
    print(f"悬停后: 窗口 {w4}x{h4}（比 {w5}x{h5} 宽了 {w4 - w5}px）")
    if w4 > w5:
        print("面板展开: OK")
    else:
        print("面板没有展开: 失败")

    time.sleep(1.5)
    print("--- log ---")
    if os.path.exists(LOG):
        with open(LOG, encoding="utf-8") as f:
            print(f.read())
    print("--- config.ini ---")
    if os.path.exists(CFG):
        with open(CFG, encoding="utf-8") as f:
            print(f.read())

    subprocess.run(["taskkill", "/F", "/IM", "DeepSeekPet.exe"], capture_output=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
