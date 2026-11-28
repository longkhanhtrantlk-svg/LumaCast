#pragma once
#include <QSettings>
#include <QString>

// Ngon ngu giao dien. Chu GOC trong ma la tieng Viet; ban tieng Anh nam o translations/lumacast_en.ts (ngu canh "lc").
//   LC_TR("chữ")            chu hien cho nguoi dung
//   LC_TRC("chữ", "nghĩa")  khi mot chu tieng Viet can hai ban tieng Anh khac nhau (phan biet bang "nghia")
//   LC_NOOP("chữ")          danh dau chu trong bang tinh; dich sau bang lc::i18n::translate()
//   LC_BOTH(bieu thuc)      chu vua hien cho nguoi dung vua ghi log: dung ca hai ban (xem Text)
// Them chu moi: boc bang mot trong cac macro tren roi them <message> vao file .ts (test `i18n` bao ten chu con thieu).
// LOG VA FILE PROJECT KHONG DICH: dong log viet thang tieng Viet; chu sinh tu ham dung chung thi bao bang SourceScope.
// Giu nguyen thuat ngu: Preview, Program, CUT, TAKE, FTB, Scene, Output, Layer.
namespace lc {

enum class Language { Vietnamese, English };

namespace i18n {

constexpr const char* kKeyLanguage = "ui/language";                   // "vi" | "en": nguoi dung chon trong app (uu tien nhat)
constexpr const char* kKeyInstallLanguage = "ui/installLanguage";     // bo cai ghi (registry cua nguoi dung): "vi" | "en"
constexpr const char* kInstallHintFile = "install-language.txt";      // hoac file canh exe (ban dong goi khac): "vi" | "en"
constexpr const char* kContext = "lc";

const char* languageId(Language l);
bool languageFromId(const QString& id, Language& out);

// Ngon ngu se dung o lan mo app: chon trong app > chon luc cai (bo cai ghi vao cai dat; hoac file canh exe) > tieng Viet.
Language configured(QSettings& st, const QString& appDir);
void save(QSettings& st, Language l);

// Goi MOT lan, truoc khi dung giao dien (doi ngon ngu chi co tac dung o lan mo sau). Tieng Viet = khong cai bo dich.
void install(Language l);
Language current();

QString translate(const char* source, const char* meaning = nullptr);

// Trong pham vi nay (rieng luong hien tai) translate() tra chu GOC: dung khi tao chu de ghi log.
class SourceScope {
public:
    SourceScope();
    ~SourceScope();
    SourceScope(const SourceScope&) = delete;
    SourceScope& operator=(const SourceScope&) = delete;
};

struct Text {
    QString shown;                                                     // theo ngon ngu giao dien
    QString source;                                                    // tieng Viet, de ghi log
};

template <class F>
Text both(F make) {
    Text t;
    t.shown = make();
    SourceScope scope;
    t.source = make();
    return t;
}

} // namespace i18n
} // namespace lc

#define LC_TR(s) ::lc::i18n::translate(s)
#define LC_TRC(s, meaning) ::lc::i18n::translate(s, meaning)
#define LC_NOOP(s) s
#define LC_BOTH(...) ::lc::i18n::both([&] { return QString(__VA_ARGS__); })
