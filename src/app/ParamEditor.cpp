#include "ParamEditor.h"
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <algorithm>

namespace lc {

ParamEditor::ParamEditor(std::shared_ptr<const EffectDef> def, const QVariantMap& values, QWidget* parent)
    : QWidget(parent), def_(std::move(def)) {
    for (const ParamDef& p : def_->params) values_[p.name] = sanitizeParam(p, values.contains(p.name) ? values.value(p.name) : p.def);
    build();
}

void ParamEditor::build() {
    auto* form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);
    widgets_.assign(def_->params.size(), nullptr);
    for (int i = 0; i < def_->params.size(); ++i) {
        const ParamDef& p = def_->params[i];
        const QString objName = "param_" + p.name;
        QWidget* w = nullptr;
        switch (p.type) {
        case ParamDef::Float: {
            auto* s = new QDoubleSpinBox;
            s->setRange(p.min, p.max);
            s->setDecimals(3);
            s->setSingleStep(std::max(0.001, (p.max - p.min) / 100.0));
            s->setValue(values_[p.name].toDouble());
            connect(s, &QDoubleSpinBox::valueChanged, this, [this, i](double v) {
                values_[def_->params[i].name] = v;
                emit changed(values_, i);
            });
            w = s;
            break;
        }
        case ParamDef::Int: {
            auto* s = new QSpinBox;
            s->setRange(int(p.min), int(p.max));
            s->setValue(values_[p.name].toInt());
            connect(s, &QSpinBox::valueChanged, this, [this, i](int v) {
                values_[def_->params[i].name] = v;
                emit changed(values_, i);
            });
            w = s;
            break;
        }
        case ParamDef::Bool: {
            auto* c = new QCheckBox;
            c->setChecked(values_[p.name].toBool());
            connect(c, &QCheckBox::toggled, this, [this, i](bool v) {
                values_[def_->params[i].name] = v;
                emit changed(values_, i);
            });
            w = c;
            break;
        }
        case ParamDef::Choice: {
            auto* c = new QComboBox;
            c->addItems(p.choices);
            c->setCurrentIndex(values_[p.name].toInt());
            connect(c, &QComboBox::currentIndexChanged, this, [this, i](int v) {
                values_[def_->params[i].name] = v;
                emit changed(values_, i);
            });
            w = c;
            break;
        }
        case ParamDef::Color: {
            auto* b = new QPushButton;
            w = b;
            widgets_[i] = b;
            refreshColorButton(i);
            connect(b, &QPushButton::clicked, this, [this, i] {
                const QString name = def_->params[i].name;
                const QColor c = QColorDialog::getColor(values_[name].value<QColor>(), this, def_->params[i].label, QColorDialog::ShowAlphaChannel);
                if (!c.isValid()) return;
                values_[name] = c;
                refreshColorButton(i);
                emit changed(values_, i);
            });
            break;
        }
        case ParamDef::Point: {
            auto* host = new QWidget;
            auto* l = new QHBoxLayout(host);
            l->setContentsMargins(0, 0, 0, 0);
            const QPointF pt = values_[p.name].toPointF();
            for (int k = 0; k < 2; ++k) {
                auto* s = new QDoubleSpinBox;
                s->setObjectName(objName + (k == 0 ? "_x" : "_y"));
                s->setRange(-1, 2);
                s->setDecimals(3);
                s->setSingleStep(0.01);
                s->setPrefix(k == 0 ? "x " : "y ");
                s->setValue(k == 0 ? pt.x() : pt.y());
                connect(s, &QDoubleSpinBox::valueChanged, this, [this, i, k](double v) {
                    QPointF q = values_[def_->params[i].name].toPointF();
                    if (k == 0) q.setX(v); else q.setY(v);
                    values_[def_->params[i].name] = q;
                    emit changed(values_, i);
                });
                l->addWidget(s);
            }
            w = host;
            break;
        }
        }
        w->setObjectName(objName);
        widgets_[i] = w;
        form->addRow(p.label, w);
    }
}

void ParamEditor::refreshColorButton(int i) {
    auto* b = static_cast<QPushButton*>(widgets_[i]);
    const QColor c = values_[def_->params[i].name].value<QColor>();
    b->setText(c.name(QColor::HexArgb));
    b->setStyleSheet(QString("background:%1;color:%2;").arg(c.name(), c.lightness() > 128 ? "#000" : "#fff"));
}

void ParamEditor::setValues(const QVariantMap& v) {
    for (int i = 0; i < def_->params.size(); ++i) {
        const ParamDef& p = def_->params[i];
        values_[p.name] = sanitizeParam(p, v.contains(p.name) ? v.value(p.name) : p.def);
        QWidget* w = widgets_[i];
        QSignalBlocker b(w);
        switch (p.type) {
        case ParamDef::Float: static_cast<QDoubleSpinBox*>(w)->setValue(values_[p.name].toDouble()); break;
        case ParamDef::Int: static_cast<QSpinBox*>(w)->setValue(values_[p.name].toInt()); break;
        case ParamDef::Bool: static_cast<QCheckBox*>(w)->setChecked(values_[p.name].toBool()); break;
        case ParamDef::Choice: static_cast<QComboBox*>(w)->setCurrentIndex(values_[p.name].toInt()); break;
        case ParamDef::Color: refreshColorButton(i); break;
        case ParamDef::Point:
            for (int k = 0; k < 2; ++k) {
                auto* s = w->findChild<QDoubleSpinBox*>(w->objectName() + (k == 0 ? "_x" : "_y"));
                QSignalBlocker sb(s);
                s->setValue(k == 0 ? values_[p.name].toPointF().x() : values_[p.name].toPointF().y());
            }
            break;
        }
    }
}

} // namespace lc
