// ============================================================================
//  wicdecoder.h —— 用 Windows Imaging Component 解码图片
//
//  GDI+ 在本机不能解 WebP，所以自定义素材走 WIC。
//  支持 png / jpg / bmp / gif / webp，动图可以逐帧读取。
//
//  用法：
//      WicImage img;
//      if (img.open(path)) {
//          UINT n = img.frameCount();               // 1 表示静态图
//          img.seek(0);
//          Gdiplus::Bitmap* bmp = img.toBitmap();   // 调用方负责 delete
//      }
// ============================================================================
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <windows.h>
#include <objidl.h>
#include <objbase.h>
#include <propidl.h>
#include <wincodec.h>
#include <gdiplus.h>

#include <string>
#include <vector>
#include <algorithm>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

using std::wstring;

class WicImage {
public:
    ~WicImage() { close(); }

    void close() {
        if (m_converter) { m_converter->Release(); m_converter = nullptr; }
        if (m_frame)     { m_frame->Release();     m_frame = nullptr; }
        if (m_decoder)   { m_decoder->Release();   m_decoder = nullptr; }
        if (m_factory)   { m_factory->Release();   m_factory = nullptr; }
        m_frameCount = 0;
        m_index = 0;
        m_width = m_height = 0;
        m_delayMs = 100;
    }

    bool open(const wstring& path) {
        close();
        // 需要 COM（WIC 工厂是 COM 组件）。若调用方已初始化过，会返回
        // RPC_E_CHANGED_MODE 之类的错误，这里也当作可用继续尝试。
        HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        (void)hrCo;

        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&m_factory));
        if (FAILED(hr) || !m_factory) return false;
        hr = m_factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                  WICDecodeMetadataCacheOnDemand, &m_decoder);
        if (FAILED(hr) || !m_decoder) return false;
        hr = m_decoder->GetFrameCount(&m_frameCount);
        if (FAILED(hr) || m_frameCount == 0) return false;
        return seek(0);
    }

    bool isOpen() const { return m_decoder != nullptr; }
    UINT frameCount() const { return m_frameCount; }
    UINT frameIndex() const { return m_index; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    int delayMs() const { return m_delayMs; }

    bool seek(UINT idx) {
        if (!m_decoder || m_frameCount == 0) return false;
        idx %= m_frameCount;

        if (m_converter) { m_converter->Release(); m_converter = nullptr; }
        if (m_frame)     { m_frame->Release();     m_frame = nullptr; }

        HRESULT hr = m_decoder->GetFrame(idx, &m_frame);
        if (FAILED(hr) || !m_frame) return false;

        UINT w = 0, h = 0;
        if (FAILED(m_frame->GetSize(&w, &h)) || w == 0 || h == 0) return false;
        m_width = (int)w;
        m_height = (int)h;

        hr = m_factory->CreateFormatConverter(&m_converter);
        if (FAILED(hr) || !m_converter) return false;
        hr = m_converter->Initialize(m_frame, GUID_WICPixelFormat32bppPBGRA,
                                     WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) return false;

        m_delayMs = readDelay();
        m_index = idx;
        return true;
    }

    // 当前帧 -> GDI+ Bitmap（调用方 delete）
    Gdiplus::Bitmap* toBitmap() {
        if (!m_converter || m_width <= 0 || m_height <= 0) return nullptr;

        const UINT stride = (UINT)m_width * 4;
        std::vector<BYTE> buf((size_t)stride * (size_t)m_height);
        if (FAILED(m_converter->CopyPixels(nullptr, stride, (UINT)buf.size(), buf.data())))
            return nullptr;

        Gdiplus::Bitmap* bmp = new Gdiplus::Bitmap(m_width, m_height, PixelFormat32bppARGB);
        if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok) { delete bmp; return nullptr; }

        Gdiplus::Rect r(0, 0, m_width, m_height);
        Gdiplus::BitmapData bd;
        if (bmp->LockBits(&r, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bd) != Gdiplus::Ok) {
            delete bmp;
            return nullptr;
        }

        for (int y = 0; y < m_height; ++y) {
            BYTE* dst = (BYTE*)bd.Scan0 + (size_t)y * bd.Stride;
            const BYTE* src = buf.data() + (size_t)y * stride;
            for (int x = 0; x < m_width; ++x) {
                // WIC 输出预乘 BGRA；GDI+ 需要非预乘 ARGB
                const BYTE b = src[x * 4 + 0];
                const BYTE g = src[x * 4 + 1];
                const BYTE r0 = src[x * 4 + 2];
                const BYTE a = src[x * 4 + 3];
                if (a == 0) {
                    dst[x * 4 + 0] = dst[x * 4 + 1] = dst[x * 4 + 2] = 0;
                    dst[x * 4 + 3] = 0;
                } else if (a == 255) {
                    dst[x * 4 + 0] = b;
                    dst[x * 4 + 1] = g;
                    dst[x * 4 + 2] = r0;
                    dst[x * 4 + 3] = 255;
                } else {
                    dst[x * 4 + 0] = (BYTE)((std::min)(255, (b  * 255 + a / 2) / a));
                    dst[x * 4 + 1] = (BYTE)((std::min)(255, (g  * 255 + a / 2) / a));
                    dst[x * 4 + 2] = (BYTE)((std::min)(255, (r0 * 255 + a / 2) / a));
                    dst[x * 4 + 3] = a;
                }
            }
        }
        bmp->UnlockBits(&bd);
        return bmp;
    }

private:
    // GIF：/grctlext/Delay（1/100 秒）；WebP 动图：/ANMF/Duration（毫秒）
    int readDelay() {
        int delay = 100;
        IWICMetadataQueryReader* reader = nullptr;
        if (FAILED(m_frame->GetMetadataQueryReader(&reader)) || !reader) return delay;

        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(reader->GetMetadataByName(L"/grctlext/Delay", &v)) &&
            v.vt == VT_UI2 && v.uiVal > 0)
            delay = (int)v.uiVal * 10;
        PropVariantClear(&v);

        if (delay == 100) {
            PropVariantInit(&v);
            if (SUCCEEDED(reader->GetMetadataByName(L"/ANMF/Duration", &v)) &&
                v.vt == VT_UI4 && v.uiVal > 0)
                delay = (int)v.uiVal;
            PropVariantClear(&v);
        }

        reader->Release();
        if (delay < 16) delay = 16;
        if (delay > 2000) delay = 2000;
        return delay;
    }

    IWICImagingFactory*    m_factory = nullptr;
    IWICBitmapDecoder*     m_decoder = nullptr;
    IWICBitmapFrameDecode* m_frame = nullptr;
    IWICFormatConverter*   m_converter = nullptr;
    UINT m_frameCount = 0;
    UINT m_index = 0;
    int  m_width = 0;
    int  m_height = 0;
    int  m_delayMs = 100;
};
