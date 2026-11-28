#include "LiveActions.h"
#include "core/I18n.h"
#include <algorithm>

namespace lc {

QString LiveAction::id() const {
    switch (kind) {
    case Lock: return "lock";
    case Take: return "take";
    case Cut: return "cut";
    case Ftb: return "ftb";
    case BlackAll: return "blackAll";
    case SlideNext: return "slide.next";
    case SlidePrev: return "slide.prev";
    case Preview: return QString("preview.%1").arg(index);
    case OutputBlack: return QString("output.%1.black").arg(index);
    case OutputFreeze: return QString("output.%1.freeze").arg(index);
    case OutputPattern: return QString("output.%1.pattern").arg(index);
    case None: break;
    }
    return {};
}

LiveAction LiveAction::fromId(const QString& id) {
    LiveAction a;
    const QStringList p = id.split('.');
    auto number = [](const QString& s, int max) {
        bool ok = false;
        const int v = s.toInt(&ok);
        return ok && v >= 1 && v <= max && s == QString::number(v) ? v : 0;
    };
    if (p.size() == 1) {
        a.kind = id == "lock" ? Lock : id == "take" ? Take : id == "cut" ? Cut : id == "ftb" ? Ftb : id == "blackAll" ? BlackAll : None;
    } else if (id == "slide.next" || id == "slide.prev") {
        a.kind = id == "slide.next" ? SlideNext : SlidePrev;
    } else if (p.size() == 2 && p[0] == "preview") {
        a.index = number(p[1], kPreviewCount);
        if (a.index) a.kind = Preview;
    } else if (p.size() == 3 && p[0] == "output") {
        a.index = number(p[1], kMaxOutputIndex);
        if (a.index) a.kind = p[2] == "black" ? OutputBlack : p[2] == "freeze" ? OutputFreeze : p[2] == "pattern" ? OutputPattern : None;
    }
    if (a.kind == None) a.index = 0;
    return a;
}

QString LiveAction::label(const QString& outputName) const {
    const QString out = QString("Output %1%2").arg(index).arg(outputName.isEmpty() ? QString() : " (" + outputName + ")");
    switch (kind) {
    case Lock: return LC_TR("Khoá vận hành (chỉ khoá)");
    case Take: return "TAKE";
    case Cut: return "CUT";
    case Ftb: return "FTB";
    case BlackAll: return "BLACK ALL";
    case SlideNext: return LC_TR("PowerPoint: slide tiếp");
    case SlidePrev: return LC_TR("PowerPoint: slide trước");
    case Preview: return QString(LC_TR("Chọn Preview %1")).arg(index);
    case OutputBlack: return out + LC_TR(": Đen");
    case OutputFreeze: return out + LC_TR(": Dừng");
    case OutputPattern: return out + LC_TR(": Mẫu");
    case None: break;
    }
    return "?";
}

int LiveAction::order() const {
    switch (kind) {
    case Lock: return 0;
    case Take: return 1;
    case Cut: return 2;
    case Ftb: return 3;
    case BlackAll: return 4;
    case SlideNext: return 5;
    case SlidePrev: return 6;
    case Preview: return 10 + index;
    case OutputBlack: return 100 + index * 3;
    case OutputFreeze: return 101 + index * 3;
    case OutputPattern: return 102 + index * 3;
    case None: break;
    }
    return 100000;
}

QStringList LiveAction::allIds(int outputCount) {
    QStringList ids = {"lock", "take", "cut", "ftb", "blackAll", "slide.next", "slide.prev"};
    for (int i = 1; i <= kPreviewCount; ++i) ids << QString("preview.%1").arg(i);
    for (int o = 1; o <= std::min(outputCount, kMaxOutputIndex); ++o)
        for (const char* what : {"black", "freeze", "pattern"}) ids << QString("output.%1.%2").arg(o).arg(what);
    return ids;
}

} // namespace lc
