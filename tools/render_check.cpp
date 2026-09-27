// ============================================================================
//  离线渲染检查工具（不参与正式程序）
//  把桌宠面板 + 当前动画帧渲染成一张 PNG，用来核对排版与配色。
//  用法：面板渲染到 build\shots\offline_panel.png
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
#include <objidl.h>
#include <gdiplus.h>

#include <string>
#include <vector>
#include <cstdio>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

using namespace Gdiplus;
using std::wstring;

static const ARGB kColPanelBg   = 0xE6222735;
static const ARGB kColPanelEdge = 0x55FFFFFF;
static const ARGB kColTitle     = 0xFFFFFFFF;
static const ARGB kColBalance   = 0xFF5B9BFF;
static const ARGB kColBalanceLo = 0xFFFF7A7A;
static const ARGB kColText      = 0xFFE9EDF5;
static const ARGB kColDim       = 0xFF98A2B3;
static const ARGB kColHover     = 0x30FFFFFF;
static const ARGB kColSep       = 0x22FFFFFF;

static const int kPanelW = 272;
static const int kHeadH  = 78;
static const int kRowH   = 34;
static const int kFootH  = 28;

static Font* MakeFont(const wchar_t* family, REAL size, INT style) {
    Font* f = new Font(family, size, style, UnitPixel);
    if (f->GetLastStatus() != Ok) {
        delete f;
        f = new Font(L"Microsoft YaHei UI", size, style, UnitPixel);
        if (f->GetLastStatus() != Ok) { delete f; f = new Font(L"Segoe UI", size, style, UnitPixel); }
    }
    return f;
}

static void DrawStr(Graphics& gfx, const wstring& s, Font* font, ARGB color,
                    const RectF& box, StringAlignment align = StringAlignmentNear,
                    StringAlignment valign = StringAlignmentCenter, bool noWrap = true) {
    if (!font || s.empty()) return;
    SolidBrush brush{ Color(color) };
    StringFormat fmt;
    fmt.SetAlignment(align);
    fmt.SetLineAlignment(valign);
    if (noWrap) fmt.SetFormatFlags(StringFormatFlagsNoWrap);
    gfx.SetTextRenderingHint(TextRenderingHintAntiAlias);
    gfx.DrawString(s.c_str(), (INT)s.size(), font, box, &fmt, &brush);
}

static void DrawPanel(Graphics& gfx, int ox, int oy, bool low, int hoverRow,
                      const std::vector<wstring>& rows, bool pinned) {
    GraphicsPath path;
    REAL r = 12.0f;
    REAL x = (REAL)ox, y = (REAL)oy, pw = (REAL)kPanelW;
    REAL ph = (REAL)(kHeadH + (int)rows.size() * kRowH + kFootH);
    path.AddArc(x, y, r * 2, r * 2, 180, 90);
    path.AddArc(x + pw - r * 2, y, r * 2, r * 2, 270, 90);
    path.AddArc(x + pw - r * 2, y + ph - r * 2, r * 2, r * 2, 0, 90);
    path.AddArc(x, y + ph - r * 2, r * 2, r * 2, 90, 90);
    path.CloseFigure();
    SolidBrush bg{ Color(kColPanelBg) };
    gfx.FillPath(&bg, &path);
    Pen pen{ Color(kColPanelEdge), 1.0f };
    gfx.DrawPath(&pen, &path);

    Font* fTitle = MakeFont(L"Microsoft YaHei UI", 17.0f, FontStyleBold);
    Font* fBig   = MakeFont(L"Microsoft YaHei UI", 26.0f, FontStyleBold);
    Font* fText  = MakeFont(L"Microsoft YaHei UI", 15.0f, FontStyleRegular);
    Font* fSmall = MakeFont(L"Microsoft YaHei UI", 13.0f, FontStyleRegular);

    DrawStr(gfx, L"DeepSeek 余额", fTitle, kColTitle, RectF(x + 14.0f, y + 12.0f, pw - 28.0f, 20.0f));

    wstring big = low ? L"¥ 0.42" : L"¥ 128.76";
    DrawStr(gfx, big, fBig, low ? kColBalanceLo : kColBalance,
            RectF(x + 14.0f, y + 32.0f, pw - 28.0f, 26.0f));
    DrawStr(gfx, low ? L"余额可用  ·  CNY" : L"余额可用  ·  CNY", fSmall, kColDim,
            RectF(x + 14.0f, y + 56.0f, pw - 28.0f, 16.0f));

    Pen sep{ Color(kColSep), 1.0f };
    gfx.DrawLine(&sep, x + 10.0f, y + (REAL)kHeadH, x + pw - 10.0f, y + (REAL)kHeadH);

    SolidBrush hover{ Color(kColHover) };
    for (size_t i = 0; i < rows.size(); ++i) {
        RectF rowBox(x + 6.0f, y + (REAL)kHeadH + (REAL)i * kRowH + 2.0f, pw - 12.0f, (REAL)kRowH - 4.0f);
        if ((int)i == hoverRow) {
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
        if (i == 5) col = 0xFFFFB4B4;
        if (i == 3) col = 0xFF9BD0FF;
        DrawStr(gfx, rows[i], fText, col, RectF(rowBox.X + 10.0f, rowBox.Y, rowBox.Width - 20.0f, rowBox.Height));
    }
    DrawStr(gfx, pinned ? L"已固定：点击桌宠取消固定" : L"点击桌宠固定面板 · 右键更多",
            fSmall, kColDim, RectF(x + 14.0f, y + ph - (REAL)kFootH + 4.0f, pw - 28.0f, 16.0f));

    delete fTitle; delete fBig; delete fText; delete fSmall;
}

static Bitmap* LoadPng(const wchar_t* path) {
    Bitmap* b = new Bitmap(path);
    if (b->GetLastStatus() != Ok) { delete b; return nullptr; }
    return b;
}

int wmain(int argc, wchar_t** argv) {
    GdiplusStartupInput gsi;
    ULONG_PTR token = 0;
    GdiplusStartup(&token, &gsi, nullptr);

    const wchar_t* assets = argc > 1 ? argv[1] : L"assets";
    wstring a = assets;
    std::vector<wstring> rows = {
        L"刷新余额", L"复制余额到剪贴板", L"配置 API Key / 阈值",
        L"启动 DeepSeek Harness", L"取消总在最前", L"退出桌宠"
    };

    // 画布：左=桌宠(第 12 帧) 中=常态面板(第3项高亮) 右=低余额面板(第1项高亮)
    int W = 1600, H = 460;
    Bitmap canvas(W, H, PixelFormat32bppARGB);
    Graphics gfx(&canvas);
    gfx.SetSmoothingMode(SmoothingModeAntiAlias);
    gfx.Clear(Color(255, 245, 246, 248));

    Bitmap* fr = LoadPng((a + L"\\frames_normal\\frame_011.png").c_str());
    if (fr) {
        gfx.DrawImage(fr, RectF(30.0f, 60.0f, (REAL)fr->GetWidth(), (REAL)fr->GetHeight()),
                      0.0f, 0.0f, (REAL)fr->GetWidth(), (REAL)fr->GetHeight(), UnitPixel);
    }
    Bitmap* lowb = LoadPng((a + L"\\pet_low.png").c_str());
    if (lowb) {
        gfx.DrawImage(lowb, RectF(420.0f, 60.0f, (REAL)lowb->GetWidth(), (REAL)lowb->GetHeight()),
                      0.0f, 0.0f, (REAL)lowb->GetWidth(), (REAL)lowb->GetHeight(), UnitPixel);
    }

    DrawPanel(gfx, 800, 60, false, 3, rows, true);
    DrawPanel(gfx, 1090, 60, true, 0, rows, true);

    CLSID pngClsid;
    CLSIDFromString(L"{557cf406-1a04-11d3-9a73-0000f81ef32e}", &pngClsid);
    canvas.Save(L"build\\shots\\offline_panel.png", &pngClsid, nullptr);
    wprintf(L"offline_panel.png written\n");

    delete fr;
    delete lowb;
    GdiplusShutdown(token);
    return 0;
}
