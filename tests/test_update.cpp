// Tu cap nhat (backend gia, KHONG dung mang): khong tu tai / cai, khoa van hanh chan het, mat mang im lang, tat tu kiem tra,
// quay ve ban truoc, phat hien ban moi khong khoi dong noi, va duong cai dat tren cua so chinh (xac nhan -> luu -> dong).
#include "MainWindow.h"
#include "Theme.h"
#include "UpdateDialog.h"
#include "core/AppSettings.h"
#include "core/I18n.h"
#include "core/Project.h"
#include "core/Update.h"
#include "media/StaticSources.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest/QTest>
#include <atomic>
#include <iostream>

using namespace lc;
using update::Service;

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
template <class F> static bool waitFor(F cond, int ms = 4000) {
    QElapsedTimer t; t.start();
    while (t.elapsed() < ms) { pump(10); if (cond()) return true; }
    return cond();
}

struct Fake : update::Backend {
    // ket qua gia
    std::atomic<bool> installed{true}, offline{false}, downloadFails{false}, applyFails{false};
    QString latest = "0.2.0", previous;
    // da bi goi gi
    std::atomic<int> checks{0}, downloads{0}, applies{0}, rollbacks{0};
    QStringList applyArgs;
    std::atomic<int> delayMs{0};

    bool supported(QString* why) const override {
        if (!installed && why) *why = "khong phai ban cai";
        return installed;
    }
    QString currentVersion() const override { return "0.1.0"; }
    update::CheckResult check(update::Release& r, QString* err) override {
        ++checks;
        QThread::msleep(unsigned(delayMs.load()));
        if (offline) { if (err) *err = "mat mang"; return update::CheckResult::Failed; }
        if (latest == currentVersion()) return update::CheckResult::UpToDate;
        r.version = latest;
        r.delta = true;
        r.downloadBytes = 3 * 1048576;
        r.notes = "Ghi chu";
        return update::CheckResult::Available;
    }
    bool download(const std::function<void(int)>& progress, QString* err) override {
        ++downloads;
        progress(50);
        if (downloadFails) { if (err) *err = "dut mang"; return false; }
        progress(100);
        return true;
    }
    bool applyAndRestart(const QStringList& args, QString* err) override {
        ++applies;
        applyArgs = args;
        if (applyFails) { if (err) *err = "khong sao luu duoc"; return false; }
        return true;
    }
    QString previousVersion() const override { return previous; }
    QString dir;                                        // thu muc cai gia
    QString installDir() const override { return dir; }
    bool rollback(QString*) override { ++rollbacks; return true; }
};

// ---------------- 1. Service ----------------
static void testService() {
    QSettings st = appSettings();
    st.clear();
    auto fake = std::make_shared<Fake>();
    Service s(fake);
    bool locked = false;
    s.setLockProvider([&] { return locked; });
    CHECK(s.state() == Service::State::Idle && s.autoCheck(), "mac dinh: chua kiem tra, tu kiem tra BAT");

    s.setAutoCheck(false);
    s.checkAuto();
    pump(100);
    CHECK(fake->checks == 0, "tat tu kiem tra: khong kiem tra");
    s.setAutoCheck(true);
    locked = true;
    s.checkAuto();
    s.checkNow();
    pump(100);
    CHECK(fake->checks == 0, "dang khoa van hanh: khong kiem tra");
    locked = false;

    fake->offline = true;
    s.checkAuto();
    CHECK(waitFor([&] { return fake->checks == 1 && !s.busy(); }) && s.state() == Service::State::Idle && s.message().isEmpty() && !s.shouldNotify(),
          "mat mang khi tu kiem tra: im lang, khong thong bao");
    s.checkNow();
    CHECK(waitFor([&] { return s.state() == Service::State::Failed; }) && s.message().contains("mat mang"), "bam kiem tra khi mat mang: bao mot dong");
    fake->offline = false;

    s.checkAuto();
    CHECK(waitFor([&] { return s.state() == Service::State::Available; }) && s.release().version == "0.2.0" && s.shouldNotify(), "co ban moi: bao");
    pump(150);
    CHECK(fake->downloads == 0 && fake->applies == 0, "co ban moi: KHONG tu tai, KHONG tu cai");
    locked = true;
    CHECK(!s.shouldNotify(), "dang khoa: khong nhac");
    s.download();
    pump(100);
    CHECK(fake->downloads == 0 && s.state() == Service::State::Available, "dang khoa: khong tai");
    locked = false;

    fake->downloadFails = true;
    s.download();
    CHECK(waitFor([&] { return fake->downloads == 1 && !s.busy(); }) && s.state() == Service::State::Available && s.message().contains("dut mang"),
          "tai dut giua chung: van con ban moi de tai lai, co thong bao");
    fake->downloadFails = false;
    s.download();
    CHECK(waitFor([&] { return s.state() == Service::State::Ready; }) && s.progress() == 100, "tai xong: cho nguoi van hanh");
    pump(150);
    CHECK(fake->applies == 0, "tai xong: KHONG tu cai / khoi dong lai");

    QString err;
    locked = true;
    CHECK(!s.apply({}, &err) && fake->applies == 0, "dang khoa: khong cai");
    locked = false;
    fake->applyFails = true;
    CHECK(!s.apply({"--project", "a.json"}, &err) && err == "khong sao luu duoc" && !appSettings().contains(update::kKeyVerifying),
          "trinh cap nhat khong chay duoc: bao loi, khong danh dau ban moi");
    fake->applyFails = false;
    CHECK(s.apply({"--project", "a.json"}, &err) && fake->applies == 2 && fake->applyArgs == QStringList({"--project", "a.json"}) &&
              appSettings().value(update::kKeyVerifying).toString() == "0.2.0",
          "bam cap nhat: goi trinh cap nhat, nho ban moi de theo doi");

    // quay ve
    CHECK(!s.rollback(&err) && fake->rollbacks == 0, "chua co ban truoc: khong quay ve");
    fake->previous = "0.0.9";
    locked = true;
    CHECK(!s.rollback(&err) && fake->rollbacks == 0, "dang khoa: khong quay ve");
    locked = false;
    CHECK(s.rollback(&err) && fake->rollbacks == 1 && appSettings().value(update::kKeySkipVersion).toString() == "0.1.0",
          "quay ve: goi trinh cap nhat va nho bo qua ban vua bo");

    // ban bi quay ve: tu kiem tra khong nhac lai, bam kiem tra thi van thay
    QSettings st2 = appSettings();
    st2.setValue(update::kKeySkipVersion, "0.2.0");
    st2.sync();
    Service again(fake);
    again.checkAuto();
    CHECK(waitFor([&] { return !again.busy(); }) && again.state() == Service::State::Idle, "ban vua quay ve: tu kiem tra khong nhac lai");
    again.checkNow();
    CHECK(waitFor([&] { return again.state() == Service::State::Available; }), "bam kiem tra: van thay ban do");

    fake->latest = "0.1.0";
    Service current(fake);
    current.checkNow();
    CHECK(waitFor([&] { return current.state() == Service::State::UpToDate; }) && !current.shouldNotify(), "ban moi nhat: khong nhac");

    // huy Service khi dang kiem tra (thoat app): khong duoc crash
    fake->delayMs = 200;
    {
        Service dying(fake);
        dying.checkNow();
    }
    pump(400);
    CHECK(true, "thoat khi dang kiem tra: khong crash");

    auto zip = std::make_shared<Fake>();
    zip->installed = false;
    Service none(zip);
    none.checkAuto();
    none.checkNow();
    pump(100);
    CHECK(none.state() == Service::State::Unsupported && zip->checks == 0 && !none.shouldNotify(), "ban khong cai bang bo cai: khong ho tro, khong lam gi");

    // backend that: chay tu thu muc build thi khong ho tro, va khong dung mang
    auto real = update::createBackend("https://github.com/longkhanhtrantlk-svg/LumaCast");
    QString why;
    CHECK(!real->supported(&why) && !why.isEmpty() && real->previousVersion().isEmpty(), "ban chay tu thu muc build: tu cap nhat khong hoat dong");
}

// ---------------- 2. ban moi khong khoi dong noi ----------------
static void testGuard(const QString& dir) {
    QSettings st(dir + "/guard.ini", QSettings::IniFormat);
    CHECK(!update::guard::beginStartup(st, "0.2.0"), "khong vua cap nhat: khong de nghi gi");
    update::guard::markPending(st, "0.2.0");
    CHECK(!update::guard::beginStartup(st, "0.2.0"), "lan mo dau sau cap nhat: chua ket luan");
    CHECK(update::guard::beginStartup(st, "0.2.0"), "lan mo truoc khong xac nhan duoc (crash): de nghi quay ve");
    update::guard::confirm(st);
    CHECK(!update::guard::beginStartup(st, "0.2.0") && !st.contains(update::kKeyVerifying), "chay on roi: thoi theo doi");
    update::guard::markPending(st, "0.2.0");
    CHECK(!update::guard::beginStartup(st, "0.1.0") && !st.contains(update::kKeyVerifying), "cai khong thanh (van ban cu): bo danh dau");

    QSettings src(dir + "/source.ini", QSettings::IniFormat);
    qunsetenv("LUMACAST_UPDATE_SOURCE");
    CHECK(update::configuredSource(src).startsWith("https://github.com/"), "noi lay mac dinh: GitHub");
    src.setValue(update::kKeySource, "D:/ban-moi");
    CHECK(update::configuredSource(src) == "D:/ban-moi", "doi duoc trong cai dat");
    qputenv("LUMACAST_UPDATE_SOURCE", "E:/thu");
    CHECK(update::configuredSource(src) == "E:/thu", "bien moi truong uu tien (de thu)");
    qunsetenv("LUMACAST_UPDATE_SOURCE");

    QSettings lang(dir + "/lang.ini", QSettings::IniFormat);
    lang.setValue(i18n::kKeyInstallLanguage, "en");
    CHECK(i18n::configured(lang, dir) == Language::English, "ngon ngu bo cai ghi vao cai dat");
    i18n::save(lang, Language::Vietnamese);
    CHECK(i18n::configured(lang, dir) == Language::Vietnamese, "chon trong app van uu tien hon");
}

// ---------------- 3. cua so chinh ----------------
static Project makeProject(const QString& file) {
    Project p;
    SourceDef a; a.type = "color"; a.color = Qt::blue; a.label = "Xanh";
    auto sa = p.addSource(a);
    SourceDef b; b.type = "color"; b.color = Qt::red; b.label = "Do";
    auto sb = p.addSource(b);
    for (auto& [name, src] : {std::pair<const char*, std::shared_ptr<Source>>{"Xanh", sa}, {"Do", sb}}) {
        auto s = std::make_shared<Scene>();
        s->name = name; s->size = {320, 180};
        s->layers.push_back({src, {0, 0, 1, 1}, 1.0f, LayerFit::Stretch, true});
        p.scenes.push_back(s);
    }
    auto ch = std::make_shared<Channel>();
    ch->name = "K"; ch->size = {320, 180}; ch->scenes = p.scenes; ch->program = 0; ch->preview = 1;
    p.channels = {ch};
    OutputConfig o; o.name = "Output 1"; o.size = {160, 90};
    p.outputs = {o};
    p.filePath = file;
    return p;
}

static void testWindow(const QString& dir) {
    QSettings st = appSettings();
    st.clear();
    st.sync();
    const QString file = dir + "/show.json";
    Project p = makeProject(file);
    RenderEngine engine(p);
    engine.startEngine();
    {
        MainWindow w(p, engine, {});
        w.setAskOnClose(false);
        w.show();
        pump(700);
        CHECK(w.updates().state() == Service::State::Unsupported, "cua so moi tao: chua co backend (test va --benchmark khong cham mang)");
        auto fake = std::make_shared<Fake>();
        w.setUpdateBackend(fake);
        auto* button = w.findChild<QPushButton*>("statusUpdate");
        auto* action = w.findChild<QAction*>("updateAction");
        CHECK(button && action && !button->isVisible(), "co muc Help > Kiem tra cap nhat; chua co ban moi thi khong co nut");
        w.updates().checkAuto();
        CHECK(waitFor([&] { return button->isVisible(); }) && button->text().contains("0.2.0") && button->focusPolicy() == Qt::NoFocus,
              "co ban moi: nut o thanh trang thai, khong chiem ban phim");
        w.setLocked(true);
        CHECK(!button->isVisible() && !action->isEnabled(), "khoa van hanh: an thong bao, mo muc kiem tra");
        CHECK(!w.applyUpdate(false) && !w.applyUpdate(true), "khoa van hanh: khong cai, khong quay ve");
        w.setLocked(false);
        CHECK(button->isVisible() && action->isEnabled(), "mo khoa: thong bao hien lai");

        w.showUpdateDialog();
        pump(200);
        auto* dlg = w.findChild<UpdateDialog*>("updateDialog");
        CHECK(dlg && dlg->isVisible() && !dlg->isModal() && (dlg->windowFlags() & Qt::Tool) == Qt::Tool, "hop thoai cap nhat khong chan");
        if (!dlg) return;
        auto* download = dlg->findChild<QPushButton*>("updDownload");
        auto* apply = dlg->findChild<QPushButton*>("updApply");
        auto* rollback = dlg->findChild<QPushButton*>("updRollback");
        bool noFocus = true;
        for (auto* b : dlg->findChildren<QPushButton*>()) noFocus = noFocus && b->focusPolicy() == Qt::NoFocus && !b->autoDefault();
        CHECK(noFocus, "nut trong hop thoai khong nhan ban phim");
        CHECK(download->isVisible() && !apply->isVisible() && !rollback->isVisible(), "co ban moi: chi co nut Tai ve");

        // Enter / Space khi hop thoai dang duoc chon van la CUT / TAKE, khong dung toi cap nhat
        dlg->activateWindow();
        pump(200);
        const int program = p.channels[0]->program;
        QTest::keyClick(dlg, Qt::Key_Return);
        CHECK(waitFor([&] { return p.channels[0]->program != program; }), "Enter khi dang mo hop thoai cap nhat: van la CUT");
        CHECK(fake->downloads == 0 && fake->applies == 0, "phim khong kich hoat tai / cai");

        download->click();
        CHECK(waitFor([&] { return apply->isVisible(); }) && !download->isVisible(), "tai xong: hien nut Cap nhat va mo lai");

        int asked = 0;
        bool answer = false;
        QString question;
        w.setUpdateConfirm([&](const QString& q) { ++asked; question = q; return answer; });
        apply->click();
        pump(200);
        CHECK(asked == 1 && question.contains("0.2.0") && fake->applies == 0 && w.isVisible(), "tra loi Khong: khong co gi xay ra");

        // project khong luu duoc -> khong cap nhat
        answer = true;
        p.filePath = dir + "/khong-co-thu-muc/x.json";
        CHECK(!w.applyUpdate(false) && fake->applies == 0 && w.isVisible(), "luu project that bai: khong cap nhat");
        p.filePath = file;
        fake->applyFails = true;
        CHECK(!w.applyUpdate(false) && w.isVisible(), "trinh cap nhat loi: app van mo, bao loi");
        fake->applyFails = false;
        QFile::remove(file);
        CHECK(w.applyUpdate(false) && fake->applies == 2, "dong y: goi trinh cap nhat");
        CHECK(fake->applyArgs == QStringList({"--reopen", file}), "mo lai dung project dang dung");
        CHECK(QFile::exists(file), "project da luu truoc khi thoat");
        CHECK(waitFor([&] { return !w.isVisible(); }), "cua so dong de trinh cap nhat lam viec");
    }
    engine.stopEngine();
}

// ---------------- 4. khong de file cua nguoi dung trong thu muc cai ----------------
static void testInstallDir(const QString& dir) {
    const QString install = dir + "/LumaCast", current = install + "/current", outside = dir + "/tai-lieu";
    QDir().mkpath(current);
    QDir().mkpath(outside);
    CHECK(update::isInside(current + "/a.json", install) && update::isInside(install.toUpper() + "/current/x/../a.json", install) &&
              !update::isInside(dir + "/LumaCast-khac/a.json", install) && !update::isInside(outside + "/a.json", install) &&
              !update::isInside(current + "/a.json", QString()),
          "nhan ra duong dan nam trong thu muc cai");

    Project p = makeProject(QString());
    RenderEngine engine(p);
    engine.startEngine();
    {
        MainWindow w(p, engine, {});
        w.setAskOnClose(false);
        w.show();
        pump(600);
        auto fake = std::make_shared<Fake>();
        fake->dir = install;
        w.setUpdateBackend(fake);
        QStringList warnings, suggestions;
        QString answer;
        w.setWarningSink([&](const QString& s) { warnings << s; });
        w.setSavePathAsker([&](const QString&, const QString& s) { suggestions << s; return answer; });

        const QString documents = QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
        CHECK(!documents.isEmpty() && update::isInside(w.defaultSavePath("a.json"), documents), "hop luu mac dinh vao Documents");
        answer = current + "/project.lumacast.json";
        w.saveProject(false);
        pump(400);
        CHECK(suggestions.size() == 1 && update::isInside(suggestions.value(0), documents), "project moi: goi y trong Documents");
        CHECK(warnings.size() == 1 && !QFile::exists(answer) && p.filePath.isEmpty(), "chon thu muc cai: canh bao, khong luu");
        CHECK(!w.createProject(current + "/moi.json", p) && warnings.size() == 2 && !QFile::exists(current + "/moi.json"),
              "Project moi: cung khong tao trong thu muc cai");

        // project da nam san trong thu muc cai: Ctrl+S canh bao va cho chon cho khac
        p.filePath = current + "/cu.json";
        answer = outside + "/cu.json";
        w.saveProject(false);
        CHECK(waitFor([&] { return QFile::exists(outside + "/cu.json"); }) && p.filePath == answer && warnings.size() == 3 &&
                  !QFile::exists(current + "/cu.json") && update::isInside(suggestions.value(1), documents),
              "project trong thu muc cai: luu ra cho khac");

        // cap nhat: project trong thu muc cai ma nguoi dung huy chon cho khac -> khong cap nhat
        w.updates().checkNow();
        CHECK(waitFor([&] { return w.updates().state() == Service::State::Available; }), "co ban moi");
        w.updates().download();
        CHECK(waitFor([&] { return w.updates().state() == Service::State::Ready; }), "da tai");
        w.setUpdateConfirm([](const QString&) { return true; });
        p.filePath = current + "/cu.json";
        answer.clear();
        CHECK(!w.applyUpdate(false) && fake->applies == 0 && w.isVisible(), "project se bi xoa va khong chon cho khac: khong cap nhat");
        answer = outside + "/show.json";
        CHECK(w.applyUpdate(false) && QFile::exists(answer) && fake->applyArgs == QStringList({"--reopen", answer}),
              "chon cho an toan: luu roi cap nhat, mo lai tu cho do");
    }
    engine.stopEngine();
}

// ---------------- 5. mo lai sau cap nhat ma mat file: khong duoc thoat ----------------
static void testReopenMissing(const QString& dir) {
    QProcess app;
    app.start(QStringLiteral(LUMACAST_EXE), {"--reopen", dir + "/khong-con.json", "--no-audio"});
    CHECK(app.waitForStarted(5000), "chay lumacast --reopen");
    CHECK(!app.waitForFinished(4000), "file mat: app van mo (khong thoat im lang)");
    app.kill();
    app.waitForFinished(5000);
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
    qunsetenv("LUMACAST_UPDATE_SOURCE");
    QApplication app(argc, argv);
    app.setApplicationVersion("0.1.0");
    applyDarkTheme(app);
    if (!appSettings().fileName().startsWith(dir.path())) { std::cerr << "FAIL: test phai dung cai dat rieng\n"; return 1; }

    testService();
    testGuard(dir.path());
    testWindow(dir.path());
    testInstallDir(dir.path());
    testReopenMissing(dir.path());
    std::cout << (g_fail ? "THAT BAI " : "TAT CA DAT ") << g_fail << " loi\n";
    return g_fail ? 1 : 0;
}
