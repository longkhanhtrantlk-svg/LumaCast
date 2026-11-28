// PowerPoint qua COM (late binding: khong can thu vien Office luc dich; may khong co PowerPoint chi bao loi).
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <oleauto.h>

#include "live/SlideRemote.h"
#include "core/I18n.h"
#include <mutex>

namespace lc {
namespace {

struct Variant {
    VARIANT v;
    Variant() { VariantInit(&v); }
    ~Variant() { VariantClear(&v); }
    Variant(const Variant&) = delete;
    Variant& operator=(const Variant&) = delete;
    IDispatch* object() const { return v.vt == VT_DISPATCH ? v.pdispVal : nullptr; }
    double number() {
        Variant out;
        return SUCCEEDED(VariantChangeType(&out.v, &v, 0, VT_R8)) ? out.v.dblVal : 0.0;
    }
};

// Goi thuoc tinh / phuong thuc theo ten; `arg` (neu co) la mot tham so so nguyen.
HRESULT call(IDispatch* obj, const wchar_t* name, Variant& out, const int* arg = nullptr) {
    if (!obj) return E_POINTER;
    DISPID id = 0;
    LPOLESTR n = const_cast<LPOLESTR>(name);
    HRESULT hr = obj->GetIDsOfNames(IID_NULL, &n, 1, LOCALE_USER_DEFAULT, &id);
    if (FAILED(hr)) return hr;
    VARIANT a;
    VariantInit(&a);
    DISPPARAMS params{nullptr, nullptr, 0, 0};
    if (arg) {
        a.vt = VT_I4;
        a.lVal = *arg;
        params.rgvarg = &a;
        params.cArgs = 1;
    }
    return obj->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET | DISPATCH_METHOD, &params, &out.v, nullptr, nullptr);
}

bool busy(HRESULT hr) { return hr == RPC_E_CALL_REJECTED || hr == RPC_E_SERVERCALL_RETRYLATER; }

void keepMtaAlive() {
    static std::once_flag once;
    std::call_once(once, [] {
        CO_MTA_USAGE_COOKIE cookie{};
        CoIncrementMTAUsage(&cookie);
    });
}

// Cua so slideshow dau tien cua PowerPoint dang chay.
HRESULT slideShowWindow(Variant& window, QString& error) {
    keepMtaAlive();
    CLSID clsid;
    if (FAILED(CLSIDFromProgID(L"PowerPoint.Application", &clsid))) { error = LC_TR("Máy này không có PowerPoint"); return E_FAIL; }
    IUnknown* unknown = nullptr;
    if (FAILED(GetActiveObject(clsid, nullptr, &unknown)) || !unknown) { error = LC_TR("PowerPoint chưa mở"); return E_FAIL; }
    IDispatch* app = nullptr;
    HRESULT hr = unknown->QueryInterface(IID_IDispatch, reinterpret_cast<void**>(&app));
    unknown->Release();
    if (FAILED(hr)) { error = LC_TR("Không nói chuyện được với PowerPoint"); return hr; }
    Variant windows, count;
    hr = call(app, L"SlideShowWindows", windows);
    app->Release();
    if (SUCCEEDED(hr)) hr = call(windows.object(), L"Count", count);
    if (busy(hr)) { error = LC_TR("PowerPoint đang bận (có hộp thoại đang mở?)"); return hr; }
    if (FAILED(hr)) { error = LC_TR("Không nói chuyện được với PowerPoint"); return hr; }
    if (count.number() < 1) { error = LC_TR("PowerPoint chưa trình chiếu"); return E_FAIL; }
    const int first = 1;
    hr = call(windows.object(), L"Item", window, &first);
    if (FAILED(hr)) error = busy(hr) ? LC_TR("PowerPoint đang bận (có hộp thoại đang mở?)") : LC_TR("Không nói chuyện được với PowerPoint");
    return hr;
}

class PowerPointBackend : public SlideBackend {
public:
    SlideStep step(int direction) override {
        SlideStep r;
        Variant window, view, ignored;
        if (FAILED(slideShowWindow(window, r.error))) return r;
        HRESULT hr = call(window.object(), L"View", view);
        if (SUCCEEDED(hr)) hr = call(view.object(), direction >= 0 ? L"Next" : L"Previous", ignored);
        if (FAILED(hr)) {
            r.error = busy(hr) ? LC_TR("PowerPoint đang bận (có hộp thoại đang mở?)") : LC_TR("PowerPoint không nhận lệnh chuyển slide");
            return r;
        }
        r.ok = true;
        Variant position, presentation, slides, count;
        if (SUCCEEDED(call(view.object(), L"CurrentShowPosition", position))) r.position = int(position.number());
        if (SUCCEEDED(call(window.object(), L"Presentation", presentation)) && SUCCEEDED(call(presentation.object(), L"Slides", slides)) &&
            SUCCEEDED(call(slides.object(), L"Count", count)))
            r.count = int(count.number());
        return r;
    }

    double slideAspect() override {
        Variant window, presentation, setup, width, height;
        QString error;
        if (FAILED(slideShowWindow(window, error))) return 0;
        if (FAILED(call(window.object(), L"Presentation", presentation)) || FAILED(call(presentation.object(), L"PageSetup", setup)) ||
            FAILED(call(setup.object(), L"SlideWidth", width)) || FAILED(call(setup.object(), L"SlideHeight", height)))
            return 0;
        const double w = width.number(), h = height.number();
        return w > 0 && h > 0 ? w / h : 0;
    }
};

} // namespace

std::shared_ptr<SlideBackend> createPowerPointBackend() { return std::make_shared<PowerPointBackend>(); }

} // namespace lc
#endif
