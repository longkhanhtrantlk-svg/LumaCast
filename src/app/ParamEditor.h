#pragma once
#include "render/EffectDef.h"
#include <QWidget>
#include <memory>

class QFormLayout;

namespace lc {

// Bo chinh tham so TU SINH tu EffectDef (plug-in hay dung san): float/int -> o so, bool -> hop tick, choice -> danh sach,
// color -> nut chon mau, point -> hai o so. Phat `changed` moi khi nguoi dung doi mot tham so.
class ParamEditor : public QWidget {
    Q_OBJECT
public:
    ParamEditor(std::shared_ptr<const EffectDef> def, const QVariantMap& values, QWidget* parent = nullptr);
    QVariantMap values() const { return values_; }
    void setValues(const QVariantMap& v);            // cap nhat o nhap, KHONG phat `changed`

signals:
    void changed(const QVariantMap& values, int paramIndex);

private:
    void build();
    void refreshColorButton(int i);

    std::shared_ptr<const EffectDef> def_;
    QVariantMap values_;
    std::vector<QWidget*> widgets_;                  // 1 widget chinh cho moi tham so (Point: widget bao hai o)
};

} // namespace lc
