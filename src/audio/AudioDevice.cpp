#include "AudioDevice.h"
#include "AudioStream.h"
#include <QDebug>
#include <atomic>
#include <chrono>
#include <thread>

namespace lc {

struct NullAudioDevice::Impl {
    std::thread th;
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> consumed{0};
};

NullAudioDevice::NullAudioDevice() : d_(std::make_unique<Impl>()) {}
NullAudioDevice::~NullAudioDevice() { stop(); }
uint64_t NullAudioDevice::consumedFrames() const { return d_->consumed; }

bool NullAudioDevice::start(RenderFn fn, QString*) {
    stop();
    d_->stop = false;
    d_->th = std::thread([this, fn = std::move(fn)] {
        using Clock = std::chrono::steady_clock;
        const auto t0 = Clock::now();
        std::vector<float> buf(kAudioRate / 10 * kAudioChannels);          // toi da 100 ms moi lan
        uint64_t done = 0;
        while (!d_->stop) {
            const double el = std::chrono::duration<double>(Clock::now() - t0).count();
            const uint64_t want = uint64_t(el * kAudioRate);
            while (want > done && !d_->stop) {
                const int n = int(std::min<uint64_t>(want - done, buf.size() / kAudioChannels));
                fn(buf.data(), n);
                done += uint64_t(n);
                d_->consumed = done;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
    return true;
}

void NullAudioDevice::stop() {
    d_->stop = true;
    if (d_->th.joinable()) d_->th.join();
}

std::vector<AudioDeviceInfo> listAudioDevices() {
#ifdef _WIN32
    return listWasapiDevices();
#else
    return {AudioDeviceInfo{"", "Mặc định (Null — chưa hỗ trợ phát trên hệ điều hành này)", true}};
#endif
}

std::unique_ptr<AudioDevice> createAudioDevice(const QString& id, QString* err) {
    if (id == "null") return std::make_unique<NullAudioDevice>();
#ifdef _WIN32
    QString e;
    if (auto d = createWasapiDevice(id, &e)) return d;
    if (err) *err = e;
    qWarning().noquote() << "Âm thanh: không mở được thiết bị" << (id.isEmpty() ? "mặc định" : id) << "-" << e << "- dùng Null";
#else
    Q_UNUSED(err);
#endif
    return std::make_unique<NullAudioDevice>();
}

} // namespace lc
