#pragma once
#include "Source.h"
#include <QColor>
#include <QImage>

namespace lc {

QImage makeTestPattern(int w, int h, const QString& label = {});

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
