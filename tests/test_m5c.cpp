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

    engine.stopEngine();
    std::cout << (g_fail ? "THAT BAI: " : "TAT CA QUA ") << g_fail << " loi\n";
    return g_fail ? 1 : 0;
}
