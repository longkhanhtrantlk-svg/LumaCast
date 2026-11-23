#pragma once
#include "render/Engine.h"
#include "render/OutputHealth.h"
#include <QObject>
#include <QTimer>
#include <vector>

namespace lc {

// Theo doi tinh trang tung Output (UI thread): phat hien mat man hinh, sai man hinh, cua so khong hien.
// Quy tac an toan: Output fullscreen khong bao gio duoc hien len man hinh KHAC man da cau hinh
// (neu man mat -> an cua so; man quay lai -> hien lai dung man). Ghi log khi tinh trang doi.
class OutputSupervisor : public QObject {
    Q_OBJECT
public:
    explicit OutputSupervisor(RenderEngine& engine, QObject* parent = nullptr);
    OutputHealth health(int i) const { return i >= 0 && i < int(last_.size()) ? last_[i] : OutputHealth::Ok; }
    QString text(int i) const;
    int badCount() const;                      // so Output khong Ok
public slots:
    void check();                              // goi dinh ky + khi QGuiApplication bao co man hinh them/bot
signals:
    void healthChanged(int output, int health);
private:
    RenderEngine& engine_;
    QTimer timer_;
    std::vector<OutputHealth> last_;
};

} // namespace lc
