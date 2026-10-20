// tmdq_chart.cpp - the chart renderer for the WPF D3DImage spike.
//
// A native Direct2D renderer (the one dotnet/chart-spike measured) drawing into a Direct3D 11
// texture that is SHARED with a Direct3D 9Ex device, because that is the only thing WPF's
// D3DImage accepts: D3DImage.SetBackBuffer(IDirect3DSurface9). The chain is
//
//     Direct2D  ->  D3D11 shared texture  ==(DXGI shared handle)==  D3D9Ex texture surface  ->  WPF
//
// The geometry code (culling + per-pixel-column min/max/mean envelope) is the same arithmetic as
// TmChart::rebuildSeriesGeometry() and chart-spike's buildGeometry(); it is copied rather than
// shared because the spike directories are deliberately independent of each other.
//
// Flat C ABI, same style as tmdq_abi.h, so the WPF host binds it with P/Invoke and nothing here
// knows about WPF.

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include <d2d1_1.h>
#include <d3d11.h>
#include <d3d9.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

extern "C" {

struct tmdq_chart_timing
{
    double geometry_ms;   ///< culling + envelope, CPU
    double raster_ms;     ///< BeginDraw .. EndDraw .. Flush, CPU cost of issuing the draw
    double gpu_wait_ms;   ///< waiting for the GPU to finish (0 when the fence is off)
    int32_t segments;     ///< line segments drawn, all series
};

} // extern "C"

namespace {

struct Seg { float x1, y1, x2, y2; };

struct Series
{
    std::vector<double> xs, ys;
};

constexpr int   kMaxSamplesPerPixelColumn = 2;  // PlotConstants::kMaxSamplesPerPixelColumn
constexpr float kPenWidth = 1.5f;

double msSince(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

Series makeSeries(int n, uint32_t seed)
{
    Series s;
    s.xs.resize(n);
    s.ys.resize(n);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> noise(-3.0, 3.0);
    for (int i = 0; i < n; ++i)
    {
        s.xs[i] = static_cast<double>(i);
        s.ys[i] = 20.0 + 10.0 * std::sin(i / 5000.0 + seed) + noise(rng);
    }
    return s;
}

void buildGeometry(const Series& s, int plot_w, int plot_h, double xl, double xu, double yl, double yu,
                   std::vector<Seg>& segs, std::vector<D2D1_POINT_2F>& band)
{
    segs.clear();
    band.clear();

    int begin = 0, end = static_cast<int>(s.xs.size());
    {
        const auto lo = std::lower_bound(s.xs.begin(), s.xs.end(), xl);
        const auto hi = std::upper_bound(s.xs.begin(), s.xs.end(), xu);
        begin = std::max(0, static_cast<int>(lo - s.xs.begin()) - 1);
        end   = std::min(end, static_cast<int>(hi - s.xs.begin()) + 1);
    }

    const double xspan = (xu - xl) != 0.0 ? (xu - xl) : 1.0;
    const double yspan = (yu - yl) != 0.0 ? (yu - yl) : 1.0;
    auto px = [&](double v) { return static_cast<float>((v - xl) / xspan * plot_w); };
    auto py = [&](double v) { return static_cast<float>(plot_h - (v - yl) / yspan * plot_h); };

    const bool decimate = (end - begin) > plot_w * kMaxSamplesPerPixelColumn;

    float prev_x = 0, prev_y = 0;
    bool have_prev = false;
    auto addPoint = [&](float x, float y) {
        if (have_prev) { segs.push_back({ prev_x, prev_y, x, y }); }
        prev_x = x; prev_y = y; have_prev = true;
    };

    if (!decimate)
    {
        for (int i = begin; i < end; ++i) { addPoint(px(s.xs[i]), py(s.ys[i])); }
        return;
    }

    std::vector<D2D1_POINT_2F> tops, bottoms;
    int column = INT32_MIN;
    float col_x = 0, lo = 0, hi = 0;
    double sum = 0;
    int count = 0;
    auto flushColumn = [&]() {
        if (column == INT32_MIN) { return; }
        tops.push_back({ col_x, lo });
        bottoms.push_back({ col_x, hi });
        addPoint(col_x, static_cast<float>(sum / count));
        column = INT32_MIN;
    };
    for (int i = begin; i < end; ++i)
    {
        const float x = px(s.xs[i]);
        const float y = py(s.ys[i]);
        const int col = static_cast<int>(std::floor(x));
        if (col != column)
        {
            flushColumn();
            column = col; col_x = x; lo = y; hi = y; sum = y; count = 1;
        }
        else
        {
            lo = std::min(lo, y); hi = std::max(hi, y); sum += y; ++count;
        }
    }
    flushColumn();
    for (const auto& p : tops) { band.push_back(p); }
    for (size_t i = bottoms.size(); i-- > 0; ) { band.push_back(bottoms[i]); }
}

struct Chart
{
    int w = 0, h = 0;
    std::string adapter;
    bool hardware = false;
    std::string last_error;

    ComPtr<ID3D11Device>         d3d;
    ComPtr<ID3D11DeviceContext>  d3dctx;
    ComPtr<ID3D11Query>          fence;
    ComPtr<ID2D1Factory1>        factory;
    ComPtr<ID2D1Device>          d2ddev;
    ComPtr<ID2D1DeviceContext>   ctx;
    ComPtr<ID2D1SolidColorBrush> pen, fill, marker;

    ComPtr<ID3D11Texture2D>      shared_tex;
    ComPtr<ID2D1Bitmap1>         target;

    ComPtr<IDirect3D9Ex>         d9;
    ComPtr<IDirect3DDevice9Ex>   dev9;
    ComPtr<IDirect3DTexture9>    tex9;
    ComPtr<IDirect3DSurface9>    surf9;

    std::vector<Series>          series;
    std::vector<std::vector<Seg>> segs;
    std::vector<std::vector<D2D1_POINT_2F>> bands;

    bool fail(const char* what, HRESULT hr)
    {
        char b[160];
        std::snprintf(b, sizeof b, "%s failed, HRESULT 0x%08lX", what, static_cast<unsigned long>(hr));
        last_error = b;
        return false;
    }

    bool initDevices()
    {
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
                                       D3D11_SDK_VERSION, &d3d, nullptr, &d3dctx);
        hardware = SUCCEEDED(hr);
        if (!hardware)
        {
            // WARP cannot share surfaces with a D3D9 device, so this fails later and says so.
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0,
                                   D3D11_SDK_VERSION, &d3d, nullptr, &d3dctx);
        }
        if (FAILED(hr)) { return fail("D3D11CreateDevice", hr); }

        ComPtr<IDXGIDevice> dxgi;
        d3d.As(&dxgi);
        ComPtr<IDXGIAdapter> ad;
        if (SUCCEEDED(dxgi->GetAdapter(&ad)))
        {
            DXGI_ADAPTER_DESC d{};
            ad->GetDesc(&d);
            char name[256]{};
            WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, name, sizeof name - 1, nullptr, nullptr);
            adapter = name;
        }

        D2D1_FACTORY_OPTIONS opts{ D2D1_DEBUG_LEVEL_NONE };
        hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &opts,
                               reinterpret_cast<void**>(factory.GetAddressOf()));
        if (FAILED(hr)) { return fail("D2D1CreateFactory", hr); }
        if (FAILED(hr = factory->CreateDevice(dxgi.Get(), &d2ddev))) { return fail("CreateDevice", hr); }
        if (FAILED(hr = d2ddev->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &ctx))) { return fail("CreateDeviceContext", hr); }
        ctx->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        ctx->CreateSolidColorBrush(D2D1::ColorF(0.42f, 0.38f, 0.90f), &pen);
        ctx->CreateSolidColorBrush(D2D1::ColorF(0.42f, 0.38f, 0.90f, 0.25f), &fill);
        ctx->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &marker);

        D3D11_QUERY_DESC qd{};
        qd.Query = D3D11_QUERY_EVENT;
        d3d->CreateQuery(&qd, &fence);

        // The D3D9Ex device exists only to own the surface WPF composes; nothing is ever drawn on it.
        hr = Direct3DCreate9Ex(D3D_SDK_VERSION, &d9);
        if (FAILED(hr)) { return fail("Direct3DCreate9Ex", hr); }
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = GetDesktopWindow();
        pp.BackBufferFormat = D3DFMT_UNKNOWN;
        pp.BackBufferWidth = 1;
        pp.BackBufferHeight = 1;
        hr = d9->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, GetDesktopWindow(),
                                D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED | D3DCREATE_FPU_PRESERVE,
                                &pp, nullptr, &dev9);
        if (FAILED(hr)) { return fail("IDirect3D9Ex::CreateDeviceEx", hr); }
        return true;
    }

    bool createTarget(int nw, int nh)
    {
        target.Reset(); shared_tex.Reset(); surf9.Reset(); tex9.Reset();
        w = nw; h = nh;

        D3D11_TEXTURE2D_DESC td{};
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        td.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
        HRESULT hr = d3d->CreateTexture2D(&td, nullptr, &shared_tex);
        if (FAILED(hr)) { return fail("CreateTexture2D(shared)", hr); }

        ComPtr<IDXGIResource> res;
        shared_tex.As(&res);
        HANDLE shared = nullptr;
        if (FAILED(hr = res->GetSharedHandle(&shared))) { return fail("GetSharedHandle", hr); }

        // Opening the D3D11 texture on the D3D9Ex device: the shared handle goes in as the
        // out-parameter, with the D3D9 format that matches B8G8R8A8.
        hr = dev9->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &tex9, &shared);
        if (FAILED(hr)) { return fail(hardware ? "IDirect3DDevice9Ex::CreateTexture(shared)" : "D9 share (WARP cannot share)", hr); }
        if (FAILED(hr = tex9->GetSurfaceLevel(0, &surf9))) { return fail("GetSurfaceLevel", hr); }

        ComPtr<IDXGISurface> dxs;
        shared_tex.As(&dxs);
        D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (FAILED(hr = ctx->CreateBitmapFromDxgiSurface(dxs.Get(), &bp, &target))) { return fail("CreateBitmapFromDxgiSurface", hr); }
        ctx->SetTarget(target.Get());
        return true;
    }
};

} // namespace

extern "C" {

__declspec(dllexport) void* tmdq_chart_create(int w, int h)
{
    std::unique_ptr<Chart> c(new (std::nothrow) Chart());
    if (!c) { return nullptr; }
    if (!c->initDevices() || !c->createTarget(w, h))
    {
        // Keep the object so the caller can read the reason; flag it unusable by a null surface.
        return c.release();
    }
    return c.release();
}

__declspec(dllexport) const char* tmdq_chart_error(void* p) { return static_cast<Chart*>(p)->last_error.c_str(); }
__declspec(dllexport) const char* tmdq_chart_adapter(void* p) { return static_cast<Chart*>(p)->adapter.c_str(); }
__declspec(dllexport) int tmdq_chart_is_hardware(void* p) { return static_cast<Chart*>(p)->hardware ? 1 : 0; }

/// The IDirect3DSurface9* to hand to D3DImage.SetBackBuffer. Null if creation failed.
__declspec(dllexport) void* tmdq_chart_surface(void* p) { return static_cast<Chart*>(p)->surf9.Get(); }

__declspec(dllexport) int tmdq_chart_resize(void* p, int w, int h)
{
    Chart* c = static_cast<Chart*>(p);
    return c->createTarget(w, h) ? 0 : 1;
}

__declspec(dllexport) void tmdq_chart_generate(void* p, int series, int samples)
{
    Chart* c = static_cast<Chart*>(p);
    c->series.clear();
    for (int i = 0; i < series; ++i) { c->series.push_back(makeSeries(samples, 7u + i)); }
    c->segs.assign(series, {});
    c->bands.assign(series, {});
}

/// Draws one frame. `marker` is painted as a 10 px strip at the top and again at the bottom of the
/// image, encoded in the pixel colour, so a host can read both back from what is actually on screen
/// and tell whether it saw one frame or half of two. `fence` != 0 waits for the GPU to finish.
__declspec(dllexport) int tmdq_chart_render(void* p, double xl, double xu, double yl, double yu,
                                            uint32_t marker, int fence, tmdq_chart_timing* t)
{
    Chart* c = static_cast<Chart*>(p);
    if (!c->target) { return 1; }

    auto g0 = std::chrono::steady_clock::now();
    int total = 0;
    for (size_t i = 0; i < c->series.size(); ++i)
    {
        buildGeometry(c->series[i], c->w, c->h, xl, xu, yl, yu, c->segs[i], c->bands[i]);
        total += static_cast<int>(c->segs[i].size());
    }
    const double geo_ms = msSince(g0);

    auto r0 = std::chrono::steady_clock::now();
    c->ctx->BeginDraw();
    c->ctx->Clear(D2D1::ColorF(1, 1, 1, 1));
    for (size_t i = 0; i < c->series.size(); ++i)
    {
        const auto& band = c->bands[i];
        if (band.size() >= 3)
        {
            ComPtr<ID2D1PathGeometry> g;
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(c->factory->CreatePathGeometry(&g)) && SUCCEEDED(g->Open(&sink)))
            {
                sink->BeginFigure(band[0], D2D1_FIGURE_BEGIN_FILLED);
                for (size_t k = 1; k < band.size(); ++k) { sink->AddLine(band[k]); }
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                sink->Close();
                c->ctx->FillGeometry(g.Get(), c->fill.Get());
                c->ctx->DrawGeometry(g.Get(), c->pen.Get(), 1.0f);
            }
        }
        for (const Seg& s : c->segs[i])
        {
            c->ctx->DrawLine(D2D1::Point2F(s.x1, s.y1), D2D1::Point2F(s.x2, s.y2), c->pen.Get(), kPenWidth);
        }
    }

    // Frame marker: R = low byte, G = next byte, B = 0xA5 so "not a marker" is recognisable.
    c->ctx->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    c->marker->SetColor(D2D1::ColorF((marker & 0xFF) / 255.0f, ((marker >> 8) & 0xFF) / 255.0f, 0xA5 / 255.0f));
    c->ctx->FillRectangle(D2D1::RectF(0, 0, 40, 10), c->marker.Get());
    c->ctx->FillRectangle(D2D1::RectF(0, static_cast<float>(c->h - 10), 40, static_cast<float>(c->h)), c->marker.Get());
    c->ctx->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    const HRESULT hr = c->ctx->EndDraw();
    c->ctx->Flush();
    const double raster_ms = msSince(r0);
    if (FAILED(hr)) { c->fail("EndDraw", hr); return 2; }

    double wait_ms = 0;
    if (fence && c->fence)
    {
        auto w0 = std::chrono::steady_clock::now();
        c->d3dctx->End(c->fence.Get());
        BOOL done = FALSE;
        while (c->d3dctx->GetData(c->fence.Get(), &done, sizeof done, 0) != S_OK) {}
        wait_ms = msSince(w0);
    }
    else
    {
        c->d3dctx->Flush();   // submit, but do not wait
    }

    if (t) { t->geometry_ms = geo_ms; t->raster_ms = raster_ms; t->gpu_wait_ms = wait_ms; t->segments = total; }
    return 0;
}

__declspec(dllexport) void tmdq_chart_destroy(void* p) { delete static_cast<Chart*>(p); }

} // extern "C"
