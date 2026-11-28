#pragma once
#include "live/WindowCapture.h"
#include <memory>

namespace lc {

// Backend bat cua so GIA (test, hoac LUMACAST_WINDOW_BACKEND=fake): moi cua so la mot mau mau dac, chi phat khung khi doi
// (nhu cua so that: dung yen thi khong co khung moi).
class FakeWindowCapture : public WindowCaptureBackend {
public:
    FakeWindowCapture();
    QString id() const override { return "fake"; }
    bool available() const override { return true; }
    QString unavailableReason() const override { return {}; }
    std::vector<CaptureWindow> list() override;
    LiveOpenResult open(const WindowTarget& target) override;

    void addWindow(const CaptureWindow& w, int width, int height);
    void closeWindow(quint64 handle);
    void setColor(quint64 handle, quint32 rgb);            // "slide moi"
    void setMinimized(quint64 handle, bool on);

    struct State;
private:
    std::shared_ptr<State> s_;
};

} // namespace lc
