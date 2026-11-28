// Ngon ngu giao dien: (1) moi chu trong ma co ban tieng Anh; (2) khong con chu tieng Viet viet thang ngoai macro dich (tru log);
// (3) thu tu chon ngon ngu (trong app > luc cai > tieng Viet); (4) giao dien tieng Anh khong sot chu tieng Viet; log van tieng Viet;
// project mo duoc o ca hai ngon ngu; trang dieu khien web; menu Cai dat -> Ngon ngu.
// LC_SHOT_DIR=<thu muc> de luu anh chup giao dien tieng Anh.
#include "AboutInfo.h"
#include "InputDialog.h"
#include "InspectorPanel.h"
#include "LiveDialogs.h"
#include "MainWindow.h"
#include "OverlayPanel.h"
#include "ScreenDialog.h"
#include "SettingsDialog.h"
#include "Shortcuts.h"
#include "Text.h"
#include "Theme.h"
#include "control/ControlApi.h"
#include "control/ControlPage.h"
#include "core/AppSettings.h"
#include "core/I18n.h"
#include "core/Project.h"
#include "media/StaticSources.h"
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTreeWidget>
#include <QXmlStreamReader>
#include <iostream>

using namespace lc;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { ++g_fail; std::cerr << "FAIL: " << msg << "  (" #cond ")\n"; } else std::cout << "ok:   " << msg << "\n"; } while (0)

static void pump(int ms) {
    QElapsedTimer t; t.start();
    while (t.elapsed() < ms) {
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QThread::msleep(5);
    }
}

// Chu cai chi co trong tieng Viet (co dau).
static bool hasVietnamese(const QString& s) {
    static const QRegularExpression re(QString::fromUtf8(
        "[àáảãạăằắẳẵặâầấẩẫậđèéẻẽẹêềếểễệìíỉĩịòóỏõọôồốổỗộơờớởỡợùúủũụưừứửữựỳýỷỹỵĐ]"), QRegularExpression::CaseInsensitiveOption);
    return re.match(s).hasMatch();
}

static QString readFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readAll()).replace("\r\n", "\n");
}

// ---------------- doc chuoi trong ma C++ ----------------
struct Literal { int start, end, line; QString text; bool raw; };

static std::vector<Literal> literals(const QString& t, QString* codeOnly) {
    std::vector<Literal> out;
    QString code = t;                                       // ban sao: noi dung chuoi + comment thay bang khoang trang
    auto blank = [&](int from, int to) { for (int k = from; k < to && k < code.size(); ++k) if (code[k] != '\n') code[k] = ' '; };
    int line = 1;
    const int n = t.size();
    for (int i = 0; i < n;) {
        const QChar c = t[i];
        if (c == '\n') { ++line; ++i; continue; }
        if (c == '/' && i + 1 < n && t[i + 1] == '/') { int j = t.indexOf('\n', i); if (j < 0) j = n; blank(i, j); i = j; continue; }
        if (c == '/' && i + 1 < n && t[i + 1] == '*') {
            int j = t.indexOf("*/", i + 2); j = j < 0 ? n : j + 2;
            line += t.mid(i, j - i).count('\n'); blank(i, j); i = j; continue;
        }
        if (c == 'R' && i + 1 < n && t[i + 1] == '"') {
            const int open = t.indexOf('(', i + 2);
            const QString close = ")" + t.mid(i + 2, open - i - 2) + "\"";
            int j = t.indexOf(close, open + 1); if (j < 0) j = n;
            out.push_back({i, int(j + close.size()), line, t.mid(open + 1, j - open - 1), true});
            line += t.mid(i, j - i).count('\n'); blank(i + 2, j + close.size() - 1); i = j + close.size(); continue;
        }
        if (c == '\'') {
            if (i + 2 < n && t[i + 1] != '\\' && t[i + 2] == '\'') { i += 3; continue; }
            if (i + 1 < n && t[i + 1] == '\\') { const int j = t.indexOf('\'', i + 3); i = j < 0 ? n : j + 1; continue; }
            ++i; continue;
        }
        if (c == '"') {
            QString s;
            int j = i + 1;
            while (j < n && t[j] != '"' && t[j] != '\n') {
                if (t[j] == '\\' && j + 1 < n) {
                    const QChar e = t[j + 1];
                    s += e == 'n' ? QChar('\n') : e == 't' ? QChar('\t') : e == 'r' ? QChar('\r') : e;
                    j += 2;
                } else s += t[j++];
            }
            out.push_back({i, j + 1, line, s, false});
            blank(i + 1, j);
            i = j + 1; continue;
        }
        ++i;
    }
    if (codeOnly) *codeOnly = code;
    return out;
}

using Key = QPair<QString, QString>;                       // (chu goc, nghia)

// ---------------- 1 + 2: bang dich va ma nguon ----------------
static void testSources(const QString& root) {
    QMap<Key, QString> table;
    {
        QFile f(root + "/translations/lumacast_en.ts");
        CHECK(f.open(QIODevice::ReadOnly), "doc duoc translations/lumacast_en.ts");
        QXmlStreamReader x(&f);
        QString src, comment, tr;
        while (!x.atEnd()) {
            x.readNext();
            if (x.isStartElement() && x.name() == u"source") src = x.readElementText();
            else if (x.isStartElement() && x.name() == u"comment") comment = x.readElementText();
            else if (x.isStartElement() && x.name() == u"translation") tr = x.readElementText();
            else if (x.isEndElement() && x.name() == u"message") { table.insert({src, comment}, tr); src.clear(); comment.clear(); tr.clear(); }
        }
        CHECK(!x.hasError() && table.size() > 800, "file .ts hop le (" + std::to_string(table.size()) + " chu)");
    }

    QSet<Key> used;
    QStringList missing, hardcoded;
    static const QRegularExpression macro(R"(\b(?:(LC_TR|LC_TRC|LC_NOOP)\(|LC_TEXT\(\w+,)\s*$)");
    static const QRegularExpression logCall(R"(\bq(Info|Warning|Critical|Debug)\s*\()");
    QDirIterator it(root + "/src", {"*.cpp", "*.h"}, QDir::Files, QDirIterator::Subdirectories);
    int files = 0;
    while (it.hasNext()) {
        const QString path = it.next();
        const QString rel = QDir(root).relativeFilePath(path);
        if (rel.endsWith("core/I18n.h")) continue;
        ++files;
        const QString t = readFile(path);
        QString code;
        const auto lits = literals(t, &code);
        const QStringList lines = t.split('\n');
        for (size_t k = 0; k < lits.size();) {
            // gop cac chuoi viet lien nhau
            size_t last = k;
            QString text = lits[k].text;
            while (last + 1 < lits.size() && code.mid(lits[last].end, lits[last + 1].start - lits[last].end).trimmed().isEmpty()) text += lits[++last].text;
            const Literal& first = lits[k];
            const QString before = code.mid(std::max(0, first.start - 80), std::min(80, first.start));
            const auto m = macro.match(before);
            if (m.hasMatch()) {
                QString meaning;
                if (m.captured(1) == "LC_TRC" && last + 1 < lits.size() && code.mid(lits[last].end, lits[last + 1].start - lits[last].end).trimmed() == ",") {
                    meaning = lits[last + 1].text;
                    ++last;
                }
                const Key key{text, meaning};
                used.insert(key);
                if (!table.contains(key)) missing << rel + ":" + QString::number(first.line) + ": " + text;
            } else if (!first.raw && hasVietnamese(text)) {
                // duoc phep: dong ghi log (log khong dich), dong danh dau khong-dich
                int from = first.start - 1, depth = 0;
                while (from >= 0) {                                      // dau cau lenh chua chuoi
                    const QChar c = code[from];
                    if (c == ')' || c == ']') ++depth;
                    else if (c == '(' || c == '[') depth = std::max(0, depth - 1);
                    else if (depth == 0 && (c == ';' || c == '{' || c == '}')) break;
                    --from;
                }
                const bool isLog = logCall.match(code.mid(from + 1, first.start - from - 1)).hasMatch();
                const bool marked = lines.value(first.line - 1).contains("khong-dich");
                const bool logOnly = rel.endsWith("media/FFmpegSource.cpp") && (text.contains("phần mềm") || text.contains("phần cứng") || text.contains("trực tiếp"));
                if (!isLog && !marked && !logOnly) hardcoded << rel + ":" + QString::number(first.line) + ": " + text.left(60);
            }
            k = last + 1;
        }
    }
    // plug-in di kem
    QDirIterator pit(root + "/plugins", {"plugin.json"}, QDir::Files, QDirIterator::Subdirectories);
    while (pit.hasNext()) {
        const QString path = pit.next();
        const QJsonObject o = QJsonDocument::fromJson(readFile(path).toUtf8()).object();
        QStringList texts{o["name"].toString(), o["description"].toString()};
        for (const auto& v : o["params"].toArray()) {
            texts << v.toObject()["label"].toString();
            for (const auto& c : v.toObject()["choices"].toArray()) texts << c.toString();
        }
        for (const QString& s : texts) {
            if (s.isEmpty()) continue;
            used.insert({s, QString()});
            if (!table.contains({s, QString()})) missing << QDir(root).relativeFilePath(path) + ": " + s;
        }
    }
    CHECK(files > 100, "quet du ma nguon (" + std::to_string(files) + " file)");
    CHECK(missing.isEmpty(), "moi chu trong ma co ban tieng Anh:\n" + missing.join("\n").toStdString());
    CHECK(hardcoded.isEmpty(), "khong con chu tieng Viet ngoai macro dich (tru log):\n" + hardcoded.join("\n").toStdString());

    QStringList stale, args, vi, empty;
    static const QRegularExpression arg("%\\d");
    auto argsOf = [](const QString& s) {
        QStringList a;
        auto m = arg.globalMatch(s);
        while (m.hasNext()) a << m.next().captured();
        a.sort();
        return a;
    };
    for (auto e = table.constBegin(); e != table.constEnd(); ++e) {
        const QString src = e.key().first, tr = e.value();
        if (!used.contains(e.key())) stale << src;
        if (tr.trimmed().isEmpty()) empty << src;
        if (argsOf(src) != argsOf(tr)) args << src + " -> " + tr;
        if (hasVietnamese(tr)) vi << tr;
    }
    CHECK(stale.isEmpty(), "khong co ban dich thua:\n" + stale.join("\n").toStdString());
    CHECK(empty.isEmpty(), "khong co ban dich rong:\n" + empty.join("\n").toStdString());
    CHECK(args.isEmpty(), "tham so %1 %2... khop giua hai ngon ngu:\n" + args.join("\n").toStdString());
    CHECK(vi.isEmpty(), "ban tieng Anh khong con chu co dau:\n" + vi.join("\n").toStdString());
    QStringList terms;                                       // thuat ngu giu nguyen
    for (auto e = table.constBegin(); e != table.constEnd(); ++e)
        for (const char* term : {"Preview", "Program", "CUT", "TAKE", "FTB"}) {
            const QRegularExpression word(QString("(?<![A-Za-z])%1(?![A-Za-z])").arg(term));
            if (e.key().first.contains(word) && !e.value().contains(word)) terms << QString("%1: %2 -> %3").arg(term, e.key().first, e.value());
        }
    CHECK(terms.isEmpty(), "giu nguyen Preview / Program / CUT / TAKE / FTB:\n" + terms.join("\n").toStdString());
}

// ---------------- 3: chon ngon ngu ----------------
static void testChoice(const QString& dir) {
    QSettings st(dir + "/choice.ini", QSettings::IniFormat);
    const QString app = dir + "/appdir";
    QDir().mkpath(app);
    auto hint = [&](const QByteArray& text) {
        QFile f(app + "/" + i18n::kInstallHintFile);
        f.open(QIODevice::WriteOnly | QIODevice::Truncate);
        f.write(text);
    };
    CHECK(i18n::configured(st, app) == Language::Vietnamese, "chua chon gi: tieng Viet");
    hint("en");
    CHECK(i18n::configured(st, app) == Language::English, "chua chon trong app: theo luc cai");
    hint("\xEF\xBB\xBF" "en\r\n");
    CHECK(i18n::configured(st, app) == Language::English, "file cua bo cai co BOM / xuong dong van doc duoc");
    hint("fr");
    CHECK(i18n::configured(st, app) == Language::Vietnamese, "gia tri la: tieng Viet");
    hint("en");
    i18n::save(st, Language::Vietnamese);
    CHECK(i18n::configured(st, app) == Language::Vietnamese, "chon trong app uu tien hon luc cai");
    i18n::save(st, Language::English);
    hint("vi");
    CHECK(i18n::configured(st, app) == Language::English, "chon trong app uu tien hon luc cai (chieu nguoc)");
    st.setValue(i18n::kKeyLanguage, "xx");
    CHECK(i18n::configured(st, app) == Language::Vietnamese, "cai dat hong: bo qua, theo luc cai");

    CHECK(i18n::current() == Language::Vietnamese && text::lockedWhy() == "Đang khoá vận hành", "mac dinh khi chay: tieng Viet");
    i18n::install(Language::English);
    CHECK(i18n::current() == Language::English && text::lockedWhy() == "Show lock is on", "cai ban tieng Anh");
    CHECK(i18n::translate("Dừng", "output freeze") == "Freeze", "chu phan biet theo nghia");
    {
        i18n::SourceScope scope;
        CHECK(text::lockedWhy() == "Đang khoá vận hành", "SourceScope (ghi log): tra chu tieng Viet");
    }
    CHECK(text::lockedWhy() == "Show lock is on", "het SourceScope: lai tieng Anh");
    const i18n::Text both = LC_BOTH(QString(LC_TR("Kênh %1")).arg(2));
    CHECK(both.shown == "Channel 2" && both.source == "Kênh 2", "LC_BOTH: giao dien tieng Anh, log tieng Viet");
    i18n::install(Language::Vietnamese);
    CHECK(text::lockedWhy() == "Đang khoá vận hành", "go ban dich: tieng Viet");
}

// ---------------- 4: giao dien tieng Anh ----------------
static QStringList textsOf(QObject* o) {
    QStringList s;
    if (auto* a = qobject_cast<QAction*>(o)) {
        s << a->text() << a->toolTip() << a->statusTip();
        return s;
    }
    auto* w = qobject_cast<QWidget*>(o);
    if (!w) return s;
    s << w->toolTip() << w->statusTip() << w->accessibleName();
    if (w->isWindow() || qobject_cast<QDockWidget*>(w)) s << w->windowTitle();
    if (auto* b = qobject_cast<QAbstractButton*>(w)) s << b->text();
    if (auto* l = qobject_cast<QLabel*>(w)) s << l->text();
    if (auto* e = qobject_cast<QLineEdit*>(w)) s << e->placeholderText();
    if (auto* g = qobject_cast<QGroupBox*>(w)) s << g->title();
    if (auto* m = qobject_cast<QMenu*>(w)) s << m->title();
    if (auto* sp = qobject_cast<QSpinBox*>(w)) s << sp->prefix() << sp->suffix() << sp->specialValueText();
    if (auto* sp = qobject_cast<QDoubleSpinBox*>(w)) s << sp->prefix() << sp->suffix() << sp->specialValueText();
    if (auto* c = qobject_cast<QComboBox*>(w))
        for (int i = 0; i < c->count(); ++i) s << c->itemText(i) << c->itemData(i, Qt::ToolTipRole).toString();
    if (auto* l = qobject_cast<QListWidget*>(w))
        for (int i = 0; i < l->count(); ++i) s << l->item(i)->text() << l->item(i)->toolTip();
    if (auto* t = qobject_cast<QTabBar*>(w))
        for (int i = 0; i < t->count(); ++i) s << t->tabText(i) << t->tabToolTip(i);
    if (auto* t = qobject_cast<QTableWidget*>(w)) {
        for (int c = 0; c < t->columnCount(); ++c) if (auto* h = t->horizontalHeaderItem(c)) s << h->text();
        for (int r = 0; r < t->rowCount(); ++r)
            for (int c = 0; c < t->columnCount(); ++c) {
                if (auto* i = t->item(r, c)) s << i->text() << i->toolTip();
            }
    }
    if (auto* sb = qobject_cast<QStatusBar*>(w)) s << sb->currentMessage();
    return s;
}

// Chu Viet khong dau tung lot luoi (khong co dau nen hasVietnamese khong thay).
static bool undiacritised(const QString& s) {
    static const QRegularExpression re(R"((cao|khong|chua|nguon|ngang|xoay|khe|phien|thieu))", QRegularExpression::CaseInsensitiveOption);
    return re.match(s).hasMatch();
}

static void checkNoVietnamese(QWidget* root, const std::string& tag) {
    QStringList found;
    QList<QObject*> all{root};
    all << root->findChildren<QObject*>();
    for (QObject* o : all) {
        // ten ngon ngu viet bang chinh ngon ngu do: giu "Tiếng Việt" / "Ngôn ngữ" de nguoi chon nham tim lai duoc
        if (o->objectName() == "languageMenu" || o->objectName() == "language_vi") continue;
        if (o->objectName() == "camDevice") continue;                // ten thiet bi cua may (du lieu, khong phai chu cua app)
        if (auto* a = qobject_cast<QAction*>(o); a && a->menu() && a->menu()->objectName() == "languageMenu") continue;
        for (const QString& s : textsOf(o))
            if (hasVietnamese(s) || undiacritised(s)) found << QString("%1[%2]: %3").arg(o->metaObject()->className(), o->objectName(), s.left(90));
    }
    found.removeDuplicates();
    CHECK(found.isEmpty(), tag + ": khong sot chu tieng Viet\n" + found.join("\n").toStdString());
}

static void shot(QWidget& w, const QString& name) {
    if (qEnvironmentVariableIsSet("LC_SHOT_DIR")) w.grab().save(qEnvironmentVariable("LC_SHOT_DIR") + "/" + name + ".png");
}

static Project makeProject() {
    Project p;
    SourceDef a; a.type = "color"; a.color = Qt::blue; a.label = "Blue";
    auto sa = p.addSource(a);
    auto s = std::make_shared<Scene>();
    s->name = "Blue"; s->size = {1320, 360};
    s->layers.push_back({sa, {0, 0, 1, 1}, 1.0f, LayerFit::Stretch, true});
    p.scenes = {s};
    auto ch = std::make_shared<Channel>();
    ch->name = "K"; ch->size = {1320, 360}; ch->scenes = p.scenes; ch->program = 0; ch->preview = 0;
    p.channels = {ch};
    OutputConfig o; o.name = "Output 1"; o.size = {160, 90};
    p.outputs = {o};
    return p;
}

static void testEnglishUi(const QString& dir) {
    i18n::install(Language::English);

    const OverlayDef def = defaultOverlay(OverlayType::Ticker);
    CHECK(def.name == "Ticker" && def.text == "Ticker text", "ten mac dinh cua lop phu moi theo ngon ngu");

    Project p = makeProject();
    for (OverlayType t : {OverlayType::Title, OverlayType::Countdown, OverlayType::Ticker, OverlayType::Logo}) {
        OverlayDef d = defaultOverlay(t);
        auto defs = p.channels[0]->overlays.defs();
        d.id = int(defs.size()) + 1;
        defs.push_back(d);
        p.channels[0]->overlays.setDefs(defs);
    }
    RenderEngine engine(p);
    engine.startEngine();
    {
        MainWindow w(p, engine, {});
        w.setAskOnClose(false);
        w.resize(1536, 780);
        w.show();
        pump(900);
        for (int id : {1, 2, 3, 4}) {
            w.overlayPanel()->selectId(id);
            pump(150);
            checkNoVietnamese(&w, "cua so chinh (overlay " + std::to_string(id) + ")");
        }
        shot(w, "en-main");
        for (QDockWidget* d : w.findChildren<QDockWidget*>()) {
            if (d->isHidden()) continue;
            d->raise();
            pump(150);
            shot(w, "en-tab-" + d->objectName());
        }
        w.setLocked(true);
        pump(300);
        checkNoVietnamese(&w, "cua so chinh khi khoa");
        shot(w, "en-locked");
        w.setLocked(false);
        for (const char* layout : {"live", "big", "prepare"}) {
            w.applyLayout(layout);
            pump(300);
            checkNoVietnamese(&w, std::string("bo cuc ") + layout);
        }

        auto* languageMenu = w.findChild<QMenu*>("languageMenu");
        auto* vi = w.findChild<QAction*>("language_vi");
        auto* en = w.findChild<QAction*>("language_en");
        CHECK(languageMenu && vi && en && vi->text() == "Tiếng Việt" && en->text() == "English", "menu Cai dat -> Ngon ngu co du hai muc");
        if (vi && en) {
            vi->trigger();
            QSettings st = appSettings();
            CHECK(st.value(i18n::kKeyLanguage).toString() == "vi" && vi->isChecked() && !en->isChecked(), "chon Tieng Viet: duoc luu");
            CHECK(i18n::current() == Language::English && w.statusBar()->currentMessage().contains("next time"),
                  "khong doi ngay; bao ap dung o lan mo sau (khong chan phim live)");
            CHECK(!QApplication::activeModalWidget(), "khong bat hop thoai chan");
            en->trigger();
            CHECK(appSettings().value(i18n::kKeyLanguage).toString() == "en", "chon lai English: duoc luu");
            w.setLocked(true);
            CHECK(!languageMenu->isEnabled(), "khoa van hanh: menu ngon ngu bi mo");
            w.setLocked(false);
            CHECK(languageMenu->isEnabled(), "mo khoa: menu ngon ngu dung lai duoc");
        }

        CHECK(!shortcuts::reservedReason(QKeySequence(Qt::ALT | Qt::Key_S)).isEmpty(), "Alt+S (menu Settings) duoc giu o ban tieng Anh");

        {
            QSettings run(dir + "/run.ini", QSettings::IniFormat);
            SettingsDialog dlg(w.control(), &w, &run, "C:/x/lumacast.exe");
            dlg.show(); pump(200);
            checkNoVietnamese(&dlg, "Cai dat van hanh");
            shot(dlg, "en-settings");
        }
        {
            std::unique_ptr<ScreenDialog> dlg(w.createScreenDialog());
            dlg->show(); pump(300);
            checkNoVietnamese(dlg.get(), "Man hinh va khung hinh");
            shot(*dlg, "en-screens");
        }
        {
            NewProjectDialog dlg(&w);
            dlg.show(); pump(200);
            checkNoVietnamese(&dlg, "Project moi");
        }
        {
            InputDialog dlg(w.midi(), {"Output 1"}, shortcuts::defaults(), &w);
            dlg.show(); pump(200);
            checkNoVietnamese(&dlg, "Phim tat va MIDI");
            shot(dlg, "en-input");
        }
        {
            NdiSourceDialog a(&w);
            a.show(); pump(500);
            checkNoVietnamese(&a, "Them nguon NDI");
            CameraSourceDialog b(&w);
            b.show(); pump(200);
            checkNoVietnamese(&b, "Them camera");
            NdiOutputDialog c({"K"}, &w);
            c.show(); pump(200);
            checkNoVietnamese(&c, "Them Output NDI");
        }
        w.showMapping();
        pump(400);
        if (auto* m = w.findChild<QWidget*>("mappingWindow")) {
            checkNoVietnamese(m, "Mapping");
            m->hide();
        } else CHECK(false, "mo duoc cua so Mapping");
        w.showMultiview();
        pump(400);
        checkNoVietnamese(&w, "sau khi mo Multiview");
        CHECK(!hasVietnamese(aboutHtml()), "hop Gioi thieu: tieng Anh");

        // thong bao cua dieu khien ngoai: giao dien/web tieng Anh, log tieng Viet
        ControlApi api(p, engine);
        const CmdResult r = api.setPreview(9, 1);
        CHECK(r.message.startsWith("No channel 9") && r.logMessage.startsWith("Không có kênh 9"), "loi API: message tieng Anh, log tieng Viet");
        const QString page = QString::fromUtf8(controlPageHtml());
        CHECK(page.contains("<html lang=\"en\">") && page.contains("LumaCast — Control") && !hasVietnamese(page) && !page.contains("%STRINGS%"),
              "trang dieu khien web: tieng Anh, khong sot cho dien chu");

        // project: file luu o ban tieng Anh mo duoc o ca hai ngon ngu, va hai lan mo cho ra noi dung giong het
        const QString file = dir + "/proj.json";
        QString err;
        CHECK(saveProject(p, file, &err), "luu project o ban tieng Anh");
        auto reopen = [&](Language l, const QString& out) {
            i18n::install(l);
            Project q;
            const bool ok = loadProject(q, file, &err) && q.channels.size() == 1 && saveProject(q, out, &err);
            return ok ? readFile(out) : QString();
        };
        const QString vi2 = reopen(Language::Vietnamese, dir + "/proj-vi.json");
        const QString en2 = reopen(Language::English, dir + "/proj-en.json");
        CHECK(!vi2.isEmpty() && vi2 == en2, "mo o tieng Viet va tieng Anh: noi dung giong het " + err.toStdString());
        w.close();
    }
    engine.stopEngine();
    i18n::install(Language::Vietnamese);
    const QString page = QString::fromUtf8(controlPageHtml());
    CHECK(page.contains("<html lang=\"vi\">") && page.contains("LumaCast — Điều khiển") && page.contains("\"black\":\"Đen\""),
          "trang dieu khien web mac dinh tieng Viet");
}

int main(int argc, char** argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(fmt);
    QTemporaryDir dir;
    QSettings::setDefaultFormat(QSettings::IniFormat);                    // khong lam ban cau hinh that cua may
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.filePath("cfg"));
    qputenv("LUMACAST_CONFIG_DIR", dir.filePath("config").toUtf8());
    QApplication app(argc, argv);
    applyDarkTheme(app);
    if (!appSettings().fileName().startsWith(dir.path())) { std::cerr << "FAIL: test phai dung cai dat rieng\n"; return 1; }

    testSources(QStringLiteral(LUMACAST_SOURCE_DIR));
    testChoice(dir.path());
    testEnglishUi(dir.path());
    std::cout << (g_fail ? "THAT BAI " : "TAT CA DAT ") << g_fail << " loi\n";
    return g_fail ? 1 : 0;
}
