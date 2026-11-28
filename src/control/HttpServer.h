#pragma once
#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QMap>
#include <QObject>
#include <QPair>
#include <QTcpServer>
#include <QTimer>
#include <functional>

class QTcpSocket;

namespace lc {

struct HttpRequest {
    QByteArray method;                       // "GET", "POST"... (chu hoa)
    QByteArray path;                         // da giai ma %xx, khong gom query
    QByteArray query;                        // phan sau '?', chua giai ma
    QMap<QByteArray, QByteArray> headers;    // khoa viet thuong
    QByteArray body;
    QHostAddress peer;
    QByteArray header(const QByteArray& lowerName) const { return headers.value(lowerName); }
};

struct HttpResponse {
    int status = 200;
    QByteArray contentType = "application/json; charset=utf-8";
    QByteArray body;
    QList<QPair<QByteArray, QByteArray>> headers;   // header bo sung
    bool sse = false;                               // true: giu ket noi mo (Server-Sent Events), body = su kien dau tien

    static HttpResponse json(int status, const QByteArray& body) { HttpResponse r; r.status = status; r.body = body; return r; }
};

// May chu HTTP/1.1 toi thieu (chi QtNetwork): moi ket noi mot yeu cau roi dong ("Connection: close"),
// ngoai tru luong SSE (GET dai han) do handler yeu cau. Co gioi han kich thuoc, so ket noi, thoi gian cho,
// va tu choi moi yeu cau sai dinh dang bang ma 4xx/5xx dung. Chay tren thread cua QObject (UI thread).
class HttpServer : public QObject {
    Q_OBJECT
public:
    struct Limits {
        int maxHeaderBytes = 8 * 1024;        // ca dong yeu cau + header
        int maxBodyBytes = 16 * 1024;
        int maxHeaders = 64;
        int maxTargetBytes = 2048;
        int maxConnections = 32;
        int maxSseClients = 8;
        int requestTimeoutMs = 5000;          // tu luc ket noi den khi nhan du yeu cau
        int writeTimeoutMs = 5000;            // tu luc phan hoi xong den khi dong
        int sseHeartbeatMs = 15000;
        int sseMaxBacklogBytes = 256 * 1024;  // client doc qua cham -> ngat
    };
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    explicit HttpServer(QObject* parent = nullptr);
    ~HttpServer() override;
    void setHandler(Handler h) { handler_ = std::move(h); }
    void setLimits(const Limits& l) { limits_ = l; }
    const Limits& limits() const { return limits_; }

    bool listen(const QHostAddress& addr, quint16 port);
    void close();
    bool isListening() const { return server_.isListening(); }
    quint16 port() const { return server_.serverPort(); }
    QString errorString() const { return server_.errorString(); }

    // Gui mot su kien SSE toi moi client dang mo luong.
    void broadcastEvent(const QByteArray& event, const QByteArray& data);
    int sseClients() const { return sse_.size(); }
    int connections() const { return conns_.size(); }

    static QByteArray reasonPhrase(int status);

private:
    struct Conn {
        QByteArray buf;
        QElapsedTimer age;
        bool responded = false;
        bool sse = false;
    };
    void onNewConnection();
    void onReadyRead(QTcpSocket* s);
    void dropConn(QTcpSocket* s);
    void sweep();
    void respond(QTcpSocket* s, const HttpResponse& r, bool head = false);
    void fail(QTcpSocket* s, int status, const QByteArray& code, const QString& message);
    bool parseAndDispatch(QTcpSocket* s, Conn& c);

    QTcpServer server_;
    Limits limits_;
    Handler handler_;
    QHash<QTcpSocket*, Conn> conns_;
    QList<QTcpSocket*> sse_;
    QTimer sweepTimer_;
    QElapsedTimer sinceBeat_;
};

} // namespace lc
