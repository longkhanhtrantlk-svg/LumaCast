// Bat cua so bang Windows Graphics Capture (Windows 10 1903+). Khung chi den khi noi dung cua so doi.
#ifdef _WIN32
// Header Windows / WinRT phai dung TRUOC header Qt (tranh dung macro).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include "live/WindowCapture.h"
#include "core/I18n.h"
#include "live/SlideRemote.h"
#include <QFileInfo>
#include <QString>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

namespace lc {

namespace wg = winrt::Windows::Graphics;
namespace wgc = winrt::Windows::Graphics::Capture;
namespace wgd = winrt::Windows::Graphics::DirectX;

namespace {

QString windowClassOf(HWND h) {
    wchar_t buf[256];
    return QString::fromWCharArray(buf, GetClassNameW(h, buf, 256));
}

QString windowTitleOf(HWND h) {
    wchar_t buf[512];
    return QString::fromWCharArray(buf, GetWindowTextW(h, buf, 512));
}

QString processNameOf(DWORD pid) {
    QString name;
    if (HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t path[1024];
        DWORD n = 1024;
        if (QueryFullProcessImageNameW(p, 0, path, &n)) name = QFileInfo(QString::fromWCharArray(path, int(n))).fileName();
        CloseHandle(p);
    }
    return name;
}

QRect toRect(const RECT& r) { return QRect(r.left, r.top, r.right - r.left, r.bottom - r.top); }

// Cac cua so con cua cua so PowerPoint dung de nhan ra va cat slideshow "trong cua so".
struct PptParts {
    HWND dockTop = nullptr, mdi = nullptr, scroll = nullptr;
};
BOOL CALLBACK findPptParts(HWND h, LPARAM lp) {
    auto* p = reinterpret_cast<PptParts*>(lp);
    const QString cls = windowClassOf(h);
    if (cls == "MsoCommandBarDock" && windowTitleOf(h) == "MsoDockTop") p->dockTop = h;
    else if (cls == "MDIClient" && !p->mdi) p->mdi = h;
    else if (cls == "NUIScrollbar" && IsWindowVisible(h)) p->scroll = h;
    return TRUE;
}
PptParts pptParts(HWND top) {
    PptParts p;
    EnumChildWindows(top, findPptParts, reinterpret_cast<LPARAM>(&p));
    return p;
}

// Slideshow toan man hinh: lop "screenClass". Slideshow trong cua so: van la "PPTFrameClass" nhu cua so soan thao, nhung
// khong co ribbon (MsoDockTop an hoac cao 0). Khong dua vao tieu de vi tieu de doi theo ngon ngu Office.
bool isSlideShowWindow(HWND top, const QString& app, const QString& cls) {
    if (app.compare("POWERPNT.EXE", Qt::CaseInsensitive) != 0) return false;
    if (cls == "screenClass") return true;
    if (cls != "PPTFrameClass") return false;
    const PptParts p = pptParts(top);
    if (!p.mdi || !p.dockTop) return false;
    RECT r{};
    GetWindowRect(p.dockTop, &r);
    return !IsWindowVisible(p.dockTop) || r.bottom - r.top <= 0;
}

BOOL CALLBACK enumTop(HWND h, LPARAM lp) {
    auto* out = reinterpret_cast<std::vector<CaptureWindow>*>(lp);
    if (!IsWindowVisible(h)) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId()) return TRUE;                  // khong bat chinh LumaCast (hinh long vo han)
    if (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof cloaked)) && cloaked) return TRUE;
    CaptureWindow w;
    w.handle = quint64(reinterpret_cast<quintptr>(h));
    w.windowClass = windowClassOf(h);
    w.title = windowTitleOf(h);
    w.app = processNameOf(pid);
    w.slideShow = isSlideShowWindow(h, w.app, w.windowClass);
    if (w.app.isEmpty() || (w.title.isEmpty() && !w.slideShow)) return TRUE;
    out->push_back(std::move(w));
    return TRUE;
}

// Vung can lay trong anh cua so (diem anh anh bat): vung lam viec cua cua so, voi PowerPoint la vung chieu slide.
QRect contentRect(HWND top, bool slideShow, double slideAspect) {
    RECT frame{};
    if (FAILED(DwmGetWindowAttribute(top, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof frame)) && !GetWindowRect(top, &frame)) return {};
    QRect area;
    if (slideShow && windowClassOf(top) == "PPTFrameClass") {
        const PptParts p = pptParts(top);
        RECT r{};
        if (p.mdi && GetWindowRect(p.mdi, &r)) {
            area = toRect(r);
            RECT s{};
            if (p.scroll && GetWindowRect(p.scroll, &s) && s.right >= r.right - 1 && s.left > r.left) area.setWidth(s.left - r.left);
        }
    }
    if (area.isEmpty()) {
        RECT c{};
        POINT o{0, 0};
        if (!GetClientRect(top, &c) || !ClientToScreen(top, &o)) return {};
        area = QRect(o.x, o.y, c.right, c.bottom);
    }
    if (slideShow) area = fitAspectRect(area, slideAspect);
    return area.translated(-frame.left, -frame.top);
}

class WgcInput : public LiveInput {
public:
    WgcInput(HWND hwnd, bool slideShow) : hwnd_(hwnd), slideShow_(slideShow) {}
    ~WgcInput() override {
        try {
            if (session_) session_.Close();
            if (pool_) pool_.Close();
        } catch (const winrt::hresult_error&) {
        }
    }

    bool start() {
        try {
            const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
            if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, dev_.put(), nullptr, ctx_.put())))
                winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, dev_.put(), nullptr, ctx_.put()));
            winrt::com_ptr<::IInspectable> inspectable;
            winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dev_.as<IDXGIDevice>().get(), inspectable.put()));
            rtDevice_ = inspectable.as<wgd::Direct3D11::IDirect3DDevice>();

            auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
            winrt::check_hresult(interop->CreateForWindow(hwnd_, winrt::guid_of<wgc::IGraphicsCaptureItem>(), winrt::put_abi(item_)));
            size_ = item_.Size();
            pool_ = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(rtDevice_, wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size_);
            session_ = pool_.CreateCaptureSession(item_);
            using winrt::Windows::Foundation::Metadata::ApiInformation;
            const winrt::hstring type = L"Windows.Graphics.Capture.GraphicsCaptureSession";
            if (ApiInformation::IsPropertyPresent(type, L"IsCursorCaptureEnabled")) session_.IsCursorCaptureEnabled(false);
            if (ApiInformation::IsPropertyPresent(type, L"IsBorderRequired")) {
                try { session_.IsBorderRequired(false); } catch (const winrt::hresult_error&) {}      // vien vang chi hien tren man, khong vao anh
            }
            session_.StartCapture();
            return true;
        } catch (const winrt::hresult_error&) {
            return false;
        }
    }

    Event capture(int timeoutMs, LiveVideoFrame& v, LiveAudioBlock&) override {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        try {
            do {
                if (!IsWindow(hwnd_) || !IsWindowVisible(hwnd_) || IsIconic(hwnd_)) return Event::Lost;
                updateSlideAspect();
                wgc::Direct3D11CaptureFrame frame{nullptr};
                while (auto next = pool_.TryGetNextFrame()) frame = next;       // chi lay khung moi nhat
                if (frame) {
                    const auto contentSize = frame.ContentSize();
                    const bool resized = contentSize.Width != size_.Width || contentSize.Height != size_.Height;
                    const bool ok = copy(frame, v);
                    frame.Close();
                    if (resized && contentSize.Width > 0 && contentSize.Height > 0) {
                        size_ = contentSize;
                        pool_.Recreate(rtDevice_, wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size_);
                    }
                    if (ok) return Event::Video;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(4));
            } while (std::chrono::steady_clock::now() < until);
            return Event::None;
        } catch (const winrt::hresult_error&) {
            return Event::Lost;
        }
    }

private:
    // Ti le slide hoi qua COM; PowerPoint dang ban thi thu lai sau (toi khi do chi cat theo vung chieu).
    void updateSlideAspect() {
        if (!slideShow_ || aspect_ > 0) return;
        const auto now = std::chrono::steady_clock::now();
        if (now < nextAspectTry_) return;
        nextAspectTry_ = now + std::chrono::seconds(2);
        aspect_ = slideBackend()->slideAspect();
    }

    bool copy(const wgc::Direct3D11CaptureFrame& frame, LiveVideoFrame& v) {
        auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        winrt::com_ptr<ID3D11Texture2D> tex;
        winrt::check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D), tex.put_void()));
        D3D11_TEXTURE2D_DESC d{};
        tex->GetDesc(&d);
        QRect r = contentRect(hwnd_, slideShow_, aspect_);
        r &= QRect(0, 0, std::min<int>(int(d.Width), frame.ContentSize().Width), std::min<int>(int(d.Height), frame.ContentSize().Height));
        if (r.width() < 2 || r.height() < 2) return false;

        if (!staging_ || stagingW_ != r.width() || stagingH_ != r.height()) {
            D3D11_TEXTURE2D_DESC s{};
            s.Width = UINT(r.width());
            s.Height = UINT(r.height());
            s.MipLevels = 1;
            s.ArraySize = 1;
            s.Format = d.Format;
            s.SampleDesc.Count = 1;
            s.Usage = D3D11_USAGE_STAGING;
            s.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            staging_ = nullptr;
            winrt::check_hresult(dev_->CreateTexture2D(&s, nullptr, staging_.put()));
            stagingW_ = r.width();
            stagingH_ = r.height();
        }
        const D3D11_BOX box{UINT(r.left()), UINT(r.top()), 0, UINT(r.left() + r.width()), UINT(r.top() + r.height()), 1};
        ctx_->CopySubresourceRegion(staging_.get(), 0, 0, 0, 0, tex.get(), 0, &box);
        D3D11_MAPPED_SUBRESOURCE m{};
        winrt::check_hresult(ctx_->Map(staging_.get(), 0, D3D11_MAP_READ, 0, &m));
        v.w = r.width();
        v.h = r.height();
        v.rgba.resize(size_t(v.w) * size_t(v.h) * 4);
        for (int y = 0; y < v.h; ++y) {
            const auto* src = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(m.pData) + size_t(y) * m.RowPitch);
            auto* dst = reinterpret_cast<uint32_t*>(v.rgba.data() + size_t(y) * size_t(v.w) * 4);
            for (int x = 0; x < v.w; ++x) {
                const uint32_t p = src[x];                                      // BGRA -> RGBA, bo trong suot
                dst[x] = 0xff000000u | (p & 0x0000ff00u) | ((p >> 16) & 0xffu) | ((p & 0xffu) << 16);
            }
        }
        ctx_->Unmap(staging_.get(), 0);
        return true;
    }

    HWND hwnd_;
    bool slideShow_;
    double aspect_ = 0;
    std::chrono::steady_clock::time_point nextAspectTry_{};
    winrt::com_ptr<ID3D11Device> dev_;
    winrt::com_ptr<ID3D11DeviceContext> ctx_;
    winrt::com_ptr<ID3D11Texture2D> staging_;
    int stagingW_ = 0, stagingH_ = 0;
    wgd::Direct3D11::IDirect3DDevice rtDevice_{nullptr};
    wgc::GraphicsCaptureItem item_{nullptr};
    wg::SizeInt32 size_{};
    wgc::Direct3D11CaptureFramePool pool_{nullptr};
    wgc::GraphicsCaptureSession session_{nullptr};
};

// Giu MTA song suot tien trinh: moi thread chua CoInitialize deu dung duoc WinRT (va bo dem factory cua C++/WinRT khong bi treo).
void keepMtaAlive() {
    static const bool once = [] {
        CO_MTA_USAGE_COOKIE cookie{};
        CoIncrementMTAUsage(&cookie);
        return true;
    }();
    (void)once;
}

class WgcBackend : public WindowCaptureBackend {
public:
    QString id() const override { return "wgc"; }
    bool available() const override { return true; }
    QString unavailableReason() const override { return {}; }

    std::vector<CaptureWindow> list() override {
        std::vector<CaptureWindow> out;
        EnumWindows(enumTop, reinterpret_cast<LPARAM>(&out));
        return out;
    }

    LiveOpenResult open(const WindowTarget& target) override {
        keepMtaAlive();
        LiveOpenResult r;
        const std::vector<CaptureWindow> wins = list();
        const int i = findCaptureWindow(wins, target);
        if (i < 0) {
            r.unavailable = true;
            r.error = target.isSlideShow() ? LC_TR("Đang chờ slideshow PowerPoint") : LC_TR("Chưa thấy cửa sổ cần bắt");
            return r;
        }
        const CaptureWindow& w = wins[size_t(i)];
        const HWND hwnd = reinterpret_cast<HWND>(quintptr(w.handle));
        if (IsIconic(hwnd)) { r.error = LC_TR("Cửa sổ đang thu nhỏ — mở lại cửa sổ đó"); return r; }
        auto input = std::make_unique<WgcInput>(hwnd, w.slideShow);
        if (!input->start()) { r.error = LC_TR("Windows không cho bắt cửa sổ này"); return r; }
        r.input = std::move(input);
        return r;
    }
};

} // namespace

std::shared_ptr<WindowCaptureBackend> createWgcWindowCapture() {
    keepMtaAlive();
    try {
        if (!wgc::GraphicsCaptureSession::IsSupported()) return nullptr;
    } catch (const winrt::hresult_error&) {
        return nullptr;
    }
    return std::make_shared<WgcBackend>();
}

} // namespace lc
#endif
