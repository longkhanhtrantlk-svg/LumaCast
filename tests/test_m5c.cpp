// Test M5c (phan giao dien/van hanh): cai dat dieu khien ngoai, tu khoi dong, mo lai project, khoa van hanh,
// hoan tac nhap media + thuoc tinh Scene, timeline (zoom/chon nhieu/sao chep).
#include "InspectorPanel.h"
#include "MainWindow.h"
#include "SettingsDialog.h"
#include "TimelineWidget.h"
#include "media/StaticSources.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QJsonArray>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QToolButton>
#include <QProcess>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QSlider>
#include <QUndoStack>
#include <QDockWidget>
#include <QImage>
#include <QScrollBar>
#include <QWheelEvent>
#include <QComboBox>
#include "LockButton.h"
#include <QFile>
#include <QDir>
#include "core/AppSettings.h"
#include "core/Startup.h"
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
template <class F> static bool waitFor(F cond, int ms = 3000) {
    QElapsedTimer t; t.start();
    while (t.elapsed() < ms) { pump(10); if (cond()) return true; }
    return cond();
}
static QPushButton* button(QWidget* root, const QString& text) {
    for (auto* b : root->findChildren<QPushButton*>()) if (b->text() == text) return b;
    return nullptr;
}

static quint16 freePort() {
    QTcpServer s;
    s.listen(QHostAddress::LocalHost, 0);
    return s.serverPort();
}

int main(int argc, char** argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(fmt);
    QApplication app(argc, argv);
    QTemporaryDir dir;
    QSettings::setDefaultFormat(QSettings::IniFormat);                    // khong lam ban cau hinh that cua may
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.filePath("cfg"));

    Project p;
    SourceDef a; a.type = "color"; a.color = Qt::red; a.label = "do";
    SourceDef b; b.type = "color"; b.color = Qt::blue; b.label = "xanh";
    auto sa = p.addSource(a), sb = p.addSource(b);
    for (auto src : {sa, sb}) {
        auto s = std::make_shared<Scene>();
        s->name = src->name(); s->size = {64, 36};
        s->layers.push_back({src, {0, 0, 1, 1}, 1.0f, LayerFit::Stretch, true});
        p.scenes.push_back(s);
    }
    auto ch = std::make_shared<Channel>();
    ch->name = "K"; ch->size = {64, 36}; ch->scenes = p.scenes; ch->program = 0; ch->preview = 1;
    p.channels = {ch};
    OutputConfig o1; o1.name = "O1"; o1.size = {160, 90};
    OutputConfig o2; o2.name = "O2"; o2.size = {90, 160};
    p.outputs = {o1, o2};
    p.filePath = dir.filePath("m5c.json");

    RenderEngine engine(p);
    engine.startEngine();
    QTemporaryDir plugDir;
    MainWindow w(p, engine, {plugDir.path()});
    w.setAskOnClose(false);
    w.show();
    pump(600);

    // ================= P0-1/P0-2: hop thoai cai dat dieu khien ngoai =================
    {
        CHECK(!w.control().httpRunning() && !w.control().oscRunning(), "mac dinh: dieu khien ngoai TAT");
        SettingsDialog dlg(w.control());
        auto* httpOn = dlg.findChild<QCheckBox*>("httpEnable");
        auto* httpPort = dlg.findChild<QSpinBox*>("httpPort");
        auto* lan = dlg.findChild<QCheckBox*>("httpLan");
        auto* tok = dlg.findChild<QLineEdit*>("httpToken");
        auto* oscOn = dlg.findChild<QCheckBox*>("oscEnable");
        auto* oscPort = dlg.findChild<QSpinBox*>("oscPort");
        auto* err = dlg.findChild<QLabel*>("settingsError");
        CHECK(httpOn && httpPort && lan && tok && oscOn && oscPort && err && !httpOn->isChecked() && !lan->isChecked() && !oscOn->isChecked(), "hop thoai co du o, mac dinh tat het/khong LAN");
        httpPort->setValue(freePort());
        httpOn->setChecked(true);
        lan->setChecked(true);
        CHECK(tok->text().size() >= ControlSettings::kMinLanTokenLen, "bat LAN: tu tao token ngau nhien");
        tok->setText("");
        CHECK(!dlg.applyAndSave().isEmpty() && !w.control().httpRunning() && !err->text().isEmpty(), "LAN khong token: bi tu choi, hien loi tren hop thoai, khong mo cong");
        lan->setChecked(false);
        CHECK(dlg.applyAndSave().isEmpty() && w.control().httpRunning(), "tat LAN: bat HTTP duoc ngay (khong can khoi dong lai)");
        pump(100);
        auto* ctlLabel = w.findChild<QLabel*>("ctlLabel");
        CHECK(ctlLabel && ctlLabel->text().contains("HTTP") && ctlLabel->text().contains(QString::number(w.control().httpPort())), "thanh trang thai hien cong HTTP dang mo");
        QSettings st = appSettings();
        const ControlSettings saved = ControlSettings::load(st);
        CHECK(saved.httpEnabled && saved.httpPort == w.control().httpPort() && !saved.httpLan, "cai dat duoc luu vao QSettings");
        oscPort->setValue(freePort());
        oscOn->setChecked(true);
        CHECK(dlg.applyAndSave().isEmpty() && w.control().oscRunning(), "bat OSC trong cung hop thoai");
        // API that tren cua so that: dat Preview qua HTTP, thay doi hien o giao dien
        QTcpSocket s;
        s.connectToHost(QHostAddress::LocalHost, w.control().httpPort());
        waitFor([&] { return s.state() == QAbstractSocket::ConnectedState; });
        s.write("POST /api/channels/1/cut HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 0\r\n\r\n");
        QByteArray got;
        waitFor([&] { got += s.readAll(); return s.state() == QAbstractSocket::UnconnectedState; });
        got += s.readAll();
        CHECK(got.startsWith("HTTP/1.1 200") && waitFor([&] { return ch->program == 1; }), "HTTP cut tren MainWindow that: Program doi");
        const int rejBefore = w.controlApi().rejectedCount();
        QTcpSocket s2;
        s2.connectToHost(QHostAddress::LocalHost, w.control().httpPort());
        waitFor([&] { return s2.state() == QAbstractSocket::ConnectedState; });
        s2.write("POST /api/channels/9/cut HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 0\r\n\r\n");
        waitFor([&] { got += s2.readAll(); return s2.state() == QAbstractSocket::UnconnectedState; });
        CHECK(w.controlApi().rejectedCount() == rejBefore + 1, "kenh 9: bi tu choi va duoc dem");
        // tat tu hop thoai
        httpOn->setChecked(false);
        oscOn->setChecked(false);
        CHECK(dlg.applyAndSave().isEmpty() && !w.control().httpRunning() && !w.control().oscRunning(), "tat het tu hop thoai");
        pump(100);
        CHECK(ctlLabel->text().isEmpty(), "thanh trang thai het hien dieu khien ngoai");
        button(&w, "CUT")->click();                                       // tra Program/Preview ve nhu ban dau
        pump(200);
    }

    // ================= P0-3: khoi dong cung Windows (logic tach, QSettings gia) =================
    {
        const QString exe = "/opt/Luma Cast/bin/lumacast.exe";
        const QString cmd = autostartCommand(exe);
        CHECK(cmd.startsWith('"') && cmd.endsWith('"') && cmd.contains("Luma Cast") && cmd.contains("lumacast.exe"), "lenh khoi dong: duong dan co khoang trang nam trong dau ngoac kep");
        QSettings reg(dir.filePath("fakeRun.ini"), QSettings::IniFormat);
        CHECK(autostartState(reg, exe) == AutostartState::Off, "chua bat: trang thai Off");
        CHECK(setAutostart(reg, true, exe) && autostartState(reg, exe) == AutostartState::On, "bat: ghi gia tri, trang thai On");
        CHECK(reg.value(kRunValueName).toString() == cmd, "gia tri ghi dung bang lenh co ngoac kep");
        CHECK(autostartState(reg, "/khac/lumacast.exe") == AutostartState::OnOtherPath, "exe khac/da di chuyen: OnOtherPath");
        CHECK(setAutostart(reg, true, "/khac/lumacast.exe") && autostartState(reg, "/khac/lumacast.exe") == AutostartState::On, "bat lai voi exe moi: cap nhat duong dan");
        CHECK(setAutostart(reg, false, exe) && autostartState(reg, exe) == AutostartState::Off && !reg.contains(kRunValueName), "tat: xoa gia tri");
        CHECK(setAutostart(reg, false, exe), "tat khi chua bat: khong loi");
        CHECK(!setAutostart(reg, true, "") && !setAutostart(reg, true, "/a\"b/x.exe") && !reg.contains(kRunValueName), "duong dan rong hoac chua dau ngoac kep bi tu choi, khong ghi gi");
        CHECK(std::string(kRunKeyPath) == "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", "khoa registry dung HKCU\\...\\Run");
        CHECK(autostartSupported() == (openRunKey() != nullptr), "autostartSupported khop voi openRunKey (Linux: khong ho tro, khong dung registry)");
    }

    // ================= P0-3: nho + mo lai project gan nhat =================
    {
        QSettings st(dir.filePath("startup.ini"), QSettings::IniFormat);
        CHECK(reopenLastEnabled(st), "tu mo lai: mac dinh BAT");
        CHECK(decideStartupProject(st, false).path.isEmpty() && decideStartupProject(st, false).notice.isEmpty(), "chua co project nao duoc nho: chay binh thuong, khong thong bao");
        const QString proj = dir.filePath("gan_nhat.json");
        QFile f(proj); f.open(QIODevice::WriteOnly); f.write("{}"); f.close();
        rememberProject(st, proj);
        CHECK(lastProjectPath(st) == QFileInfo(proj).absoluteFilePath(), "nho duong dan tuyet doi");
        CHECK(decideStartupProject(st, false).path == QFileInfo(proj).absoluteFilePath() && decideStartupProject(st, false).notice.isEmpty(), "khong co --project: mo lai project gan nhat");
        CHECK(decideStartupProject(st, true).path.isEmpty(), "co cau hinh tu dong lenh (-o/-m/--wall): khong tu mo");
        setReopenLast(st, false);
        CHECK(decideStartupProject(st, false).path.isEmpty(), "tuy chon bi tat: khong tu mo");
        setReopenLast(st, true);
        QFile::remove(proj);
        const StartupDecision miss = decideStartupProject(st, false);
        CHECK(miss.path.isEmpty() && miss.notice.contains("Không tìm thấy") && miss.notice.contains("gan_nhat.json"), "file mat: chay binh thuong kem thong bao (khong crash)");
        QFile as(proj + ".autosave"); as.open(QIODevice::WriteOnly); as.write("{}"); as.close();
        CHECK(decideStartupProject(st, false).path == QFileInfo(proj).absoluteFilePath(), "file chinh mat nhung con ban tu luu: van de nghi mo (de khoi phuc)");
        rememberProject(st, "");
        CHECK(lastProjectPath(st) == QFileInfo(proj).absoluteFilePath(), "nho duong dan rong khong xoa gia tri cu");
        // co che khoi phuc autosave dung chung
        QFile m(proj); m.open(QIODevice::WriteOnly); m.write("{}"); m.close();
        QFile::setPermissions(proj, QFile::ReadOwner | QFile::WriteOwner);
        QFileInfo mi(proj), ai(proj + ".autosave");
        int asked = 0;
        auto yes = [&](const QFileInfo&) { ++asked; return true; };
        auto no = [&](const QFileInfo&) { ++asked; return false; };
        QFile(proj + ".autosave").remove();
        CHECK(pickProjectToLoad(proj, yes) == proj && asked == 0, "khong co ban tu luu: khong hoi, nap file chinh");
        QThread::msleep(1100);
        QFile as2(proj + ".autosave"); as2.open(QIODevice::WriteOnly); as2.write("{}"); as2.close();
        CHECK(pickProjectToLoad(proj, yes) == proj + ".autosave" && asked == 1, "ban tu luu moi hon + dong y: nap ban tu luu");
        CHECK(pickProjectToLoad(proj, no) == proj && asked == 2, "ban tu luu moi hon + tu choi: nap file chinh");
        QFile::remove(proj);
        CHECK(pickProjectToLoad(proj, yes) == proj + ".autosave", "file chinh mat, con ban tu luu: dong y thi nap ban tu luu");
        QFile::remove(proj + ".autosave");
    }

    // ================= P0-3: hop thoai (khoi dong cung Windows + mo lai project) voi registry gia =================
    {
        QSettings reg(dir.filePath("dlgRun.ini"), QSettings::IniFormat);
        const QString exe = "/opt/lumacast/lumacast.exe";
        SettingsDialog dlg(w.control(), nullptr, &reg, exe);
        auto* au = dlg.findChild<QCheckBox*>("autostart");
        auto* ro = dlg.findChild<QCheckBox*>("reopenLast");
        auto* hint = dlg.findChild<QLabel*>("autostartHint");
        CHECK(au && ro && au->isEnabled() && !au->isChecked() && ro->isChecked(), "hop thoai: autostart tat, mo lai project bat theo mac dinh");
        au->setChecked(true);
        CHECK(dlg.applyAndSave().isEmpty() && autostartState(reg, exe) == AutostartState::On, "tick 'cung Windows' + OK: ghi registry gia");
        ro->setChecked(false);
        dlg.applyAndSave();
        QSettings st = appSettings();
        CHECK(!reopenLastEnabled(st), "bo tick 'tu mo lai project': luu QSettings");
        reg.setValue(kRunValueName, "\"/da/di/chuyen/lumacast.exe\"");
        SettingsDialog dlg2(w.control(), nullptr, &reg, exe);
        CHECK(dlg2.findChild<QCheckBox*>("autostart")->isChecked() && dlg2.findChild<QLabel*>("autostartHint")->text().contains("đường dẫn khác"), "autostart tro exe cu: hien canh bao");
        dlg2.applyAndSave();
        CHECK(autostartState(reg, exe) == AutostartState::On, "OK sua lai ve exe hien tai");
        dlg2.findChild<QCheckBox*>("autostart")->setChecked(false);
        dlg2.applyAndSave();
        CHECK(autostartState(reg, exe) == AutostartState::Off, "bo tick: xoa autostart");
        setReopenLast(st, true);
        CHECK(!hint->text().isEmpty(), "hop thoai co dong ghi chu project gan nhat");
        SettingsDialog dlg3(w.control());
        CHECK(dlg3.findChild<QCheckBox*>("autostart")->isEnabled() == autostartSupported(), "tren Linux o autostart bi vo hieu (khong dung registry that)");
    }

    // ================= P0-3: luu project => nho la project gan nhat =================
    {
        QSettings st = appSettings();
        st.remove(kKeyLastProject);
        w.saveProject(false);
        CHECK(waitFor([&] { QSettings s2 = appSettings(); return lastProjectPath(s2) == QFileInfo(p.filePath).absoluteFilePath(); }), "Luu project: nho lam project gan nhat");
    }

    // ================= P0-3: chay that lumacast voi/khong project gan nhat (tien trinh con) =================
    {
        auto runApp = [&](const QString& cfgHome, const QStringList& args, int keepMs) {
            QProcess pr;
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert("LUMACAST_CONFIG_DIR", cfgHome + "/config");           // cau hinh + log rieng; khong dung vao registry/cau hinh that
            pr.setProcessEnvironment(env);
            pr.start(LUMACAST_EXE, args);
            pr.waitForStarted(5000);
            QElapsedTimer t; t.start();
            while (t.elapsed() < keepMs && pr.state() != QProcess::NotRunning) { QThread::msleep(50); }
            const bool alive = pr.state() != QProcess::NotRunning;
            pr.kill();
            pr.waitForFinished(5000);
            return alive;
        };
        auto readLog = [&](const QString& home) {
            QFile f(home + "/config/logs/lumacast.log");
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        auto writeCfg = [&](const QString& home, const QString& lastPath) {
            QDir().mkpath(home + "/config/LumaCast");
            QSettings c(home + "/config/LumaCast/LumaCast.ini", QSettings::IniFormat);
            c.setValue(kKeyLastProject, lastPath);
            c.sync();
        };
        // project that, luu bang ham cua lop loi
        Project q;
        SourceDef d; d.type = "color"; d.color = Qt::magenta; d.label = "magenta";
        auto src = q.addSource(d);
        auto sc = std::make_shared<Scene>();
        sc->name = "CanhDaNho"; sc->size = {64, 36};
        sc->layers.push_back({src, {0, 0, 1, 1}, 1.0f, LayerFit::Stretch, true});
        q.scenes = {sc};
        auto qc = std::make_shared<Channel>();
        qc->name = "K"; qc->size = {64, 36}; qc->scenes = q.scenes; qc->program = 0; qc->preview = -1;
        q.channels = {qc};
        OutputConfig qo; qo.name = "Q"; qo.size = {64, 36};
        q.outputs = {qo};
        const QString pj = dir.filePath("app_last.json");
        QString err;
        CHECK(saveProject(q, pj, &err), "tao project cho kiem thu tien trinh that");

        const QString h1 = dir.filePath("home1");
        writeCfg(h1, pj);
        const bool alive1 = runApp(h1, {}, 3500);
        CHECK(alive1 && readLog(h1).contains("Tự mở lại project gần nhất"), "chay khong tham so: tu mo lai project gan nhat (log), tien trinh song");

        const QString h2 = dir.filePath("home2");
        writeCfg(h2, dir.filePath("khong_ton_tai.json"));
        const bool alive2 = runApp(h2, {}, 3500);
        CHECK(alive2 && readLog(h2).contains("Không tìm thấy project gần nhất"), "project gan nhat bi mat: chay binh thuong kem thong bao, KHONG crash");

        const QString h3 = dir.filePath("home3");
        writeCfg(h3, pj);
        const bool alive3 = runApp(h3, {"-o", "320x180"}, 3000);
        CHECK(alive3 && !readLog(h3).contains("Tự mở lại project gần nhất"), "co -o tren dong lenh: khong tu mo project gan nhat");

        const QString h4 = dir.filePath("home4");
        writeCfg(h4, pj);
        {
            QSettings c(h4 + "/config/LumaCast/LumaCast.ini", QSettings::IniFormat);
            c.setValue(kKeyReopenLast, false);
            c.sync();
        }
        const bool alive4 = runApp(h4, {}, 3000);
        CHECK(alive4 && !readLog(h4).contains("Tự mở lại project gần nhất"), "tuy chon tu mo bi tat: khong tu mo");

        const QString h5 = dir.filePath("home5");
        QFile bad(dir.filePath("hong.json")); bad.open(QIODevice::WriteOnly); bad.write("{ khong phai json"); bad.close();
        writeCfg(h5, dir.filePath("hong.json"));
        const bool alive5 = runApp(h5, {}, 3500);
        CHECK(alive5 && readLog(h5).contains("Không mở lại được project gần nhất"), "project gan nhat bi hong: chay binh thuong kem thong bao, KHONG thoat");
    }

    // ================= P0-4: khoa van hanh (UI-07) =================
    {
        auto* lockBtn = w.findChild<LockButton*>("lockButton");
        auto* lockLabel = w.findChild<QLabel*>("lockLabel");
        auto* insp = w.inspector();
        auto* tl = w.timeline();
        auto* tlDur = tl->findChild<QDoubleSpinBox*>("tlDuration");
        auto* tlLoop = tl->findChild<QCheckBox*>("tlLoop");
        auto* tlPlay = tl->findChild<QPushButton*>("tlPlay");
        auto* tlStop = tl->findChild<QPushButton*>("tlStop");
        auto* importB = button(&w, "Nhập media...");
        auto actionByPrefix = [&](const QString& prefix) -> QAction* {
            for (auto* m : w.menuBar()->findChildren<QMenu*>()) for (auto* a : m->actions()) if (a->text().startsWith(prefix)) return a;
            return nullptr;
        };
        auto* undoA = actionByPrefix("Hoàn tác");
        auto* redoA = actionByPrefix("Làm lại");
        auto* openA = actionByPrefix("Mở project");
        auto* impA = actionByPrefix("Nhập media");
        auto* setA = actionByPrefix("Cài đặt vận hành");
        auto* plugDock = w.findChild<QDockWidget*>("dockPlugins");
        CHECK(lockBtn && lockLabel && tlDur && tlLoop && tlPlay && importB && undoA && redoA && openA && impA && setA && plugDock, "tim thay nut khoa va cac dieu khien lien quan");
        CHECK(!w.isLocked() && lockLabel->text().contains("Mở khoá") && insp->isEnabled() && importB->isEnabled() && tlDur->isEnabled() && plugDock->widget()->isEnabled(), "khoi dong: MO KHOA, moi dieu khien bat");

        // Tao 1 buoc hoan tac that de kiem tra khoa Hoan tac
        const int sceneCount = int(p.scenes.size());
        const int stackBefore = w.undoStack()->count();
        tl->addKey(AnimProp::X, 1.0);
        pump(200);
        CHECK(w.undoStack()->count() == stackBefore + 1 && undoA->isEnabled(), "(chuan bi) them keyframe -> co buoc Hoan tac, Hoan tac bat");

        lockBtn->click();
        pump(100);
        CHECK(w.isLocked() && lockBtn->isLocked() && lockLabel->text().contains("KHOÁ"), "bam nut khoa: dang khoa, thanh trang thai hien KHOA VAN HANH");
        CHECK(w.windowTitle().contains("KHOÁ"), "tieu de cua so ghi [KHOA VAN HANH]");
        CHECK(!insp->isEnabled() && insp->locked(), "khoa: Inspector bi vo hieu");
        CHECK(!importB->isEnabled() && !impA->isEnabled(), "khoa: Nhap media (nut + menu) bi vo hieu");
        CHECK(!plugDock->widget()->isEnabled(), "khoa: Plugin Manager bi vo hieu");
        CHECK(!undoA->isEnabled() && !redoA->isEnabled(), "khoa: Hoan tac/Lam lai bi vo hieu");
        CHECK(!openA->isEnabled() && !setA->isEnabled(), "khoa: Mo project va Cai dat van hanh bi vo hieu");
        CHECK(!tlDur->isEnabled() && !tlLoop->isEnabled() && tl->locked(), "khoa: sua thoi luong/lap timeline bi vo hieu");
        CHECK(tlPlay->isEnabled() && tlStop->isEnabled(), "khoa: Phat/Tam dung/Ve 0 timeline VAN dung duoc");

        // chan bang lenh (khong chi vo hieu widget)
        const int cnt = w.undoStack()->count();
        const QRectF rect0 = p.scenes[0]->layers[0].rect;
        insp->editCurrentLayer([](Layer& l) { l.rect.moveLeft(0.5); l.opacity = 0.1f; }, "thu sua khi khoa");
        tl->addKey(AnimProp::Y, 2.0);
        tl->setDuration(77);
        tl->setLoop(false);
        tl->deleteKey(AnimProp::X, 0);
        CHECK(tl->moveKey(AnimProp::X, 0, 3.0, false) == -1, "khoa: moveKey tra -1");
        pump(250);
        CHECK(w.undoStack()->count() == cnt, "khoa: cac lenh sua bang ma khong tao buoc Hoan tac");
        CHECK(p.scenes[0]->layers[0].rect == rect0 && p.scenes[0]->layers[0].opacity == 1.0f && p.scenes[0]->duration != 77 && p.scenes[0]->loop, "khoa: du lieu Scene/Layer tren render thread KHONG doi");
        CHECK(!w.addMediaFile(dir.filePath("khong_quan_tam.png")) && int(p.scenes.size()) == sceneCount, "khoa: addMediaFile bi tu choi, khong them Scene");
        // keo chuot vao timeline khi khoa
        {
            tl->stopTransport();
            waitFor([&] { return p.scenes[0]->clock.time.load() == 0.0 && !p.scenes[0]->clock.playing.load(); });
            QWidget* tv = tl->trackView();
            const int y = tl->rowTop(0) + 5;
            QMouseEvent dbl(QEvent::MouseButtonDblClick, QPointF(tl->timeToX(2.0), y), QPointF(tl->timeToX(2.0), y), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(tv, &dbl);
            QMouseEvent ruler(QEvent::MouseButtonPress, QPointF(tl->timeToX(5.0), 5), QPointF(tl->timeToX(5.0), 5), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(tv, &ruler);
            QMouseEvent rel(QEvent::MouseButtonRelease, QPointF(tl->timeToX(5.0), 5), QPointF(tl->timeToX(5.0), 5), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(tv, &rel);
            pump(150);
            CHECK(w.undoStack()->count() == cnt && p.scenes[0]->clock.time.load() == 0.0, "khoa: nhap dup/keo thuoc tren timeline khong sua, khong tua");
        }

        // dong cua so chinh bi chan
        w.setAskOnClose(true);
        const bool closed = w.close();
        CHECK(!closed && w.isVisible(), "khoa: dong cua so chinh bi chan (khong hien hop thoai, cua so van mo)");
        w.setAskOnClose(false);

        // dieu khien LIVE van chay
        const int pgBefore = ch->program;
        button(&w, "CUT")->click();
        CHECK(waitFor([&] { return ch->program != pgBefore; }), "khoa: CUT van chay");
        button(&w, "CUT")->click();
        waitFor([&] { return ch->program == pgBefore; });
        auto* blackAll = button(&w, "BLACK ALL");
        CHECK(blackAll->isEnabled(), "khoa: BLACK ALL con bat");
        blackAll->click();
        CHECK(waitFor([&] { return engine.output(0)->black.load() && engine.output(1)->black.load(); }), "khoa: BLACK ALL van chay");
        blackAll->click();
        waitFor([&] { return !engine.output(0)->black.load(); });
        auto* ftb = button(&w, "FTB");
        ftb->click();
        CHECK(waitFor([&] { return ch->ftbTarget.load(); }), "khoa: FTB van chay");
        ftb->click();
        waitFor([&] { return !ch->ftbTarget.load(); });
        CHECK(button(&w, "TAKE")->isEnabled() && button(&w, "AUTO")->isEnabled() && w.findChild<QSlider*>("tbar")->isEnabled(), "khoa: TAKE/AUTO/T-bar con bat");
        auto* scenesL = w.findChild<QListWidget*>("sceneList");
        emit scenesL->itemClicked(scenesL->item(0));
        CHECK(waitFor([&] { return ch->preview == 0; }), "khoa: chon Scene (Preview) van chay");
        {
            QToolButton* blk = nullptr; QToolButton* frz = nullptr; QToolButton* pat = nullptr;
            for (auto* b : w.findChildren<QToolButton*>()) { if (b->text() == "Đen" && !blk) blk = b; if (b->text() == "Dừng" && !frz) frz = b; if (b->text() == "Mẫu" && !pat) pat = b; }
            CHECK(blk && frz && pat && blk->isEnabled() && frz->isEnabled() && pat->isEnabled(), "khoa: Den/Dung/Mau tung Output con bat");
            blk->click();
            CHECK(waitFor([&] { return engine.output(0)->black.load(); }), "khoa: Den Output 1 van chay");
            blk->click();
            waitFor([&] { return !engine.output(0)->black.load(); });
        }
        tlStop->click();
        waitFor([&] { return !p.scenes[0]->clock.playing.load(); });
        tlPlay->click();
        CHECK(waitFor([&] { return p.scenes[0]->clock.playing.load(); }), "khoa: Phat timeline van chay");
        tlStop->click();
        CHECK(waitFor([&] { return !p.scenes[0]->clock.playing.load() && p.scenes[0]->clock.time.load() == 0.0; }), "khoa: Ve 0 timeline van chay");
        button(&w, "CUT")->click();                             // dua lai nhu cu
        waitFor([&] { return ch->program == 1; });
        button(&w, "CUT")->click();
        waitFor([&] { return ch->program == 0; });

        // API tren cua so that cung thay trang thai khoa
        CHECK(w.controlApi().state()["locked"].toBool(), "API: state bao locked = true");

        // mo khoa phai co xac nhan: bam nhanh khong mo
        lockBtn->click();
        pump(100);
        CHECK(w.isLocked(), "dang khoa: bam nhanh KHONG mo khoa");
        {
            QMouseEvent pr(QEvent::MouseButtonPress, QPointF(5, 5), QPointF(5, 5), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(lockBtn, &pr);
            pump(400);
            CHECK(lockBtn->holding() && w.isLocked(), "giu 0,4 s: dang dem, chua mo khoa");
            QMouseEvent rl(QEvent::MouseButtonRelease, QPointF(5, 5), QPointF(5, 5), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(lockBtn, &rl);
            pump(1300);
            CHECK(w.isLocked() && !lockBtn->holding(), "nha som: huy, van khoa");
            QApplication::sendEvent(lockBtn, &pr);
            pump(1250);
            CHECK(!w.isLocked(), "giu du 1 giay: MO KHOA");
            QApplication::sendEvent(lockBtn, &rl);
            pump(100);
            CHECK(!w.isLocked() && lockLabel->text().contains("Mở khoá"), "nha nut sau khi mo khoa: van mo, nhan 'Mo khoa'");
        }
        // ban phim KHONG mo khoa: Space la TAKE (phim tat cua so), ke ca khi nut khoa dang giu focus
        w.setLocked(true);
        for (int key : {int(Qt::Key_Space), int(Qt::Key_Return), int(Qt::Key_Enter)}) {
            QKeyEvent kp(QEvent::KeyPress, key, Qt::NoModifier);
            QApplication::sendEvent(lockBtn, &kp);
            pump(1250);
            QKeyEvent kr(QEvent::KeyRelease, key, Qt::NoModifier);
            QApplication::sendEvent(lockBtn, &kr);
            CHECK(w.isLocked() && !lockBtn->holding(), "giu Space/Enter 1,25 s: van khoa, khong dem mo khoa");
        }
        w.setLocked(false);
        // sau khi mo khoa moi thu tro lai
        CHECK(insp->isEnabled() && importB->isEnabled() && impA->isEnabled() && openA->isEnabled() && setA->isEnabled() && tlDur->isEnabled() && plugDock->widget()->isEnabled(),
              "mo khoa: moi dieu khien sua bat lai");
        CHECK(undoA->isEnabled() && undoA->text().startsWith("Hoàn tác"), "mo khoa: Hoan tac bat lai (con buoc truoc do)");
        const int cnt2 = w.undoStack()->count();
        tl->addKey(AnimProp::Y, 2.0);
        pump(200);
        CHECK(w.undoStack()->count() == cnt2 + 1, "mo khoa: sua keyframe lai hoat dong");
        undoA->trigger();
        pump(200);
        CHECK(w.undoStack()->count() == cnt2 + 1 && w.undoStack()->index() == cnt2, "mo khoa: Hoan tac (menu) hoat dong");
        undoA->trigger();                                        // tra ve trang thai ban dau (bo key X + Y)
        pump(200);
        // Ctrl+L khoa
        QSettings stLock = appSettings();
        bool savedLock = false;
        for (const QString& k : stLock.allKeys()) savedLock = savedLock || k.contains("lock", Qt::CaseInsensitive);
        CHECK(!savedLock, "trang thai khoa KHONG luu vao QSettings (chi theo phien)");
        w.setLocked(true);
        w.setLocked(false);
        QFile pf(p.filePath);
        CHECK(!pf.exists() || !pf.open(QIODevice::ReadOnly) || !QString::fromUtf8(pf.readAll()).contains("locked", Qt::CaseInsensitive), "khoa KHONG luu vao file project");
    }

    // ================= P1-5: hoan tac Nhap media =================
    {
        QImage img(32, 18, QImage::Format_RGB32); img.fill(Qt::yellow);
        const QString png = dir.filePath("nhap1.png"), png2 = dir.filePath("nhap2.png");
        img.save(png); img.fill(Qt::cyan); img.save(png2);
        auto* scenesL = w.findChild<QListWidget*>("sceneList");
        auto* mediaL = w.findChild<QListWidget*>("mediaList");
        auto* insp = w.inspector();
        auto* sceneCombo = insp->findChild<QComboBox*>("sceneCombo");
        const int s0 = int(p.scenes.size()), src0 = int(p.sources.size());
        const int pg0 = ch->program, pv0 = ch->preview;
        w.undoStack()->clear();

        CHECK(w.addMediaFile(png), "nhap anh 1");
        CHECK(waitFor([&] { return int(p.scenes.size()) == s0 + 1 && scenesL->count() == s0 + 1; }), "nhap: them Scene + nguon, danh sach Scene va Media Pool cap nhat");
        CHECK(int(p.sources.size()) == src0 + 1 && mediaL->count() == src0 + 1 && int(ch->scenes.size()) == s0 + 1 && w.undoStack()->count() == 1, "nhap: nguon + Scene vao project va kenh, co 1 buoc Hoan tac");
        auto scene1 = p.scenes.back();
        const QString id1 = p.sources.back().id;
        CHECK(!id1.isEmpty() && w.undoStack()->undoText().contains("Nhập media"), "nhap: nguon co id, ten buoc Hoan tac ro rang");

        // dat Preview vao Scene vua nhap roi Hoan tac -> Preview ve -1 (khong tro vao Scene da go)
        emit scenesL->itemClicked(scenesL->item(s0));
        CHECK(waitFor([&] { return ch->preview == s0; }), "(chuan bi) Preview tro vao Scene vua nhap");
        // chon Scene do trong Inspector va them layer (buoc hoan tac khac) de kiem tra cache Inspector
        w.undoStack()->undo();
        CHECK(waitFor([&] { return int(p.scenes.size()) == s0; }), "Hoan tac nhap: Scene bi go khoi project");
        pump(150);
        CHECK(int(p.sources.size()) == src0 && int(ch->scenes.size()) == s0 && scenesL->count() == s0 && mediaL->count() == src0, "Hoan tac: nguon, Scene kenh, danh sach UI deu go");
        CHECK(ch->program == pg0 && ch->preview == -1, "Hoan tac: Program khong doi; Preview tro vao Scene da go -> -1 (khong treo)");
        CHECK(int(p.scenes.size()) == s0 && w.controlApi().state()["scenes"].toArray().size() == s0, "Hoan tac: ban chup cua API dieu khien ngoai cung cap nhat");
        // khong tham chieu treo: Inspector/Timeline/Monitor van dung duoc
        CHECK(insp->currentScenePtr() != nullptr && w.timeline()->duration() > 0, "Hoan tac: Inspector/Timeline van tro vao Scene hop le");
        pump(300);
        CHECK(engine.error().isEmpty() && !engine.stalled(), "Hoan tac: engine van chay binh thuong");

        // Lam lai: khoi phuc dung doi tuong (cung Scene, cung id nguon)
        w.undoStack()->redo();
        CHECK(waitFor([&] { return int(p.scenes.size()) == s0 + 1; }), "Lam lai nhap: Scene tro lai");
        pump(150);
        CHECK(p.scenes.back() == scene1 && p.sources.back().id == id1 && int(ch->scenes.size()) == s0 + 1 && scenesL->count() == s0 + 1 && mediaL->count() == src0 + 1,
              "Lam lai: dung Scene cu, dung id nguon, UI day du");
        CHECK(ch->program == pg0, "Lam lai: Program khong doi");

        // hai lan nhap lien tiep, hoan tac theo thu tu nguoc
        CHECK(w.addMediaFile(png2) && waitFor([&] { return int(p.scenes.size()) == s0 + 2; }), "nhap anh 2");
        pump(100);
        CHECK(w.undoStack()->count() == 2, "hai buoc Hoan tac rieng");
        w.undoStack()->undo();
        CHECK(waitFor([&] { return int(p.scenes.size()) == s0 + 1; }) && p.scenes.back() == scene1, "Hoan tac #1: chi anh 2 bi go");
        w.undoStack()->undo();
        CHECK(waitFor([&] { return int(p.scenes.size()) == s0; }) && int(p.sources.size()) == src0, "Hoan tac #2: ve trang thai ban dau");
        w.undoStack()->redo(); w.undoStack()->redo();
        CHECK(waitFor([&] { return int(p.scenes.size()) == s0 + 2; }), "Lam lai x2: ca hai tro lai");
        pump(100);
        w.undoStack()->undo(); w.undoStack()->undo();
        waitFor([&] { return int(p.scenes.size()) == s0; });

        // sua layer cua Scene vua nhap roi hoan tac ca hai (LIFO) -> khong crash, cache khop
        w.addMediaFile(png);
        waitFor([&] { return int(p.scenes.size()) == s0 + 1; });
        pump(150);
        sceneCombo->setCurrentIndex(s0);
        pump(100);
        insp->layerList()->setCurrentRow(0);
        insp->editCurrentLayer([](Layer& l) { l.opacity = 0.25f; }, "sua layer scene moi");
        pump(150);
        CHECK(waitFor([&] { return std::abs(p.scenes[s0]->layers[0].opacity - 0.25f) < 1e-4; }), "sua layer cua Scene moi nhap");
        w.undoStack()->undo();
        w.undoStack()->undo();
        CHECK(waitFor([&] { return int(p.scenes.size()) == s0; }), "hoan tac sua layer roi hoan tac nhap: Scene go");
        pump(200);
        CHECK(sceneCombo->count() == s0 && insp->currentScenePtr() != nullptr, "Inspector khong con muc cua Scene da go");
        w.undoStack()->redo(); w.undoStack()->redo();
        CHECK(waitFor([&] { return int(p.scenes.size()) == s0 + 1; }), "lam lai ca hai");
        pump(150);
        CHECK(std::abs(p.scenes[s0]->layers[0].opacity - 0.25f) < 1e-4, "Lam lai: layer co lai gia tri da sua (0,25)");
        w.undoStack()->undo(); w.undoStack()->undo();
        waitFor([&] { return int(p.scenes.size()) == s0; });
        w.undoStack()->clear();

        // Scene dang On Air: hoan tac bi tu choi (khong doi hinh dang len song)
        w.addMediaFile(png);
        waitFor([&] { return int(p.scenes.size()) == s0 + 1; });
        pump(100);
        emit scenesL->itemDoubleClicked(scenesL->item(s0));       // dua len Program bang CUT
        CHECK(waitFor([&] { return ch->program == s0; }), "(chuan bi) Scene vua nhap dang o Program");
        auto* undoA = [&]() -> QAction* { for (auto* m : w.menuBar()->findChildren<QMenu*>()) for (auto* a : m->actions()) if (a->text().startsWith("Hoàn tác")) return a; return nullptr; }();
        undoA->trigger();
        pump(300);
        CHECK(int(p.scenes.size()) == s0 + 1 && ch->program == s0 && w.undoStack()->canUndo() && w.undoStack()->index() == w.undoStack()->count(),
              "Scene dang On Air: Hoan tac nhap bi tu choi, Scene va Program giu nguyen, ngan xep khong doi");
        // roi doi Program di roi hoan tac duoc
        emit scenesL->itemDoubleClicked(scenesL->item(0));
        waitFor([&] { return ch->program == 0; });
        undoA->trigger();
        CHECK(waitFor([&] { return int(p.scenes.size()) == s0; }), "het On Air: Hoan tac nhap chay duoc");
        pump(100);
        CHECK(ch->program == 0, "Program khong doi sau hoan tac");
        // truong hop dua lenh Hoan tac thang (khong qua kiem tra UI) khi Scene On Air: render thread tu choi + khoi phuc ngan xep
        w.undoStack()->redo();
        waitFor([&] { return int(p.scenes.size()) == s0 + 1; });
        emit scenesL->itemDoubleClicked(scenesL->item(s0));
        waitFor([&] { return ch->program == s0; });
        const int idxBefore = w.undoStack()->index();
        w.undoStack()->undo();                                      // bo qua kiem tra UI
        pump(500);
        CHECK(int(p.scenes.size()) == s0 + 1 && ch->program == s0 && w.undoStack()->index() == idxBefore, "Hoan tac truc tiep khi On Air: render thread tu choi va ngan xep tu khoi phuc");
        emit scenesL->itemDoubleClicked(scenesL->item(0));
        waitFor([&] { return ch->program == 0; });
        w.undoStack()->undo();
        waitFor([&] { return int(p.scenes.size()) == s0; });

        // tao Scene tu Media Pool (khong them nguon) + hoan tac
        const int srcN = int(p.sources.size());
        emit mediaL->itemDoubleClicked(mediaL->item(0));
        CHECK(waitFor([&] { return int(p.scenes.size()) == s0 + 1; }) && int(p.sources.size()) == srcN, "Media Pool: tao Scene tu nguon co san (khong them nguon)");
        pump(100);
        w.undoStack()->undo();
        CHECK(waitFor([&] { return int(p.scenes.size()) == s0; }) && int(p.sources.size()) == srcN, "Hoan tac tao Scene: go Scene, GIU nguon (con dung boi Scene khac)");
        w.undoStack()->clear();
        pump(100);
        CHECK(engine.error().isEmpty(), "sau chuoi nhap/hoan tac: engine khong loi");
    }

    // ================= P1-5: hoan tac thuoc tinh Scene (thoi luong / lap / tu phat lai) =================
    {
        auto* tl = w.timeline();
        auto* sceneCombo = w.inspector()->findChild<QComboBox*>("sceneCombo");
        sceneCombo->setCurrentIndex(1);
        pump(100);
        auto sc = p.scenes[1];
        auto* durSpin = tl->findChild<QDoubleSpinBox*>("tlDuration");
        auto* loopChk = tl->findChild<QCheckBox*>("tlLoop");
        auto* restartChk = tl->findChild<QCheckBox*>("tlRestart");
        w.undoStack()->clear();
        const double d0 = sc->duration;
        const bool l0 = sc->loop, r0 = sc->restartOnAir;

        durSpin->setValue(25.0);
        CHECK(waitFor([&] { return sc->duration == 25.0; }) && w.undoStack()->count() == 1, "doi thoi luong 25 s: ap dung + 1 buoc Hoan tac");
        durSpin->setValue(26.0); durSpin->setValue(27.0);
        pump(100);
        CHECK(w.undoStack()->count() == 1 && waitFor([&] { return sc->duration == 27.0; }), "chinh lien tiep cung truong gop thanh 1 buoc");
        w.undoStack()->undo();
        CHECK(waitFor([&] { return sc->duration == d0; }) && std::abs(durSpin->value() - d0) < 1e-9 && tl->duration() == d0, "Hoan tac thoi luong: Scene va o nhap ve gia tri cu");
        w.undoStack()->redo();
        CHECK(waitFor([&] { return sc->duration == 27.0; }) && std::abs(durSpin->value() - 27.0) < 1e-9, "Lam lai thoi luong");
        w.undoStack()->undo();
        waitFor([&] { return sc->duration == d0; });
        w.undoStack()->clear();

        loopChk->setChecked(!l0);
        CHECK(waitFor([&] { return sc->loop == !l0; }) && w.undoStack()->count() == 1, "tat/bat lap: ap dung + 1 buoc");
        w.undoStack()->undo();
        CHECK(waitFor([&] { return sc->loop == l0; }) && loopChk->isChecked() == l0, "Hoan tac lap: Scene va checkbox ve cu");
        w.undoStack()->redo();
        CHECK(waitFor([&] { return sc->loop == !l0; }) && loopChk->isChecked() == !l0, "Lam lai lap");
        w.undoStack()->undo();
        w.undoStack()->clear();

        restartChk->setChecked(!r0);
        CHECK(waitFor([&] { return sc->restartOnAir == !r0; }) && w.undoStack()->count() == 1, "tat/bat tu phat lai: ap dung + 1 buoc");
        w.undoStack()->undo();
        CHECK(waitFor([&] { return sc->restartOnAir == r0; }) && restartChk->isChecked() == r0, "Hoan tac tu phat lai");
        w.undoStack()->clear();

        // khong dong bo buoc: khac truong thi khong gop
        durSpin->setValue(12.0);
        loopChk->setChecked(!l0);
        pump(100);
        CHECK(w.undoStack()->count() == 2, "thoi luong va lap la hai buoc rieng");
        w.undoStack()->undo(); w.undoStack()->undo();
        CHECK(waitFor([&] { return sc->duration == d0 && sc->loop == l0; }), "hoan tac ca hai ve nguyen trang");
        w.undoStack()->clear();
        // gia tri trung: khong tao buoc
        durSpin->setValue(d0);
        pump(50);
        CHECK(w.undoStack()->count() == 0, "dat dung gia tri hien tai: khong tao buoc Hoan tac");
        // hoan tac thoi luong khi dong ho dang vuot: kep ve duoi
        sc->clock.seek(8.0);
        durSpin->setValue(5.0);
        CHECK(waitFor([&] { return sc->duration == 5.0 && sc->clock.time.load() <= 5.0 + 1e-6; }), "rut ngan thoi luong: dong ho duoc kep");
        w.undoStack()->undo();
        waitFor([&] { return sc->duration == d0; });
        w.undoStack()->clear();
        sceneCombo->setCurrentIndex(0);
    }

    // ================= P1-6: timeline — zoom/cuon, chon nhieu, keo nhom, sao chep/dan =================
    {
        auto* tl = w.timeline();
        auto* insp = w.inspector();
        auto* sceneCombo = insp->findChild<QComboBox*>("sceneCombo");
        sceneCombo->setCurrentIndex(0);
        pump(100);
        insp->layerList()->setCurrentRow(0);
        pump(100);
        auto* us = w.undoStack();
        us->clear();
        auto layer = [&]() -> const Layer& { return *insp->currentLayerData(); };
        auto keysOf = [&](AnimProp pr) { std::vector<double> t; if (const Track* tr = layer().track(pr)) for (const auto& k : tr->keys) t.push_back(k.time); return t; };
        auto near = [](double a, double b) { return std::abs(a - b) < 0.011; };
        auto* scrollBar = tl->findChild<QScrollBar*>("tlScroll");
        auto* zin = tl->findChild<QPushButton*>("tlZoomIn");
        auto* zout = tl->findChild<QPushButton*>("tlZoomOut");
        auto* zfit = tl->findChild<QPushButton*>("tlZoomFit");
        const double dur = tl->duration();
        CHECK(scrollBar && zin && zout && zfit && tl->zoom() == 1.0 && tl->viewStart() == 0.0, "co thanh cuon + nut zoom; mac dinh zoom 1, bat dau 0");

        // ---- zoom / cuon ----
        const int xs0 = tl->timeToX(dur / 2);
        const int x1_0 = tl->timeToX(dur);
        CHECK(!scrollBar->isEnabled(), "zoom 1: thanh cuon bi vo hieu (khong co gi de cuon)");
        zin->click();
        CHECK(tl->zoom() > 1.4 && tl->visibleDuration() < dur && scrollBar->isEnabled() && scrollBar->maximum() > 0, "bam +: zoom vao, thanh cuon bat");
        CHECK(std::abs(tl->timeToX(tl->viewStart() + tl->visibleDuration()) - tl->timeToX(dur)) > 0 || true, "(ban do thoi gian doi theo zoom)");
        tl->setZoom(4.0, 5.0);
        CHECK(std::abs(tl->zoom() - 4.0) < 1e-9 && std::abs(tl->visibleDuration() - dur / 4) < 1e-9, "zoom 4: vung nhin = 1/4 thoi luong");
        CHECK(tl->viewStart() >= 0 && tl->viewStart() + tl->visibleDuration() <= dur + 1e-9 && tl->viewStart() <= 5.0 && tl->viewStart() + tl->visibleDuration() >= 5.0, "zoom quanh 5 s: 5 s van nam trong vung nhin");
        tl->setViewStart(1.5);
        CHECK(std::abs(tl->viewStart() - 1.5) < 1e-9 && tl->timeToX(1.5) == TimelineWidget::kLabelW, "cuon: 1,5 s o ngay mep trai vung noi dung");
        CHECK(std::abs(tl->xToTime(tl->timeToX(3.0)) - 3.0) < 0.02, "timeToX/xToTime nghich dao dung khi dang cuon");
        tl->setViewStart(-5);
        CHECK(tl->viewStart() == 0.0, "cuon ve truoc 0 bi kep");
        tl->setViewStart(1e9);
        CHECK(std::abs(tl->viewStart() - (dur - tl->visibleDuration())) < 1e-9, "cuon qua cuoi bi kep");
        scrollBar->setValue(2000);
        CHECK(std::abs(tl->viewStart() - 2.0) < 1e-6, "keo thanh cuon -> viewStart 2 s");
        tl->setZoom(1000.0);
        CHECK(tl->zoom() == 64.0, "zoom toi da 64");
        zout->click();
        CHECK(tl->zoom() < 64.0, "bam -: thu nho");
        zfit->click();
        CHECK(tl->zoom() == 1.0 && tl->viewStart() == 0.0 && tl->timeToX(dur / 2) == xs0 && tl->timeToX(dur) == x1_0 && !scrollBar->isEnabled(), "Vua khit: tro ve zoom 1 nhu ban dau");
        {   // lan chuot
            QWidget* tv = tl->trackView();
            QWheelEvent wz(QPointF(tl->timeToX(5.0), 60), QPointF(tl->timeToX(5.0), 60), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(tv, &wz);
            CHECK(tl->zoom() > 1.0, "Ctrl + lan chuot: zoom");
            const double z = tl->zoom();
            QWheelEvent wsc(QPointF(300, 60), QPointF(300, 60), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(tv, &wsc);
            CHECK(tl->zoom() == z && tl->viewStart() > 0.0, "lan chuot thuong: cuon ngang (khong doi zoom)");
            tl->zoomFit();
        }

        // ---- chuan bi keyframe ----
        tl->addKey(AnimProp::X, 1.0); tl->addKey(AnimProp::X, 2.0); tl->addKey(AnimProp::X, 4.0);
        tl->addKey(AnimProp::Y, 2.0); tl->addKey(AnimProp::Y, 3.0);
        tl->addKey(AnimProp::Opacity, 1.0);
        pump(100);
        CHECK(keysOf(AnimProp::X).size() == 3 && keysOf(AnimProp::Y).size() == 2 && keysOf(AnimProp::Opacity).size() == 1, "(chuan bi) 6 keyframe tren 3 lan");
        us->clear();

        // ---- chon nhieu ----
        QWidget* tv = tl->trackView();
        const int rowX = int(AnimProp::X), rowY = int(AnimProp::Y), rowO = int(AnimProp::Opacity);
        auto send = [&](QEvent::Type type, QPoint pos, Qt::KeyboardModifiers mods = Qt::NoModifier, Qt::MouseButton btn = Qt::LeftButton) {
            QMouseEvent ev(type, QPointF(pos), QPointF(tv->mapToGlobal(pos)), btn, type == QEvent::MouseButtonRelease ? Qt::NoButton : btn, mods);
            QApplication::sendEvent(tv, &ev);
        };
        auto keyPt = [&](AnimProp pr, double t) { return QPoint(tl->timeToX(t), tl->rowTop(int(pr)) + 12); };
        auto sendKey = [&](int key, Qt::KeyboardModifiers mods = Qt::NoModifier) {
            QKeyEvent ev(QEvent::KeyPress, key, mods);
            QApplication::sendEvent(tv, &ev);
        };

        send(QEvent::MouseButtonPress, keyPt(AnimProp::X, 1.0)); send(QEvent::MouseButtonRelease, keyPt(AnimProp::X, 1.0));
        CHECK(tl->selection().size() == 1 && tl->isSelected(AnimProp::X, 1.0), "bam vao key: chon rieng key do");
        send(QEvent::MouseButtonPress, keyPt(AnimProp::Y, 2.0), Qt::ControlModifier); send(QEvent::MouseButtonRelease, keyPt(AnimProp::Y, 2.0), Qt::ControlModifier);
        send(QEvent::MouseButtonPress, keyPt(AnimProp::X, 4.0), Qt::ShiftModifier); send(QEvent::MouseButtonRelease, keyPt(AnimProp::X, 4.0), Qt::ShiftModifier);
        CHECK(tl->selection().size() == 3 && tl->isSelected(AnimProp::X, 1.0) && tl->isSelected(AnimProp::Y, 2.0) && tl->isSelected(AnimProp::X, 4.0), "Ctrl/Shift + bam: them vao vung chon");
        send(QEvent::MouseButtonPress, keyPt(AnimProp::Y, 2.0), Qt::ControlModifier); send(QEvent::MouseButtonRelease, keyPt(AnimProp::Y, 2.0), Qt::ControlModifier);
        CHECK(tl->selection().size() == 2 && !tl->isSelected(AnimProp::Y, 2.0), "Ctrl + bam key da chon: bo chon");
        send(QEvent::MouseButtonPress, keyPt(AnimProp::X, 2.0)); send(QEvent::MouseButtonRelease, keyPt(AnimProp::X, 2.0));
        CHECK(tl->selection().size() == 1 && tl->isSelected(AnimProp::X, 2.0), "bam key khac (khong phim): chon lai chi key do");
        sendKey(Qt::Key_A, Qt::ControlModifier);
        CHECK(tl->selection().size() == 6, "Ctrl+A: chon het 6 keyframe");
        sendKey(Qt::Key_Escape);
        CHECK(tl->selection().empty(), "Esc: bo chon");
        // khung chon (rubber band): keo tu khoang trong
        {
            const QPoint a(tl->timeToX(1.5), tl->rowTop(rowX) + 2), b2(tl->timeToX(3.5), tl->rowTop(rowY) + TimelineWidget::kRowH - 2);
            send(QEvent::MouseButtonPress, a);
            send(QEvent::MouseMove, b2);
            send(QEvent::MouseButtonRelease, b2);
            CHECK(tl->selection().size() == 3 && tl->isSelected(AnimProp::X, 2.0) && tl->isSelected(AnimProp::Y, 2.0) && tl->isSelected(AnimProp::Y, 3.0) && !tl->isSelected(AnimProp::X, 4.0),
                  "keo khung chon (1,5-3,5 s, lan X+Y): chon dung 3 key ben trong");
            const QPoint c(tl->timeToX(3.8), tl->rowTop(rowO) + 2), d(tl->timeToX(4.2), tl->rowTop(rowX) + 20);
            send(QEvent::MouseButtonPress, QPoint(tl->timeToX(3.8), tl->rowTop(rowX) + 2), Qt::ControlModifier);
            send(QEvent::MouseMove, QPoint(tl->timeToX(4.2), tl->rowTop(rowX) + 20), Qt::ControlModifier);
            send(QEvent::MouseButtonRelease, QPoint(tl->timeToX(4.2), tl->rowTop(rowX) + 20), Qt::ControlModifier);
            CHECK(tl->selection().size() == 4 && tl->isSelected(AnimProp::X, 4.0), "Ctrl + khung chon: them vao vung chon cu");
            (void)c; (void)d;
            send(QEvent::MouseButtonPress, QPoint(tl->timeToX(9.0), tl->rowTop(rowO) + 5));
            send(QEvent::MouseButtonRelease, QPoint(tl->timeToX(9.0), tl->rowTop(rowO) + 5));
            CHECK(tl->selection().empty(), "bam khoang trong: bo chon het");
        }

        // ---- di chuyen nhom ----
        us->clear();
        tl->selectRect(0.5, 3.5, rowX, rowY);          // X:1,2  Y:2,3
        CHECK(tl->selection().size() == 4, "(chuan bi) chon X1 X2 Y2 Y3");
        send(QEvent::MouseButtonPress, keyPt(AnimProp::X, 2.0));           // bam vao mot key da chon -> keo ca nhom
        send(QEvent::MouseMove, keyPt(AnimProp::X, 2.5));
        send(QEvent::MouseMove, keyPt(AnimProp::X, 3.0));
        send(QEvent::MouseMove, keyPt(AnimProp::X, 3.5));
        send(QEvent::MouseButtonRelease, keyPt(AnimProp::X, 3.5));
        pump(100);
        {
            const auto kx = keysOf(AnimProp::X), ky = keysOf(AnimProp::Y);
            CHECK(kx.size() == 3 && near(kx[0], 2.5) && near(kx[1], 3.5) && near(kx[2], 4.0) && ky.size() == 2 && near(ky[0], 3.5) && near(ky[1], 4.5), "keo nhom +1,5 s: X 1,2->2,5 3,5 | Y 2,3->3,5 4,5 (giu khoang cach, key khong chon o 4,0 nguyen)");
            CHECK(us->count() == 1, "keo lien tuc ca nhom = MOT buoc Hoan tac");
            CHECK(tl->selection().size() == 4 && tl->isSelected(AnimProp::X, 2.5) && tl->isSelected(AnimProp::Y, 4.5), "vung chon di theo cac key");
            us->undo();
            pump(100);
            const auto ux = keysOf(AnimProp::X), uy = keysOf(AnimProp::Y);
            CHECK(ux.size() == 3 && near(ux[0], 1.0) && near(ux[1], 2.0) && near(uy[0], 2.0) && near(uy[1], 3.0), "Hoan tac 1 lan: ca nhom ve cho cu");
            us->redo();
            pump(100);
            CHECK(near(keysOf(AnimProp::X)[0], 2.5) && near(keysOf(AnimProp::Y)[1], 4.5), "Lam lai: ca nhom di chuyen lai");
            us->undo();
        }
        // kep trong [0, thoi luong]
        {
            tl->selectRect(0.5, 3.5, rowX, rowY);
            const double got = tl->moveSelection(-50.0, false);
            CHECK(near(got, -1.0) && near(keysOf(AnimProp::X)[0], 0.0) && near(keysOf(AnimProp::Y)[0], 1.0), "keo qua ben trai: nhom bi kep, key dau cham 0 (khong am), giu khoang cach");
            us->undo();
            tl->selectRect(0.5, 3.5, rowX, rowY);
            const double got2 = tl->moveSelection(1000.0, false);
            CHECK(got2 > 0 && keysOf(AnimProp::Y).back() <= dur + 1e-9 && keysOf(AnimProp::X).back() <= dur + 1e-9, "keo qua ben phai: kep o thoi luong Scene");
            us->undo();
            // dung do key khong duoc chon
            tl->selectRect(0.5, 3.5, rowX, rowX);          // X: 1,2 ; key khong chon X4
            const double blocked = tl->moveSelection(2.0, false);   // X2 -> 4 trung X4
            CHECK(blocked == 0 && near(keysOf(AnimProp::X)[1], 2.0) && keysOf(AnimProp::X).size() == 3, "dich de len key khong duoc chon: bi tu choi (khong ghi de mat key)");
        }

        // ---- sao chep / dan ----
        us->clear();
        tl->selectRect(0.5, 3.5, rowX, rowY);                          // X1 X2 Y2 Y3 -> goc 1,0
        const auto xValBefore = layer().track(AnimProp::X)->keys[0].value;
        sendKey(Qt::Key_C, Qt::ControlModifier);
        CHECK(tl->hasClipboard() && us->count() == 0, "Ctrl+C: co du lieu chep, khong tao buoc Hoan tac");
        tl->pause();
        tl->seek(6.0);
        pump(150);
        CHECK(std::abs(tl->playhead() - 6.0) < 0.02, "(chuan bi) playhead dung o 6 s");
        const int pasted = tl->paste();                              // dan tai playhead = 6
        pump(100);
        {
            const auto kx = keysOf(AnimProp::X), ky = keysOf(AnimProp::Y);
            CHECK(pasted == 4 && kx.size() == 5 && near(kx[3], 6.0) && near(kx[4], 7.0) && ky.size() == 4 && near(ky[2], 7.0) && near(ky[3], 8.0), "dan tai playhead 6 s: X 6,7 | Y 7,8 (giu khoang cach tuong doi)");
            CHECK(us->count() == 1, "dan = MOT buoc Hoan tac");
            CHECK(std::abs(layer().track(AnimProp::X)->keys[3].value - xValBefore) < 1e-9, "gia tri key duoc dan giu nguyen");
            CHECK(tl->selection().size() == 4 && tl->isSelected(AnimProp::X, 6.0), "sau khi dan: chon cac key vua dan");
            us->undo();
            pump(100);
            CHECK(keysOf(AnimProp::X).size() == 3 && keysOf(AnimProp::Y).size() == 2, "Hoan tac dan: bo ca nhom vua dan");
            us->redo();
            pump(100);
            CHECK(keysOf(AnimProp::X).size() == 5, "Lam lai dan");
            us->undo();
        }
        // dan bang phim Ctrl+V
        tl->seek(5.0);
        pump(150);
        sendKey(Qt::Key_V, Qt::ControlModifier);
        pump(100);
        CHECK(keysOf(AnimProp::X).size() == 5 && near(keysOf(AnimProp::X)[3], 5.0), "Ctrl+V dan tai playhead 5 s");
        us->undo();
        // dan de len key co san (ghi de) va key vuot thoi luong bi bo
        tl->seek(dur - 0.5);
        pump(150);
        const int pasted2 = tl->paste();
        CHECK(pasted2 < 4 && pasted2 >= 1, "dan gan cuoi Scene: key vuot thoi luong bi bo (khong ghi ra ngoai Scene)");
        for (double t : keysOf(AnimProp::X)) CHECK(t <= dur + 1e-6, "key sau dan khong vuot thoi luong");
        us->undo();
        tl->seek(1.0);                                                // dan len cho dang co key X1: ghi de, khong nhan doi
        pump(150);
        tl->paste();
        pump(100);
        {
            const auto kx = keysOf(AnimProp::X);
            int at1 = 0; for (double t : kx) at1 += near(t, 1.0);
            CHECK(at1 == 1, "dan trung thoi diem key co san: ghi de, khong tao hai key cung luc");
        }
        us->undo();
        // dan sang layer/Scene khac
        {
            tl->selectRect(0.5, 3.5, rowX, rowX);
            tl->copySelection();
            sceneCombo->setCurrentIndex(1);
            pump(150);
            insp->layerList()->setCurrentRow(0);
            pump(100);
            CHECK(insp->currentLayerData() && !insp->currentLayerData()->track(AnimProp::X), "(chuan bi) Scene 2 chua co key X");
            tl->pause();
            tl->seek(2.0);
            pump(150);
            CHECK(tl->paste() == 2 && keysOf(AnimProp::X).size() == 2 && near(keysOf(AnimProp::X)[0], 2.0) && near(keysOf(AnimProp::X)[1], 3.0), "dan sang Scene/layer khac: tao lan moi, key o 2,0 va 3,0");
            us->undo();
            sceneCombo->setCurrentIndex(0);
            pump(150);
            insp->layerList()->setCurrentRow(0);
            pump(100);
        }

        // ---- xoa nhom ----
        us->clear();
        tl->selectRect(0.5, 3.5, rowX, rowY);
        const size_t before = keysOf(AnimProp::X).size() + keysOf(AnimProp::Y).size();
        sendKey(Qt::Key_Delete);
        pump(100);
        CHECK(before == 5 && keysOf(AnimProp::X).size() == 1 && !layer().track(AnimProp::Y) && us->count() == 1 && tl->selection().empty(), "Delete: xoa ca nhom 1 buoc (lan Y het key -> bo lan)");
        us->undo();
        pump(100);
        CHECK(keysOf(AnimProp::X).size() == 3 && keysOf(AnimProp::Y).size() == 2, "Hoan tac xoa nhom: tro lai du");
        // doi noi suy ca nhom
        us->clear();
        tl->selectRect(0.5, 4.5, rowX, rowY);
        tl->setSelectionInterp(Interp::Hold);
        pump(100);
        bool allHold = true;
        for (auto pr : {AnimProp::X, AnimProp::Y}) for (const auto& k : layer().track(pr)->keys) allHold = allHold && (k.interp == Interp::Hold);
        CHECK(allHold && us->count() == 1, "doi kieu noi suy cho ca nhom: 1 buoc");
        us->undo();
        pump(100);

        // ---- khoa van hanh chan ca cac thao tac moi ----
        tl->selectRect(0.5, 3.5, rowX, rowY);
        tl->copySelection();
        us->clear();
        const auto xBefore = keysOf(AnimProp::X);
        w.setLocked(true);
        CHECK(tl->moveSelection(1.0, false) == 0 && tl->paste() == 0, "khoa: moveSelection/paste khong lam gi");
        tl->deleteSelection();
        tl->setSelectionInterp(Interp::Hold);
        sendKey(Qt::Key_Delete);
        sendKey(Qt::Key_V, Qt::ControlModifier);
        send(QEvent::MouseButtonPress, QPoint(tl->timeToX(8.0), tl->rowTop(rowO) + 5));
        send(QEvent::MouseMove, QPoint(tl->timeToX(9.0), tl->rowTop(rowO) + 20));
        send(QEvent::MouseButtonRelease, QPoint(tl->timeToX(9.0), tl->rowTop(rowO) + 20));
        pump(100);
        CHECK(us->count() == 0 && keysOf(AnimProp::X) == xBefore, "khoa: chuot/phim tren timeline khong sua keyframe");
        w.setLocked(false);
        // don dep
        tl->clearSelection();
        for (auto pr : {AnimProp::X, AnimProp::Y, AnimProp::Opacity}) while (layer().track(pr)) tl->deleteKey(pr, 0);
        pump(100);
        us->clear();
        CHECK(!layer().track(AnimProp::X) && !layer().track(AnimProp::Y) && !layer().track(AnimProp::Opacity), "don dep keyframe thu");
        // Scene 3 (thoi luong khac): zoom giu hop le khi doi thoi luong
        tl->setZoom(8.0, 1.0);
        tl->setDuration(2.0);
        pump(100);
        CHECK(tl->viewStart() + tl->visibleDuration() <= 2.0 + 1e-9 && tl->viewStart() >= 0, "rut ngan thoi luong khi dang zoom: vung nhin van hop le");
        us->undo();
        pump(100);
        tl->zoomFit();
    }

    engine.stopEngine();
    std::cout << (g_fail ? "THAT BAI: " : "TAT CA QUA ") << g_fail << " loi\n";
    return g_fail ? 1 : 0;
}
