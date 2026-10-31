// d2d_chart_bench.cpp - does Direct2D beat QPainter for this chart's geometry?
//
// WHY THIS IS C++ AND NOT WIN2D
//
// The intended spike was Win2D (Microsoft's own Direct2D wrapper, so the chart could
// live in C#). It does not build on this machine: Microsoft.Graphics.Win2D pulls in
// Microsoft.WindowsAppSDK, whose MrtCore.PriGen.targets require Visual Studio's
// AppxPackage MSBuild tooling (Microsoft.Build.Packaging.Pri.Tasks.dll,
// Microsoft.Build.AppxPackage.dll). Only VS BuildTools is installed, without that
// workload. Setting AppxGeneratePriEnabled=false clears the first task and the build
// then fails on the next one - fighting a package's own tooling requirements with
// property hacks is not a foundation to measure on. See README.md.
//
// **Win2D IS Direct2D.** CanvasDrawingSession wraps ID2D1DeviceContext and the
// rasterizer underneath is identical, so measuring Direct2D natively answers the
// question that actually decides the chart: is this rasterizer fast enough to make the
// port's rendering story safe? What it does NOT measure is the per-call managed-to-
// native overhead Win2D would add on top, which is stated in the README rather than
// guessed at.
//
// WHAT IT MEASURES
//
// The same geometry, shapes and pen as the Qt baselines recorded in docs/claude.md
// (v2.13.0, release build, 1200x700 plot, 1.5 px non-cosmetic pen, antialiasing on):
//
//     samples/series  series   first paint   repaint
//     360,000         3        36 ms         8 ms
//     360,000         12       157 ms        34 ms
//     1,000,000       3        79 ms         13 ms
//
// "first paint" is build geometry + rasterize; "repaint" is rasterize only, from the
// cached geometry. Both are reported here so the comparison is like for like.
//
// It also answers the Direct2D analogue of this project's drawLines()-vs-drawPath()
// finding - Qt measured 1423 ms through drawPath() against 27 ms through drawLines()
// for the same geometry, because a non-cosmetic pen sends a path through Qt's stroker.
// Direct2D has no such split, but DrawLine-per-segment and one batched geometry are
// still different amounts of API traffic, so both are timed.
//
// Build + run: run_d2d_bench.ps1

// Before any Windows header. <windows.h> arrives via d3d11.h and defines min/max as
// MACROS, which turns every std::min/std::max below into
// "error C2589: '(': illegal token on right side of '::'". NOMINMAX suppresses them.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include <d3d11.h>
#include <d2d1_1.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace {

constexpr int    kPlotW = 1200;
constexpr int    kPlotH = 700;
constexpr float  kPenWidth = 1.5f;
/// PlotConstants::kMaxSamplesPerPixelColumn, the threshold TmChart decimates above.
constexpr int    kMaxSamplesPerPixelColumn = 2;

struct Series
{
    std::vector<double> xs;
    std::vector<double> ys;
};

/// One line segment in pixels, matching TmChart's QVector<QLineF>.
struct Seg { float x1, y1, x2, y2; };

/// Synthetic series shaped like a Receiver SNR channel: a slow drift with noise, which
/// is what makes the per-column envelope do real work (a flat line would collapse to
/// one column value and flatter the decimation).
Series makeSeries(int n, uint32_t seed)
{
    Series s;
    s.xs.resize(n);
    s.ys.resize(n);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> noise(-3.0, 3.0);
    for (int i = 0; i < n; ++i)
    {
        const double t = static_cast<double>(i);
        s.xs[i] = t;
        s.ys[i] = 20.0 + 10.0 * std::sin(t / 5000.0) + noise(rng);
    }
    return s;
}

/// Ports TmChart::rebuildSeriesGeometry() - the culling and the per-pixel-column
/// envelope - unchanged in substance. Pure arithmetic, which is exactly why it is the
/// part of the chart that transfers to any rasterizer.
///
/// Culling: binary search to the visible range, keeping ONE sample on each side so the
/// line still enters and leaves the view along the segment the data describes.
/// Decimation: above kMaxSamplesPerPixelColumn samples per column, each column becomes
/// a band from its min to its max plus a line through its MEAN.
void buildGeometry(const Series& s, double x_lower, double x_upper,
                   double y_lower, double y_upper,
                   std::vector<Seg>& segs, std::vector<D2D1_POINT_2F>& band)
{
    segs.clear();
    band.clear();

    int begin = 0;
    int end   = static_cast<int>(s.xs.size());
    {
        const auto lo = std::lower_bound(s.xs.begin(), s.xs.end(), x_lower);
        const auto hi = std::upper_bound(s.xs.begin(), s.xs.end(), x_upper);
        begin = std::max(0, static_cast<int>(lo - s.xs.begin()) - 1);
        end   = std::min(end, static_cast<int>(hi - s.xs.begin()) + 1);
    }

    const double xs_span = (x_upper - x_lower) != 0.0 ? (x_upper - x_lower) : 1.0;
    const double ys_span = (y_upper - y_lower) != 0.0 ? (y_upper - y_lower) : 1.0;
    auto px = [&](double vx) { return static_cast<float>((vx - x_lower) / xs_span * kPlotW); };
    auto py = [&](double vy) { return static_cast<float>(kPlotH - (vy - y_lower) / ys_span * kPlotH); };

    const bool decimate = (end - begin) > kPlotW * kMaxSamplesPerPixelColumn;

    float prev_x = 0.0f, prev_y = 0.0f;
    bool  have_prev = false;
    auto addPoint = [&](float x, float y) {
        if (have_prev) { segs.push_back({ prev_x, prev_y, x, y }); }
        prev_x = x; prev_y = y; have_prev = true;
    };

    if (!decimate)
    {
        segs.reserve(static_cast<size_t>(std::max(0, end - begin - 1)));
        for (int i = begin; i < end; ++i) { addPoint(px(s.xs[i]), py(s.ys[i])); }
        return;
    }

    segs.reserve(kPlotW);
    std::vector<D2D1_POINT_2F> tops, bottoms;
    tops.reserve(kPlotW);
    bottoms.reserve(kPlotW);

    int    column = INT32_MIN;
    float  col_x = 0.0f, lo = 0.0f, hi = 0.0f;
    double sum = 0.0;
    int    count = 0;

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
        const int   col = static_cast<int>(std::floor(x));
        if (col != column)
        {
            flushColumn();
            column = col; col_x = x; lo = y; hi = y; sum = y; count = 1;
        }
        else
        {
            lo = std::min(lo, y);
            hi = std::max(hi, y);
            sum += y;
            ++count;
        }
    }
    flushColumn();

    // Tops left-to-right then bottoms right-to-left, so closing traces the outline -
    // the same construction TmChart uses.
    band.reserve(tops.size() + bottoms.size());
    for (const auto& p : tops) { band.push_back(p); }
    for (size_t i = bottoms.size(); i-- > 0; ) { band.push_back(bottoms[i]); }
}

double msSince(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();
}

/// Blocks until the GPU has actually finished the submitted work.
///
/// NECESSARY, and getting this wrong is how a GPU benchmark reports absurd numbers.
/// Neither ID2D1DeviceContext::Flush() nor ID3D11DeviceContext::Flush() waits for
/// completion - they submit. The first version of the D3D path below measured 0.01 ms
/// for the twelve-series case, roughly 1800x "faster" than Direct2D, because it was
/// timing the CPU cost of issuing 24 draw calls and nothing else. An event query is
/// what turns a submission timing into a rendering timing.
///
/// Applied to BOTH paths, because Direct2D's Flush() does not wait either: its numbers
/// happened to look plausible only because its cost is dominated by CPU-side
/// tessellation and per-call overhead, which the timer did capture.
void waitForGpu(ID3D11DeviceContext* ctx, ID3D11Query* fence)
{
    if (!ctx || !fence) { return; }
    ctx->End(fence);
    BOOL done = FALSE;
    while (ctx->GetData(fence, &done, sizeof(done), 0) != S_OK)
    {
        // S_FALSE means still pending; spin rather than sleep, since the whole point
        // is to measure the completion time.
    }
}

struct D2D
{
    ComPtr<ID3D11Device>        d3d;
    ComPtr<ID2D1Factory1>       factory;
    ComPtr<ID2D1Device>         device;
    ComPtr<ID2D1DeviceContext>  ctx;
    ComPtr<ID2D1Bitmap1>        target;
    ComPtr<ID2D1SolidColorBrush> pen;
    ComPtr<ID2D1SolidColorBrush> fill;
    ComPtr<ID3D11DeviceContext>  d3dctx;   ///< For the GPU fence, not for drawing.
    ComPtr<ID3D11Query>          fence;
    bool hardware = false;

    bool init()
    {
        // Offscreen: a D3D11 device with no swap chain and no window at all, which is
        // what makes this measurable headlessly - and is also why render tests get
        // EASIER on this stack than with QT_QPA_PLATFORM=offscreen.
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL got{};
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                       nullptr, 0, D3D11_SDK_VERSION, &d3d, &got, nullptr);
        hardware = SUCCEEDED(hr);
        if (!hardware)
        {
            // Stated rather than hidden: WARP is Microsoft's software rasterizer, so a
            // WARP number is not a GPU number.
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                                   nullptr, 0, D3D11_SDK_VERSION, &d3d, &got, nullptr);
        }
        if (FAILED(hr)) { std::printf("  D3D11CreateDevice failed 0x%08lX\n", hr); return false; }

        ComPtr<IDXGIDevice> dxgi;
        if (FAILED(d3d.As(&dxgi))) { return false; }

        D2D1_FACTORY_OPTIONS opts{ D2D1_DEBUG_LEVEL_NONE };
        hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                               __uuidof(ID2D1Factory1), &opts,
                               reinterpret_cast<void**>(factory.GetAddressOf()));
        if (FAILED(hr)) { std::printf("  D2D1CreateFactory failed 0x%08lX\n", hr); return false; }
        if (FAILED(factory->CreateDevice(dxgi.Get(), &device))) { return false; }
        if (FAILED(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &ctx))) { return false; }

        D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (FAILED(ctx->CreateBitmap(D2D1::SizeU(kPlotW, kPlotH), nullptr, 0, &bp, &target)))
        {
            return false;
        }
        ctx->SetTarget(target.Get());
        ctx->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        ctx->CreateSolidColorBrush(D2D1::ColorF(0.42f, 0.38f, 0.90f), &pen);
        ctx->CreateSolidColorBrush(D2D1::ColorF(0.42f, 0.38f, 0.90f, 0.25f), &fill);

        d3d->GetImmediateContext(&d3dctx);
        D3D11_QUERY_DESC qd{};
        qd.Query = D3D11_QUERY_EVENT;
        d3d->CreateQuery(&qd, &fence);

        return pen && fill && d3dctx && fence;
    }
};

/// Builds the min-max band as a closed geometry. v2.14.0 added this band to the Qt
/// chart, so it is part of what the 34 ms baseline pays for - leaving it out would
/// measure less work than Qt and flatter Direct2D.
ComPtr<ID2D1PathGeometry> makeBand(D2D& d, const std::vector<D2D1_POINT_2F>& band)
{
    ComPtr<ID2D1PathGeometry> g;
    if (band.size() < 3 || FAILED(d.factory->CreatePathGeometry(&g))) { return nullptr; }
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(g->Open(&sink))) { return nullptr; }
    sink->BeginFigure(band[0], D2D1_FIGURE_BEGIN_FILLED);
    for (size_t i = 1; i < band.size(); ++i) { sink->AddLine(band[i]); }
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    return g;
}

/// Filled then stroked, matching TmChart: the fill shows the spread and the outline is
/// what keeps a one-sample dropout as sharp as it was before the band existed.
void drawBands(D2D& d, const std::vector<ComPtr<ID2D1PathGeometry>>& bands)
{
    for (const auto& g : bands)
    {
        if (!g) { continue; }
        d.ctx->FillGeometry(g.Get(), d.fill.Get());
        d.ctx->DrawGeometry(g.Get(), d.pen.Get(), 1.0f);
    }
}

/// Rasterize with one DrawLine call per segment - the direct analogue of the
/// drawLines() call Qt measured at 27 ms where drawPath() took 1423 ms.
double drawPerSegment(D2D& d, const std::vector<std::vector<Seg>>& all,
                      const std::vector<ComPtr<ID2D1PathGeometry>>& bands)
{
    auto t0 = std::chrono::steady_clock::now();
    d.ctx->BeginDraw();
    d.ctx->Clear(D2D1::ColorF(D2D1::ColorF::White));
    drawBands(d, bands);
    for (const auto& segs : all)
    {
        for (const Seg& s : segs)
        {
            d.ctx->DrawLine(D2D1::Point2F(s.x1, s.y1), D2D1::Point2F(s.x2, s.y2),
                            d.pen.Get(), kPenWidth);
        }
    }
    d.ctx->EndDraw();
    d.ctx->Flush();
    waitForGpu(d.d3dctx.Get(), d.fence.Get());
    return msSince(t0);
}

/// Rasterize each series as ONE batched path geometry instead of N DrawLine calls.
/// Builds one open path geometry per series from its segments. Hoisted OUT of the timed
/// draw: TmChart caches its segments, so timing path construction here would have
/// compared Direct2D-building-geometry against Qt-reusing-geometry. The first version of
/// this benchmark did exactly that, and made batched look ~40% worse than it is.
std::vector<ComPtr<ID2D1PathGeometry>> makeLinePaths(
    D2D& d, const std::vector<std::vector<Seg>>& all)
{
    std::vector<ComPtr<ID2D1PathGeometry>> geoms;
    geoms.reserve(all.size());
    for (const auto& segs : all)
    {
        if (segs.empty()) { continue; }
        ComPtr<ID2D1PathGeometry> g;
        if (FAILED(d.factory->CreatePathGeometry(&g))) { continue; }
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(g->Open(&sink))) { continue; }
        sink->BeginFigure(D2D1::Point2F(segs[0].x1, segs[0].y1), D2D1_FIGURE_BEGIN_HOLLOW);
        for (const Seg& s : segs) { sink->AddLine(D2D1::Point2F(s.x2, s.y2)); }
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();
        geoms.push_back(g);
    }
    return geoms;
}

double drawBatchedGeometry(D2D& d, const std::vector<ComPtr<ID2D1PathGeometry>>& geoms,
                           const std::vector<ComPtr<ID2D1PathGeometry>>& bands)
{
    auto t0 = std::chrono::steady_clock::now();
    d.ctx->BeginDraw();
    d.ctx->Clear(D2D1::ColorF(D2D1::ColorF::White));
    drawBands(d, bands);
    for (const auto& g : geoms) { d.ctx->DrawGeometry(g.Get(), d.pen.Get(), kPenWidth); }
    d.ctx->EndDraw();
    d.ctx->Flush();
    waitForGpu(d.d3dctx.Get(), d.fence.Get());
    return msSince(t0);
}

// ---------------------------------------------------------------------------
// Direct3D 11 path
//
// Direct2D already runs ON Direct3D - its device is created from the same
// IDXGIDevice this uses - so going to D3D directly removes a layer rather than adding
// one. The question is whether declining the abstraction is worth anything for THIS
// geometry, where the whole series is a vertex buffer and the band is a triangle strip
// by construction.
//
// Pixel-equivalence is the thing that makes the comparison honest, so:
//   - the mean line is EXPANDED TO QUADS at kPenWidth rather than drawn as a
//     LINELIST. A D3D line primitive is one pixel wide, so a line list would have been
//     measuring thinner output than Direct2D's 1.5 px pen and flattered D3D.
//   - antialiasing is 4x MSAA on the render target, against Direct2D's
//     PER_PRIMITIVE mode. Not identical algorithms, but both are real AA rather than
//     none.
// ---------------------------------------------------------------------------

constexpr UINT kMsaaSamples = 4;

/// Pixel-space position. The vertex shader does the pixel-to-NDC transform from a
/// constant buffer, so the CPU hands over the same coordinates Direct2D received -
/// moving that transform to the CPU would have shifted work out of the timed draw.
struct Vtx { float x, y; };

const char* kShaderSrc = R"HLSL(
cbuffer Viewport : register(b0) { float2 inv_size; float2 pad; };
struct VSOut { float4 pos : SV_Position; };
VSOut VSMain(float2 p : POSITION)
{
    VSOut o;
    // Pixel space -> NDC. Y flips because pixel y grows downward.
    o.pos = float4(p.x * inv_size.x * 2.0 - 1.0, 1.0 - p.y * inv_size.y * 2.0, 0.0, 1.0);
    return o;
}
float4 PSMain(VSOut i) : SV_Target { return float4(0.42, 0.38, 0.90, 1.0); }
float4 PSBand(VSOut i) : SV_Target { return float4(0.42, 0.38, 0.90, 0.25); }
)HLSL";

struct D3DPath
{
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<ID3D11Texture2D>     msaa;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11VertexShader>  vs;
    ComPtr<ID3D11PixelShader>   ps_line;
    ComPtr<ID3D11PixelShader>   ps_band;
    ComPtr<ID3D11InputLayout>   layout;
    ComPtr<ID3D11Buffer>        cb;
    ComPtr<ID3D11BlendState>    blend;
    ComPtr<ID3D11Query>         fence;

    // Per-case geometry, uploaded once - the equivalent of TmChart's cached segments.
    std::vector<ComPtr<ID3D11Buffer>> line_vb, band_vb;
    std::vector<UINT>                 line_count, band_count;

    bool init(ID3D11Device* dev)
    {
        dev->GetImmediateContext(&ctx);

        D3D11_TEXTURE2D_DESC td{};
        td.Width = kPlotW; td.Height = kPlotH; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = kMsaaSamples;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &msaa))) { return false; }
        if (FAILED(dev->CreateRenderTargetView(msaa.Get(), nullptr, &rtv))) { return false; }

        ComPtr<ID3DBlob> vsb, psb, pbb, err;
        if (FAILED(D3DCompile(kShaderSrc, std::strlen(kShaderSrc), nullptr, nullptr, nullptr,
                              "VSMain", "vs_4_0", 0, 0, &vsb, &err))) { return false; }
        if (FAILED(D3DCompile(kShaderSrc, std::strlen(kShaderSrc), nullptr, nullptr, nullptr,
                              "PSMain", "ps_4_0", 0, 0, &psb, &err))) { return false; }
        if (FAILED(D3DCompile(kShaderSrc, std::strlen(kShaderSrc), nullptr, nullptr, nullptr,
                              "PSBand", "ps_4_0", 0, 0, &pbb, &err))) { return false; }
        if (FAILED(dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs))) { return false; }
        if (FAILED(dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps_line))) { return false; }
        if (FAILED(dev->CreatePixelShader(pbb->GetBufferPointer(), pbb->GetBufferSize(), nullptr, &ps_band))) { return false; }

        const D3D11_INPUT_ELEMENT_DESC ie[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        if (FAILED(dev->CreateInputLayout(ie, 1, vsb->GetBufferPointer(), vsb->GetBufferSize(), &layout))) { return false; }

        struct CB { float inv_w, inv_h, p0, p1; } cbd{ 1.0f / kPlotW, 1.0f / kPlotH, 0, 0 };
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = sizeof(CB);
        bd.Usage = D3D11_USAGE_IMMUTABLE;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA sd{ &cbd, 0, 0 };
        if (FAILED(dev->CreateBuffer(&bd, &sd, &cb))) { return false; }

        // Straight alpha for the translucent band, matching Direct2D's blend.
        D3D11_BLEND_DESC bl{};
        bl.RenderTarget[0].BlendEnable = TRUE;
        bl.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        bl.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bl.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        bl.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(dev->CreateBlendState(&bl, &blend))) { return false; }

        D3D11_QUERY_DESC qd{};
        qd.Query = D3D11_QUERY_EVENT;
        return SUCCEEDED(dev->CreateQuery(&qd, &fence));
    }

    /// Uploads one case's geometry. Outside the timed draw, like the Direct2D paths.
    bool upload(ID3D11Device* dev,
                const std::vector<std::vector<Seg>>& segs,
                const std::vector<std::vector<D2D1_POINT_2F>>& bands)
    {
        line_vb.clear(); band_vb.clear(); line_count.clear(); band_count.clear();

        for (size_t s = 0; s < segs.size(); ++s)
        {
            // Each segment becomes a quad of kPenWidth, as two triangles. This is the
            // work a D3D renderer does that Direct2D hides.
            std::vector<Vtx> v;
            v.reserve(segs[s].size() * 6);
            for (const Seg& g : segs[s])
            {
                const float dx = g.x2 - g.x1, dy = g.y2 - g.y1;
                const float len = std::sqrt(dx * dx + dy * dy);
                if (len <= 0.0f) { continue; }
                const float nx = -dy / len * (kPenWidth * 0.5f);
                const float ny =  dx / len * (kPenWidth * 0.5f);
                const Vtx a{ g.x1 + nx, g.y1 + ny }, b{ g.x2 + nx, g.y2 + ny };
                const Vtx c{ g.x2 - nx, g.y2 - ny }, e{ g.x1 - nx, g.y1 - ny };
                v.push_back(a); v.push_back(b); v.push_back(c);
                v.push_back(a); v.push_back(c); v.push_back(e);
            }

            // The band arrives as tops left-to-right then bottoms right-to-left, so
            // interleaving top[i] with its matching bottom gives a triangle strip.
            std::vector<Vtx> bv;
            const size_t n = bands[s].size();
            if (n >= 4 && (n % 2) == 0)
            {
                const size_t m = n / 2;
                bv.reserve(n);
                for (size_t i = 0; i < m; ++i)
                {
                    bv.push_back({ bands[s][i].x, bands[s][i].y });
                    const D2D1_POINT_2F& bot = bands[s][n - 1 - i];
                    bv.push_back({ bot.x, bot.y });
                }
            }

            ComPtr<ID3D11Buffer> lb, bb;
            if (!v.empty())
            {
                D3D11_BUFFER_DESC bd{};
                bd.ByteWidth = static_cast<UINT>(v.size() * sizeof(Vtx));
                bd.Usage = D3D11_USAGE_IMMUTABLE;
                bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
                D3D11_SUBRESOURCE_DATA sd{ v.data(), 0, 0 };
                if (FAILED(dev->CreateBuffer(&bd, &sd, &lb))) { return false; }
            }
            if (!bv.empty())
            {
                D3D11_BUFFER_DESC bd{};
                bd.ByteWidth = static_cast<UINT>(bv.size() * sizeof(Vtx));
                bd.Usage = D3D11_USAGE_IMMUTABLE;
                bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
                D3D11_SUBRESOURCE_DATA sd{ bv.data(), 0, 0 };
                if (FAILED(dev->CreateBuffer(&bd, &sd, &bb))) { return false; }
            }
            line_vb.push_back(lb);
            band_vb.push_back(bb);
            line_count.push_back(static_cast<UINT>(v.size()));
            band_count.push_back(static_cast<UINT>(bv.size()));
        }
        return true;
    }

    /// @p repeats issues the whole frame's draw calls n times before fencing once.
    /// Used to VALIDATE the fence: if the reported time does not scale with the work,
    /// the fence is not waiting and the number is submission cost, not render cost.
    double draw(int repeats = 1)
    {
        auto t0 = std::chrono::steady_clock::now();

        const float clear[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        ID3D11RenderTargetView* rt = rtv.Get();
        ctx->OMSetRenderTargets(1, &rt, nullptr);
        D3D11_VIEWPORT vp{ 0.0f, 0.0f, static_cast<float>(kPlotW), static_cast<float>(kPlotH), 0.0f, 1.0f };
        ctx->RSSetViewports(1, &vp);
        ctx->ClearRenderTargetView(rt, clear);

        ctx->IASetInputLayout(layout.Get());
        ctx->VSSetShader(vs.Get(), nullptr, 0);
        ID3D11Buffer* cbs = cb.Get();
        ctx->VSSetConstantBuffers(0, 1, &cbs);
        const float bf[4] = { 0, 0, 0, 0 };
        ctx->OMSetBlendState(blend.Get(), bf, 0xFFFFFFFF);

        const UINT stride = sizeof(Vtx), offset = 0;

        for (int pass = 0; pass < repeats; ++pass)
        {
        // Band first so the mean line reads on top, as in TmChart.
        ctx->PSSetShader(ps_band.Get(), nullptr, 0);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        for (size_t i = 0; i < band_vb.size(); ++i)
        {
            if (!band_vb[i] || band_count[i] < 3) { continue; }
            ID3D11Buffer* vb = band_vb[i].Get();
            ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
            ctx->Draw(band_count[i], 0);
        }

        ctx->PSSetShader(ps_line.Get(), nullptr, 0);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        for (size_t i = 0; i < line_vb.size(); ++i)
        {
            if (!line_vb[i] || line_count[i] == 0) { continue; }
            ID3D11Buffer* vb = line_vb[i].Get();
            ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
            ctx->Draw(line_count[i], 0);
        }
        }

        ctx->Flush();
        waitForGpu(ctx.Get(), fence.Get());
        return msSince(t0);
    }

    /// Draw calls issued per frame - the number Direct2D replaces with thousands.
    size_t drawCalls() const
    {
        size_t n = 0;
        for (size_t i = 0; i < band_vb.size(); ++i) { if (band_vb[i] && band_count[i] >= 3) { ++n; } }
        for (size_t i = 0; i < line_vb.size(); ++i) { if (line_vb[i] && line_count[i]) { ++n; } }
        return n;
    }
};

struct Case { int samples; int series; const char* qt_first; const char* qt_repaint; };

} // namespace

int main()
{
    std::printf("Direct2D chart benchmark - %dx%d, %.1f px pen, AA on\n", kPlotW, kPlotH, kPenWidth);

    D2D d;
    if (!d.init()) { std::printf("Direct2D init FAILED\n"); return 1; }
    std::printf("device: %s\n\n", d.hardware ? "D3D11 HARDWARE" : "WARP (software - not a GPU number)");

    // The three shapes docs/claude.md records Qt numbers for.
    const Case cases[] = {
        {  360000,  3, "36",  "8"  },
        {  360000, 12, "157", "34" },
        { 1000000,  3, "79",  "13" },
    };

    D3DPath d3d;
    const bool d3d_ok = d3d.init(d.d3d.Get());
    if (!d3d_ok) { std::printf("D3D11 path init FAILED - D2D columns only\n\n"); }

    std::printf("%-16s %-6s %-10s %-11s %-9s %-9s %s\n",
                "samples/series", "series", "geometry", "d2d per-seg", "d2d batch",
                "d3d11", "qt first/repaint");
    std::printf("%s\n", std::string(96, '-').c_str());

    for (const Case& c : cases)
    {
        std::vector<Series> series;
        series.reserve(c.series);
        for (int i = 0; i < c.series; ++i) { series.push_back(makeSeries(c.samples, 1234u + i)); }

        const double x_lower = 0.0;
        const double x_upper = static_cast<double>(c.samples - 1);
        const double y_lower = 0.0, y_upper = 40.0;

        std::vector<std::vector<Seg>>          segs(c.series);
        std::vector<std::vector<D2D1_POINT_2F>> bands(c.series);

        // Geometry build: what Qt's "first paint" includes and its "repaint" does not.
        auto g0 = std::chrono::steady_clock::now();
        for (int i = 0; i < c.series; ++i)
        {
            buildGeometry(series[i], x_lower, x_upper, y_lower, y_upper, segs[i], bands[i]);
        }
        const double geom_ms = msSince(g0);

        // One warm pass each, so the reported number is steady-state rather than
        // first-call device and shader setup - the same mistake the interop spike's
        // first marshalling figure made.
        // Bands are built once, outside the timed draws, because TmChart caches its
        // geometry too - the comparison is against Qt's cached-geometry repaint.
        std::vector<ComPtr<ID2D1PathGeometry>> band_geoms;
        band_geoms.reserve(c.series);
        for (int i = 0; i < c.series; ++i) { band_geoms.push_back(makeBand(d, bands[i])); }

        // Line paths built once as well, for the same reason as the bands.
        const std::vector<ComPtr<ID2D1PathGeometry>> line_geoms = makeLinePaths(d, segs);

        drawPerSegment(d, segs, band_geoms);
        drawBatchedGeometry(d, line_geoms, band_geoms);

        double per_seg = 1e30, batched = 1e30;
        for (int rep = 0; rep < 5; ++rep)
        {
            per_seg = std::min(per_seg, drawPerSegment(d, segs, band_geoms));
            batched = std::min(batched, drawBatchedGeometry(d, line_geoms, band_geoms));
        }

        // D3D11: same cached-geometry rule - vertex buffers uploaded outside the draw.
        double d3d_ms = -1.0, d3d_floor = -1.0;
        size_t d3d_calls = 0;
        if (d3d_ok && d3d.upload(d.d3d.Get(), segs, bands))
        {
            d3d.draw();   // warm
            double one_shot = 1e30;
            for (int rep = 0; rep < 5; ++rep) { one_shot = std::min(one_shot, d3d.draw()); }
            d3d_calls = d3d.drawCalls();

            // A single frame's D3D work is BELOW the fence's own overhead, so the
            // one-shot figure is a FLOOR, not a measurement. Timing 50x the work
            // returned only 2.8x to 7.3x the time, which is how that was caught - and
            // is why reporting the one-shot number would have claimed D3D was ~500x
            // faster than Direct2D when the truth is that it is simply unmeasurable
            // this way.
            //
            // Two-point solve instead. With t(n) = fixed + n*w:
            //     w = (t(50) - t(1)) / 49
            // which cancels the fixed fence cost and leaves the per-frame render.
            constexpr int kBulk = 50;
            double t_bulk = 1e30;
            for (int rep = 0; rep < 3; ++rep) { t_bulk = std::min(t_bulk, d3d.draw(kBulk)); }
            d3d_floor = one_shot;
            d3d_ms    = (t_bulk - one_shot) / (kBulk - 1);
        }

        size_t total_segs = 0, band_pts = 0;
        for (const auto& v : segs)  { total_segs += v.size(); }
        for (const auto& v : bands) { band_pts  += v.size(); }

        std::printf("%-16d %-6d %-10.2f %-11.2f %-9.2f %-9.2f %s / %s ms\n",
                    c.samples, c.series, geom_ms, per_seg, batched, d3d_ms,
                    c.qt_first, c.qt_repaint);
        std::printf("%-16s %-6s %zu segments + %zu band points | d2d calls ~%zu, d3d calls %zu\n",
                    "", "", total_segs, band_pts, total_segs + bands.size() * 2, d3d_calls);
        if (d3d_floor > 0.0)
        {
            std::printf("%-16s %-6s d3d %.0f us/frame derived; single-shot floor %.3f ms "
                        "was fence overhead, not render\n",
                        "", "", d3d_ms * 1000.0, d3d_floor);
        }
    }

    std::printf("\ngeometry  = port of TmChart::rebuildSeriesGeometry (cull + per-column envelope)\n");
    std::printf("per-seg   = band (fill+outline) then one DrawLine per segment\n");
    std::printf("batched   = band (fill+outline) then one DrawGeometry per series\n");
    std::printf("d3d11     = DERIVED per-frame render, (t(50x) - t(1x)) / 49: one frame is\n");
    std::printf("            faster than the GPU fence overhead, so a one-shot is a floor\n");
    std::printf("            band as a triangle strip + line expanded to quads at the SAME\n");
    std::printf("            1.5 px width, 4x MSAA - pixel-equivalent, not 1-px line prims\n");
    std::printf("qt first  = build + rasterize; qt repaint = rasterize from cached geometry\n");
    std::printf("All timings best-of-5 after a warm pass, geometry cached outside the draw,\n");
    std::printf("each with an explicit Flush() so the timer measures rendering not submission.\n");
    return 0;
}
