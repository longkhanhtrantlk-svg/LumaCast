#pragma once
#include <QColor>
#include <QImage>
#include <QWidget>

namespace lc {

// Khung xem Preview (vien xanh la) hoac Program (vien do + nhan ON AIR).
class MonitorWidget : public QWidget {
public:
    MonitorWidget(const QString& title, const QColor& color, bool program, QWidget* parent = nullptr);
    void setImage(const QImage& img);
    void setSceneName(const QString& name);
    void setOnAir(bool on);
    QSize sizeHint() const override { return {420, 260}; }
    QSize minimumSizeHint() const override { return {200, 130}; }
    bool hasImage() const { return !img_.isNull(); }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QString title_, scene_;
    QColor color_;
    bool program_, onAir_ = false;
    QImage img_;
};

} // namespace lc
