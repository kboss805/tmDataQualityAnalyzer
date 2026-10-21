// tmdq_chart.cpp - the chart renderer for the WPF D3DImage spike and its Avalonia twin.
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
//
// Two ways out, chosen at creation:
//   - tmdq_chart_create():    a D3D11 texture shared (legacy handle) with a D3D9Ex surface, for WPF's
//                             D3DImage. The host's own fence is the only synchronisation.
//   - tmdq_chart_create_ex(w, h, TMDQ_CHART_KEYED_MUTEX): a D3D11 texture with a keyed mutex and NO
//                             D3D9 device, for a compositor that imports D3D11 textures directly
//                             (Avalonia's ICompositionGpuInterop). Each render acquires key 0 and
//                             releases key 1; the compositor acquires 1 and releases 0. That handshake,
//                             done by the GPU, is what stops the renderer drawing into a texture the
//                             compositor is still reading. tmdq_chart_set_buffers(p, n) makes it n
//                             textures, which the host rotates with tmdq_chart_select().
//
// The file also holds the screen probe both hosts check themselves with (tmdq_capture_*, DXGI
// Desktop Duplication); see the section comment further down.

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

#include <DirectXPackedVector.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <d3d9.h>
#include <dxgi1_6.h>
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
    bool keyed = false;                 ///< keyed-mutex D3D11 sharing, no D3D9 (see the file comment)
    HANDLE shared_handle = nullptr;     ///< the D3D11 texture's legacy shared handle
    double last_acquire_ms = 0.0;       ///< how long the last render waited for the compositor
    std::string adapter;
    bool hardware = false;
    std::string last_error;

    ComPtr<ID3D11Device>         d3d;
    ComPtr<ID3D11DeviceContext>  d3dctx;
    ComPtr<ID3D11Query>          fence;
    ComPtr<ID2D1Factory1>        factory;
    ComPtr<ID2D1Device>          d2ddev;
    ComPtr<ID2D1DeviceContext>   ctx;
    ComPtr<ID2D1SolidColorBrush> pen, fill, marker, orient;
    ComPtr<IDXGIKeyedMutex>      km;

    ComPtr<ID3D11Texture2D>      shared_tex;   ///< the CURRENT target (keyed mode: bufs[cur])
    ComPtr<ID2D1Bitmap1>         target;

    /// Keyed mode only: the textures a compositor imports, rendered into in turn. One is the simplest
    /// hand-over but stalls: the compositor holds a texture until its next composition, so with one the
    /// renderer can only draw every other refresh. WPF's D3DImage hides this by copying into its own
    /// front buffer; a direct-import compositor (Avalonia) needs the host to rotate buffers itself.
    struct Buf { ComPtr<ID3D11Texture2D> tex; ComPtr<IDXGIKeyedMutex> km; ComPtr<ID2D1Bitmap1> bmp; HANDLE handle = nullptr; };
    std::vector<Buf>             bufs;
    int                          nbufs = 1;
    int                          cur = 0;

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
        ctx->CreateSolidColorBrush(D2D1::ColorF(0.0f, 1.0f, 0.0f), &orient);

        D3D11_QUERY_DESC qd{};
        qd.Query = D3D11_QUERY_EVENT;
        d3d->CreateQuery(&qd, &fence);

        if (keyed)
        {
            return true;   // a compositor that imports D3D11 directly needs no D3D9 device
        }

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

    void select(int i)
    {
        cur = i;
        shared_tex = bufs[i].tex; km = bufs[i].km; target = bufs[i].bmp; shared_handle = bufs[i].handle;
        ctx->SetTarget(target.Get());
    }

    bool createTarget(int nw, int nh)
    {
        target.Reset(); shared_tex.Reset(); surf9.Reset(); tex9.Reset(); km.Reset();
        shared_handle = nullptr;
        bufs.clear();
        w = nw; h = nh;
        if (keyed)
        {
            for (int i = 0; i < nbufs; ++i)
            {
                if (!createOne()) { bufs.clear(); return false; }
                bufs.push_back(Buf{ shared_tex, km, target, shared_handle });
            }
            select(0);
            return true;
        }
        return createOne();
    }

    bool createOne()
    {

        D3D11_TEXTURE2D_DESC td{};
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        td.MiscFlags = keyed ? D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX : D3D11_RESOURCE_MISC_SHARED;
        HRESULT hr = d3d->CreateTexture2D(&td, nullptr, &shared_tex);
        if (FAILED(hr)) { return fail("CreateTexture2D(shared)", hr); }

        ComPtr<IDXGIResource> res;
        shared_tex.As(&res);
        HANDLE shared = nullptr;
        if (FAILED(hr = res->GetSharedHandle(&shared))) { return fail("GetSharedHandle", hr); }
        shared_handle = shared;

        if (keyed)
        {
            if (FAILED(hr = shared_tex.As(&km))) { return fail("IDXGIKeyedMutex", hr); }
        }
        else
        {
            // Opening the D3D11 texture on the D3D9Ex device: the shared handle goes in as the
            // out-parameter, with the D3D9 format that matches B8G8R8A8.
            hr = dev9->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &tex9, &shared);
            if (FAILED(hr)) { return fail(hardware ? "IDirect3DDevice9Ex::CreateTexture(shared)" : "D9 share (WARP cannot share)", hr); }
            if (FAILED(hr = tex9->GetSurfaceLevel(0, &surf9))) { return fail("GetSurfaceLevel", hr); }
        }

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

/// Flags for tmdq_chart_create_ex.
constexpr int TMDQ_CHART_KEYED_MUTEX = 1;

__declspec(dllexport) void* tmdq_chart_create_ex(int w, int h, int flags)
{
    std::unique_ptr<Chart> c(new (std::nothrow) Chart());
    if (!c) { return nullptr; }
    c->keyed = (flags & TMDQ_CHART_KEYED_MUTEX) != 0;
    if (!c->initDevices() || !c->createTarget(w, h))
    {
        // Keep the object so the caller can read the reason; flag it unusable by a null surface.
        return c.release();
    }
    return c.release();
}

__declspec(dllexport) void* tmdq_chart_create(int w, int h) { return tmdq_chart_create_ex(w, h, 0); }

/// The D3D11 texture's legacy shared handle, for a compositor that imports it. Null if creation failed.
__declspec(dllexport) void* tmdq_chart_shared_handle(void* p) { return static_cast<Chart*>(p)->shared_handle; }

/// Keyed mode: recreates the target as n shared textures (1..4) for the host to rotate. 0 on success.
__declspec(dllexport) int tmdq_chart_set_buffers(void* p, int n)
{
    Chart* c = static_cast<Chart*>(p);
    if (!c->keyed || n < 1 || n > 4) { return 1; }
    c->nbufs = n;
    return c->createTarget(c->w, c->h) ? 0 : 1;
}

/// Keyed mode: the shared handle of buffer i, and which buffer the next render draws into.
__declspec(dllexport) void* tmdq_chart_shared_handle_at(void* p, int i)
{
    Chart* c = static_cast<Chart*>(p);
    return i >= 0 && i < static_cast<int>(c->bufs.size()) ? c->bufs[i].handle : nullptr;
}
__declspec(dllexport) void tmdq_chart_select(void* p, int i)
{
    Chart* c = static_cast<Chart*>(p);
    if (i >= 0 && i < static_cast<int>(c->bufs.size())) { c->select(i); }
}

/// How long the last render waited to acquire the texture from the compositor (keyed mode; 0 otherwise).
__declspec(dllexport) double tmdq_chart_last_acquire_ms(void* p) { return static_cast<Chart*>(p)->last_acquire_ms; }

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

    c->last_acquire_ms = 0.0;
    if (c->km)
    {
        // Wait for the compositor to hand the texture back (it releases key 0 when it has read it).
        const auto a0 = std::chrono::steady_clock::now();
        const HRESULT acq = c->km->AcquireSync(0, 2000);
        c->last_acquire_ms = msSince(a0);
        if (acq != S_OK) { c->fail("IDXGIKeyedMutex::AcquireSync (the compositor never released it)", acq); return 3; }
    }

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
    // Orientation: a green block at the TOP right only, so a host can tell an image shown upside down.
    c->ctx->FillRectangle(D2D1::RectF(static_cast<float>(c->w - 40), 0, static_cast<float>(c->w), 10), c->orient.Get());
    c->ctx->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    const HRESULT hr = c->ctx->EndDraw();
    c->ctx->Flush();
    const double raster_ms = msSince(r0);
    if (FAILED(hr))
    {
        if (c->km) { c->km->ReleaseSync(0); }   // give it back unchanged
        c->fail("EndDraw", hr);
        return 2;
    }

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

    if (c->km)
    {
        c->km->ReleaseSync(1);   // the compositor may read it now
    }

    if (t) { t->geometry_ms = geo_ms; t->raster_ms = raster_ms; t->gpu_wait_ms = wait_ms; t->segments = total; }
    return 0;
}

__declspec(dllexport) void tmdq_chart_destroy(void* p) { delete static_cast<Chart*>(p); }

} // extern "C"

// ---------------------------------------------------------------------------------------------
// Screen capture through DXGI Desktop Duplication.
//
// The spikes check what is ON THE GLASS, not what a framework believes it drew. GDI (GetPixel /
// BitBlt on the screen DC) does that for WPF, but returns black for a window composed through
// DirectComposition, which is how Avalonia draws on Windows. Desktop Duplication reads the
// composed desktop whatever produced it, so both spikes use it and are judged the same way. It
// also has an advantage GetPixel lacks: every pixel asked for in one call comes from the SAME
// desktop frame, so a top/bottom marker mismatch is a tear and not a sampling race.
// ---------------------------------------------------------------------------------------------

namespace {

struct Capture
{
    ComPtr<ID3D11Device>           dev;
    ComPtr<ID3D11DeviceContext>    ctx;
    ComPtr<IDXGIOutput1>           output;
    ComPtr<IDXGIOutputDuplication> dup;
    RECT                           desk{};
    ComPtr<ID3D11Texture2D>        staging;
    int                            staging_w = 0, staging_h = 0;
    DXGI_FORMAT                    staging_fmt = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT                    frame_fmt = DXGI_FORMAT_UNKNOWN;   ///< the format of the frame last acquired
    float                          sdr_white = 1.0f;   ///< scRGB value of SDR white on this output (1.0 = 80 nits)
    WCHAR                          gdi_name[32]{};
    std::string                    error;
    std::string                    describe;       ///< the output's colour space and how frames are delivered
    std::string                    describe_base;

    bool fail(const char* what, HRESULT hr)
    {
        char b[160];
        std::snprintf(b, sizeof b, "%s failed, HRESULT 0x%08lX", what, static_cast<unsigned long>(hr));
        error = b;
        return false;
    }

    bool open(int x, int y)
    {
        ComPtr<IDXGIFactory1> factory;
        HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(factory.GetAddressOf()));
        if (FAILED(hr)) { return fail("CreateDXGIFactory1", hr); }
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT a = 0; factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a)
        {
            ComPtr<IDXGIOutput> out;
            for (UINT o = 0; adapter->EnumOutputs(o, &out) != DXGI_ERROR_NOT_FOUND; ++o)
            {
                DXGI_OUTPUT_DESC d{};
                out->GetDesc(&d);
                const RECT& r = d.DesktopCoordinates;
                if (x >= r.left && x < r.right && y >= r.top && y < r.bottom)
                {
                    desk = r;
                    std::memcpy(gdi_name, d.DeviceName, sizeof gdi_name);
                    if (FAILED(hr = out.As(&output))) { return fail("IDXGIOutput1", hr); }
                    hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
                                           D3D11_SDK_VERSION, &dev, nullptr, &ctx);
                    if (FAILED(hr)) { return fail("D3D11CreateDevice(capture)", hr); }
                    return duplicate();
                }
                out.Reset();
            }
            adapter.Reset();
        }
        error = "no output contains that point";
        return false;
    }

    bool duplicate()
    {
        dup.Reset();
        // Ask for FP16 first. On an HDR desktop the compositor works in scRGB with SDR content at the SDR
        // white level (240 nits = 3.0 here); an 8-bit BGRA copy is that clamped at 1.0 (80 nits), so every
        // channel brighter than about #9C clips to #FF and darker ones read 3x too bright in linear light.
        // FP16 is lossless and rgbAt() divides the white level back out. DuplicateOutput1 needs the caller
        // to be per-monitor-v2 DPI aware - WPF is not by default, which silently put it on the legacy call -
        // so the thread is made so for the call.
        ComPtr<IDXGIOutput5> out5;
        HRESULT hr = E_NOINTERFACE;
        const char* how = "DuplicateOutput";
        if (SUCCEEDED(output.As(&out5)))
        {
            const DPI_AWARENESS_CONTEXT was = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            const DXGI_FORMAT formats[] = { DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_B8G8R8A8_UNORM };
            hr = out5->DuplicateOutput1(dev.Get(), 0, 2, formats, &dup);
            if (was) { SetThreadDpiAwarenessContext(was); }
            how = "DuplicateOutput1";
        }
        if (FAILED(hr))
        {
            hr = output->DuplicateOutput(dev.Get(), &dup);
            how = "DuplicateOutput";
        }
        if (FAILED(hr)) { return fail(how, hr); }

        DXGI_OUTDUPL_DESC dd{};
        dup->GetDesc(&dd);
        std::string cs = "unknown";
        UINT bits = 0;
        ComPtr<IDXGIOutput6> out6;
        if (SUCCEEDED(output.As(&out6)))
        {
            DXGI_OUTPUT_DESC1 d1{};
            if (SUCCEEDED(out6->GetDesc1(&d1)))
            {
                bits = d1.BitsPerColor;
                cs = d1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ? "HDR (PQ / BT.2020)"
                   : d1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709   ? "SDR (sRGB)"
                   : "colour space " + std::to_string(static_cast<int>(d1.ColorSpace));
            }
        }
        sdr_white = querySdrWhite();
        char b[320];
        std::snprintf(b, sizeof b, "%s, desktop %s, %u bits per colour, SDR white %.0f nits",
                      how, cs.c_str(), bits, sdr_white * 80.0f);
        describe_base = b;
        describe = describe_base + ", no frame read yet";
        return true;
    }

    void noteFrameFormat(DXGI_FORMAT f)
    {
        describe = describe_base + (f == DXGI_FORMAT_R16G16B16A16_FLOAT ? ", frames read as FP16 scRGB (lossless)"
                                  : f == DXGI_FORMAT_B8G8R8A8_UNORM     ? ", frames read as 8-bit BGRA"
                                  : ", frames read as DXGI format " + std::to_string(static_cast<int>(f)));
    }

    /// The SDR white level Windows composes SDR content at on this output, as an scRGB value. On an HDR
    /// desktop an app's #FFFFFF lands at this value, not at 1.0, so FP16 readback divides by it to get
    /// back (close to) what the app wrote. 1.0 when it cannot be read (an SDR desktop).
    float querySdrWhite() const
    {
        UINT32 np = 0, nm = 0;
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm) != ERROR_SUCCESS) { return 1.0f; }
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(np);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(nm);
        if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np, paths.data(), &nm, modes.data(), nullptr) != ERROR_SUCCESS) { return 1.0f; }
        for (UINT32 i = 0; i < np; ++i)
        {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME src{};
            src.header = { DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof src, paths[i].sourceInfo.adapterId, paths[i].sourceInfo.id };
            if (DisplayConfigGetDeviceInfo(&src.header) != ERROR_SUCCESS || std::wcscmp(src.viewGdiDeviceName, gdi_name) != 0) { continue; }
            DISPLAYCONFIG_SDR_WHITE_LEVEL wl{};
            wl.header = { DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL, sizeof wl, paths[i].targetInfo.adapterId, paths[i].targetInfo.id };
            if (DisplayConfigGetDeviceInfo(&wl.header) == ERROR_SUCCESS && wl.SDRWhiteLevel > 0)
            {
                return static_cast<float>(wl.SDRWhiteLevel) / 1000.0f;   // 1000 = 80 nits = scRGB 1.0
            }
        }
        return 1.0f;
    }

    static uint8_t encodeSrgb(float linear)
    {
        const float v = std::clamp(linear, 0.0f, 1.0f);
        const float e = v <= 0.0031308f ? 12.92f * v : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
        return static_cast<uint8_t>(std::lround(e * 255.0f));
    }

    /// One pixel of the staging copy as 0x00RRGGBB. BGRA is read as is; an FP16 scRGB pixel (an HDR
    /// desktop) is divided by the SDR white level and sRGB-encoded - an approximation of what the app
    /// wrote, close but not exact, which is why the checks compare pixels or use thresholds.
    uint32_t rgbAt(const uint8_t* px) const
    {
        if (staging_fmt == DXGI_FORMAT_R16G16B16A16_FLOAT)
        {
            const auto* h = reinterpret_cast<const DirectX::PackedVector::HALF*>(px);
            const uint32_t r = encodeSrgb(DirectX::PackedVector::XMConvertHalfToFloat(h[0]) / sdr_white);
            const uint32_t g = encodeSrgb(DirectX::PackedVector::XMConvertHalfToFloat(h[1]) / sdr_white);
            const uint32_t b = encodeSrgb(DirectX::PackedVector::XMConvertHalfToFloat(h[2]) / sdr_white);
            return (r << 16) | (g << 8) | b;
        }
        return (static_cast<uint32_t>(px[2]) << 16) | (static_cast<uint32_t>(px[1]) << 8) | px[0];   // B, G, R, A
    }

    int bytesPerPixel() const { return staging_fmt == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8 : 4; }


    /// Acquires the next desktop frame (waiting up to timeout_ms), copies the requested rectangles
    /// into one staging texture laid out side by side, and maps it. Returns 1 with the frame mapped
    /// (the caller unmaps and releases via done()), 0 if no new frame arrived, -1 on error.
    int acquire(int timeout_ms, ComPtr<ID3D11Texture2D>& frame)
    {
        DXGI_OUTDUPL_FRAME_INFO fi{};
        ComPtr<IDXGIResource> res;
        HRESULT hr = dup->AcquireNextFrame(static_cast<UINT>(timeout_ms), &fi, &res);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) { return 0; }
        if (hr == DXGI_ERROR_ACCESS_LOST) { duplicate(); return 0; }
        if (FAILED(hr)) { fail("AcquireNextFrame", hr); return -1; }
        if (fi.LastPresentTime.QuadPart == 0)
        {
            // No new image: only the pointer moved - or this is a fresh duplication's first frame, which on
            // this machine arrives with an empty (black) texture rather than the desktop as it is.
            dup->ReleaseFrame();
            return 0;
        }
        if (FAILED(hr = res.As(&frame))) { dup->ReleaseFrame(); fail("desktop frame texture", hr); return -1; }
        D3D11_TEXTURE2D_DESC fd{};
        frame->GetDesc(&fd);
        if (fd.Format != DXGI_FORMAT_B8G8R8A8_UNORM && fd.Format != DXGI_FORMAT_R16G16B16A16_FLOAT)
        {
            dup->ReleaseFrame();
            error = "unsupported desktop frame format " + std::to_string(static_cast<int>(fd.Format));
            return -1;
        }
        if (fd.Format != frame_fmt) { noteFrameFormat(fd.Format); }
        frame_fmt = fd.Format;
        return 1;
    }

    /// A CPU-readable texture at least w x h in the format of the frame just acquired. Duplication can
    /// hand back BGRA from one call and FP16 from the next on the same desktop, so the format is checked
    /// every time rather than fixed at open.
    bool ensureStaging(int w, int h)
    {
        if (staging && staging_fmt == frame_fmt && staging_w >= w && staging_h >= h) { return true; }
        D3D11_TEXTURE2D_DESC td{};
        td.Width = static_cast<UINT>(w); td.Height = static_cast<UINT>(h); td.MipLevels = 1; td.ArraySize = 1;
        td.Format = frame_fmt; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_STAGING; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        staging.Reset();
        const HRESULT hr = dev->CreateTexture2D(&td, nullptr, &staging);
        if (FAILED(hr)) { return fail("CreateTexture2D(staging)", hr); }
        staging_w = w; staging_h = h; staging_fmt = frame_fmt;
        return true;
    }

    /// Clamps a desktop-space rectangle to this output and returns it in output space.
    bool toBox(int x, int y, int w, int h, D3D11_BOX& box) const
    {
        const int l = x - desk.left, t = y - desk.top;
        const int ow = desk.right - desk.left, oh = desk.bottom - desk.top;
        if (l < 0 || t < 0 || l + w > ow || t + h > oh) { return false; }
        box = D3D11_BOX{ static_cast<UINT>(l), static_cast<UINT>(t), 0,
                         static_cast<UINT>(l + w), static_cast<UINT>(t + h), 1 };
        return true;
    }
};

} // namespace

extern "C" {

/// Opens Desktop Duplication on the monitor that contains desktop point (x, y). Check
/// tmdq_capture_error() for an empty string to know it worked.
__declspec(dllexport) void* tmdq_capture_create(int x, int y)
{
    auto* c = new (std::nothrow) Capture();
    if (c) { c->open(x, y); }
    return c;
}

__declspec(dllexport) const char* tmdq_capture_error(void* p) { return static_cast<Capture*>(p)->error.c_str(); }
__declspec(dllexport) const char* tmdq_capture_describe(void* p) { return static_cast<Capture*>(p)->describe.c_str(); }
__declspec(dllexport) void tmdq_capture_destroy(void* p) { delete static_cast<Capture*>(p); }

/// Reads @p n desktop pixels, all from ONE desktop frame. xy holds x0, y0, x1, y1, ... in desktop
/// coordinates; out receives 0x00RRGGBB per pixel. Waits up to timeout_ms for a frame the desktop
/// has not yet shown this capture. Returns 1 if read, 0 if no new frame arrived, -1 on error.
__declspec(dllexport) int tmdq_capture_pixels(void* p, int n, const int* xy, uint32_t* out, int timeout_ms)
{
    Capture* c = static_cast<Capture*>(p);
    if (!c->dup || n <= 0) { return -1; }
    ComPtr<ID3D11Texture2D> frame;
    const int got = c->acquire(timeout_ms, frame);
    if (got != 1) { return got; }
    if (!c->ensureStaging(n, 1)) { c->dup->ReleaseFrame(); return -1; }
    for (int i = 0; i < n; ++i)
    {
        D3D11_BOX box{};
        if (!c->toBox(xy[2 * i], xy[2 * i + 1], 1, 1, box)) { c->dup->ReleaseFrame(); c->error = "point outside the output"; return -1; }
        c->ctx->CopySubresourceRegion(c->staging.Get(), 0, static_cast<UINT>(i), 0, 0, frame.Get(), 0, &box);
    }
    D3D11_MAPPED_SUBRESOURCE m{};
    const HRESULT hr = c->ctx->Map(c->staging.Get(), 0, D3D11_MAP_READ, 0, &m);
    if (SUCCEEDED(hr))
    {
        const uint8_t* row = static_cast<const uint8_t*>(m.pData);
        for (int i = 0; i < n; ++i) { out[i] = c->rgbAt(row + c->bytesPerPixel() * i); }
        c->ctx->Unmap(c->staging.Get(), 0);
    }
    c->dup->ReleaseFrame();
    if (FAILED(hr)) { c->fail("Map(staging)", hr); return -1; }
    return 1;
}

/// Copies a desktop rectangle (desktop coordinates) into out as top-down 8-bit BGRA rows of w*4 bytes
/// (converted from FP16 on an HDR desktop).
/// Same return values as tmdq_capture_pixels.
__declspec(dllexport) int tmdq_capture_region(void* p, int x, int y, int w, int h, uint8_t* out, int timeout_ms)
{
    Capture* c = static_cast<Capture*>(p);
    if (!c->dup || w <= 0 || h <= 0) { return -1; }
    ComPtr<ID3D11Texture2D> frame;
    const int got = c->acquire(timeout_ms, frame);
    if (got != 1) { return got; }
    D3D11_BOX box{};
    if (!c->toBox(x, y, w, h, box) || !c->ensureStaging(w, h)) { c->dup->ReleaseFrame(); if (c->error.empty()) { c->error = "region outside the output"; } return -1; }
    c->ctx->CopySubresourceRegion(c->staging.Get(), 0, 0, 0, 0, frame.Get(), 0, &box);
    D3D11_MAPPED_SUBRESOURCE m{};
    const HRESULT hr = c->ctx->Map(c->staging.Get(), 0, D3D11_MAP_READ, 0, &m);
    if (SUCCEEDED(hr))
    {
        for (int r = 0; r < h; ++r)
        {
            const uint8_t* src = static_cast<const uint8_t*>(m.pData) + static_cast<size_t>(r) * m.RowPitch;
            uint8_t* dst = out + static_cast<size_t>(r) * w * 4;
            for (int col = 0; col < w; ++col)
            {
                const uint32_t rgb = c->rgbAt(src + static_cast<size_t>(c->bytesPerPixel()) * col);
                dst[4 * col] = static_cast<uint8_t>(rgb); dst[4 * col + 1] = static_cast<uint8_t>(rgb >> 8);
                dst[4 * col + 2] = static_cast<uint8_t>(rgb >> 16); dst[4 * col + 3] = 0xFF;
            }
        }
        c->ctx->Unmap(c->staging.Get(), 0);
    }
    c->dup->ReleaseFrame();
    if (FAILED(hr)) { c->fail("Map(staging)", hr); return -1; }
    return 1;
}

} // extern "C"
