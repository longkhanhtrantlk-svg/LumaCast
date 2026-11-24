// Ban dich KHONG co NDI (LUMACAST_ENABLE_NDI=OFF): khong co ma nao nap/khai bao API cua NDI. Moi truy van NDI bao "khong co trong ban nay".
#include "live/Ndi.h"

namespace lc {

std::shared_ptr<NdiBackend> createUnavailableNdiBackend(const QString& reason) {
    class None : public NdiBackend {
    public:
        explicit None(QString r) : r_(std::move(r)) {}
        QString id() const override { return "none"; }
        bool available() const override { return false; }
        QString unavailableReason() const override { return r_; }
        std::vector<NdiSourceInfo> listSources(int) override { return {}; }
        LiveOpenResult openReceiver(const QString&, const QString&) override { LiveOpenResult r; r.unavailable = true; r.error = r_; return r; }
        std::unique_ptr<NdiSender> createSender(const QString&, QString* err) override { if (err) *err = r_; return nullptr; }
    private:
        QString r_;
    };
    return std::make_shared<None>(reason);
}

std::shared_ptr<NdiBackend> createRealNdiBackend(QString* why) {
    if (why) *why = "NDI không có trong bản LumaCast này";
    return nullptr;
}

} // namespace lc
