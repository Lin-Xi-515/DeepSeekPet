// ============================================================================
//  DeepSeek 桌宠 (DeepSeekPet)
//
//  · 悬浮在桌面上的透明置顶小窗口，鼠标悬浮显示余额面板
//  · 单击桌宠固定 / 取消固定面板；按住可拖动；滚轮 / 菜单可缩放
//  · 余额低于阈值（默认 1 元）时自动切换为"顶锅哭哭"形象
//  · 面板内可一键启动 DeepSeek Harness
//
//  纯 Win32 + GDI+ + WinHTTP 实现，无第三方依赖。
//  构建：build.bat
// ============================================================================

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <winhttp.h>
#include <objidl.h>
#include <objbase.h>
#include <propidl.h>
#include <shtypes.h>
#include <shobjidl.h>
#include <gdiplus.h>

#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <cstdarg>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

#include "wicdecoder.h"   // WIC 解码（自定义素材：png/jpg/gif/webp）

using namespace Gdiplus;
using std::wstring;

// ============================================================ 常量 / 资源
#define IDI_PET         101
#define IDR_PET_NORMAL  201
#define IDR_PET_LOW     202

#define WM_TRAYICON     (WM_APP + 1)
#define WM_BALANCE_DONE (WM_APP + 2)
#define WM_SECOND_INST  (WM_APP + 10)

#define IDM_TRAY_SHOW    40001
#define IDM_TRAY_REFRESH 40002
#define IDM_TRAY_CONFIG  40003
#define IDM_TRAY_QUIT    40004

#define IDC_KEY     1001
#define IDC_THRESH  1002
#define IDC_REFRESH 1003
#define IDC_DSH     1004
#define IDC_AUTORUN 1005
#define IDC_TIP     1006

static const wchar_t* kAppName   = L"DeepSeekPet";
static const wchar_t* kWndClass  = L"DeepSeekPetWnd";
static const wchar_t* kMutexName = L"DeepSeekPet.SingleInstance";
static const wchar_t* kApiHost   = L"api.deepseek.com";
static const wchar_t* kApiPath   = L"/user/balance";

static const UINT_PTR kTimerTick    = 1;
static const UINT_PTR kTimerRefresh = 2;

static const int kSpriteBaseH = 320;    // 素材归一化高度
static const int kAnimTickMs  = 25;     // 主循环 tick
static const int kPanelW      = 272;
static const int kPanelGap    = 8;
static const int kHeadH       = 78;
static const int kRowH        = 34;
static const int kFootH       = 28;

static const double kScaleMin  = 0.5;
static const double kScaleMax  = 2.0;
static const double kScaleStep = 0.15;

static const ARGB kColPanelBg   = 0xE6222735;
static const ARGB kColPanelEdge = 0x55FFFFFF;
static const ARGB kColTitle     = 0xFFFFFFFF;
static const ARGB kColBalance   = 0xFF5B9BFF;
static const ARGB kColBalanceLo = 0xFFFF7A7A;
static const ARGB kColText      = 0xFFE9EDF5;
static const ARGB kColDim       = 0xFF98A2B3;
static const ARGB kColHover     = 0x30FFFFFF;
static const ARGB kColSep       = 0x22FFFFFF;

enum RowId {
    ROW_REFRESH = 0, ROW_COPY, ROW_ZOOM_IN, ROW_ZOOM_OUT,
    ROW_ZOOM_RESET, ROW_MATERIAL, ROW_CONFIG, ROW_LAUNCH, ROW_TOPMOST, ROW_QUIT,
    ROW_COUNT
};

// 「更换素材」二级菜单的条目
enum MatRow {
    MAT_BUILTIN = 0, MAT_CUSTOM, MAT_LOAD, MAT_FOLDER, MAT_RESET, MAT_BACK,
    MAT_COUNT
};

// ============================================================ 基础工具
static wstring U8ToW(const std::string& s) {
    if (s.empty()) return wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (n <= 0) return wstring();
    wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n);
    return out;
}

static std::string WToU8(const wstring& s) {
    if (s.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n, nullptr, nullptr);
    return out;
}

static wstring TrimW(const wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == wstring::npos) return wstring();
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

static wstring AppDataDir() {
    wchar_t dir[MAX_PATH]{};
    GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH);
    wstring d = wstring(dir) + L"\\DeepSeekPet";
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}

static wstring ExeDir() {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    wstring s = exe;
    size_t p = s.find_last_of(L"\\/");
    return p == wstring::npos ? wstring(L".") : s.substr(0, p);
}

static bool FileExists(const wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static std::string ReadWholeFile(const wstring& p) {
    std::string out;
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return out;
    char buf[8192];
    DWORD rd = 0;
    while (ReadFile(h, buf, sizeof(buf), &rd, nullptr) && rd > 0) out.append(buf, rd);
    CloseHandle(h);
    return out;
}

// 从 JSON 里取整数/字符串字段（够用即可）
static int JsonInt(const std::string& s, const char* key, int defVal) {
    std::string pat = std::string("\"") + key + "\"";
    size_t k = s.find(pat);
    if (k == std::string::npos) return defVal;
    size_t c = s.find(':', k);
    if (c == std::string::npos) return defVal;
    return atoi(s.c_str() + c + 1);
}

static std::string JsonStr(const std::string& s, const char* key) {
    std::string pat = std::string("\"") + key + "\"";
    size_t k = s.find(pat);
    if (k == std::string::npos) return std::string();
    size_t c = s.find(':', k);
    if (c == std::string::npos) return std::string();
    size_t q1 = s.find('"', c + 1);
    if (q1 == std::string::npos) return std::string();
    size_t q2 = s.find('"', q1 + 1);
    if (q2 == std::string::npos) return std::string();
    return s.substr(q1 + 1, q2 - q1 - 1);
}

// ============================================================ 日志
// 每次打开/写入/关闭，避免持有句柄；用 Win32 API，规避 CRT 文件函数的问题
static void LogLine(const wchar_t* text) {
    static wstring s_path;
    if (s_path.empty()) s_path = AppDataDir() + L"\\pet.log";
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t line[2600];
    _snwprintf_s(line, _TRUNCATE, L"[%02d:%02d:%02d] %s\r\n",
                 st.wHour, st.wMinute, st.wSecond, text);

    int need = WideCharToMultiByte(CP_UTF8, 0, line, -1, nullptr, 0, nullptr, nullptr);
    if (need <= 1) return;
    std::string utf8((size_t)need - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, line, -1, &utf8[0], need, nullptr, nullptr);

    HANDLE h = CreateFileW(s_path.c_str(), FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, nullptr, FILE_END);
    DWORD wr = 0;
    WriteFile(h, utf8.data(), (DWORD)utf8.size(), &wr, nullptr);
    CloseHandle(h);
}

static void Log(const wchar_t* fmt, ...) {
    wchar_t buf[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);
    LogLine(buf);
}

// ============================================================ 配置
struct Config {
    wstring apiKey;
    double  lowThreshold = 1.0;
    int     refreshSec   = 300;
    double  scale        = 1.0;
    int     winX = -1;
    int     winY = -1;
    bool    autoStart = false;
    wstring dshCommand;
    wstring dshArgs;
    bool    useCustomMaterial = false;   // 是否使用自定义素材
    wstring customMaterial;              // 自定义素材的本地副本路径

    wstring path() const { return AppDataDir() + L"\\config.ini"; }

    void load() {
        std::string data = ReadWholeFile(path());
        if (data.size() >= 3 && (unsigned char)data[0] == 0xEF &&
            (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF)
            data.erase(0, 3);

        size_t pos = 0;
        while (pos < data.size()) {
            size_t eol = data.find('\n', pos);
            if (eol == std::string::npos) eol = data.size();
            std::string line = data.substr(pos, eol - pos);
            pos = eol + 1;
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            wstring k = TrimW(U8ToW(line.substr(0, eq)));
            wstring v = TrimW(U8ToW(line.substr(eq + 1)));
            if      (k == L"api_key")       apiKey = v;
            else if (k == L"low_threshold") lowThreshold = _wtof(v.c_str());
            else if (k == L"refresh_sec")   refreshSec = _wtoi(v.c_str());
            else if (k == L"scale")         scale = _wtof(v.c_str());
            else if (k == L"window_x")      winX = _wtoi(v.c_str());
            else if (k == L"window_y")      winY = _wtoi(v.c_str());
            else if (k == L"auto_start")    autoStart = (_wtoi(v.c_str()) != 0);
            else if (k == L"dsh_command")   dshCommand = v;
            else if (k == L"dsh_args")      dshArgs = v;
            else if (k == L"use_custom_material") useCustomMaterial = (_wtoi(v.c_str()) != 0);
            else if (k == L"custom_material")     customMaterial = v;
        }
        if (refreshSec < 30) refreshSec = 300;
        if (lowThreshold <= 0) lowThreshold = 1.0;
        if (scale < kScaleMin || scale > kScaleMax) scale = 1.0;
    }

    void save() const {
        std::string s;
        s += "# DeepSeek 桌宠配置 (UTF-8)\n";
        s += "# api_key       : DeepSeek 开放平台 API Key，用于查询余额\n";
        s += "# low_threshold : 余额低于该值(元)时切换成哭哭形象\n";
        s += "# scale         : 桌宠缩放比例 (0.50 ~ 2.00)\n";
        s += "# dsh_command   : 自定义启动 DeepSeek Harness 的命令，留空自动探测\n";
        s += "api_key=" + WToU8(apiKey) + "\n";
        char num[64];
        sprintf_s(num, "%.2f", lowThreshold); s += "low_threshold=" + std::string(num) + "\n";
        sprintf_s(num, "%d", refreshSec);     s += "refresh_sec=" + std::string(num) + "\n";
        sprintf_s(num, "%.2f", scale);        s += "scale=" + std::string(num) + "\n";
        sprintf_s(num, "%d", winX);           s += "window_x=" + std::string(num) + "\n";
        sprintf_s(num, "%d", winY);           s += "window_y=" + std::string(num) + "\n";
        sprintf_s(num, "%d", autoStart ? 1 : 0); s += "auto_start=" + std::string(num) + "\n";
        s += "dsh_command=" + WToU8(dshCommand) + "\n";
        s += "dsh_args=" + WToU8(dshArgs) + "\n";
        char flag[8];
        sprintf_s(flag, "%d", useCustomMaterial ? 1 : 0);
        s += "use_custom_material=" + std::string(flag) + "\n";
        s += "custom_material=" + WToU8(customMaterial) + "\n";

        HANDLE h = CreateFileW(path().c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;
        DWORD wr = 0;
        WriteFile(h, s.data(), (DWORD)s.size(), &wr, nullptr);
        CloseHandle(h);
    }
};

// ============================================================ 余额
struct Balance {
    bool      loaded = false;
    bool      ok = false;
    double    total = 0.0;
    bool      available = false;
    wstring   message = L"尚未查询";

    bool isLow(const Config& c) const { return ok && total < c.lowThreshold; }
};

// ============================================================ 动画帧
struct Frame {
    Bitmap* bmp = nullptr;
    int w = 0, h = 0;
};

struct FrameSet {
    std::vector<Frame> base;     // 原始帧（320 高）
    std::vector<Frame> shown;    // 当前显示用（按 zoom 缩放）
    std::vector<BYTE> hit;       // 命中图（1 字节/像素，基于 base[0]）
    int hitW = 0, hitH = 0;
    int frameMs = 90;
    int index = 0;
    double scale = 0.0;

    // ---- 自定义素材（可能是不定尺寸的 png/jpg/gif/webp）----
    bool     custom = false;     // true 表示这组来自自定义文件
    double   norm = 1.0;         // 把素材归一化到 320 高所需的系数
    int      customH = 0;        // 素材原始高度

    bool empty() const { return base.empty(); }
    int count() const { return (int)shown.size(); }
    int width() const { return shown.empty() ? 0 : shown[0].w; }
    int height() const { return shown.empty() ? 0 : shown[0].h; }
    const Frame& cur() const { return shown[(size_t)index % shown.size()]; }

    // 命中测试：hit 与 base 帧同尺寸，做比例映射后二分；再向四周膨胀 pad 像素，
    // 让判定范围覆盖图标柔和的边缘（避免"判定区比图标小"）
    bool hitTest(int x, int y, int pad = 3) const {
        if (hitW <= 0 || hitH <= 0) return false;
        int sx = (int)((double)x * hitW / (std::max)(1, width()));
        int sy = (int)((double)y * hitH / (std::max)(1, height()));
        // 先按膨胀量放宽：直接检查周围 (2*pad+1)^2 邻域内是否有命中像素
        if (pad <= 0) {
            if (sx < 0 || sy < 0 || sx >= hitW || sy >= hitH) return false;
            return hit[(size_t)sy * hitW + sx] != 0;
        }
        for (int dy = -pad; dy <= pad; ++dy) {
            int yy = sy + dy;
            if (yy < 0 || yy >= hitH) continue;
            const BYTE* row = &hit[(size_t)yy * hitW];
            for (int dx = -pad; dx <= pad; ++dx) {
                int xx = sx + dx;
                if (xx < 0 || xx >= hitW) continue;
                if (row[xx]) return true;
            }
        }
        return false;
    }

    void freeFrames(std::vector<Frame>& v) {
        for (auto& f : v) { delete f.bmp; f.bmp = nullptr; }
        v.clear();
    }

    void release() { freeFrames(base); freeFrames(shown); hit.clear(); }

    // 用一帧生成命中图（alpha 阈值放低到 8，把柔和边缘也算进去）
    void buildHitFrom(const Frame& f) {
        hit.clear();
        hitW = hitH = 0;
        if (!f.bmp || f.w <= 0 || f.h <= 0) return;
        BitmapData bd{};
        Rect r(0, 0, f.w, f.h);
        if (f.bmp->LockBits(&r, ImageLockModeRead, PixelFormat32bppARGB, &bd) != Ok) return;
        hit.assign((size_t)f.w * f.h, 0);
        for (int y = 0; y < f.h; ++y) {
            const BYTE* row = (const BYTE*)bd.Scan0 + (size_t)y * bd.Stride;
            for (int x = 0; x < f.w; ++x)
                if (row[x * 4 + 3] > 8) hit[(size_t)y * f.w + x] = 1;
        }
        f.bmp->UnlockBits(&bd);
        hitW = f.w;
        hitH = f.h;
    }

    void applyScale(double s) {
        if (base.empty()) return;
        // 自定义素材：base 是原始尺寸，需要先归一化到 320 高再乘 zoom
        const double f = custom ? (norm * s) : s;
        if (std::fabs(f - scale) < 1e-6 && !shown.empty()) return;
        freeFrames(shown);
        shown.reserve(base.size());
        for (auto& src : base) {
            int nw = (std::max)(1, (int)std::lround(src.w * f));
            int nh = (std::max)(1, (int)std::lround(src.h * f));
            Bitmap* nb = new Bitmap(nw, nh, PixelFormat32bppARGB);
            {
                Graphics g(nb);
                g.SetCompositingMode(CompositingModeSourceCopy);
                g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
                g.DrawImage(src.bmp, RectF(0.0f, 0.0f, (REAL)nw, (REAL)nh),
                            0.0f, 0.0f, (REAL)src.w, (REAL)src.h, UnitPixel);
            }
            Frame f2;
            f2.bmp = nb;
            f2.w = nw;
            f2.h = nh;
            shown.push_back(f2);
        }
        scale = f;
        if (index >= (int)shown.size()) index = 0;
    }

    // 追加原始帧（自定义素材用；调用方负责把 bmp 的所有权交进来）
    void addRawFrame(Bitmap* bmp) {
        if (!bmp) return;
        Frame f;
        f.bmp = bmp;
        f.w = (int)bmp->GetWidth();
        f.h = (int)bmp->GetHeight();
        if (f.w <= 0 || f.h <= 0) { delete bmp; return; }
        base.push_back(f);
    }

    // 收尾：按 320 高归一化 + 生成命中图
    void finishCustom(int srcHeight) {
        custom = true;
        customH = srcHeight > 0 ? srcHeight : (base.empty() ? kSpriteBaseH : base[0].h);
        norm = (double)kSpriteBaseH / (double)(std::max)(1, customH);
        scale = 0.0;
        shown.clear();
        if (!base.empty()) buildHitFrom(base[0]);
    }
};

// ============================================================ 全局状态
struct App {
    HINSTANCE hInst = nullptr;
    HWND      hwnd = nullptr;
    HICON     hIcon = nullptr;
    bool      trayAdded = false;

    Config  cfg;
    Balance bal;
    FrameSet fsBuiltin, fsLow, fsCustom;
    FrameSet* active = nullptr;
    double   zoom = 1.0;
    bool     zoomDirty = false;
    int      zoomSaveTicks = 0;
    ULONGLONG lastAnim = 0;

    int posX = 300, posY = 300, posTX = 300, posTY = 300;
    bool dragging = false;
    POINT dragOff{};
    POINT dragDownScreen{};
    bool moved = false;

    bool pinned = false;
    bool panelShown = false;
    int  hoverRow = -1;
    bool topMost = true;
    bool refreshing = false;

    int panelW = kPanelW, panelH = 0, rowsTop = kHeadH;
    std::vector<wstring> rows;
    bool materialsMode = false;          // 是否处于「更换素材」二级菜单
    bool customActive = false;           // 当前是否在使用自定义素材

    HDC     memDC = nullptr;
    HBITMAP memBmp = nullptr;
    void*   memBits = nullptr;
    int     bufW = 0, bufH = 0;

    Font* fTitle = nullptr;
    Font* fBig = nullptr;
    Font* fText = nullptr;
    Font* fSmall = nullptr;

    HWND hConfigDlg = nullptr;
    int  configDelay = 0;

    // 悬停防抖：面板状态刚变过 / 光标刚移动过的一段短时间内，不再翻转面板，
    // 避免"窗口移动 + 光标移动"叠加时在边界上翻转一次
    ULONGLONG panelToggleTick = 0;
    ULONGLONG cursorMoveTick = 0;
    POINT     lastCursor{ -100000, -100000 };

    // 已见过的最大面板高度：进入/退出二级菜单时行数会变，
    // 这里始终按最大高度保留窗口，避免窗口顶部移动把菜单项从光标下挪走
    int maxPanelH = 0;
};

static App g;

// ============================================================ 几何
// 窗口内布局（水平）：[透明填充 SpriteX] [桌宠 SpriteW] [间距] [面板 panelW]
// 窗口内布局（垂直）：面板贴窗口【顶部】向下排布；桌宠贴窗口【底部】。
//   —— 这样"面板展开/收起"只改变窗口【顶部】的位置与高度，
//      桌宠在屏幕上的位置、以及它周围（尤其下方）的区域完全不动。
static int SpriteW() { return g.active ? g.active->width() : kSpriteBaseH; }
static int SpriteH() { return g.active ? g.active->height() : kSpriteBaseH; }

static int SpriteX() {
    int maxW = 0;
    if (!g.fsBuiltin.base.empty()) maxW = (std::max)(maxW, g.fsBuiltin.width());
    if (!g.fsLow.base.empty())    maxW = (std::max)(maxW, g.fsLow.width());
    if (maxW <= 0) maxW = SpriteW();
    return (std::max)(0, maxW - SpriteW());
}

static int WinW() { return SpriteX() + SpriteW() + (g.panelShown ? kPanelGap + g.panelW : 0); }

// 垂直方向：面板贴窗口顶部，桌宠贴窗口底部。
// 面板展开/收起只改变窗口【顶部】，桌宠屏幕位置不变；"图标下方"的区域也不变。
// 高度取"见过的最大面板高度"，这样主菜单 <-> 二级菜单切换时窗口高度不变，
// 面板 Y 坐标稳定，菜单项不会被挪出光标范围。
static int PanelHeight() {
    int h = (g.panelH > 0 ? g.panelH : kHeadH + kFootH);
    if (g.maxPanelH < h) g.maxPanelH = h;
    return (std::max)(h, g.maxPanelH);
}
static int WinH() { return (std::max)(SpriteH(), g.panelShown ? PanelHeight() : 0); }

// 桌宠在窗口内的纵向偏移（贴底）
static int SpriteYOff() { return (std::max)(0, WinH() - SpriteH()); }

static int PanelX() { return SpriteX() + SpriteW() + kPanelGap; }
static int PanelY() { return 0; }   // 面板贴窗口顶部

static Rect PanelRect() {
    return Rect(PanelX(), PanelY(), g.panelW, g.panelH);
}


// ============================================================ 素材加载
static Bitmap* BitmapFromPngBytes(const void* data, size_t size) {
    if (!data || size == 0) return nullptr;
    HGLOBAL hmem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hmem) return nullptr;
    void* p = GlobalLock(hmem);
    memcpy(p, data, size);
    GlobalUnlock(hmem);

    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(hmem, TRUE, &stream) != S_OK) { GlobalFree(hmem); return nullptr; }
    Bitmap* bmp = Bitmap::FromStream(stream);
    stream->Release();
    if (!bmp || bmp->GetLastStatus() != Ok) { delete bmp; return nullptr; }
    return bmp;
}

static Bitmap* LoadPngFile(const wstring& path) {
    std::string data = ReadWholeFile(path);
    if (data.empty()) return nullptr;
    return BitmapFromPngBytes(data.data(), data.size());
}

static Bitmap* LoadPngResource(int resId) {
    HRSRC hr = FindResourceW(g.hInst, MAKEINTRESOURCEW(resId), L"PNG");
    if (!hr) return nullptr;
    HGLOBAL hg = LoadResource(g.hInst, hr);
    DWORD size = SizeofResource(g.hInst, hr);
    void* data = LockResource(hg);
    if (!data || !size) return nullptr;
    return BitmapFromPngBytes(data, size);
}

// 从竖排 strip 图切帧
static bool LoadStrip(const wstring& png, const wstring& meta, FrameSet& fs, int defaultMs) {
    if (!FileExists(png)) return false;
    std::string js = FileExists(meta) ? ReadWholeFile(meta) : std::string();
    fs.frameMs = JsonInt(js, "frame_ms", defaultMs);
    if (fs.frameMs < 16 || fs.frameMs > 2000) fs.frameMs = defaultMs;

    Bitmap* strip = LoadPngFile(png);
    if (!strip) { Log(L"strip 解码失败: %s", png.c_str()); return false; }
    int sw = (int)strip->GetWidth();
    int sh = (int)strip->GetHeight();
    int fh = JsonInt(js, "h", kSpriteBaseH);
    if (fh <= 0 || fh > sh) fh = kSpriteBaseH;
    int n = sh / fh;
    if (n <= 0) { delete strip; return false; }

    for (int i = 0; i < n; ++i) {
        Rect r(0, i * fh, sw, fh);
        Bitmap* fr = strip->Clone(r, PixelFormat32bppARGB);
        if (!fr || fr->GetLastStatus() != Ok) { delete fr; break; }
        Frame f;
        f.bmp = fr;
        f.w = (int)fr->GetWidth();
        f.h = (int)fr->GetHeight();
        fs.base.push_back(f);
    }
    delete strip;
    Log(L"strip 载入 %d 帧 (%dx%d/帧, %dms)", (int)fs.base.size(), sw, fh, fs.frameMs);
    return !fs.base.empty();
}

// 内嵌单张 PNG 作为静态帧
static bool LoadStaticResource(int resId, FrameSet& fs, int ms) {
    Bitmap* bmp = LoadPngResource(resId);
    if (!bmp) return false;
    Frame f;
    f.bmp = bmp;
    f.w = (int)bmp->GetWidth();
    f.h = (int)bmp->GetHeight();
    fs.base.push_back(f);
    fs.frameMs = ms;
    Log(L"内嵌素材载入 %dx%d", f.w, f.h);
    return true;
}

static bool LoadFrameSet(const wchar_t* name, int resId, FrameSet& fs, int defaultMs) {
    wstring base = ExeDir() + L"\\assets\\" + name;
    bool ok = LoadStrip(base + L".png", base + L".meta.json", fs, defaultMs);
    if (!ok) ok = LoadStaticResource(resId, fs, defaultMs);
    if (ok && !fs.base.empty()) fs.buildHitFrom(fs.base[0]);
    fs.custom = false;
    fs.norm = 1.0;
    return ok;
}

// ============================================================ 自定义素材
static wstring MaterialsDir() {
    wstring d = AppDataDir() + L"\\materials";
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}

// 用 WIC 打开任意图片（png/jpg/gif/webp，动图逐帧），填进 fs
static bool LoadCustomMaterialInto(const wstring& path, FrameSet& fs, int maxFrames = 240) {
    WicImage img;
    if (!img.open(path)) {
        Log(L"自定义素材无法解码: %s", path.c_str());
        return false;
    }
    if (img.width() <= 0 || img.height() <= 0) return false;
    if (img.width() > 4096 || img.height() > 4096) {
        Log(L"自定义素材太大: %dx%d", img.width(), img.height());
        return false;
    }

    const UINT total = img.frameCount();
    const UINT take = (total > (UINT)maxFrames) ? (UINT)maxFrames : total;
    const int srcH = img.height();

    std::vector<Bitmap*> frames;
    frames.reserve(take);
    for (UINT i = 0; i < take; ++i) {
        if (!img.seek(i)) break;
        Bitmap* b = img.toBitmap();
        if (!b) continue;
        frames.push_back(b);
    }
    if (frames.empty()) {
        Log(L"自定义素材没有可用帧: %s", path.c_str());
        return false;
    }

    fs.release();
    for (Bitmap* b : frames) fs.addRawFrame(b);   // 所有权转移
    fs.frameMs = (total > 1) ? img.delayMs() : 90;
    if (fs.frameMs < 16 || fs.frameMs > 2000) fs.frameMs = 90;
    fs.finishCustom(srcH);
    Log(L"自定义素材载入: %s（%u 帧, 原始 %dx%d, 每帧 %dms）",
        path.c_str(), total, img.width(), srcH, fs.frameMs);
    return true;
}

// 切回内置素材 / 切到自定义素材
// 注意：内置素材（fsBuiltin）常驻内存，不重复解码 strip 图
//      （实测：释放 20MB 位图后再解码同尺寸图，GDI+ 会卡住）
static void SelectSprite();   // 定义在下面

static bool ApplyMaterialChoice(bool useCustom) {
    if (!useCustom) {
        g.customActive = false;
        g.cfg.useCustomMaterial = false;
        SelectSprite();          // 会切回 fsBuiltin
        Log(L"[素材] 已切回内置（无需重新解码）");
        return true;
    }

    if (g.cfg.customMaterial.empty() || !FileExists(g.cfg.customMaterial)) {
        Log(L"自定义素材文件不存在: %s", g.cfg.customMaterial.c_str());
        return false;
    }
    Log(L"[素材] 载入自定义：%s", g.cfg.customMaterial.c_str());
    FrameSet tmp;
    if (!LoadCustomMaterialInto(g.cfg.customMaterial, tmp)) return false;
    g.fsCustom.release();
    g.fsCustom = std::move(tmp);
    g.customActive = true;
    g.cfg.useCustomMaterial = true;
    g.fsCustom.applyScale(g.zoom);   // 预缩放，切换后立即可绘制
    SelectSprite();
    return true;
}

// 把用户选的文件复制到 materials 目录，返回副本路径
static wstring ImportMaterialFile(const wstring& src) {
    if (!FileExists(src)) return wstring();
    size_t dot = src.find_last_of(L'.');
    wstring ext = (dot == wstring::npos) ? L".png" : src.substr(dot);
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t name[128];
    swprintf_s(name, L"\\material_%04d%02d%02d_%02d%02d%02d%s",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, ext.c_str());
    wstring dst = MaterialsDir() + name;
    if (!CopyFileW(src.c_str(), dst.c_str(), FALSE)) {
        Log(L"复制素材失败 (%lu): %s -> %s", GetLastError(), src.c_str(), dst.c_str());
        return wstring();
    }
    return dst;
}

// ============================================================ 形象 / 缩放
static void SelectSprite() {
    bool low = g.bal.isLow(g.cfg);
    // 余额低 -> 内置"哭哭"；否则 -> 自定义素材（若启用且可用）或内置常态
    FrameSet* want = nullptr;
    if (low) want = &g.fsLow;
    else if (g.customActive && !g.fsCustom.empty()) want = &g.fsCustom;
    else want = &g.fsBuiltin;

    if (!want || want->empty()) want = &g.fsBuiltin;
    if (!want || want->empty()) want = &g.fsLow;
    if (!want || want->empty()) return;

    if (want != g.active) {
        g.active = want;
        g.active->index = 0;
    }
    g.active->applyScale(g.zoom);
    g.lastAnim = GetTickCount64();
    Log(L"形象: %s%s, %d 帧, 缩放 %.0f%%",
        low ? L"哭哭" : L"常态",
        (!low && g.active == &g.fsCustom) ? L"(自定义素材)" : L"",
        g.active->count(), g.zoom * 100.0);
}

static void UpdatePanelSize() {
    g.panelW = kPanelW;
    g.panelH = kHeadH + (int)g.rows.size() * kRowH + kFootH;
    g.rowsTop = kHeadH;
    if (g.panelH < 1) g.panelH = kHeadH + kFootH;
    // 记录见过的最大面板高度：主菜单/二级菜单切换时窗口高度保持一致
    if (g.maxPanelH < g.panelH) g.maxPanelH = g.panelH;
}

static void SyncWindow(bool applyPos);

// ---- 几何模型 ----
// 桌宠在屏幕上的位置是唯一基准：窗口位置由它反推。
// 纵向：窗口顶部在 桌宠顶部 - SpriteYOff()（即面板往上长），桌宠屏幕位置恒定。
static int SpriteScreenX() { return g.posX + SpriteX(); }
static int SpriteScreenY() { return g.posY + SpriteYOff(); }

static void SetSpritePos(int sx, int sy) {
    g.posX = g.posTX = sx - SpriteX();
    g.posY = g.posTY = sy - SpriteYOff();
    if (g.hwnd)
        SetWindowPos(g.hwnd, nullptr, g.posX, g.posY, WinW(), WinH(),
                     SWP_NOZORDER | SWP_NOACTIVATE);
}

static void ApplyZoom(double z) {
    z = (std::max)(kScaleMin, (std::min)(kScaleMax, z));
    if (std::fabs(z - g.zoom) < 1e-6) return;

    // 锚点：桌宠水平中心 + 底边（缩放后这两个位置不变）
    const int anchorCx = SpriteScreenX() + SpriteW() / 2;
    const int anchorBottom = SpriteScreenY() + SpriteH();

    g.zoom = z;
    if (g.active) g.active->applyScale(z);

    SetSpritePos(anchorCx - SpriteW() / 2, anchorBottom - SpriteH());
    SyncWindow(true);
    g.zoomDirty = true;
    g.zoomSaveTicks = 20;
    Log(L"缩放 -> %.0f%% (%dx%d) 桌宠中心x=%d 底边y=%d",
        z * 100.0, SpriteW(), SpriteH(),
        SpriteScreenX() + SpriteW() / 2, SpriteScreenY() + SpriteH());
}

// ============================================================ 提示
static void Notify(const wstring& text) {
    if (g.trayAdded) {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = g.hwnd;
        nid.uID = 1;
        nid.uFlags = NIF_INFO;
        nid.dwInfoFlags = NIIF_INFO;
        wcsncpy_s(nid.szInfoTitle, L"DeepSeek 桌宠", _TRUNCATE);
        wcsncpy_s(nid.szInfo, text.c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }
}

// ============================================================ 余额查询
static DWORD WINAPI BalanceThread(LPVOID) {
    std::string body;
    int status = 0;
    wstring err;

    HINTERNET hs = WinHttpOpen(L"DeepSeekPet/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hs) {
        err = L"WinHttpOpen 失败";
    } else {
        WinHttpSetTimeouts(hs, 5000, 5000, 8000, 8000);
        HINTERNET hc = WinHttpConnect(hs, kApiHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hc) {
            err = L"连接 api.deepseek.com 失败";
        } else {
            HINTERNET hr = WinHttpOpenRequest(hc, L"GET", kApiPath, nullptr,
                                              WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                              WINHTTP_FLAG_SECURE);
            if (!hr) {
                err = L"创建请求失败";
            } else {
                wstring hdr = L"Authorization: Bearer " + g.cfg.apiKey +
                              L"\r\nAccept: application/json\r\n";
                if (!WinHttpSendRequest(hr, hdr.c_str(), (DWORD)-1L,
                                        WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
                    err = L"发送请求失败（检查网络/代理）";
                } else if (!WinHttpReceiveResponse(hr, nullptr)) {
                    err = L"接收响应失败";
                } else {
                    DWORD code = 0, len = sizeof(code);
                    WinHttpQueryHeaders(hr, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                        WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX);
                    status = (int)code;
                    for (;;) {
                        DWORD avail = 0;
                        if (!WinHttpQueryDataAvailable(hr, &avail) || avail == 0) break;
                        std::string chunk((size_t)avail, '\0');
                        DWORD rd = 0;
                        if (!WinHttpReadData(hr, &chunk[0], avail, &rd) || rd == 0) break;
                        chunk.resize(rd);
                        body += chunk;
                        if (body.size() > 65536) break;
                    }
                    if (code == 401) err = L"API Key 无效或已过期 (401)";
                    else if (code == 402) err = L"账户余额不足 (402)";
                    else if (code < 200 || code >= 300) err = L"HTTP " + std::to_wstring(code);
                }
                WinHttpCloseHandle(hr);
            }
            WinHttpCloseHandle(hc);
        }
        WinHttpCloseHandle(hs);
    }

    double total = 0.0;
    bool available = false, parsed = false;
    wstring currency;
    if (!body.empty()) {
        std::string av = JsonStr(body, "is_available");
        available = (av == "true");
        size_t bp = body.find("\"balance_infos\"");
        if (bp != std::string::npos) {
            size_t scan = body.find('[', bp);
            for (int i = 0; i < 8 && scan != std::string::npos; ++i) {
                size_t ob = body.find('{', scan);
                if (ob == std::string::npos) break;
                size_t oe = body.find('}', ob);
                if (oe == std::string::npos) break;
                std::string obj = body.substr(ob, oe - ob + 1);
                scan = oe + 1;
                std::string cur = JsonStr(obj, "currency");
                std::string tot = JsonStr(obj, "total_balance");
                if (tot.empty()) continue;
                double v = atof(tot.c_str());
                if (cur == "CNY") { total = v; currency = U8ToW(cur); parsed = true; break; }
                if (!parsed) { total = v; currency = U8ToW(cur); parsed = true; }
            }
        }
    }

    if (parsed) {
        g.bal.total = total;
        g.bal.ok = true;
        g.bal.available = available;
        g.bal.message = (available ? L"余额可用" : L"余额不可用");
        if (!currency.empty()) g.bal.message += L" · " + currency;
        Log(L"余额: %.2f (%s)", total, currency.c_str());
    } else {
        g.bal.ok = false;
        g.bal.message = err.empty() ? L"解析响应失败" : err;
        Log(L"余额查询失败: %s (status=%d)", g.bal.message.c_str(), status);
    }
    g.bal.loaded = true;
    g.refreshing = false;
    if (g.hwnd) PostMessageW(g.hwnd, WM_BALANCE_DONE, 0, 0);
    return 0;
}

static void StartRefresh() {
    if (g.refreshing) return;
    if (g.cfg.apiKey.empty()) {
        g.bal.loaded = true;
        g.bal.ok = false;
        g.bal.message = L"未配置 API Key";
        return;
    }
    g.refreshing = true;
    HANDLE h = CreateThread(nullptr, 0, BalanceThread, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
    else g.refreshing = false;
}

// ============================================================ 绘制
static Font* MakeFont(const wchar_t* family, REAL size, INT style) {
    Font* f = new Font(family, size, style, UnitPixel);
    if (f->GetLastStatus() != Ok) {
        delete f;
        f = new Font(L"Microsoft YaHei UI", size, style, UnitPixel);
        if (f->GetLastStatus() != Ok) { delete f; f = new Font(L"Segoe UI", size, style, UnitPixel); }
    }
    return f;
}

static void DrawStr(Graphics& gfx, const wstring& s, Font* font, ARGB color, const RectF& box) {
    if (!font || s.empty()) return;
    SolidBrush brush{ Color(color) };
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentNear);
    fmt.SetLineAlignment(StringAlignmentCenter);
    fmt.SetFormatFlags(StringFormatFlagsNoWrap);
    gfx.SetTextRenderingHint(TextRenderingHintAntiAlias);
    gfx.DrawString(s.c_str(), (INT)s.size(), font, box, &fmt, &brush);
}

static void EnsureBuffer(int w, int h) {
    if (w == g.bufW && h == g.bufH && g.memBmp) return;
    if (g.memBmp) { DeleteObject(g.memBmp); g.memBmp = nullptr; }
    if (g.memDC) { DeleteDC(g.memDC); g.memDC = nullptr; }

    HDC screen = GetDC(nullptr);
    g.memDC = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    g.memBmp = CreateDIBSection(g.memDC, &bi, DIB_RGB_COLORS, &g.memBits, nullptr, 0);
    if (g.memBmp) SelectObject(g.memDC, g.memBmp);
    g.bufW = w;
    g.bufH = h;
}

static void Render() {
    int w = WinW(), h = WinH();
    if (w <= 0 || h <= 0) return;
    EnsureBuffer(w, h);
    if (!g.memBmp || !g.memBits) return;

    Graphics gfx(g.memDC);
    gfx.SetCompositingMode(CompositingModeSourceCopy);
    gfx.Clear(Color(0, 0, 0, 0));
    gfx.SetCompositingMode(CompositingModeSourceOver);
    gfx.SetSmoothingMode(SmoothingModeAntiAlias);

    // 桌宠
    if (g.active && !g.active->shown.empty()) {
        const Frame& f = g.active->cur();
        int sx = SpriteX();
        if (f.bmp)
            gfx.DrawImage(f.bmp, RectF((REAL)sx, (REAL)(h - f.h), (REAL)f.w, (REAL)f.h),
                          0.0f, 0.0f, (REAL)f.w, (REAL)f.h, UnitPixel);
    }

    // 面板
    if (g.panelShown) {
        Rect pr = PanelRect();
        REAL x = (REAL)pr.X, y = (REAL)pr.Y;
        REAL pw = (REAL)pr.Width, ph = (REAL)pr.Height;
        REAL r = 12.0f;

        GraphicsPath path;
        path.AddArc(x, y, r * 2, r * 2, 180, 90);
        path.AddArc(x + pw - r * 2, y, r * 2, r * 2, 270, 90);
        path.AddArc(x + pw - r * 2, y + ph - r * 2, r * 2, r * 2, 0, 90);
        path.AddArc(x, y + ph - r * 2, r * 2, r * 2, 90, 90);
        path.CloseFigure();
        SolidBrush bg{ Color(kColPanelBg) };
        gfx.FillPath(&bg, &path);
        Pen edge{ Color(kColPanelEdge), 1.0f };
        gfx.DrawPath(&edge, &path);

        DrawStr(gfx, L"DeepSeek 余额", g.fText, kColTitle,
                RectF(x + 14.0f, y + 12.0f, pw - 28.0f, 20.0f));

        wstring big;
        ARGB bigColor = kColBalance;
        if (g.bal.ok) {
            wchar_t num[64];
            swprintf_s(num, L"¥ %.2f", g.bal.total);
            big = num;
            if (g.bal.isLow(g.cfg)) bigColor = kColBalanceLo;
        } else if (g.refreshing || !g.bal.loaded) {
            big = L"查询中…";
            bigColor = kColDim;
        } else {
            big = L"—";
            bigColor = kColDim;
        }
        DrawStr(gfx, big, g.fBig, bigColor, RectF(x + 14.0f, y + 32.0f, pw - 28.0f, 26.0f));

        wstring stat = g.refreshing ? wstring(L"正在刷新…") : g.bal.message;
        DrawStr(gfx, stat, g.fSmall, g.bal.ok ? kColDim : kColBalanceLo,
                RectF(x + 14.0f, y + 56.0f, pw - 28.0f, 16.0f));

        Pen sep{ Color(kColSep), 1.0f };
        gfx.DrawLine(&sep, x + 10.0f, y + (REAL)g.rowsTop, x + pw - 10.0f, y + (REAL)g.rowsTop);

        SolidBrush hover{ Color(kColHover) };
        for (size_t i = 0; i < g.rows.size(); ++i) {
            RectF rowBox(x + 6.0f, y + (REAL)g.rowsTop + (REAL)i * kRowH + 2.0f,
                         pw - 12.0f, (REAL)kRowH - 4.0f);
            if ((int)i == g.hoverRow) {
                GraphicsPath rp;
                REAL rr = 8.0f;
                rp.AddArc(rowBox.X, rowBox.Y, rr * 2, rr * 2, 180, 90);
                rp.AddArc(rowBox.GetRight() - rr * 2, rowBox.Y, rr * 2, rr * 2, 270, 90);
                rp.AddArc(rowBox.GetRight() - rr * 2, rowBox.GetBottom() - rr * 2, rr * 2, rr * 2, 0, 90);
                rp.AddArc(rowBox.X, rowBox.GetBottom() - rr * 2, rr * 2, rr * 2, 90, 90);
                rp.CloseFigure();
                gfx.FillPath(&hover, &rp);
            }
            ARGB col = kColText;
            if ((int)i == ROW_QUIT) col = 0xFFFFB4B4;
            if ((int)i == ROW_LAUNCH) col = 0xFF9BD0FF;
            DrawStr(gfx, g.rows[i], g.fText, col,
                    RectF(rowBox.X + 10.0f, rowBox.Y, rowBox.Width - 20.0f, rowBox.Height));
        }

        wchar_t foot[160];
        swprintf_s(foot, L"%d%%  ·  滚轮缩放  ·  %s", (int)std::lround(g.zoom * 100.0),
                   g.pinned ? L"已固定，点击桌宠取消" : L"点击桌宠固定面板");
        DrawStr(gfx, foot, g.fSmall, kColDim,
                RectF(x + 14.0f, y + ph - (REAL)kFootH + 4.0f, pw - 28.0f, 16.0f));
    }

    // GDI+ 输出非预乘 ARGB，这里转成预乘
    BYTE* px = (BYTE*)g.memBits;
    size_t total = (size_t)w * h;
    for (size_t i = 0; i < total; ++i) {
        BYTE a = px[i * 4 + 3];
        if (a == 255) continue;
        if (a == 0) { px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = 0; continue; }
        px[i * 4]     = (BYTE)((px[i * 4]     * a + 127) / 255);
        px[i * 4 + 1] = (BYTE)((px[i * 4 + 1] * a + 127) / 255);
        px[i * 4 + 2] = (BYTE)((px[i * 4 + 2] * a + 127) / 255);
    }

    POINT dst{ g.posX, g.posY };
    SIZE size{ w, h };
    POINT src{ 0, 0 };
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    HDC screen = GetDC(nullptr);
    UpdateLayeredWindow(g.hwnd, screen, &dst, &size, g.memDC, &src, 0, &bf, ULW_ALPHA);
    ReleaseDC(nullptr, screen);
}

static void SyncWindow(bool applyPos) {
    UINT flags = SWP_NOACTIVATE | SWP_NOZORDER | (applyPos ? 0 : SWP_NOMOVE);
    SetWindowPos(g.hwnd, g.topMost ? HWND_TOPMOST : HWND_NOTOPMOST,
                 g.posX, g.posY, WinW(), WinH(), flags);
    Render();
}

// ============================================================ 面板行
static void BuildRows() {
    g.rows.clear();
    if (g.materialsMode) {
        // 「更换素材」二级菜单
        // 注意：行序必须和 MatRow 枚举一致（MAT_BUILTIN=0, MAT_CUSTOM=1, MAT_LOAD=2 ...）
        g.rows.push_back(g.customActive ? L"○ 使用内置素材" : L"● 使用内置素材");
        g.rows.push_back(g.customActive ? L"● 使用自定义素材" : L"○ 使用自定义素材");
        g.rows.push_back(L"从文件加载素材…");
        g.rows.push_back(L"打开素材文件夹");
        g.rows.push_back(L"删除自定义素材（恢复默认）");
        g.rows.push_back(L"返回上级菜单");
        UpdatePanelSize();
        return;
    }
    g.rows.push_back(g.bal.ok ? L"刷新余额" : L"刷新余额（未配置/失败）");
    g.rows.push_back(L"复制余额到剪贴板");
    g.rows.push_back(L"放大桌宠  +15%");
    g.rows.push_back(L"缩小桌宠  -15%");
    g.rows.push_back(L"恢复原始大小 100%");
    g.rows.push_back(L"更换素材…");
    g.rows.push_back(L"配置 API Key / 阈值");
    g.rows.push_back(L"启动 DeepSeek Harness");
    g.rows.push_back(g.topMost ? L"取消总在最前" : L"总在最前");
    g.rows.push_back(L"退出桌宠");
    UpdatePanelSize();
}

// 切换主菜单 / 素材二级菜单（保持面板展开状态与几何）
static void SetMaterialsMode(bool on) {
    if (g.materialsMode == on) return;
    g.materialsMode = on;
    const int sx = SpriteScreenX();
    const int sy = SpriteScreenY();
    BuildRows();
    SetSpritePos(sx, sy);
    SyncWindow(true);
}

// ============================================================ 交互
static void SetPanelShown(bool show) {
    if (g.panelShown == show) return;
    // 关键：先记住桌宠当前的屏幕位置，展开/收起面板只改窗口尺寸，桌宠保持不动
    const int sx = SpriteScreenX();
    const int sy = SpriteScreenY();
    g.panelShown = show;
    if (!show) g.hoverRow = -1;
    SetSpritePos(sx, sy);
    SyncWindow(true);
    g.panelToggleTick = GetTickCount64();   // 记录翻转时刻，用于防抖
}

static void UpdateHover() {
    POINT pt{};
    GetCursorPos(&pt);
    const ULONGLONG now = GetTickCount64();

    // 光标每移动一下，就推迟"允许翻转"的时刻
    if (pt.x != g.lastCursor.x || pt.y != g.lastCursor.y) {
        g.lastCursor = pt;
        g.cursorMoveTick = now;
    }

    // 关键：判定用【桌宠在屏幕上的固定矩形】和【面板在屏幕上的固定矩形】，
    // 不再依赖窗口自身的实时位置/尺寸，从根上避免"窗口一动→判定变化→再动"的自激振荡。
    const int sx0 = SpriteScreenX();
    const int sy0 = SpriteScreenY();
    const int sw = SpriteW(), sh = SpriteH();

    bool inSprite = false;
    if (g.active && !g.active->shown.empty() &&
        pt.x >= sx0 && pt.x < sx0 + sw && pt.y >= sy0 && pt.y < sy0 + sh) {
        inSprite = g.active->hitTest(pt.x - sx0, pt.y - sy0);
    }

    // 面板（展开时其屏幕矩形也是稳定的）
    bool inPanel = false;
    int row = -1;
    if (g.panelShown) {
        Rect pr = PanelRect();
        int px0 = g.posX + pr.X, py0 = g.posY + pr.Y;
        inPanel = pt.x >= px0 && pt.x < px0 + pr.Width && pt.y >= py0 && pt.y < py0 + pr.Height;
        if (inPanel) {
            int rel = pt.y - py0 - g.rowsTop;
            if (rel >= 0) {
                int idx = rel / kRowH;
                if (idx >= 0 && idx < (int)g.rows.size()) row = idx;
            }
        }
    }

    bool needRepaint = (row != g.hoverRow);
    g.hoverRow = row;

    if (!g.pinned) {
        // 迟滞：进入用紧边界，离开要多离开 kHoverMargin 像素，避免边界反复横跳
        const int kHoverMargin = 8;
        const ULONGLONG kSettleMs = 250;   // 面板刚翻转 / 光标刚移动后的静默期
        bool inside = inSprite || inPanel;
        bool outside;
        if (g.panelShown) {
            // 已展开：桌宠本体 / 面板（各含缓冲带）之外才算离开
            Rect pr = PanelRect();
            int px0 = g.posX + pr.X, py0 = g.posY + pr.Y;
            bool inPanelLoose = pt.x >= px0 - kHoverMargin && pt.x < px0 + pr.Width + kHoverMargin &&
                                pt.y >= py0 - kHoverMargin && pt.y < py0 + pr.Height + kHoverMargin;
            outside = !inside && !inPanelLoose;
        } else {
            // 未展开：离开桌宠本体缓冲带即算离开
            outside = !inside &&
                      (pt.x < sx0 - kHoverMargin || pt.x > sx0 + sw + kHoverMargin ||
                       pt.y < sy0 - kHoverMargin || pt.y > sy0 + sh + kHoverMargin);
        }

        // 防抖：只有在"面板最近没翻转过"且"光标最近停住过"时才允许改变状态。
        // 这样光标扫过边界、或窗口刚移动引起的一瞬越界，都不会造成闪动。
        bool settled = (now - g.panelToggleTick >= kSettleMs) &&
                       (now - g.cursorMoveTick >= kSettleMs);
        if (settled) {
            if (inside && !g.panelShown) SetPanelShown(true);
            else if (outside && g.panelShown) SetPanelShown(false);
        }
    }
    if (needRepaint) Render();
}

static void CopyBalanceToClipboard() {
    wchar_t buf[128];
    if (g.bal.ok) swprintf_s(buf, L"DeepSeek 余额: ¥ %.2f", g.bal.total);
    else wcscpy_s(buf, L"DeepSeek 余额: 未知");
    if (OpenClipboard(g.hwnd)) {
        EmptyClipboard();
        size_t bytes = (wcslen(buf) + 1) * sizeof(wchar_t);
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (h) {
            void* p = GlobalLock(h);
            memcpy(p, buf, bytes);
            GlobalUnlock(h);
            SetClipboardData(CF_UNICODETEXT, h);
        }
        CloseClipboard();
    }
}

static void ToggleAutoStart() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) return;
    if (g.cfg.autoStart) {
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        wstring val = L"\"" + wstring(exe) + L"\"";
        RegSetValueExW(hKey, kAppName, 0, REG_SZ, (const BYTE*)val.c_str(),
                       (DWORD)((val.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(hKey, kAppName);
    }
    RegCloseKey(hKey);
}

static void LaunchHarness() {
    wstring exe, params;
    if (!g.cfg.dshCommand.empty()) {
        exe = g.cfg.dshCommand;
        params = g.cfg.dshArgs;
    } else {
        wchar_t found[MAX_PATH]{};
        if (SearchPathW(nullptr, L"dsh.exe", nullptr, MAX_PATH, found, nullptr) ||
            SearchPathW(nullptr, L"dsh.cmd", nullptr, MAX_PATH, found, nullptr)) {
            exe = found;
            if (exe.size() > 4 && (_wcsicmp(exe.c_str() + exe.size() - 4, L".cmd") == 0 ||
                                   _wcsicmp(exe.c_str() + exe.size() - 4, L".bat") == 0)) {
                params = L"/c \"" + exe + L"\"";
                exe = L"cmd.exe";
            }
        } else {
            const wchar_t* cands[] = {
                L"%LOCALAPPDATA%\\Programs\\DeepSeek Harness\\DeepSeek Harness.exe",
                L"%PROGRAMFILES%\\DeepSeek Harness\\DeepSeek Harness.exe",
            };
            for (auto c : cands) {
                wchar_t exp[MAX_PATH]{};
                ExpandEnvironmentStringsW(c, exp, MAX_PATH);
                if (GetFileAttributesW(exp) != INVALID_FILE_ATTRIBUTES) { exe = exp; break; }
            }
        }
    }

    if (exe.empty()) {
        int r = MessageBoxW(g.hwnd,
            L"没有找到 DeepSeek Harness。\n\n"
            L"可以在设置窗口的“Harness 启动命令”里填写完整路径。\n\n"
            L"是否现在打开设置窗口？",
            L"启动 DeepSeek Harness", MB_YESNO | MB_ICONINFORMATION);
        if (r == IDYES) SendMessageW(g.hwnd, WM_COMMAND, IDM_TRAY_CONFIG, 0);
        return;
    }

    std::vector<wchar_t> cmd(exe.begin(), exe.end());
    if (!params.empty()) { cmd.push_back(L' '); cmd.insert(cmd.end(), params.begin(), params.end()); }
    cmd.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                       CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        Log(L"已启动 Harness: %s", exe.c_str());
        Notify(L"已启动 DeepSeek Harness");
    } else {
        HINSTANCE r = ShellExecuteW(g.hwnd, L"open", exe.c_str(),
                                    params.empty() ? nullptr : params.c_str(), nullptr, SW_SHOWNORMAL);
        if ((INT_PTR)r <= 32) {
            Log(L"启动失败: %s", exe.c_str());
            Notify(L"启动失败，请在设置里填写正确的命令");
        }
    }
}

// ============================================================ 设置窗口
struct DlgState {
    wstring key, threshold, refresh, dshCmd;
    bool autoStart = false;
    bool ok = false;
    bool applied = false;
};
static DlgState g_dlg;
static DlgState g_dlgRef;

static void ReadDlg(HWND h, DlgState& out) {
    wchar_t buf[1024];
    GetDlgItemTextW(h, IDC_KEY, buf, 1024);     out.key = TrimW(buf);
    GetDlgItemTextW(h, IDC_THRESH, buf, 1024);  out.threshold = TrimW(buf);
    GetDlgItemTextW(h, IDC_REFRESH, buf, 1024); out.refresh = TrimW(buf);
    GetDlgItemTextW(h, IDC_DSH, buf, 1024);     out.dshCmd = TrimW(buf);
    out.autoStart = IsDlgButtonChecked(h, IDC_AUTORUN) == BST_CHECKED;
}

static bool DlgChanged(HWND h) {
    DlgState cur;
    ReadDlg(h, cur);
    return cur.key != g_dlgRef.key || cur.threshold != g_dlgRef.threshold ||
           cur.refresh != g_dlgRef.refresh || cur.dshCmd != g_dlgRef.dshCmd ||
           cur.autoStart != g_dlgRef.autoStart;
}

static INT_PTR CALLBACK ConfigProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemTextW(hDlg, IDC_KEY, g_dlg.key.c_str());
        SetDlgItemTextW(hDlg, IDC_THRESH, g_dlg.threshold.c_str());
        SetDlgItemTextW(hDlg, IDC_REFRESH, g_dlg.refresh.c_str());
        SetDlgItemTextW(hDlg, IDC_DSH, g_dlg.dshCmd.c_str());
        CheckDlgButton(hDlg, IDC_AUTORUN, g_dlg.autoStart ? BST_CHECKED : BST_UNCHECKED);
        SetWindowTextW(hDlg, L"DeepSeek 桌宠设置");
        SendMessageW(hDlg, DM_SETDEFID, IDOK, 0);
        SetWindowPos(hDlg, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(hDlg);
        SetFocus(GetDlgItem(hDlg, IDC_KEY));
        return FALSE;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDOK: {
            DlgState cur;
            ReadDlg(hDlg, cur);
            if (cur.key.empty()) {
                if (MessageBoxW(hDlg, L"还没有填写 API Key，桌宠将无法显示余额。\n\n仍然保存吗？",
                                L"DeepSeek 桌宠设置", MB_YESNO | MB_ICONWARNING) != IDYES) {
                    SetFocus(GetDlgItem(hDlg, IDC_KEY));
                    return TRUE;
                }
            }
            g_dlg = cur;
            g_dlg.ok = true;
            DestroyWindow(hDlg);
            return TRUE;
        }
        case IDCANCEL:
            if (DlgChanged(hDlg)) {
                if (MessageBoxW(hDlg, L"放弃刚才的修改吗？\n（选“否”可继续编辑，用“确定”保存）",
                                L"DeepSeek 桌宠设置", MB_YESNO | MB_ICONQUESTION) != IDYES)
                    return TRUE;
            }
            g_dlg.ok = false;
            DestroyWindow(hDlg);
            return TRUE;
        }
        break;

    case WM_CLOSE:
        if (DlgChanged(hDlg)) {
            int r = MessageBoxW(hDlg,
                L"设置已修改，是否保存？\n\n“是”=保存并关闭　“否”=不保存关闭　“取消”=继续编辑",
                L"DeepSeek 桌宠设置", MB_YESNOCANCEL | MB_ICONQUESTION);
            if (r == IDCANCEL) return TRUE;
            if (r == IDYES) {
                DlgState cur;
                ReadDlg(hDlg, cur);
                g_dlg = cur;
                g_dlg.ok = true;
            } else {
                g_dlg.ok = false;
            }
        } else {
            g_dlg.ok = false;
        }
        DestroyWindow(hDlg);
        return TRUE;

    case WM_DESTROY:
        g.hConfigDlg = nullptr;
        return TRUE;
    }
    return FALSE;
}

static void ShowConfigDialog() {
    if (g.hConfigDlg) { SetForegroundWindow(g.hConfigDlg); return; }

    g_dlg.key = g.cfg.apiKey;
    wchar_t num[64];
    swprintf_s(num, L"%.2f", g.cfg.lowThreshold); g_dlg.threshold = num;
    swprintf_s(num, L"%d", g.cfg.refreshSec);     g_dlg.refresh = num;
    g_dlg.dshCmd = g.cfg.dshCommand;
    g_dlg.autoStart = g.cfg.autoStart;
    g_dlg.ok = false;
    g_dlgRef = g_dlg;

    // 内存里组装对话框模板（控件计数必须写对，否则后面的控件不会被创建）
    std::vector<BYTE> buf(8192, 0);
    BYTE* cur = buf.data();
    DLGTEMPLATE* dlg = (DLGTEMPLATE*)cur;
    dlg->style = DS_MODALFRAME | DS_CENTER | DS_SETFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    dlg->dwExtendedStyle = 0;
    dlg->cdit = 13;
    dlg->x = 0; dlg->y = 0; dlg->cx = 356; dlg->cy = 174;
    cur += sizeof(DLGTEMPLATE);

    auto putW = [&](WORD v) { *(WORD*)cur = v; cur += sizeof(WORD); };
    auto putStr = [&](const wchar_t* s) {
        while (*s) { *(WORD*)cur = (WORD)*s++; cur += sizeof(WORD); }
        *(WORD*)cur = 0; cur += sizeof(WORD);
    };
    auto align4 = [&]() { size_t off = (size_t)(cur - buf.data()); cur += (4 - (off % 4)) % 4; };

    putW(0); putW(0);
    putStr(L"DeepSeek 桌宠设置");
    putW(9);
    putStr(L"Microsoft YaHei UI");

    auto control = [&](DWORD style, DWORD exStyle, short x, short y, short cx, short cy,
                       WORD id, WORD cls, const wchar_t* text) {
        align4();
        DLGITEMTEMPLATE* it = (DLGITEMTEMPLATE*)cur;
        it->style = style;
        it->dwExtendedStyle = exStyle;
        it->x = x; it->y = y; it->cx = cx; it->cy = cy;
        it->id = id;
        cur += sizeof(DLGITEMTEMPLATE);
        putW(0xFFFF); putW(cls);
        putStr(text);
        putW(0);
    };

    const DWORD kLabel = WS_CHILD | WS_VISIBLE | SS_LEFT;
    const DWORD kEdit  = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL;
    const DWORD kBtn   = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON;
    const DWORD kChk   = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX;

    control(kLabel, 0, 10, 12, 62, 12, (WORD)-1, 0x0082, L"API Key：");
    control(kEdit | ES_PASSWORD, WS_EX_CLIENTEDGE, 76, 10, 268, 14, IDC_KEY, 0x0081, L"");
    control(kLabel, 0, 10, 36, 102, 12, (WORD)-1, 0x0082, L"低余额阈值（元）：");
    control(kEdit, WS_EX_CLIENTEDGE, 116, 34, 68, 14, IDC_THRESH, 0x0081, L"");
    control(kLabel, 0, 196, 36, 84, 12, (WORD)-1, 0x0082, L"刷新间隔（秒）：");
    control(kEdit, WS_EX_CLIENTEDGE, 284, 34, 60, 14, IDC_REFRESH, 0x0081, L"");
    control(kLabel, 0, 10, 60, 120, 12, (WORD)-1, 0x0082, L"Harness 启动命令：");
    control(kEdit, WS_EX_CLIENTEDGE, 10, 74, 334, 14, IDC_DSH, 0x0081, L"");
    control(kLabel, 0, 10, 94, 334, 12, (WORD)-1, 0x0082,
            L"留空则自动探测 dsh；也可填写 DeepSeek Harness.exe 的完整路径。");
    control(kChk, 0, 10, 112, 170, 14, IDC_AUTORUN, 0x0080, L"开机自动启动桌宠");
    control(kBtn | BS_DEFPUSHBUTTON, 0, 20, 142, 90, 22, IDOK, 0x0080, L"确定(&O)");
    control(kBtn, 0, 122, 142, 90, 22, IDCANCEL, 0x0080, L"取消(&C)");
    control(kLabel, 0, 222, 145, 124, 16, IDC_TIP, 0x0082, L"回车保存 · Esc 取消");

    g_dlg.applied = false;
    g.hConfigDlg = CreateDialogIndirectParamW(g.hInst, dlg, g.hwnd, ConfigProc, 0);
    if (g.hConfigDlg) ShowWindow(g.hConfigDlg, SW_SHOW);
    else Log(L"创建设置窗口失败 (err=%lu)", GetLastError());
}

static void ApplyConfigFromDialog() {
    if (!g_dlg.ok || g_dlg.applied) return;
    g_dlg.applied = true;
    g.cfg.apiKey = g_dlg.key;
    g.cfg.lowThreshold = _wtof(g_dlg.threshold.c_str());
    if (g.cfg.lowThreshold <= 0) g.cfg.lowThreshold = 1.0;
    g.cfg.refreshSec = _wtoi(g_dlg.refresh.c_str());
    if (g.cfg.refreshSec < 30) g.cfg.refreshSec = 300;
    g.cfg.dshCommand = g_dlg.dshCmd;
    g.cfg.autoStart = g_dlg.autoStart;
    g.cfg.scale = g.zoom;
    g.cfg.save();
    ToggleAutoStart();
    g.bal.loaded = false;
    StartRefresh();
    SetTimer(g.hwnd, kTimerRefresh, (UINT)g.cfg.refreshSec * 1000, nullptr);
    SelectSprite();
    BuildRows();
    SyncWindow(true);
    Log(L"设置已保存");
}

// ============================================================ 托盘
// 打开系统文件选择对话框，返回所选文件路径（取消则返回空）
static wstring PickImageFile(HWND owner) {
    IFileOpenDialog* dlg = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dlg));
    if (FAILED(hr) || !dlg) {
        Log(L"创建文件对话框失败 hr=0x%08X", (unsigned)hr);
        return wstring();
    }

    const COMDLG_FILTERSPEC filters[] = {
        { L"图片文件（png/jpg/gif/webp/bmp）", L"*.png;*.jpg;*.jpeg;*.gif;*.webp;*.bmp" },
        { L"所有文件", L"*.*" },
    };
    dlg->SetFileTypes(2, filters);
    dlg->SetTitle(L"选择桌宠素材（支持动图 gif / webp，透明背景最佳）");
    dlg->SetOptions(FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);

    wstring result;
    hr = dlg->Show(owner);
    if (SUCCEEDED(hr)) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item)) && item) {
            PWSTR psz = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
                result = psz;
                CoTaskMemFree(psz);
            }
            item->Release();
        }
    }
    dlg->Release();
    return result;
}

static void OpenMaterialsFolder() {
    wstring dir = MaterialsDir();
    ShellExecuteW(g.hwnd, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// 从文件加载素材：复制到 materials 目录 -> 载入 -> 记住
static bool LoadMaterialFromFile() {
    wstring pick = PickImageFile(g.hwnd);
    if (pick.empty()) { Log(L"用户取消了素材选择"); return false; }

    wstring local = ImportMaterialFile(pick);
    if (local.empty()) {
        MessageBoxW(g.hwnd, L"复制素材文件失败，请检查磁盘权限。", L"更换素材", MB_OK | MB_ICONWARNING);
        return false;
    }

    g.cfg.customMaterial = local;
    FrameSet tmp;
    if (!LoadCustomMaterialInto(local, tmp)) {
        MessageBoxW(g.hwnd,
            L"这个文件无法解码成图片，或尺寸过大（超过 4096 像素）。\n"
            L"请换一张 png / jpg / gif / webp 试试。",
            L"更换素材", MB_OK | MB_ICONWARNING);
        return false;
    }
    g.fsCustom.release();
    g.fsCustom = std::move(tmp);
    g.fsCustom.applyScale(g.zoom);
    g.customActive = true;
    g.cfg.useCustomMaterial = true;
    g.cfg.save();
    SelectSprite();
    Notify(L"素材已更换（可在「更换素材」里切回内置）");
    return true;
}

static void AddTray() {
    if (g.trayAdded) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g.hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = g.hIcon ? g.hIcon : LoadIconW(nullptr, IDI_APPLICATION);
    wcsncpy_s(nid.szTip, L"DeepSeek 桌宠（双击打开面板）", _TRUNCATE);
    g.trayAdded = Shell_NotifyIconW(NIM_ADD, &nid) == TRUE;
}

static void ShowTrayMenu() {
    POINT pt;
    GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_TRAY_SHOW, L"显示余额面板");
    AppendMenuW(menu, MF_STRING, IDM_TRAY_REFRESH, L"刷新余额");
    AppendMenuW(menu, MF_STRING, IDM_TRAY_CONFIG, L"设置…");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_TRAY_QUIT, L"退出");
    SetForegroundWindow(g.hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, g.hwnd, nullptr);
    DestroyMenu(menu);
}

// ============================================================ 行点击
// 「更换素材」二级菜单的条目点击
static void HandleMaterialRow(int row) {
    switch (row) {
    case MAT_BUILTIN:
        if (ApplyMaterialChoice(false)) {
            g.cfg.save();
            SelectSprite();
            BuildRows();
            Notify(L"已切回内置素材");
        }
        break;

    case MAT_CUSTOM:
        if (ApplyMaterialChoice(true)) {
            g.cfg.save();
            SelectSprite();
            BuildRows();
            Notify(L"已切换到自定义素材");
        } else {
            MessageBoxW(g.hwnd,
                L"还没有可用的自定义素材。\n请先用「从文件加载素材…」选择一张图片。",
                L"更换素材", MB_OK | MB_ICONINFORMATION);
        }
        break;

    case MAT_LOAD:
        if (LoadMaterialFromFile()) {
            SelectSprite();
            BuildRows();
        }
        break;

    case MAT_FOLDER:
        OpenMaterialsFolder();
        break;

    case MAT_RESET:
        if (MessageBoxW(g.hwnd, L"删除当前自定义素材并恢复内置形象？",
                        L"更换素材", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            if (!g.cfg.customMaterial.empty()) DeleteFileW(g.cfg.customMaterial.c_str());
            g.cfg.customMaterial.clear();
            g.cfg.useCustomMaterial = false;
            g.cfg.save();
            ApplyMaterialChoice(false);
            SelectSprite();
            BuildRows();
            Notify(L"已恢复内置素材");
        }
        break;

    case MAT_BACK:
        SetMaterialsMode(false);
        break;
    }
}

static void HandleRow(int row) {
    if (g.materialsMode) { HandleMaterialRow(row); return; }
    switch (row) {
    case ROW_REFRESH: StartRefresh(); break;
    case ROW_COPY:    CopyBalanceToClipboard(); Notify(L"余额已复制到剪贴板"); break;
    case ROW_ZOOM_IN: ApplyZoom(g.zoom + kScaleStep); break;
    case ROW_ZOOM_OUT: ApplyZoom(g.zoom - kScaleStep); break;
    case ROW_ZOOM_RESET: ApplyZoom(1.0); break;
    case ROW_MATERIAL: SetMaterialsMode(true); break;
    case ROW_CONFIG:  ShowConfigDialog(); break;
    case ROW_LAUNCH:  LaunchHarness(); break;
    case ROW_TOPMOST:
        g.topMost = !g.topMost;
        BuildRows();
        SetWindowPos(g.hwnd, g.topMost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        Render();
        break;
    case ROW_QUIT: DestroyWindow(g.hwnd); break;
    }
}

// ============================================================ 窗口过程
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_TIMER:
        if (wp == kTimerTick) {
            // 首次运行延迟弹出设置窗口（非模态）
            if (g.configDelay > 0) {
                if (--g.configDelay == 0) ShowConfigDialog();
            } else if (g.configDelay == 0) {
                if (!g.cfg.apiKey.empty()) g.configDelay = -1;
            }
            if (g.configDelay == 0 && g.hConfigDlg == nullptr && g_dlg.ok && !g_dlg.applied) {
                g.configDelay = -1;
                ApplyConfigFromDialog();
            }

            // 位置平滑
            if (!g.dragging && (g.posX != g.posTX || g.posY != g.posTY)) {
                g.posX += (g.posTX - g.posX) / 3;
                g.posY += (g.posTY - g.posY) / 3;
                if (abs(g.posTX - g.posX) <= 1) g.posX = g.posTX;
                if (abs(g.posTY - g.posY) <= 1) g.posY = g.posTY;
                SetWindowPos(hwnd, nullptr, g.posX, g.posY, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                Render();
            }

            // 动画
            if (g.active && g.active->count() > 1) {
                ULONGLONG now = GetTickCount64();
                int interval = (std::max)(16, g.active->frameMs);
                if (now - g.lastAnim >= (ULONGLONG)interval) {
                    g.lastAnim = now;
                    g.active->index = (g.active->index + 1) % g.active->count();
                    Render();
                }
            }

            UpdateHover();

            // 缩放后延迟保存
            if (g.zoomDirty && --g.zoomSaveTicks <= 0) {
                g.cfg.scale = g.zoom;
                g.cfg.winX = g.posX;
                g.cfg.winY = g.posY;
                g.cfg.save();
                g.zoomDirty = false;
            }
        } else if (wp == kTimerRefresh) {
            StartRefresh();
        }
        return 0;

    case WM_BALANCE_DONE:
        SelectSprite();
        BuildRows();
        SyncWindow(true);
        return 0;

    case WM_MOUSEMOVE: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (g.dragging) {
            POINT sc = pt;
            ClientToScreen(hwnd, &sc);
            g.posX = g.posTX = sc.x - g.dragOff.x;
            g.posY = g.posTY = sc.y - g.dragOff.y;
            SetWindowPos(hwnd, nullptr, g.posX, g.posY, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            Render();
            if (abs(sc.x - g.dragDownScreen.x) > 3 || abs(sc.y - g.dragDownScreen.y) > 3)
                g.moved = true;
        }
        UpdateHover();
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        g.hoverRow = -1;
        if (!g.pinned) {
            g.materialsMode = false;   // 收起时退回主菜单
            SetPanelShown(false);
            BuildRows();
        } else {
            Render();
        }
        return 0;

    case WM_MOUSEWHEEL: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &pt);
        Rect pr = PanelRect();
        bool onPanel = g.panelShown && pt.x >= pr.X && pt.x < pr.X + pr.Width &&
                       pt.y >= pr.Y && pt.y < pr.Y + pr.Height;
        if (!onPanel) {
            int d = GET_WHEEL_DELTA_WPARAM(wp);
            ApplyZoom(g.zoom + (d > 0 ? kScaleStep : -kScaleStep));
        }
        return 0;
    }

    case WM_KEYDOWN:
        if (wp == VK_OEM_PLUS || wp == VK_ADD) ApplyZoom(g.zoom + kScaleStep);
        else if (wp == VK_OEM_MINUS || wp == VK_SUBTRACT) ApplyZoom(g.zoom - kScaleStep);
        else if (wp == VK_ESCAPE) {
            if (g.materialsMode) SetMaterialsMode(false);   // 先退回主菜单
            else { g.pinned = false; SetPanelShown(false); }
        }
        return 0;

    case WM_LBUTTONDOWN: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        g.dragDownScreen = pt;
        ClientToScreen(hwnd, &g.dragDownScreen);
        g.moved = false;
        bool onPanel = false;
        if (g.panelShown) {
            Rect pr = PanelRect();
            onPanel = pt.x >= pr.X && pt.x < pr.X + pr.Width && pt.y >= pr.Y && pt.y < pr.Y + pr.Height;
        }
        if (!onPanel) {
            g.dragging = true;
            g.dragOff = pt;
            SetCapture(hwnd);
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (g.dragging) {
            g.dragging = false;
            ReleaseCapture();
            if (!g.moved) {
                g.pinned = !g.pinned;
                if (g.pinned) SetPanelShown(true);
                else {
                    POINT cur;
                    GetCursorPos(&cur);
                    RECT wr{ g.posX, g.posY, g.posX + WinW(), g.posY + WinH() };
                    if (!(cur.x >= wr.left && cur.x < wr.right && cur.y >= wr.top && cur.y < wr.bottom))
                        SetPanelShown(false);
                }
                g.cfg.winX = g.posX;
                g.cfg.winY = g.posY;
                g.cfg.save();
                Render();
            } else {
                g.cfg.winX = g.posX;
                g.cfg.winY = g.posY;
                g.cfg.save();
            }
            return 0;
        }
        if (g.panelShown) {
            Rect pr = PanelRect();
            if (pt.x >= pr.X && pt.x < pr.X + pr.Width && pt.y >= pr.Y && pt.y < pr.Y + pr.Height) {
                int rel = pt.y - pr.Y - g.rowsTop;
                if (rel >= 0) {
                    int idx = rel / kRowH;
                    if (idx >= 0 && idx < (int)g.rows.size()) HandleRow(idx);
                }
            }
        }
        return 0;
    }

    case WM_RBUTTONUP: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ClientToScreen(hwnd, &pt);
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, IDM_TRAY_REFRESH, L"刷新余额");
        AppendMenuW(menu, MF_STRING, IDM_TRAY_CONFIG, L"设置…");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, IDM_TRAY_QUIT, L"退出");
        SetForegroundWindow(hwnd);
        int cmd = (int)TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, hwnd, nullptr);
        DestroyMenu(menu);
        if (cmd) SendMessageW(hwnd, WM_COMMAND, (WPARAM)cmd, 0);
        return 0;
    }

    case WM_NCHITTEST: {
        // pt 是屏幕坐标；同样用固定屏幕矩形判定，和悬停判定保持一致
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        const int sx0 = SpriteScreenX();
        const int sy0 = SpriteScreenY();
        if (g.active && !g.active->shown.empty() &&
            pt.x >= sx0 && pt.x < sx0 + SpriteW() &&
            pt.y >= sy0 && pt.y < sy0 + SpriteH() &&
            g.active->hitTest(pt.x - sx0, pt.y - sy0))
            return HTCLIENT;
        if (g.panelShown) {
            Rect pr = PanelRect();
            int px0 = g.posX + pr.X, py0 = g.posY + pr.Y;
            if (pt.x >= px0 && pt.x < px0 + pr.Width && pt.y >= py0 && pt.y < py0 + pr.Height)
                return HTCLIENT;
        }
        return HTTRANSPARENT;
    }

    case WM_TRAYICON:
        if (LOWORD(lp) == WM_LBUTTONDBLCLK) {
            g.pinned = true;
            SetPanelShown(true);
        } else if (LOWORD(lp) == WM_RBUTTONUP) {
            ShowTrayMenu();
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_TRAY_SHOW:    g.pinned = true; SetPanelShown(true); break;
        case IDM_TRAY_REFRESH: StartRefresh(); break;
        case IDM_TRAY_CONFIG:  ShowConfigDialog(); break;
        case IDM_TRAY_QUIT:    DestroyWindow(hwnd); break;
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, kTimerTick);
        KillTimer(hwnd, kTimerRefresh);
        if (g.trayAdded) {
            NOTIFYICONDATAW nid{};
            nid.cbSize = sizeof(nid);
            nid.hWnd = hwnd;
            nid.uID = 1;
            Shell_NotifyIconW(NIM_DELETE, &nid);
            g.trayAdded = false;
        }
        g.cfg.winX = g.posX;
        g.cfg.winY = g.posY;
        g.cfg.scale = g.zoom;
        g.cfg.save();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ============================================================ 单实例
static bool EnsureSingleInstance() {
    HANDLE mtx = CreateMutexW(nullptr, TRUE, kMutexName);
    if (mtx && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND prev = FindWindowW(kWndClass, nullptr);
        if (prev) {
            PostMessageW(prev, WM_SECOND_INST, 0, 0);
            SetForegroundWindow(prev);
        }
        return false;
    }
    return true;
}

// ============================================================ 入口
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    SetProcessDPIAware();
    g.hInst = hInst;

    GdiplusStartupInput gsi;
    ULONG_PTR token = 0;
    if (GdiplusStartup(&token, &gsi, nullptr) != Ok) {
        MessageBoxW(nullptr, L"GDI+ 初始化失败", kAppName, MB_ICONERROR);
        return 1;
    }

    if (!EnsureSingleInstance()) { GdiplusShutdown(token); return 0; }

    g.cfg.load();
    Log(L"==== 启动 DeepSeekPet (缩放 %.0f%%) ====", g.cfg.scale * 100.0);

    // 先装内置素材（常驻内存，作为兜底），再按配置尝试加载自定义素材
    LoadFrameSet(L"frames_normal", IDR_PET_NORMAL, g.fsBuiltin, 45);
    LoadFrameSet(L"frames_low", IDR_PET_LOW, g.fsLow, 90);
    if (g.cfg.useCustomMaterial && !g.cfg.customMaterial.empty()) {
        if (!ApplyMaterialChoice(true)) {
            Log(L"自定义素材不可用，回退内置素材: %s", g.cfg.customMaterial.c_str());
            g.cfg.useCustomMaterial = false;
            ApplyMaterialChoice(false);
        }
    }
    if (g.fsBuiltin.empty() && g.fsLow.empty()) {
        MessageBoxW(nullptr, L"桌宠素材缺失，程序无法启动。\n请确认 exe 同目录下有 assets 文件夹。",
                    kAppName, MB_ICONERROR);
        GdiplusShutdown(token);
        return 1;
    }

    g.zoom = g.cfg.scale;
    if (g.zoom < kScaleMin || g.zoom > kScaleMax) g.zoom = 1.0;
    SelectSprite();

    g.fTitle = MakeFont(L"Microsoft YaHei UI", 17.0f, FontStyleBold);
    g.fBig   = MakeFont(L"Microsoft YaHei UI", 26.0f, FontStyleBold);
    g.fText  = MakeFont(L"Microsoft YaHei UI", 15.0f, FontStyleRegular);
    g.fSmall = MakeFont(L"Microsoft YaHei UI", 13.0f, FontStyleRegular);

    g.topMost = true;
    BuildRows();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
    wc.hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_PET), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = kWndClass;
    if (!RegisterClassExW(&wc)) {
        Log(L"RegisterClassExW 失败 (err=%lu)", GetLastError());
        GdiplusShutdown(token);
        return 1;
    }
    g.hIcon = wc.hIcon;

    const int sw = GetSystemMetrics(SM_CXSCREEN);
    const int sh = GetSystemMetrics(SM_CYSCREEN);
    // 配置里存的是【桌宠左上角】的屏幕坐标；允许在屏幕外，不做夹回
    int sx, sy;
    if (g.cfg.winX >= 0) {
        sx = g.cfg.winX;
        sy = g.cfg.winY;
    } else {
        sx = sw - SpriteW() - 60;
        sy = sh - SpriteH() - 80;
    }
    // 创建窗口时高度等于桌宠高度，纵向偏移为 0
    g.posX = g.posTX = sx - SpriteX();
    g.posY = g.posTY = sy;

    g.hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                             kWndClass, kAppName, WS_POPUP,
                             g.posX, g.posY, WinW(), WinH(),
                             nullptr, nullptr, hInst, nullptr);
    if (!g.hwnd) {
        Log(L"CreateWindow 失败 (err=%lu)", GetLastError());
        MessageBoxW(nullptr, L"创建窗口失败", kAppName, MB_ICONERROR);
        GdiplusShutdown(token);
        return 1;
    }

    Log(L"窗口创建成功 桌宠 %dx%d @ (%d,%d)", SpriteW(), SpriteH(), sx, sy);

    ShowWindow(g.hwnd, SW_SHOWNOACTIVATE);
    SetWindowPos(g.hwnd, HWND_TOPMOST, g.posX, g.posY, WinW(), WinH(),
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    Render();
    AddTray();

    SetTimer(g.hwnd, kTimerTick, kAnimTickMs, nullptr);
    SetTimer(g.hwnd, kTimerRefresh, (UINT)g.cfg.refreshSec * 1000, nullptr);
    StartRefresh();

    if (g.cfg.apiKey.empty()) {
        g.configDelay = 40;   // 约 1 秒后弹出设置窗口
        Notify(L"还没有配置 API Key，稍后会弹出设置窗口");
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g.memBmp) DeleteObject(g.memBmp);
    if (g.memDC) DeleteDC(g.memDC);
    g.fsBuiltin.release();
    g.fsLow.release();
    g.fsCustom.release();
    delete g.fTitle; delete g.fBig; delete g.fText; delete g.fSmall;
    GdiplusShutdown(token);
    Log(L"==== 退出 ====");
    return 0;
}
