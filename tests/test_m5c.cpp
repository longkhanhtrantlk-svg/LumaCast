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
#include <QMenuBar>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QToolButton>
#include <QProcess>
#include <QFile>
#include <QDir>
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
        QSettings st("LumaCast", "LumaCast");
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
        QSettings st("LumaCast", "LumaCast");
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
        QSettings st("LumaCast", "LumaCast");
        st.remove(kKeyLastProject);
        w.saveProject(false);
        CHECK(waitFor([&] { QSettings s2("LumaCast", "LumaCast"); return lastProjectPath(s2) == QFileInfo(p.filePath).absoluteFilePath(); }), "Luu project: nho lam project gan nhat");
    }

    // ================= P0-3: chay that lumacast voi/khong project gan nhat (tien trinh con) =================
    {
        auto runApp = [&](const QString& cfgHome, const QStringList& args, int keepMs) {
            QProcess pr;
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert("XDG_CONFIG_HOME", cfgHome + "/config");
            env.insert("XDG_DATA_HOME", cfgHome + "/data");
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
            QFile f(home + "/data/LumaCast/logs/lumacast.log");
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        auto writeCfg = [&](const QString& home, const QString& lastPath) {
            QDir().mkpath(home + "/config/LumaCast");
            QSettings c(home + "/config/LumaCast/LumaCast.conf", QSettings::IniFormat);
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
            QSettings c(h4 + "/config/LumaCast/LumaCast.conf", QSettings::IniFormat);
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

    engine.stopEngine();
    std::cout << (g_fail ? "THAT BAI: " : "TAT CA QUA ") << g_fail << " loi\n";
    return g_fail ? 1 : 0;
}
