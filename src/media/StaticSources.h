#pragma once
#include "Source.h"
#include <QColor>
#include <QImage>

namespace lc {

QImage makeTestPattern(int w, int h, const QString& label = {});
// Mau thu cho TUNG man hinh LED: ket cau makeTestPattern + thuoc do moi 100 diem anh va toa do 4 goc, de kiem tra mapping/cat/xoay.
QImage makeOutputTestPattern(int w, int h, const QString& label);

class ImageSource : public Source {
public:
    ImageSource(QImage img, QString name);
    static std::shared_ptr<ImageSource> fromFile(const QString& path);   // nullptr neu loi
    QString name() const override { return name_; }
    TexRef acquire(GL& gl) override;
    QImage snapshot(int maxWidth = 160) const override;
private:
    QImage img_;
    QString name_;
    GlTexture tex_;
    bool uploaded_ = false;
};

// Thay cho file media khong mo duoc: hinh bao loi, error() khac rong (playlist bo qua muc nay).
class MissingSource : public ImageSource {
public:
    explicit MissingSource(const QString& path);
    QString error() const override { return error_; }
private:
    QString error_;
};

class ColorSource : public Source {
public:
    ColorSource(QColor c, QString name) : color_(c), name_(std::move(name)) {}
    QString name() const override { return name_; }
    TexRef acquire(GL& gl) override;
    QImage snapshot(int maxWidth = 160) const override;
private:
    QColor color_;
    QString name_;
    GlTexture tex_;
    bool uploaded_ = false;
};

} // namespace lc
