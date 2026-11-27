#include "NowShowing.h"
#include "Text.h"
#include "Ui.h"
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QWheelEvent>

namespace lc {

NowShowing::NowShowing(QWidget* parent) : QWidget(parent) {
    setObjectName("nowShowing");
    setFixedHeight(kHeight);
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(6);
    h->addWidget(new QLabel(text::nsLabel()));

    auto* host = new QWidget;
    row_ = new QHBoxLayout(host);
    row_->setContentsMargins(0, 0, 0, 0);
    row_->setSpacing(4);
    none_ = new QLabel(text::nsNone());
    none_->setObjectName("nsNone");
    none_->setStyleSheet("color:#8a90a2");
    row_->addWidget(none_);
    row_->addStretch();
    scroll_ = new QScrollArea;
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setWidgetResizable(true);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setFixedHeight(kHeight);
    scroll_->setMinimumWidth(40);
    scroll_->setWidget(host);
    scroll_->viewport()->installEventFilter(this);
    h->addWidget(scroll_, 1);

    hideAll_ = new QPushButton(text::nsHideAll());
    hideAll_->setObjectName("nsHideAll");
    hideAll_->setToolTip(text::nsHideAllTip());
    hideAll_->setProperty("compact", true);
    hideAll_->setFocusPolicy(Qt::NoFocus);
    ui::setWhy(hideAll_, text::nsNothingWhy());
    hideAll_->setEnabled(false);
    h->addWidget(hideAll_);
    connect(hideAll_, &QPushButton::clicked, this, [this] { if (onHideAll) onHideAll(); });
}

bool NowShowing::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == scroll_->viewport() && ev->type() == QEvent::Wheel) {
        QScrollBar* bar = scroll_->horizontalScrollBar();
        const auto* w = static_cast<QWheelEvent*>(ev);
        bar->setValue(bar->value() - w->angleDelta().y() / 2 - w->angleDelta().x() / 2);
        return true;
    }
    return QWidget::eventFilter(obj, ev);
}

void NowShowing::rebuild(const std::vector<OverlayFrame>& shown) {
    for (Chip& c : chips_) {
        c.box->hide();
        c.box->setObjectName("nsChipOld");
        c.box->deleteLater();
    }
    chips_.clear();
    int at = 0;
    for (const OverlayFrame& f : shown) {
        Chip c;
        c.id = f.def.id;
        c.box = new QWidget;
        c.box->setObjectName(QString("nsChip%1").arg(c.id));
        c.box->setAttribute(Qt::WA_StyledBackground);
        c.box->setStyleSheet(QString("QWidget#nsChip%1{background:#8a1c1c;border-radius:4px}").arg(c.id));
        auto* h = new QHBoxLayout(c.box);
        h->setContentsMargins(8, 1, 2, 1);
        h->setSpacing(4);
        c.text = new QLabel;
        c.text->setTextFormat(Qt::PlainText);
        c.text->setStyleSheet("color:white;font-weight:bold;background:transparent");
        h->addWidget(c.text);
        auto* x = ui::iconButton(Glyph::Close, QString("nsClose%1").arg(c.id).toUtf8().constData(), text::nsHide().arg(f.def.name));
        x->setFlat(true);
        x->setFixedSize(22, 22);
        h->addWidget(x);
        const int id = c.id;
        connect(x, &QPushButton::clicked, this, [this, id] { if (onHide) onHide(id); });
        row_->insertWidget(at++, c.box);
        chips_.push_back(c);
    }
}

void NowShowing::refresh(const std::vector<OverlayFrame>& frames) {
    std::vector<OverlayFrame> shown;
    for (auto it = frames.rbegin(); it != frames.rend(); ++it)        // lop tren cung truoc, nhu trong danh sach Overlay
        if (it->on) shown.push_back(*it);
    bool same = shown.size() == chips_.size();
    for (size_t i = 0; same && i < shown.size(); ++i) same = shown[i].def.id == chips_[i].id;
    if (!same) rebuild(shown);
    for (size_t i = 0; i < shown.size(); ++i) {
        const OverlayFrame& f = shown[i];
        QString label = f.def.name.size() > 24 ? f.def.name.left(23) + "…" : f.def.name;
        if (f.def.type == OverlayType::Countdown) label += "  " + formatCountdown(f.secondsLeft, f.def.seconds >= 3600);
        if (chips_[i].text->text() != label) chips_[i].text->setText(label);
    }
    none_->setVisible(shown.empty());
    hideAll_->setEnabled(!shown.empty());
}

} // namespace lc
