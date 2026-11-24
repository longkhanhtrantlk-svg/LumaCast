// Test M5c: dieu khien ngoai (HTTP + SSE, OSC), bao mat, duong loi va duong doc hai.
// Dung socket TCP/UDP that tren 127.0.0.1 voi cong ngau nhien; may chu chay chung vong su kien nen moi ham cho deu bom su kien.
#include "control/ControlService.h"
#include "media/StaticSources.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QUdpSocket>
#include <iostream>

using namespace lc;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { ++g_fail; std::cerr << "FAIL: " << msg << "  (" #cond ")\n"; } else std::cout << "ok:   " << msg << "\n"; } while (0)

static void pump(int ms) {
    QElapsedTimer t; t.start();
    while (t.elapsed() < ms) { QApplication::processEvents(); QThread::msleep(2); }
}
template <class F> static bool waitFor(F cond, int ms = 3000) {
    QElapsedTimer t; t.start();
    while (t.elapsed() < ms) { QApplication::processEvents(); if (cond()) return true; QThread::msleep(5); }
    return cond();
}

static QStringList g_log;
static void logHandler(QtMsgType, const QMessageLogContext&, const QString& m) { g_log << m; }
static bool logHas(const QString& needle) { for (const auto& l : g_log) if (l.contains(needle)) return true; return false; }

struct Resp {
    int status = 0;
    QByteArray head, body;
    bool closed = false;
    QJsonObject json() const { return QJsonDocument::fromJson(body).object(); }
    QByteArray header(const QByteArray& name) const {
        for (const QByteArray& l : head.split('\n'))
            if (l.toLower().startsWith(name.toLower() + ":")) return l.mid(name.size() + 1).trimmed();
        return {};
    }
};

// Gui `raw` va doc phan hoi cho den khi may chu dong ket noi (hoac het thoi gian).
static Resp rawRequest(quint16 port, const QByteArray& raw, int timeoutMs = 3000, bool closeWrite = false) {
    QTcpSocket s;
    s.connectToHost(QHostAddress::LocalHost, port);
    Resp r;
    if (!waitFor([&] { return s.state() == QAbstractSocket::ConnectedState; }, 2000)) return r;
    s.write(raw);
    if (closeWrite) s.flush();
    QByteArray all;
    QElapsedTimer t; t.start();
    while (t.elapsed() < timeoutMs && s.state() != QAbstractSocket::UnconnectedState) {
        QApplication::processEvents();
        all += s.readAll();
        QThread::msleep(2);
    }
    all += s.readAll();
    r.closed = s.state() == QAbstractSocket::UnconnectedState;
    const int h = all.indexOf("\r\n\r\n");
    r.head = h >= 0 ? all.left(h) : all;
    r.body = h >= 0 ? all.mid(h + 4) : QByteArray();
    const QList<QByteArray> parts = r.head.split(' ');
    if (parts.size() >= 2) r.status = parts[1].toInt();
    return r;
}

static Resp http(quint16 port, const QByteArray& method, const QByteArray& path, const QByteArray& body = {},
                  const QByteArray& token = {}, const QList<QByteArray>& extra = {}) {
    QByteArray raw = method + " " + path + " HTTP/1.1\r\nHost: 127.0.0.1:" + QByteArray::number(port) + "\r\n";
    if (!token.isEmpty()) raw += "Authorization: Bearer " + token + "\r\n";
    for (const auto& e : extra) raw += e + "\r\n";
    if (!body.isEmpty() || method == "POST") raw += "Content-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n";
    raw += "Connection: close\r\n\r\n" + body;
    return rawRequest(port, raw);
}

static quint16 freePort() {
    QTcpServer s;
    s.listen(QHostAddress::LocalHost, 0);
    return s.serverPort();
}

static quint16 freeUdpPort() {
    QUdpSocket s;
    s.bind(QHostAddress::LocalHost, 0);
    return s.localPort();
}

int main(int argc, char** argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(fmt);
    QApplication app(argc, argv);
    qInstallMessageHandler(logHandler);

    // ================= OSC: bo phan tich =================
    {
        QList<OscMessage> out;
        QString err;
        QByteArray m1 = buildOscMessage("/lumacast/channel/1/preview", {3});
        CHECK(m1.size() % 4 == 0 && parseOscPacket(m1, out, &err) && out.size() == 1 && out[0].address == "/lumacast/channel/1/preview" &&
                  out[0].args.size() == 1 && out[0].args[0].toInt() == 3, "OSC: thong diep int32 dung");
        QByteArray m2 = buildOscMessage("/x/y", {1.5, "xin chào", true, false, -7});
        CHECK(parseOscPacket(m2, out) && out.size() == 1 && out[0].args.size() == 5 && qAbs(out[0].args[0].toDouble() - 1.5) < 1e-6 &&
                  out[0].args[1].toString() == "xin chào" && out[0].args[2].toBool() && !out[0].args[3].toBool() && out[0].args[4].toInt() == -7,
              "OSC: float32/string UTF-8/T/F/int am");
        CHECK(parseOscPacket(buildOscMessage("/a"), out) && out.size() == 1 && out[0].args.isEmpty(), "OSC: khong tham so");
        QByteArray bundle = buildOscBundle({buildOscMessage("/a", {1}), buildOscBundle({buildOscMessage("/b", {2}), buildOscMessage("/c")})});
        CHECK(parseOscPacket(bundle, out) && out.size() == 3 && out[0].address == "/a" && out[1].address == "/b" && out[2].address == "/c", "OSC: bundle long nhau (3 thong diep)");
        // goi cut: moi do dai ngan hon deu phai bi tu choi hoac (neu hop le) khong crash
        bool noCrash = true;
        int accepted = 0;
        for (int n = 0; n < m2.size(); ++n) { QList<OscMessage> o; if (parseOscPacket(m2.left(n), o)) ++accepted; }
        CHECK(noCrash && accepted <= 1, "OSC: goi bi cut o moi do dai bi tu choi (chi con ban cat ngay sau dia chi - OSC cu khong co kieu - hop le)");
        int bundleAccepted = 0;
        for (int n = 0; n < bundle.size(); ++n) { QList<OscMessage> o; if (parseOscPacket(bundle.left(n), o) && !o.isEmpty()) ++bundleAccepted; }
        CHECK(bundleAccepted <= 3, "OSC: bundle bi cut chi chap nhan o ranh gioi phan tu (khong nua chung)");
        QByteArray badTag = buildOscMessage("/a", {1}); badTag.replace(",i", ",q");
        CHECK(!parseOscPacket(badTag, out, &err) && !err.isEmpty(), "OSC: kieu tham so la bi tu choi");
        CHECK(!parseOscPacket(buildOscMessage("khongcoslash", {1}), out), "OSC: dia chi khong bat dau bang '/' bi tu choi");
        QByteArray noNul("/abc", 4);
        CHECK(!parseOscPacket(noNul, out), "OSC: chuoi khong ket thuc bang NUL bi tu choi");
        QByteArray hugeLen = QByteArray("#bundle\0", 8) + QByteArray(8, '\0') + QByteArray("\x7f\xff\xff\xfc", 4) + buildOscMessage("/a");
        CHECK(!parseOscPacket(hugeLen, out), "OSC: phan tu bundle khai bao do dai khong lo bi tu choi");
        QByteArray deep = buildOscMessage("/a");
        for (int i = 0; i < 10; ++i) deep = buildOscBundle({deep});
        CHECK(!parseOscPacket(deep, out), "OSC: bundle long qua sau bi tu choi");
        CHECK(!parseOscPacket(QByteArray(kOscMaxPacket + 4, 'a'), out), "OSC: goi qua lon bi tu choi");
        CHECK(!parseOscPacket({}, out) && !parseOscPacket(QByteArray("ab"), out), "OSC: goi rong/qua ngan bi tu choi");
        // fuzz
        QRandomGenerator rng(12345);
        int crashFree = 0;
        for (int i = 0; i < 4000; ++i) {
            QByteArray b = (i % 2) ? bundle : m2;
            const int flips = 1 + rng.bounded(6);
            for (int k = 0; k < flips; ++k) b[rng.bounded(int(b.size()))] = char(rng.bounded(256));
            if (i % 5 == 0) b.truncate(rng.bounded(int(b.size()) + 1));
            QList<OscMessage> o; parseOscPacket(b, o); ++crashFree;
        }
        for (int i = 0; i < 2000; ++i) { QByteArray b(rng.bounded(300), 0); for (auto& c : b) c = char(rng.bounded(256)); QList<OscMessage> o; parseOscPacket(b, o); ++crashFree; }
        CHECK(crashFree == 6000, "OSC: fuzz 6000 goi ngau nhien/bi pha khong crash");
    }

    // ================= so sanh hang thoi gian =================
    CHECK(ControlApi::constantTimeEquals("abc", "abc") && !ControlApi::constantTimeEquals("abc", "abd") && !ControlApi::constantTimeEquals("abc", "abcd") &&
              !ControlApi::constantTimeEquals("", "a") && ControlApi::constantTimeEquals("", ""), "constantTimeEquals: dung/sai/khac do dai/rong");

    // ================= project + engine =================
    Project p;
    const QColor cols[] = {Qt::red, Qt::blue, Qt::green};
    for (int i = 0; i < 3; ++i) {
        SourceDef d; d.type = "color"; d.color = cols[i]; d.label = QString("c%1").arg(i);
        auto src = p.addSource(d);
        auto s = std::make_shared<Scene>();
        s->name = QString("Cảnh %1").arg(i + 1); s->size = {64, 36}; s->duration = 20;
        s->layers.push_back({src, {0, 0, 1, 1}, 1.0f, LayerFit::Stretch, true});
        p.scenes.push_back(s);
    }
    for (int c = 0; c < 2; ++c) {
        auto ch = std::make_shared<Channel>();
        ch->name = QString("K%1").arg(c + 1); ch->size = {64, 36}; ch->scenes = p.scenes; ch->program = 0; ch->preview = 1;
        p.channels.push_back(ch);
    }
    OutputConfig o1; o1.name = "O1"; o1.size = {96, 54}; o1.channel = 0;
    OutputConfig o2; o2.name = "O2"; o2.size = {96, 54}; o2.channel = 1;
    p.outputs = {o1, o2};
    RenderEngine engine(p);
    engine.startEngine();
    bool locked = false;
    ControlApi api(p, engine);
    api.setLockProvider([&] { return locked; });
    ControlService svc(api);
    auto ch1 = p.channels[0], ch2 = p.channels[1];
    pump(300);

    QTemporaryDir tmp;
    QSettings st(tmp.filePath("c.ini"), QSettings::IniFormat);

    // ================= cai dat =================
    {
        ControlSettings s;
        CHECK(!s.httpEnabled && !s.oscEnabled && !s.httpLan && !s.oscLan && s.token.isEmpty(), "cai dat mac dinh: tat het, khong LAN, khong token");
        s.httpEnabled = true; s.httpPort = 8123; s.httpLan = true; s.token = "abcdefghijklmnop"; s.oscEnabled = true; s.oscPort = 9100; s.oscLan = true;
        s.save(st); st.sync();
        const ControlSettings b = ControlSettings::load(st);
        CHECK(b.httpEnabled && b.httpPort == 8123 && b.httpLan && b.token == "abcdefghijklmnop" && b.oscEnabled && b.oscPort == 9100 && b.oscLan, "cai dat: luu/doc QSettings khop");
        ControlSettings bad; bad.httpEnabled = true; bad.httpLan = true; bad.token = "ngan";
        CHECK(!bad.validate().isEmpty(), "cai dat: LAN + token ngan bi coi la loi");
        bad.token.clear();
        CHECK(!bad.validate().isEmpty(), "cai dat: LAN khong token bi coi la loi");
        bad.httpLan = false;
        CHECK(bad.validate().isEmpty(), "cai dat: loopback khong token hop le");
        const QString t1 = ControlSettings::generateToken(), t2 = ControlSettings::generateToken();
        CHECK(t1.size() == 32 && t1 != t2, "sinh token: 32 ky tu, khac nhau moi lan");
    }

    // ================= LAN bat buoc token =================
    {
        ControlSettings s; s.httpEnabled = true; s.httpLan = true; s.httpPort = freePort(); s.token.clear();
        g_log.clear();
        const QString e = svc.apply(s);
        CHECK(!e.isEmpty() && !svc.httpRunning(), "LAN khong co token: KHONG mo cong, tra loi");
        CHECK(logHas("từ chối cấu hình"), "LAN khong co token: co dong log tu choi");
        s.token = "short";
        CHECK(!svc.apply(s).isEmpty() && !svc.httpRunning(), "LAN token ngan: khong mo cong");
        s.token = ControlSettings::generateToken();
        s.httpPort = freePort();
        CHECK(svc.apply(s).isEmpty() && svc.httpRunning(), "LAN co token dai: mo cong");
        const quint16 port = svc.httpPort();
        CHECK(http(port, "GET", "/api/state").status == 401, "LAN: khong token -> 401");
        CHECK(http(port, "GET", "/api/state", {}, s.token.toUtf8()).status == 200, "LAN: dung token -> 200");
        svc.stop();
    }

    // ================= bat/tat + log (loopback) =================
    ControlSettings cs;
    cs.httpEnabled = true; cs.httpPort = freePort();
    g_log.clear();
    CHECK(svc.apply(cs).isEmpty() && svc.httpRunning(), "bat HTTP loopback khong token");
    CHECK(logHas("HTTP đã BẬT") && logHas("127.0.0.1"), "log: ghi 'HTTP da BAT' kem dia chi loopback");
    const quint16 port = svc.httpPort();
    {
        QTcpSocket s;
        s.connectToHost(QHostAddress::LocalHost, port);
        CHECK(waitFor([&] { return s.state() == QAbstractSocket::ConnectedState; }), "nghe tren 127.0.0.1");
        CHECK(svc.http().isListening(), "may chu dang nghe");
    }

    // ================= dinh dang yeu cau xau =================
    {
        auto& L = svc.http().limits();
        Q_UNUSED(L);
        CHECK(rawRequest(port, "GARBAGE\r\n\r\n").status == 400, "yeu cau rac -> 400");
        CHECK(rawRequest(port, "GET /api/state HTTP/2.0\r\nHost: 127.0.0.1\r\n\r\n").status == 505, "HTTP/2.0 -> 505");
        CHECK(rawRequest(port, "GET api/state HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n").status == 400, "duong dan khong bat dau bang '/' -> 400");
        CHECK(rawRequest(port, "GE T / HTTP/1.1\r\n\r\n").status == 400, "dong yeu cau co 4 phan -> 400");
        CHECK(rawRequest(port, "GET /api/state HTTP/1.1\r\nHost: 127.0.0.1\r\nBadHeaderNoColon\r\n\r\n").status == 400, "header khong co ':' -> 400");
        CHECK(rawRequest(port, "POST /api/black-all HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: abc\r\n\r\n").status == 400, "Content-Length khong phai so -> 400");
        CHECK(rawRequest(port, "POST /api/black-all HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\n").status == 400, "Content-Length mau thuan -> 400");
        CHECK(rawRequest(port, "POST /api/black-all HTTP/1.1\r\nHost: 127.0.0.1\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n").status == 501, "chunked -> 501");
        CHECK(rawRequest(port, "GET /%00 HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n").status == 400, "NUL trong duong dan -> 400");
        CHECK(rawRequest(port, QByteArray("GET /") + QByteArray(3000, 'a') + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n").status == 414, "URI qua dai -> 414");
        const Resp big = rawRequest(port, "GET /api/state HTTP/1.1\r\nHost: 127.0.0.1\r\nX-Big: " + QByteArray(20000, 'x') + "\r\n\r\n");
        CHECK(big.status == 431 && big.closed, "header 20 KB -> 431 va dong ket noi");
        CHECK(rawRequest(port, "GET /api/state HTTP/1.1\r\nHost: 127.0.0.1\r\n" + QByteArray(20000, 'x')).status == 431, "header khong bao gio ket thuc, qua lon -> 431");
        QByteArray many = "GET /api/state HTTP/1.1\r\nHost: 127.0.0.1\r\n";
        for (int i = 0; i < 100; ++i) many += "X-H" + QByteArray::number(i) + ": 1\r\n";
        CHECK(rawRequest(port, many + "\r\n").status == 431, "qua nhieu header -> 431");
        const Resp body = rawRequest(port, "POST /api/black-all HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 1000000\r\n\r\n");
        CHECK(body.status == 413 && body.closed, "Content-Length 1 MB -> 413 (khong doi nhan het)");
        CHECK(rawRequest(port, "POST /api/black-all HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 20000\r\n\r\n" + QByteArray(20000, 'x')).status == 413, "body 20 KB -> 413");
        const Resp inj = http(port, "GET", "/api/state", {}, {}, {"X-Evil: a\x01"});
        CHECK(inj.status == 400, "ky tu dieu khien trong header -> 400");
        CHECK(http(port, "GET", "/api/state").status == 200, "may chu van song sau cac yeu cau xau");
        CHECK(http(port, "DELETE", "/api/state").status == 405 && http(port, "DELETE", "/api/state").header("Allow").contains("GET"), "DELETE -> 405 kem Allow");
        CHECK(http(port, "GET", "/api/channels/1/cut").status == 405, "GET toi tuyen POST -> 405");
        CHECK(http(port, "GET", "/api/nope").status == 404 && http(port, "GET", "/nope").status == 404, "duong dan la -> 404");
        CHECK(http(port, "GET", "/api/../etc/passwd").status == 404, "duong dan ../ khong thoat khoi API -> 404");
        CHECK(http(port, "GET", "/%61pi/state").status == 200, "giai ma %xx truoc khi dinh tuyen");
    }

    // ================= timeout (slowloris) =================
    {
        auto lim = svc.http().limits();
        lim.requestTimeoutMs = 400;
        svc.http().setLimits(lim);
        QTcpSocket s;
        s.connectToHost(QHostAddress::LocalHost, port);
        waitFor([&] { return s.state() == QAbstractSocket::ConnectedState; });
        s.write("GET /api/state HTTP/1.1\r\nHost: 127.0.0.1\r\n");           // khong bao gio ket thuc
        QElapsedTimer t; t.start();
        QByteArray got;
        waitFor([&] { got += s.readAll(); return s.state() == QAbstractSocket::UnconnectedState; }, 2500);
        got += s.readAll();
        CHECK(got.startsWith("HTTP/1.1 408") && t.elapsed() < 2000, "ket noi cham/do dang bi dong bang 408 sau timeout");
        lim.requestTimeoutMs = 5000;
        svc.http().setLimits(lim);
        // ket noi mo roi im lang
        QTcpSocket idle;
        auto lim2 = lim; lim2.requestTimeoutMs = 300; svc.http().setLimits(lim2);
        idle.connectToHost(QHostAddress::LocalHost, port);
        waitFor([&] { return idle.state() == QAbstractSocket::ConnectedState; });
        waitFor([&] { idle.readAll(); return idle.state() == QAbstractSocket::UnconnectedState; }, 2000);
        CHECK(idle.state() == QAbstractSocket::UnconnectedState, "ket noi im lang bi dong sau timeout");
        svc.http().setLimits(lim);
    }

    // ================= qua nhieu ket noi =================
    {
        auto lim = svc.http().limits();
        auto small = lim; small.maxConnections = 4; small.requestTimeoutMs = 2000;
        svc.http().setLimits(small);
        std::vector<std::unique_ptr<QTcpSocket>> hold;
        for (int i = 0; i < 4; ++i) { hold.push_back(std::make_unique<QTcpSocket>()); hold.back()->connectToHost(QHostAddress::LocalHost, port); }
        waitFor([&] { return svc.http().connections() >= 4; });
        const Resp over = rawRequest(port, "GET /api/state HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
        CHECK(over.status == 503, "vuot so ket noi toi da -> 503");
        hold.clear();
        waitFor([&] { return svc.http().connections() == 0; });
        svc.http().setLimits(lim);
        CHECK(http(port, "GET", "/api/state").status == 200, "het qua tai thi phuc vu lai binh thuong");
        // bao 300 ket noi rac lien tiep
        for (int i = 0; i < 300; ++i) { QTcpSocket s; s.connectToHost(QHostAddress::LocalHost, port); if (i % 3 == 0) { s.waitForConnected(50); s.write("\x16\x03\x01\x02\x00"); s.flush(); } }
        pump(200);
        CHECK(http(port, "GET", "/api/state").status == 200, "300 ket noi rac (TLS handshake, dong ngay) khong lam treo may chu");
    }

    // ================= trang thai =================
    {
        const Resp r = http(port, "GET", "/api/state");
        const QJsonObject j = r.json();
        CHECK(r.status == 200 && j["ok"].toBool() && j["channels"].toArray().size() == 2 && j["outputs"].toArray().size() == 2 && j["scenes"].toArray().size() == 3, "GET /api/state: 2 kenh, 2 output, 3 scene");
        const QJsonObject c0 = j["channels"].toArray().at(0).toObject();
        CHECK(c0["program"].toInt() == 1 && c0["preview"].toInt() == 2 && c0["programName"].toString() == "Cảnh 1" && c0["scenes"].toArray().size() == 3 && !c0["ftb"].toBool(), "state: Program=1 Preview=2, ten scene, FTB tat");
        const QJsonObject out0 = j["outputs"].toArray().at(0).toObject();
        CHECK(out0["index"].toInt() == 1 && out0["name"].toString() == "O1" && out0["channel"].toInt() == 1 && !out0["black"].toBool() && !out0["freeze"].toBool() &&
                  !out0["pattern"].toBool() && out0["health"].toString() == "ok" && out0.contains("fps") && out0["width"].toInt() == 96, "state: output co den/dung hinh/mau/tinh trang/fps/kich thuoc");
        CHECK(j.contains("locked") && !j["locked"].toBool() && j["engine"].toObject().contains("fps"), "state: co 'locked' va thong ke engine");
        CHECK(r.header("Content-Type").startsWith("application/json") && r.header("X-Content-Type-Options") == "nosniff", "phan hoi co Content-Type JSON va nosniff");
        const Resp page = http(port, "GET", "/");
        CHECK(page.status == 200 && page.header("Content-Type").startsWith("text/html") && page.header("Content-Security-Policy").contains("default-src 'none'") &&
                  page.body.contains("LumaCast"), "GET / tra trang dieu khien co CSP chat");
    }

    // ================= Preview khong doi Program =================
    {
        const int pgBefore = ch1->program;
        Resp r = http(port, "POST", "/api/channels/1/preview", R"J({"scene":3})J");
        CHECK(r.status == 200 && waitFor([&] { return ch1->preview == 2; }), "POST preview scene 3 -> Preview = Scene 3");
        pump(150);
        CHECK(ch1->program == pgBefore && ch2->program == 0 && ch2->preview == 1, "dat Preview KHONG doi Program (va khong dung kenh khac)");
        CHECK(http(port, "POST", "/api/channels/1/preview", R"J({"scene":4})J").status == 404, "preview scene 4 (khong co) -> 404");
        CHECK(http(port, "POST", "/api/channels/1/preview", R"J({"scene":0})J").status == 404, "preview scene 0 -> 404");
        CHECK(http(port, "POST", "/api/channels/1/preview", R"J({"scene":-5})J").status == 404, "preview scene am -> 404");
        CHECK(http(port, "POST", "/api/channels/9/preview", R"J({"scene":1})J").status == 404, "kenh 9 (khong co) -> 404");
        CHECK(http(port, "POST", "/api/channels/0/cut").status == 404, "kenh 0 -> 404");
        CHECK(http(port, "POST", "/api/channels/abc/cut").status == 400, "kenh 'abc' -> 400");
        CHECK(http(port, "POST", "/api/channels/99999999999999/cut").status == 400, "chi so tran so -> 400");
        CHECK(http(port, "POST", "/api/channels/1/preview", R"J({"scene":"2"})J").status == 400, "scene dang chuoi -> 400");
        CHECK(http(port, "POST", "/api/channels/1/preview", R"J({"scene":1.5})J").status == 400, "scene khong nguyen -> 400");
        CHECK(http(port, "POST", "/api/channels/1/preview", "{}").status == 400, "thieu scene -> 400");
        CHECK(http(port, "POST", "/api/channels/1/preview", "{khong phai json").status == 400, "JSON hong -> 400");
        CHECK(http(port, "POST", "/api/channels/1/preview", "[1,2]").status == 400, "JSON khong phai doi tuong -> 400");
        CHECK(http(port, "POST", "/api/channels/1/preview", "{\"scene\":").status == 400, "JSON cut -> 400");
        CHECK(http(port, "POST", "/api/channels/1/preview", QByteArray("{\"scene\":1,\"x\":\"") + QByteArray(10000, 'a') + "\"}").status == 400 ||
                  http(port, "POST", "/api/channels/1/preview", QByteArray("{\"scene\":1,\"x\":\"") + QByteArray(10000, 'a') + "\"}").status == 200,
              "JSON lon trong gioi han khong crash");
        pump(100);
        CHECK(ch1->program == pgBefore, "sau cac lenh sai Program van nguyen");
    }

    // ================= CUT =================
    {
        ch1->setPreview(1);                                                     // (an toan: khong dang chuyen) dat lai Preview = Scene 2
        pump(80);
        CHECK(http(port, "POST", "/api/channels/1/preview", R"J({"scene":2})J").status == 200, "preview scene 2");
        waitFor([&] { return ch1->preview == 1; });
        CHECK(http(port, "POST", "/api/channels/1/cut").status == 200 && waitFor([&] { return ch1->program == 1 && ch1->preview == 0; }), "POST cut: Program<->Preview hoan doi");
        CHECK(ch2->program == 0, "cut kenh 1 khong dung kenh 2");
    }

    // ================= TAKE =================
    {
        Resp r = http(port, "POST", "/api/channels/1/take", R"J({"type":"fade","duration":0.4,"easing":"linear"})J");
        CHECK(r.status == 200, "take fade 0,4 s linear -> 200");
        CHECK(waitFor([&] { return ch1->inTransition(); }, 1000), "kenh vao trang thai dang chuyen");
        Resp busy = http(port, "POST", "/api/channels/1/take", R"J({"type":"fade"})J");
        CHECK(busy.status == 409 && busy.json()["error"].toString() == "transition_in_progress", "take khi dang chuyen -> 409");
        CHECK(waitFor([&] { return !ch1->inTransition() && ch1->program == 0; }, 3000), "chuyen xong: Program doi sang Scene truoc do");
        CHECK(http(port, "POST", "/api/channels/1/take", R"J({"type":"bogus"})J").status == 400, "take kieu la -> 400");
        CHECK(http(port, "POST", "/api/channels/1/take", R"J({"duration":0})J").status == 400, "take duration 0 -> 400");
        CHECK(http(port, "POST", "/api/channels/1/take", R"J({"duration":99})J").status == 400, "take duration 99 -> 400");
        CHECK(http(port, "POST", "/api/channels/1/take", R"J({"duration":"nhanh"})J").status == 400, "take duration chuoi -> 400");
        CHECK(http(port, "POST", "/api/channels/1/take", R"J({"easing":"zig"})J").status == 400, "take easing la -> 400");
        CHECK(http(port, "POST", "/api/channels/1/take", R"J({"type":"plugin","plugin":"khong.co"})J").status == 400, "take plugin khong ton tai -> 400");
        CHECK(http(port, "POST", "/api/channels/1/take", R"J({"type":"stinger"})J").status == 400, "take stinger -> 400 (chua ho tro)");
        CHECK(http(port, "POST", "/api/channels/7/take", "{}").status == 404, "take kenh 7 -> 404");
        r = http(port, "POST", "/api/channels/1/take", R"J({"type":"push","duration":0.2})J");
        CHECK(r.status == 200 && waitFor([&] { return ch1->inTransition(); }, 1000) && waitFor([&] { return !ch1->inTransition(); }, 3000), "take transition dung san 'push'");
        r = http(port, "POST", "/api/channels/1/take", R"J({"type":"plugin","plugin":"builtin.zoom","duration":0.2})J");
        CHECK(r.status == 200 && waitFor([&] { return ch1->inTransition(); }, 1000) && waitFor([&] { return !ch1->inTransition(); }, 3000), "take plugin theo id");
        r = http(port, "POST", "/api/channels/1/take", R"J({"type":"cut"})J");
        CHECK(r.status == 200, "take type=cut -> 200");
        pump(100);
    }

    // ================= FTB =================
    {
        CHECK(http(port, "POST", "/api/channels/2/ftb", R"J({"on":true,"duration":0.2})J").status == 200 && waitFor([&] { return ch2->ftbTarget.load(); }), "ftb on");
        CHECK(waitFor([&] { return ch2->ftbAmount >= 0.99f; }, 2000) && !ch1->ftbTarget, "FTB kenh 2 ve den, kenh 1 khong doi");
        http(port, "POST", "/api/channels/2/ftb", R"J({"on":true,"duration":0.2})J");        // lap lai: khong dao nguoc
        pump(150);
        CHECK(ch2->ftbTarget, "ftb on lap lai khong dao trang thai");
        CHECK(http(port, "POST", "/api/channels/2/ftb", "{}").status == 200 && waitFor([&] { return !ch2->ftbTarget.load(); }), "ftb khong tham so = bat/tat (toggle)");
        CHECK(http(port, "POST", "/api/channels/2/ftb", R"J({"on":"co"})J").status == 400, "ftb on khong phai bool -> 400");
        CHECK(http(port, "POST", "/api/channels/2/ftb", R"J({"duration":0})J").status == 400, "ftb duration 0 -> 400");
        waitFor([&] { return ch2->ftbAmount <= 0.01f; }, 2000);
    }

    // ================= Output: Den/Dung/Mau co pham vi ro =================
    {
        auto* a = engine.output(0); auto* b = engine.output(1);
        CHECK(http(port, "POST", "/api/outputs/1/black", R"J({"on":true})J").status == 200 && waitFor([&] { return a->black.load(); }), "Den output 1");
        pump(100);
        CHECK(!b->black && !a->freeze && !a->testPattern, "Den output 1 KHONG dung output 2 va khong dung Dung hinh/Mau cua no");
        CHECK(http(port, "POST", "/api/outputs/1/black", R"J({"on":true})J").status == 200, "Den lap lai");
        pump(100);
        CHECK(a->black, "Den bat lap lai van la bat (khong toggle)");
        http(port, "POST", "/api/outputs/1/black", "{}");
        CHECK(waitFor([&] { return !a->black.load(); }), "khong tham so = toggle ve tat");
        CHECK(http(port, "POST", "/api/outputs/2/freeze", R"J({"on":true})J").status == 200 && waitFor([&] { return b->freeze.load(); }) && !a->freeze, "Dung hinh output 2 rieng");
        CHECK(http(port, "POST", "/api/outputs/2/freeze", R"J({"on":false})J").status == 200 && waitFor([&] { return !b->freeze.load(); }), "bo Dung hinh");
        CHECK(http(port, "POST", "/api/outputs/2/pattern", R"J({"on":true})J").status == 200 && waitFor([&] { return b->testPattern.load(); }) && !a->testPattern, "Mau thu output 2");
        http(port, "POST", "/api/outputs/2/pattern", R"J({"on":false})J");
        CHECK(http(port, "POST", "/api/outputs/3/black").status == 404 && http(port, "POST", "/api/outputs/0/freeze").status == 404 && http(port, "POST", "/api/outputs/-1/pattern").status == 404, "output 3/0/-1 -> 404");
        CHECK(http(port, "POST", "/api/outputs/x/black").status == 400, "output 'x' -> 400");
        CHECK(http(port, "POST", "/api/outputs/1/black", R"J({"on":1})J").status == 400, "on=1 (so, khong phai bool) -> 400");
        CHECK(http(port, "POST", "/api/black-all", R"J({"on":true})J").status == 200 && waitFor([&] { return a->black.load() && b->black.load(); }), "BLACK ALL: ca hai output den");
        CHECK(http(port, "POST", "/api/black-all", "{}").status == 200 && waitFor([&] { return !a->black.load() && !b->black.load(); }), "BLACK ALL toggle: tra hinh");
        CHECK(http(port, "POST", "/api/outputs/1/black", R"J({"on":true})J").status == 200 && waitFor([&] { return a->black.load(); }), "den output 1 (de thu trong state)");
        const QJsonObject j = http(port, "GET", "/api/state").json();
        CHECK(j["outputs"].toArray().at(0).toObject()["black"].toBool() && !j["outputs"].toArray().at(1).toObject()["black"].toBool(), "state phan anh Den tung output");
        http(port, "POST", "/api/outputs/1/black", R"J({"on":false})J");
        waitFor([&] { return !a->black.load(); });
    }

    // ================= Scene: phat / tam dung / tua =================
    {
        auto sc = p.scenes[2];
        CHECK(http(port, "POST", "/api/scenes/3/play").status == 200 && waitFor([&] { return sc->clock.playing.load(); }), "scene 3 play");
        waitFor([&] { return sc->clock.time > 0.1; });
        CHECK(http(port, "POST", "/api/scenes/3/pause").status == 200 && waitFor([&] { return !sc->clock.playing.load(); }), "scene 3 pause");
        CHECK(http(port, "POST", "/api/scenes/3/seek", R"J({"time":5.5})J").status == 200 && waitFor([&] { return std::abs(sc->clock.time.load() - 5.5) < 0.01; }), "scene 3 seek 5,5 s");
        CHECK(http(port, "POST", "/api/scenes/3/seek", R"J({"time":9999})J").status == 200 && waitFor([&] { return sc->clock.time.load() <= 20.0 + 1e-6 && sc->clock.time.load() > 19.0; }), "seek vuot thoi luong bi kep ve cuoi");
        CHECK(http(port, "POST", "/api/scenes/3/seek", R"J({"time":-1})J").status == 400, "seek am -> 400");
        CHECK(http(port, "POST", "/api/scenes/3/seek", R"J({"time":"x"})J").status == 400, "seek chuoi -> 400");
        CHECK(http(port, "POST", "/api/scenes/3/seek", "{}").status == 400, "seek thieu time -> 400");
        CHECK(http(port, "POST", "/api/scenes/3/stop").status == 200 && waitFor([&] { return sc->clock.time.load() == 0.0 && !sc->clock.playing.load(); }), "scene 3 stop ve 0");
        CHECK(http(port, "POST", "/api/scenes/4/play").status == 404 && http(port, "POST", "/api/scenes/0/stop").status == 404, "scene 4/0 -> 404");
    }

    // ================= Origin / Host (chong CSRF va DNS rebinding) =================
    {
        const Resp csrf = http(port, "POST", "/api/black-all", R"J({"on":true})J", {}, {"Origin: http://evil.example"});
        pump(150);
        CHECK(csrf.status == 403 && !engine.output(0)->black, "POST co Origin la (CSRF) -> 403 va KHONG thuc thi");
        CHECK(http(port, "POST", "/api/black-all", "{}", {}, {"Origin: null"}).status == 403, "Origin: null -> 403");
        CHECK(http(port, "GET", "/api/state", {}, {}, {"Origin: http://127.0.0.1:" + QByteArray::number(port)}).status == 200, "Origin cung nguon -> 200");
        const Resp rebind = rawRequest(port, "GET /api/state HTTP/1.1\r\nHost: evil.example\r\n\r\n");
        CHECK(rebind.status == 403 && rebind.json()["error"].toString() == "bad_host", "Host la (DNS rebinding) -> 403");
        CHECK(rawRequest(port, "GET /api/state HTTP/1.1\r\nHost: localhost:" + QByteArray::number(port) + "\r\n\r\n").status == 200, "Host: localhost -> 200");
        CHECK(rawRequest(port, "GET /api/state HTTP/1.0\r\n\r\n").status == 200, "HTTP/1.0 khong Host -> 200");
        CHECK(http(port, "OPTIONS", "/api/state").status == 405 && http(port, "OPTIONS", "/api/state").header("Access-Control-Allow-Origin").isEmpty(), "OPTIONS -> 405 va KHONG co header CORS");
    }

    // ================= Token tren loopback =================
    {
        cs.token = "token-bi-mat-123";
        CHECK(svc.apply(cs).isEmpty(), "bat lai voi token (loopback)");
        const quint16 p2 = svc.httpPort();
        api.resetLogLimiter();
        g_log.clear();
        const int rej0 = api.rejectedCount();
        CHECK(http(p2, "GET", "/api/state").status == 401, "khong token -> 401");
        const Resp r401 = http(p2, "GET", "/api/state");
        CHECK(r401.header("WWW-Authenticate") == "Bearer", "401 kem WWW-Authenticate: Bearer");
        CHECK(http(p2, "GET", "/api/state", {}, "sai").status == 401, "token sai -> 401");
        CHECK(http(p2, "GET", "/api/state", {}, "token-bi-mat-124").status == 401, "token sai 1 ky tu cuoi -> 401");
        CHECK(http(p2, "GET", "/api/state", {}, "token-bi-mat-12").status == 401, "token la tien to cua token that -> 401");
        CHECK(http(p2, "GET", "/api/state", {}, "token-bi-mat-1234").status == 401, "token dai hon -> 401");
        CHECK(http(p2, "POST", "/api/black-all", "{}").status == 401 && !engine.output(0)->black, "lenh khong token bi tu choi va khong thuc thi");
        CHECK(rawRequest(p2, "GET /api/state HTTP/1.1\r\nHost: 127.0.0.1\r\nAuthorization: Basic dG9rZW4=\r\n\r\n").status == 401, "Authorization kieu Basic -> 401");
        CHECK(rawRequest(p2, "GET /api/state HTTP/1.1\r\nHost: 127.0.0.1\r\nAuthorization: bearer token-bi-mat-123\r\n\r\n").status == 200, "scheme 'bearer' khong phan biet hoa/thuong -> 200");
        CHECK(http(p2, "GET", "/api/state", {}, "token-bi-mat-123").status == 200, "dung token -> 200");
        CHECK(http(p2, "GET", "/api/events", {}, {}).status == 401, "luong SSE cung can token");
        CHECK(api.rejectedCount() > rej0 && logHas("TỪ CHỐI") && logHas("unauthorized"), "moi lenh bi tu choi duoc dem va ghi log");
        CHECK(http(p2, "GET", "/").status == 200, "trang HTML (khong chua bi mat) van mo duoc de nhap token");
        cs.token.clear();
        svc.apply(cs);
    }
    const quint16 hp = svc.httpPort();

    // ================= SSE =================
    {
        QTcpSocket s;
        s.connectToHost(QHostAddress::LocalHost, hp);
        waitFor([&] { return s.state() == QAbstractSocket::ConnectedState; });
        s.write("GET /api/events HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
        QByteArray got;
        waitFor([&] { got += s.readAll(); return got.contains("event: state") && got.contains("\n\n"); }, 3000);
        CHECK(got.startsWith("HTTP/1.1 200") && got.contains("text/event-stream") && got.contains("event: state\ndata: {"), "SSE: tieu de dung + su kien state dau tien");
        CHECK(svc.http().sseClients() == 1, "SSE: 1 client");
        got.clear();
        http(hp, "POST", "/api/outputs/2/black", R"J({"on":true})J");
        const bool pushed = waitFor([&] { got += s.readAll(); return got.contains("\"black\":true"); }, 3000);
        CHECK(pushed, "SSE: day trang thai moi khi Output 2 den (khong can hoi lien tuc)");
        http(hp, "POST", "/api/outputs/2/black", R"J({"on":false})J");
        // tai nhieu client toi da
        auto lim = svc.http().limits(); auto l2 = lim; l2.maxSseClients = 2; svc.http().setLimits(l2);
        QTcpSocket s2, s3;
        for (QTcpSocket* x : {&s2, &s3}) { x->connectToHost(QHostAddress::LocalHost, hp); waitFor([&] { return x->state() == QAbstractSocket::ConnectedState; }); x->write("GET /api/events HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"); pump(100); }
        QByteArray g3;
        waitFor([&] { g3 += s3.readAll(); return s3.state() == QAbstractSocket::UnconnectedState; }, 2000);
        g3 += s3.readAll();
        CHECK(svc.http().sseClients() == 2 && g3.contains("503"), "SSE: client thu 3 vuot gioi han -> 503");
        svc.http().setLimits(lim);
        s.close();
        waitFor([&] { return svc.http().sseClients() == 1; });
        s2.close();
        waitFor([&] { return svc.http().sseClients() == 0; });
        CHECK(svc.http().sseClients() == 0, "SSE: client dong thi duoc don dep");
        QTcpSocket noise;
        noise.connectToHost(QHostAddress::LocalHost, hp);
        waitFor([&] { return noise.state() == QAbstractSocket::ConnectedState; });
        noise.write("GET /api/events HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
        pump(100);
        noise.write(QByteArray(100000, 'x'));                                  // client SSE gui rac: bo qua, khong crash
        pump(150);
        CHECK(http(hp, "GET", "/api/state").status == 200, "SSE: client gui rac len luong khong lam hong may chu");
    }

    // ================= Khoa van hanh (tuyen SUA cau hinh bi chan; lenh live van chay) =================
    {
        int editCalls = 0;
        api.addRoute("POST", "/api/test/edit", true, [&](const QStringList&, const QJsonObject&) { ++editCalls; return CmdResult::success(); });
        CHECK(http(hp, "POST", "/api/test/edit").status == 200 && editCalls == 1, "chua khoa: lenh sua cau hinh chay");
        locked = true;
        api.resetLogLimiter();
        g_log.clear();
        const Resp r = http(hp, "POST", "/api/test/edit");
        CHECK(r.status == 423 && r.json()["error"].toString() == "locked" && editCalls == 1, "dang khoa: lenh sua cau hinh bi tu choi 423, khong chay");
        CHECK(logHas("TỪ CHỐI") && logHas("locked"), "dang khoa: lenh bi tu choi duoc ghi log");
        CHECK(http(hp, "GET", "/api/state").json()["locked"].toBool(), "state bao 'locked': true");
        CHECK(http(hp, "POST", "/api/black-all", R"J({"on":true})J").status == 200 && waitFor([&] { return engine.output(0)->black.load(); }), "dang khoa: BLACK ALL (live) van chay");
        http(hp, "POST", "/api/black-all", R"J({"on":false})J");
        CHECK(http(hp, "POST", "/api/channels/1/preview", R"J({"scene":2})J").status == 200, "dang khoa: dat Preview (live) van chay");
        CHECK(http(hp, "POST", "/api/scenes/1/play").status == 200, "dang khoa: phat timeline (live) van chay");
        http(hp, "POST", "/api/scenes/1/stop");
        locked = false;
        CHECK(http(hp, "POST", "/api/test/edit").status == 200 && editCalls == 2, "mo khoa: lenh sua cau hinh chay lai");
        waitFor([&] { return !engine.output(0)->black.load(); });
    }

    // ================= OSC qua UDP =================
    {
        cs.httpEnabled = false;
        cs.oscEnabled = true; cs.oscPort = freeUdpPort();
        g_log.clear();
        CHECK(svc.apply(cs).isEmpty() && svc.oscRunning() && !svc.httpRunning(), "bat OSC (tat HTTP)");
        CHECK(logHas("OSC đã BẬT") && logHas("HTTP đã TẮT"), "log: bat OSC va tat HTTP");
        QUdpSocket u;
        auto send = [&](const QByteArray& b) { u.writeDatagram(b, QHostAddress::LocalHost, svc.oscPort()); };
        send(buildOscMessage("/lumacast/channel/1/preview", {3}));
        CHECK(waitFor([&] { return ch1->preview == 2; }), "OSC: /lumacast/channel/1/preview 3");
        const int pg = ch1->program;
        pump(100);
        CHECK(ch1->program == pg, "OSC: dat Preview khong doi Program");
        send(buildOscMessage("/lumacast/channel/1/cut"));
        CHECK(waitFor([&] { return ch1->program == 2; }), "OSC: cut hoan doi Program/Preview");
        send(buildOscMessage("/lumacast/output/1/black", {1}));
        CHECK(waitFor([&] { return engine.output(0)->black.load(); }) && !engine.output(1)->black, "OSC: output/1/black 1 (chi output 1)");
        send(buildOscMessage("/lumacast/output/1/black", {0}));
        CHECK(waitFor([&] { return !engine.output(0)->black.load(); }), "OSC: output/1/black 0");
        send(buildOscMessage("/lumacast/output/2/freeze", {true}));
        CHECK(waitFor([&] { return engine.output(1)->freeze.load(); }), "OSC: output/2/freeze T");
        send(buildOscMessage("/lumacast/output/2/freeze", {false}));
        send(buildOscMessage("/lumacast/output/2/pattern"));
        CHECK(waitFor([&] { return engine.output(1)->testPattern.load(); }), "OSC: pattern khong tham so = toggle");
        send(buildOscMessage("/lumacast/output/2/pattern"));
        send(buildOscMessage("/lumacast/blackall", {1}));
        CHECK(waitFor([&] { return engine.output(0)->black.load() && engine.output(1)->black.load(); }), "OSC: blackall 1");
        send(buildOscMessage("/lumacast/blackall", {0}));
        waitFor([&] { return !engine.output(0)->black.load(); });
        send(buildOscMessage("/lumacast/channel/2/ftb", {1}));
        CHECK(waitFor([&] { return ch2->ftbTarget.load(); }), "OSC: ftb 1");
        send(buildOscMessage("/lumacast/channel/2/ftb", {0}));
        waitFor([&] { return !ch2->ftbTarget.load(); });
        send(buildOscMessage("/lumacast/channel/1/take", {"fade", 0.2}));
        CHECK(waitFor([&] { return ch1->inTransition(); }, 1500) && waitFor([&] { return !ch1->inTransition(); }, 3000), "OSC: take fade 0,2 s");
        send(buildOscMessage("/lumacast/scene/2/pause"));
        send(buildOscMessage("/lumacast/scene/2/seek", {3.0}));
        CHECK(waitFor([&] { return std::abs(p.scenes[1]->clock.time.load() - 3.0) < 0.01; }), "OSC: scene/2/seek 3.0");
        send(buildOscBundle({buildOscMessage("/lumacast/output/1/black", {1}), buildOscMessage("/lumacast/output/2/black", {1})}));
        CHECK(waitFor([&] { return engine.output(0)->black.load() && engine.output(1)->black.load(); }), "OSC: bundle 2 lenh den cung luc");
        send(buildOscBundle({buildOscMessage("/lumacast/output/1/black", {0}), buildOscMessage("/lumacast/output/2/black", {0})}));
        waitFor([&] { return !engine.output(0)->black.load() && !engine.output(1)->black.load(); });

        // duong loi / doc hai
        const quint64 acc0 = svc.osc().accepted(), drop0 = svc.osc().dropped();
        const int rej0 = api.rejectedCount();
        pump(300);
        const int pg0 = ch1->program, pv0 = ch1->preview;
        api.resetLogLimiter();
        g_log.clear();
        send(QByteArray("rac rac rac rac!"));
        send(buildOscMessage("/lumacast/channel/1/preview", {3}).left(20));
        send(buildOscMessage("/lumacast/channel/9/cut"));
        send(buildOscMessage("/lumacast/channel/1/preview", {99}));
        send(buildOscMessage("/lumacast/channel/1/preview", {"ba"}));
        send(buildOscMessage("/lumacast/output/7/black", {1}));
        send(buildOscMessage("/khac/channel/1/cut"));
        send(buildOscMessage("/lumacast/channel/x/cut"));
        send(buildOscMessage("/lumacast/output/1/black", {"co"}));
        send(QByteArray(5000, 'a'));
        send(QByteArray());
        waitFor([&] { return api.rejectedCount() >= rej0 + 10; }, 2000);
        CHECK(svc.osc().dropped() >= drop0 + 4 && svc.osc().accepted() >= acc0 + 5, "OSC: goi hong bi bo, goi hop le nhung sai dia chi/chi so duoc dem");
        CHECK(api.rejectedCount() >= rej0 + 10 && logHas("gói OSC hỏng") && logHas("TỪ CHỐI"), "OSC: moi goi/lenh bi tu choi duoc ghi log");
        CHECK(ch1->program == pg0 && ch1->preview == pv0 && !engine.output(0)->black && !engine.output(1)->black, "OSC: lenh sai/hong khong lam doi trang thai");
        // lu lut
        for (int i = 0; i < 2000; ++i) send(buildOscMessage("/lumacast/channel/1/preview", {1 + i % 3}));
        pump(300);
        CHECK(svc.oscRunning() && waitFor([&] { return svc.osc().accepted() > acc0 + 100; }), "OSC: 2000 goi lien tiep khong lam do (da nhan >100)");
        QUdpSocket tcpPort;
        svc.stop();
        CHECK(!svc.oscRunning() && logHas("OSC đã TẮT"), "tat OSC + log");
    }

    // ================= OSC LAN/loopback =================
    {
        ControlSettings s; s.oscEnabled = true; s.oscPort = freeUdpPort();
        g_log.clear();
        svc.apply(s);
        CHECK(logHas("chỉ máy này"), "OSC mac dinh chi nghe may nay (co ghi log)");
        s.oscLan = true; s.oscPort = freeUdpPort();
        g_log.clear();
        svc.apply(s);
        CHECK(logHas("KHÔNG có xác thực") && svc.oscRunning(), "OSC LAN: bat duoc nhung log canh bao khong xac thuc");
        svc.stop();
    }

    // ================= don dep =================
    svc.stop();
    CHECK(!svc.httpRunning() && !svc.oscRunning(), "tat het dich vu");
    engine.stopEngine();
    qInstallMessageHandler(nullptr);
    std::cout << (g_fail ? "THAT BAI: " : "TAT CA QUA ") << g_fail << " loi\n";
    return g_fail ? 1 : 0;
}
