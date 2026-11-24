#include "live/Ndi.h"
#include "live/LiveSource.h"
#include "live/NdiFake.h"
#include <QMutex>
#include <QtGlobal>

namespace lc {

namespace {
QMutex g_mu;
// Cac bien nay co y KHONG bi huy khi thoat (runtime NDI that khong duoc go truoc cac receiver/sender con song)
auto& g_backend = *new std::shared_ptr<NdiBackend>;
auto& g_override = *new std::shared_ptr<NdiBackend>;

std::shared_ptr<NdiBackend> makeDefault() {
    if (qEnvironmentVariableIntValue("LUMACAST_NDI_DISABLE") == 1)
        return createUnavailableNdiBackend("NDI đã bị tắt (LUMACAST_NDI_DISABLE=1)");
    if (qgetenv("LUMACAST_NDI_BACKEND") == "fake") {
        auto f = std::make_shared<FakeNdiBackend>();
        f->addSource("LUMACAST-DEMO (Mẫu thử)", {});
        return f;
    }
    QString why;
    if (auto b = createRealNdiBackend(&why)) return b;
    return createUnavailableNdiBackend(why.isEmpty() ? "Chưa cài NDI Runtime" : why);
}
} // namespace

std::shared_ptr<NdiBackend> ndiBackend() {
    QMutexLocker lk(&g_mu);
    if (g_override) return g_override;
    if (!g_backend) g_backend = makeDefault();
    return g_backend;
}

void setNdiBackend(std::shared_ptr<NdiBackend> b) {
    QMutexLocker lk(&g_mu);
    g_override = std::move(b);
}

std::shared_ptr<LiveSource> makeNdiSource(const QString& name, const QString& url, const QString& label) {
    LiveOptions o;
    o.audio = true;
    auto factory = [name, url]() -> LiveOpenResult {
        auto b = ndiBackend();                      // lay backend MOI LAN mo: test doi backend, runtime cai sau van dung duoc
        if (!b->available()) { LiveOpenResult r; r.unavailable = true; r.error = b->unavailableReason(); return r; }
        return b->openReceiver(name, url);
    };
    return std::make_shared<LiveSource>(label.isEmpty() ? "NDI: " + name : label, "ndi", std::move(factory), o);
}

} // namespace lc
