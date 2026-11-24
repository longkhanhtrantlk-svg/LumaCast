#pragma once
#include "control/ControlApi.h"
#include "control/HttpServer.h"
#include <QHostAddress>
#include <QObject>
#include <QSettings>
#include <QUdpSocket>

namespace lc {

// Cau hinh dieu khien ngoai (luu bang QSettings, theo may; token luu dang chu thuong trong cau hinh nguoi dung).
struct ControlSettings {
    bool httpEnabled = false;
    quint16 httpPort = 8088;
    bool httpLan = false;               // false: chi 127.0.0.1. true: nghe moi giao dien mang va BAT BUOC co token
    QString token;
    bool oscEnabled = false;
    quint16 oscPort = 9000;
    bool oscLan = false;                // OSC khong co xac thuc: mac dinh chi 127.0.0.1

    static constexpr int kMinLanTokenLen = 16;
    static ControlSettings load(QSettings& st);
    void save(QSettings& st) const;
    // Rong neu hop le; nguoc lai la mo ta loi tieng Viet.
    QString validate() const;
    static QString generateToken();     // 32 ky tu ngau nhien (CSPRNG he dieu hanh)
};

// May chu OSC (UDP). Goi hong bi bo qua (ghi log co gioi han), khong crash.
class OscServer : public QObject {
    Q_OBJECT
public:
    explicit OscServer(ControlApi& api, QObject* parent = nullptr);
    bool listen(const QHostAddress& addr, quint16 port);
    void close();
    bool isListening() const { return sock_.state() == QAbstractSocket::BoundState; }
    quint16 port() const { return sock_.localPort(); }
    QString errorString() const { return sock_.errorString(); }
    quint64 accepted() const { return accepted_; }
    quint64 dropped() const { return dropped_; }
private:
    void readPending();
    ControlApi& api_;
    QUdpSocket sock_;
    quint64 accepted_ = 0, dropped_ = 0;
};

// Quan ly vong doi HTTP + OSC theo cau hinh; ghi log moi lan bat/tat. Day trang thai SSE khi co thay doi.
class ControlService : public QObject {
    Q_OBJECT
public:
    ControlService(ControlApi& api, QObject* parent = nullptr);
    // Ap dung cau hinh: dung het roi bat lai theo cai dat. Tra ve thong bao loi (rong = tot).
    QString apply(const ControlSettings& s);
    void stop();
    const ControlSettings& settings() const { return cur_; }
    bool httpRunning() const { return http_.isListening(); }
    quint16 httpPort() const { return http_.port(); }
    bool oscRunning() const { return osc_.isListening(); }
    quint16 oscPort() const { return osc_.port(); }
    HttpServer& http() { return http_; }
    OscServer& osc() { return osc_; }
    ControlApi& api() { return api_; }

signals:
    void statusChanged();

private:
    void pushState();
    ControlApi& api_;
    HttpServer http_;
    OscServer osc_;
    ControlSettings cur_;
    QTimer stateTimer_;
    QByteArray lastState_;
};

} // namespace lc
