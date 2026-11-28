#include "OutputHealth.h"
#include "core/I18n.h"

namespace lc {

int resolveScreenIndex(const OutputConfig& c, const QStringList& names) {
    if (!c.screenName.isEmpty()) return names.indexOf(c.screenName);          // -1 neu khong con
    const int i = c.screenIndex < 0 ? 0 : c.screenIndex;
    return i < names.size() ? i : -1;
}

OutputHealth evaluateOutput(const OutputConfig& c, const QStringList& names, int windowScreenIndex, bool exposed) {
    if (!c.fullscreen) return exposed ? OutputHealth::Ok : OutputHealth::Hidden;
    const int want = resolveScreenIndex(c, names);
    if (want < 0) return OutputHealth::NoScreen;
    if (windowScreenIndex != want) return OutputHealth::WrongScreen;
    return exposed ? OutputHealth::Ok : OutputHealth::Hidden;
}

QString healthText(OutputHealth h, const OutputConfig& c) {
    switch (h) {
    case OutputHealth::Ok: return "OK";
    case OutputHealth::NoScreen:
        return QString(LC_TR("MẤT MÀN HÌNH (%1)")).arg(c.screenName.isEmpty() ? QString(LC_TR("màn %1")).arg(std::max(0, c.screenIndex) + 1) : c.screenName);
    case OutputHealth::Hidden: return LC_TR("KHÔNG HIỂN THỊ");
    case OutputHealth::WrongScreen: return LC_TR("SAI MÀN HÌNH");
    }
    return {};
}

} // namespace lc
