// Backend MIDI vao bang winmm (Windows), khong them thu vien. He khac: khong co cong nao.
#include "Midi.h"
#include "core/I18n.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#include <mutex>
#endif

namespace lc {

namespace {

#ifdef _WIN32

class WinmmMidiBackend : public MidiBackend {
public:
    ~WinmmMidiBackend() override { close(); }

    QStringList inputs() override {
        QStringList names;
        const UINT n = midiInGetNumDevs();
        for (UINT i = 0; i < n; ++i) {
            MIDIINCAPSW caps{};
            if (midiInGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) names << QString::fromWCharArray(caps.szPname);
        }
        return names;
    }

    bool open(const QString& name, Handler handler, QString* error) override {
        close();
        const UINT n = midiInGetNumDevs();
        for (UINT i = 0; i < n; ++i) {
            MIDIINCAPSW caps{};
            if (midiInGetDevCapsW(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR || QString::fromWCharArray(caps.szPname) != name) continue;
            auto* ctx = new Ctx;
            ctx->handler = std::move(handler);
            MMRESULT r = midiInOpen(&in_, i, reinterpret_cast<DWORD_PTR>(&WinmmMidiBackend::proc), reinterpret_cast<DWORD_PTR>(ctx), CALLBACK_FUNCTION);
            if (r == MMSYSERR_NOERROR) {
                ctx_ = ctx;
                r = midiInStart(in_);
                if (r == MMSYSERR_NOERROR) return true;
                close();
            } else {
                delete ctx;
                in_ = nullptr;
            }
            if (error) *error = r == MMSYSERR_ALLOCATED ? QString(LC_TR("đang được chương trình khác dùng")) : errorText(r);
            return false;
        }
        if (error) *error = LC_TR("không còn thấy thiết bị");
        return false;
    }

    void close() override {
        if (!in_) return;
        {   // cat handler truoc: tu day callback (neu con) khong cham vao ung dung. KHONG giu mutex khi goi winmm (tranh deadlock).
            std::lock_guard lk(ctx_->mutex);
            ctx_->handler = nullptr;
        }
        midiInStop(in_);
        midiInReset(in_);
        // Dong that bai (hiem, vd. driver loi sau khi rut): giu nguyen ctx de callback muon khong doc bo nho da giai phong.
        if (midiInClose(in_) == MMSYSERR_NOERROR) delete ctx_;
        in_ = nullptr;
        ctx_ = nullptr;
    }

private:
    struct Ctx {
        std::mutex mutex;
        Handler handler;
    };

    static void CALLBACK proc(HMIDIIN, UINT msg, DWORD_PTR instance, DWORD_PTR p1, DWORD_PTR) {
        if (msg != MIM_DATA) return;
        auto* ctx = reinterpret_cast<Ctx*>(instance);
        std::lock_guard lk(ctx->mutex);
        if (ctx->handler) ctx->handler(quint8(p1 & 0xFF), quint8((p1 >> 8) & 0xFF), quint8((p1 >> 16) & 0xFF));
    }

    static QString errorText(MMRESULT r) {
        wchar_t buf[MAXERRORLENGTH] = {};
        if (midiInGetErrorTextW(r, buf, MAXERRORLENGTH) != MMSYSERR_NOERROR) return QString(LC_TR("lỗi winmm %1")).arg(r);
        return QString::fromWCharArray(buf);
    }

    HMIDIIN in_ = nullptr;
    Ctx* ctx_ = nullptr;
};

#else

class NullMidiBackend : public MidiBackend {
public:
    QStringList inputs() override { return {}; }
    bool open(const QString&, Handler, QString* error) override {
        if (error) *error = LC_TR("MIDI chỉ hỗ trợ trên Windows");
        return false;
    }
    void close() override {}
};

#endif

} // namespace

std::unique_ptr<MidiBackend> createSystemMidiBackend() {
#ifdef _WIN32
    return std::make_unique<WinmmMidiBackend>();
#else
    return std::make_unique<NullMidiBackend>();
#endif
}

} // namespace lc
