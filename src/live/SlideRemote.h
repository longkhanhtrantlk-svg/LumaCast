#pragma once
#include <QObject>
#include <QString>
#include <memory>

namespace lc {

// Chuyen slide cho slideshow PowerPoint dang chay (CTL-02): de but trinh chieu van chay khi LumaCast dang duoc chon.
// LumaCast khong dieu khien gi khac: khong mo file, khong bat/tat slideshow.

struct SlideStep {
    bool ok = false;
    QString error;
    int position = 0, count = 0;       // 0 = khong biet
};

class SlideBackend {
public:
    virtual ~SlideBackend() = default;
    virtual SlideStep step(int direction) = 0;      // +1 = tiep, -1 = lui. Co the chan (PowerPoint ban): khong goi tren UI thread.
    virtual double slideAspect() { return 0; }      // ti le rong/cao cua slide dang chieu, 0 = khong biet
};

std::shared_ptr<SlideBackend> slideBackend();
void setSlideBackend(std::shared_ptr<SlideBackend> b);     // test; nullptr = mac dinh
#ifdef _WIN32
std::shared_ptr<SlideBackend> createPowerPointBackend();
#endif

// Goi backend tren mot thread rieng; ket qua ve bang signal tren thread cua doi tuong. Huy khong bao gio cho PowerPoint.
class SlideRemote : public QObject {
    Q_OBJECT
public:
    explicit SlideRemote(QObject* parent = nullptr);
    ~SlideRemote() override;
    void step(int direction);
    struct State;
signals:
    void stepped(int direction, bool ok, int position, int count, const QString& error);
private:
    std::shared_ptr<State> s_;
};

} // namespace lc
