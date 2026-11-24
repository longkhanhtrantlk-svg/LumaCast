#include "ControlService.h"
#include "core/Features.h"
#include <QNetworkDatagram>
#include <QRandomGenerator>

namespace lc {

// ---------------------------------------------------------------- ControlSettings

ControlSettings ControlSettings::load(QSettings& st) {
    ControlSettings s;
    s.httpEnabled = st.value("control/http/enabled", false).toBool();
    s.httpPort = quint16(std::clamp(st.value("control/http/port", 8088).toInt(), 1, 65535));
    s.httpLan = st.value("control/http/lan", false).toBool();
    s.token = st.value("control/http/token").toString();
    s.oscEnabled = st.value("control/osc/enabled", false).toBool();
    s.oscPort = quint16(std::clamp(st.value("control/osc/port", 9000).toInt(), 1, 65535));
    s.oscLan = st.value("control/osc/lan", false).toBool();
    return s;
}

void ControlSettings::save(QSettings& st) const {
    st.setValue("control/http/enabled", httpEnabled);
    st.setValue("control/http/port", int(httpPort));
    st.setValue("control/http/lan", httpLan);
    st.setValue("control/http/token", token);
    st.setValue("control/osc/enabled", oscEnabled);
    st.setValue("control/osc/port", int(oscPort));
    st.setValue("control/osc/lan", oscLan);
}

QString ControlSettings::validate() const {
    if (httpEnabled && oscEnabled && httpPort == oscPort) { /* TCP va UDP khac nhau nen trung so cong van duoc */ }
    if (httpEnabled && httpPort == 0) return "Cổng HTTP không hợp lệ";
    if (oscEnabled && oscPort == 0) return "Cổng OSC không hợp lệ";
    if (httpEnabled && httpLan && token.size() < kMinLanTokenLen)
        return QString("Nghe trên mạng LAN bắt buộc có token dài ít nhất %1 ký tự").arg(kMinLanTokenLen);
    return {};
}

QString ControlSettings::generateToken() {
    static const char kAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
    QString t;
    for (int i = 0; i < 32; ++i) t += QChar(kAlphabet[QRandomGenerator::system()->bounded(int(sizeof(kAlphabet) - 1))]);
    return t;
}

// ---------------------------------------------------------------- OscServer

OscServer::OscServer(ControlApi& api, QObject* parent) : QObject(parent), api_(api) {
    connect(&sock_, &QUdpSocket::readyRead, this, &OscServer::readPending);
}

bool OscServer::listen(const QHostAddress& addr, quint16 port) {
    close();
    return sock_.bind(addr, port, QUdpSocket::DontShareAddress);
}

void OscServer::close() { sock_.close(); }

void OscServer::readPending() {
    // Gioi han so goi xu ly moi lan de luong UDP dong loat khong lam do giao dien; phan con lai xu ly o vong su kien ke tiep.
    int budget = 256;
    while (budget-- > 0 && sock_.hasPendingDatagrams()) {
        const QNetworkDatagram dg = sock_.receiveDatagram(kOscMaxPacket + 1);
        const QByteArray data = dg.data();
        QList<OscMessage> msgs;
        QString err;
        if (!parseOscPacket(data, msgs, &err)) {
            ++dropped_;
            api_.noteRejected(dg.senderAddress().toString(), "gói OSC hỏng: " + err);
            continue;
        }
        ++accepted_;
        for (const OscMessage& m : msgs) {
            const CmdResult r = api_.handleOsc(m);
            if (!r.ok()) api_.noteRejected(dg.senderAddress().toString(), QString("OSC %1 → %2: %3").arg(m.address.left(80), r.code, r.message));
        }
    }
    if (sock_.hasPendingDatagrams()) QMetaObject::invokeMethod(this, &OscServer::readPending, Qt::QueuedConnection);
}

// ---------------------------------------------------------------- ControlService

ControlService::ControlService(ControlApi& api, QObject* parent) : QObject(parent), api_(api), http_(this), osc_(api, this) {
    http_.setHandler([this](const HttpRequest& r) { return api_.handleHttp(r); });
    connect(&stateTimer_, &QTimer::timeout, this, [this] { pushState(); });
}

void ControlService::pushState() {
    if (http_.sseClients() == 0) return;
    const QByteArray s = api_.stateJson();
    if (s == lastState_) return;
    lastState_ = s;
    http_.broadcastEvent("state", s);
}

void ControlService::stop() {
    const bool wasHttp = http_.isListening(), wasOsc = osc_.isListening();
    stateTimer_.stop();
    http_.close();
    osc_.close();
    if (wasHttp) qInfo().noquote() << "Điều khiển ngoài: HTTP đã TẮT";
    if (wasOsc) qInfo().noquote() << "Điều khiển ngoài: OSC đã TẮT";
    if (wasHttp || wasOsc) emit statusChanged();
}

QString ControlService::apply(const ControlSettings& sIn) {
    stop();
    ControlSettings s = sIn;
    if (!Entitlements::current().externalControl && (s.httpEnabled || s.oscEnabled)) {     // goi khong co dieu khien ngoai: khong mo cong
        qWarning().noquote() << "Điều khiển ngoài không có trong gói giấy phép hiện tại: không mở HTTP/OSC";
        s.httpEnabled = s.oscEnabled = false;
        cur_ = s;
        emit statusChanged();
        return "Điều khiển ngoài không có trong gói giấy phép hiện tại";
    }
    cur_ = s;
    QStringList errors;
    api_.setToken(s.token.toUtf8());
    const QString bad = s.validate();
    if (!bad.isEmpty()) {
        qWarning().noquote() << "Điều khiển ngoài: từ chối cấu hình —" << bad;
        errors << bad;
    }
    if (s.httpEnabled && !(s.httpLan && s.token.size() < ControlSettings::kMinLanTokenLen)) {
        api_.setRequireToken(s.httpLan);                     // LAN: khong co token = tu choi het
        api_.setStrictHost(!s.httpLan);                      // loopback: kiem tra Host (chong DNS rebinding)
        const QHostAddress addr = s.httpLan ? QHostAddress(QHostAddress::AnyIPv4) : QHostAddress(QHostAddress::LocalHost);
        if (http_.listen(addr, s.httpPort)) {
            qInfo().noquote() << QString("Điều khiển ngoài: HTTP đã BẬT tại %1:%2 (%3, %4)")
                                     .arg(s.httpLan ? "0.0.0.0" : "127.0.0.1").arg(http_.port())
                                     .arg(s.httpLan ? "mạng LAN" : "chỉ máy này")
                                     .arg(s.token.isEmpty() ? "không token" : "có token");
            stateTimer_.start(100);
            lastState_.clear();
        } else {
            const QString e = QString("Không mở được cổng HTTP %1: %2").arg(s.httpPort).arg(http_.errorString());
            qWarning().noquote() << "Điều khiển ngoài:" << e;
            errors << e;
        }
    }
    if (s.oscEnabled) {
        const QHostAddress addr = s.oscLan ? QHostAddress(QHostAddress::AnyIPv4) : QHostAddress(QHostAddress::LocalHost);
        if (osc_.listen(addr, s.oscPort)) {
            qInfo().noquote() << QString("Điều khiển ngoài: OSC đã BẬT tại %1:%2/udp (%3)")
                                     .arg(s.oscLan ? "0.0.0.0" : "127.0.0.1").arg(osc_.port())
                                     .arg(s.oscLan ? "mạng LAN, KHÔNG có xác thực" : "chỉ máy này");
        } else {
            const QString e = QString("Không mở được cổng OSC %1: %2").arg(s.oscPort).arg(osc_.errorString());
            qWarning().noquote() << "Điều khiển ngoài:" << e;
            errors << e;
        }
    }
    emit statusChanged();
    return errors.join("\n");
}

} // namespace lc
