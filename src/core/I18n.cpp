#include "I18n.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTranslator>
#include <atomic>
#include <memory>

static void initI18nResource() { Q_INIT_RESOURCE(lumacast_i18n); }      // thu vien tinh: phai goi, va goi ngoai namespace

namespace lc::i18n {

namespace {
std::atomic<Language> gCurrent{Language::Vietnamese};
std::unique_ptr<QTranslator> gTranslator;
thread_local int tSourceDepth = 0;
} // namespace

const char* languageId(Language l) { return l == Language::English ? "en" : "vi"; }

bool languageFromId(const QString& id, Language& out) {
    const QString s = id.trimmed().toLower();
    if (s == "vi") out = Language::Vietnamese;
    else if (s == "en") out = Language::English;
    else return false;
    return true;
}

Language configured(QSettings& st, const QString& appDir) {
    Language l = Language::Vietnamese;
    if (languageFromId(st.value(kKeyLanguage).toString(), l)) return l;
    if (languageFromId(st.value(kKeyInstallLanguage).toString(), l)) return l;
    QFile hint(QDir(appDir).filePath(kInstallHintFile));
    if (hint.open(QIODevice::ReadOnly)) {
        QByteArray text = hint.read(64);
        if (text.startsWith("\xEF\xBB\xBF")) text.remove(0, 3);
        languageFromId(QString::fromLatin1(text), l);
    }
    return l;
}

void save(QSettings& st, Language l) { st.setValue(kKeyLanguage, languageId(l)); }

void install(Language l) {
    if (gTranslator) {
        QCoreApplication::removeTranslator(gTranslator.get());
        gTranslator.reset();
    }
    gCurrent = Language::Vietnamese;
    if (l != Language::English) return;
    initI18nResource();
    auto t = std::make_unique<QTranslator>();
    if (!t->load(":/i18n/lumacast_en.qm")) {
        qWarning() << "Ngon ngu: khong nap duoc ban tieng Anh, dung tieng Viet";
        return;
    }
    QCoreApplication::installTranslator(t.get());
    gTranslator = std::move(t);
    gCurrent = Language::English;
}

Language current() { return gCurrent; }

QString translate(const char* source, const char* meaning) {
    if (tSourceDepth > 0) return QString::fromUtf8(source);
    return QCoreApplication::translate(kContext, source, meaning);
}

SourceScope::SourceScope() { ++tSourceDepth; }
SourceScope::~SourceScope() { --tSourceDepth; }

} // namespace lc::i18n
