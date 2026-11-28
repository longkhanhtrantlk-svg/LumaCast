#include "HttpServer.h"
#include "core/I18n.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QUrl>
#include <algorithm>

namespace lc {

namespace {
bool isTokenChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || QByteArray("!#$%&'*+-.^_`|~").contains(c);
}
bool isToken(const QByteArray& s) {
    if (s.isEmpty()) return false;
    for (char c : s) if (!isTokenChar(c)) return false;
    return true;
}
bool hasControl(const QByteArray& s) {
    for (char c : s) if (uchar(c) < 0x20 || uchar(c) == 0x7f) return true;
    return false;
}
} // namespace

QByteArray HttpServer::reasonPhrase(int s) {
    switch (s) {
    case 200: return "OK";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 408: return "Request Timeout";
    case 409: return "Conflict";
    case 411: return "Length Required";
    case 413: return "Payload Too Large";
    case 414: return "URI Too Long";
    case 423: return "Locked";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 503: return "Service Unavailable";
    case 505: return "HTTP Version Not Supported";
    }
    return "Status";
}

HttpServer::HttpServer(QObject* parent) : QObject(parent) {
    connect(&server_, &QTcpServer::newConnection, this, &HttpServer::onNewConnection);
    connect(&sweepTimer_, &QTimer::timeout, this, &HttpServer::sweep);
    server_.setMaxPendingConnections(64);
}

HttpServer::~HttpServer() { close(); }

bool HttpServer::listen(const QHostAddress& addr, quint16 port) {
    close();
    if (!server_.listen(addr, port)) return false;
    sweepTimer_.start(200);
    sinceBeat_.start();
    return true;
}

void HttpServer::close() {
    sweepTimer_.stop();
    server_.close();
    const auto socks = conns_.keys();
    for (QTcpSocket* s : socks) dropConn(s);
    conns_.clear();
    sse_.clear();
}

void HttpServer::dropConn(QTcpSocket* s) {
    conns_.remove(s);
    sse_.removeAll(s);
    s->disconnect(this);
    s->abort();
    s->deleteLater();
}

void HttpServer::onNewConnection() {
    while (QTcpSocket* s = server_.nextPendingConnection()) {
        if (conns_.size() >= limits_.maxConnections) {                    // qua tai: tu choi nhanh, khong giu tai nguyen
            s->write("HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
            s->disconnectFromHost();
            connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            continue;
        }
        Conn c;
        c.age.start();
        conns_.insert(s, c);
        connect(s, &QTcpSocket::readyRead, this, [this, s] { onReadyRead(s); });
        connect(s, &QTcpSocket::disconnected, this, [this, s] { dropConn(s); });
        connect(s, &QTcpSocket::errorOccurred, this, [this, s](QAbstractSocket::SocketError) { dropConn(s); });
        if (s->bytesAvailable() > 0) onReadyRead(s);
    }
}

void HttpServer::sweep() {
    const auto socks = conns_.keys();
    for (QTcpSocket* s : socks) {
        auto it = conns_.find(s);
        if (it == conns_.end()) continue;
        Conn& c = it.value();
        if (c.sse) {
            if (s->bytesToWrite() > limits_.sseMaxBacklogBytes) dropConn(s);
            continue;
        }
        if (c.responded) {
            if (c.age.elapsed() > limits_.writeTimeoutMs) dropConn(s);          // phan hoi khong gui het (client ngung doc)
        } else if (c.age.elapsed() > limits_.requestTimeoutMs) {                // slowloris / yeu cau do dang
            fail(s, 408, "request_timeout", LC_TR("Hết thời gian chờ yêu cầu"));
        }
    }
    if (!sse_.isEmpty() && sinceBeat_.elapsed() >= limits_.sseHeartbeatMs) {
        sinceBeat_.restart();
        for (QTcpSocket* s : QList<QTcpSocket*>(sse_)) s->write(": ping\n\n");
    }
}

void HttpServer::fail(QTcpSocket* s, int status, const QByteArray& code, const QString& message) {
    QJsonObject o;
    o["error"] = QString::fromUtf8(code);
    o["message"] = message;
    respond(s, HttpResponse::json(status, QJsonDocument(o).toJson(QJsonDocument::Compact)));
}

void HttpServer::respond(QTcpSocket* s, const HttpResponse& r, bool head) {
    auto it = conns_.find(s);
    if (it == conns_.end() || it->responded) return;
    it->responded = true;
    it->age.restart();
    QByteArray out = "HTTP/1.1 " + QByteArray::number(r.status) + " " + reasonPhrase(r.status) + "\r\n";
    out += "Content-Type: " + r.contentType + "\r\n";
    out += "Content-Length: " + QByteArray::number(r.body.size()) + "\r\n";
    out += "Connection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n";
    for (const auto& h : r.headers) {
        if (hasControl(h.first) || hasControl(h.second)) continue;                 // chong chen header (CRLF injection)
        out += h.first + ": " + h.second + "\r\n";
    }
    out += "\r\n";
    if (!head) out += r.body;
    s->write(out);
    s->disconnectFromHost();                                                        // dong sau khi gui het
}

bool HttpServer::parseAndDispatch(QTcpSocket* s, Conn& c) {
    const int hdrEnd = c.buf.indexOf("\r\n\r\n");
    if (hdrEnd < 0) {
        if (c.buf.size() > limits_.maxHeaderBytes) { fail(s, 431, "header_too_large", LC_TR("Header quá lớn")); }
        return false;                                    // chua du header
    }
    if (hdrEnd + 4 > limits_.maxHeaderBytes) { fail(s, 431, "header_too_large", LC_TR("Header quá lớn")); return false; }

    QList<QByteArray> lines = c.buf.left(hdrEnd + 2).split('\n');     // moi dong ket thuc bang \r\n; phan tu cuoi rong
    if (!lines.isEmpty()) lines.removeLast();
    // dong yeu cau
    QByteArray first = lines.value(0);
    if (!first.endsWith('\r')) { fail(s, 400, "bad_request", LC_TR("Dòng yêu cầu sai định dạng")); return false; }
    first.chop(1);
    const QList<QByteArray> parts = first.split(' ');
    if (parts.size() != 3) { fail(s, 400, "bad_request", LC_TR("Dòng yêu cầu sai định dạng")); return false; }
    HttpRequest req;
    req.method = parts[0];
    if (!isToken(req.method) || req.method.size() > 16) { fail(s, 400, "bad_request", LC_TR("Phương thức không hợp lệ")); return false; }
    const QByteArray target = parts[1];
    if (parts[2] != "HTTP/1.1" && parts[2] != "HTTP/1.0") {
        fail(s, parts[2].startsWith("HTTP/") ? 505 : 400, "bad_version", LC_TR("Chỉ hỗ trợ HTTP/1.0 và 1.1"));
        return false;
    }
    if (target.size() > limits_.maxTargetBytes) { fail(s, 414, "uri_too_long", LC_TR("Đường dẫn quá dài")); return false; }
    if (!target.startsWith('/') || hasControl(target) || target.contains('#')) { fail(s, 400, "bad_request", LC_TR("Đường dẫn không hợp lệ")); return false; }
    const int q = target.indexOf('?');
    const QByteArray rawPath = q >= 0 ? target.left(q) : target;
    req.query = q >= 0 ? target.mid(q + 1) : QByteArray();
    req.path = QUrl::fromPercentEncoding(rawPath).toUtf8();
    if (req.path.contains('\0') || hasControl(req.path) || QUrl::fromPercentEncoding(rawPath).contains(QChar::ReplacementCharacter)) {
        fail(s, 400, "bad_request", LC_TR("Đường dẫn không hợp lệ"));
        return false;
    }
    // header
    if (lines.size() - 1 > limits_.maxHeaders) { fail(s, 431, "too_many_headers", LC_TR("Quá nhiều header")); return false; }
    for (int i = 1; i < lines.size(); ++i) {
        QByteArray ln = lines[i];
        if (!ln.endsWith('\r')) { fail(s, 400, "bad_request", LC_TR("Header sai định dạng")); return false; }
        ln.chop(1);
        const int colon = ln.indexOf(':');
        if (colon <= 0 || ln[0] == ' ' || ln[0] == '\t') { fail(s, 400, "bad_request", LC_TR("Header sai định dạng")); return false; }
        const QByteArray name = ln.left(colon).toLower();
        if (!isToken(name)) { fail(s, 400, "bad_request", LC_TR("Tên header không hợp lệ")); return false; }
        const QByteArray value = ln.mid(colon + 1).trimmed();
        if (hasControl(value)) { fail(s, 400, "bad_request", LC_TR("Giá trị header không hợp lệ")); return false; }
        if (req.headers.contains(name)) {
            if (name == "content-length" && req.headers[name] != value) { fail(s, 400, "bad_request", LC_TR("Content-Length mâu thuẫn")); return false; }
            if (name == "authorization" || name == "host") { fail(s, 400, "bad_request", LC_TR("Header bị lặp")); return false; }
            req.headers[name] += ", " + value;
        } else {
            req.headers.insert(name, value);
        }
    }
    if (req.headers.contains("transfer-encoding")) { fail(s, 501, "not_implemented", LC_TR("Không hỗ trợ Transfer-Encoding")); return false; }
    qint64 len = 0;
    if (req.headers.contains("content-length")) {
        const QByteArray v = req.headers["content-length"];
        bool ok = !v.isEmpty() && v.size() <= 9;
        for (char ch : v) ok = ok && ch >= '0' && ch <= '9';
        if (!ok) { fail(s, 400, "bad_request", LC_TR("Content-Length không hợp lệ")); return false; }
        len = v.toLongLong();
        if (len > limits_.maxBodyBytes) { fail(s, 413, "body_too_large", LC_TR("Nội dung yêu cầu quá lớn")); return false; }
    }
    const int bodyStart = hdrEnd + 4;
    if (c.buf.size() < bodyStart + len) return false;                // chua du body
    req.body = c.buf.mid(bodyStart, int(len));
    req.peer = s->peerAddress();

    HttpResponse resp;
    try {
        resp = handler_ ? handler_(req) : HttpResponse::json(404, R"J({"error":"not_found"})J");
    } catch (...) {
        fail(s, 500, "internal_error", LC_TR("Lỗi nội bộ"));
        return true;
    }
    if (resp.sse) {
        if (sse_.size() >= limits_.maxSseClients) { fail(s, 503, "too_many_streams", LC_TR("Quá nhiều luồng sự kiện")); return true; }
        QByteArray out = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-store\r\nConnection: keep-alive\r\nX-Content-Type-Options: nosniff\r\n";
        for (const auto& h : resp.headers) if (!hasControl(h.first) && !hasControl(h.second)) out += h.first + ": " + h.second + "\r\n";
        out += "\r\n" + resp.body;
        s->write(out);
        c.sse = true;
        c.buf.clear();
        sse_.append(s);
        return true;
    }
    respond(s, resp, req.method == "HEAD");
    return true;
}

void HttpServer::onReadyRead(QTcpSocket* s) {
    auto it = conns_.find(s);
    if (it == conns_.end()) return;
    Conn& c = it.value();
    if (c.sse || c.responded) { s->readAll(); return; }                   // luong SSE chi mot chieu; bo du lieu thua
    const qint64 room = qint64(limits_.maxHeaderBytes) + limits_.maxBodyBytes + 8 - c.buf.size();
    c.buf += s->read(std::max<qint64>(0, room));
    if (c.buf.size() > limits_.maxHeaderBytes + limits_.maxBodyBytes + 4) {
        fail(s, 413, "body_too_large", LC_TR("Yêu cầu quá lớn"));
        return;
    }
    parseAndDispatch(s, c);
    if (conns_.contains(s) && !conns_[s].responded && !conns_[s].sse && s->bytesAvailable() > 0) s->readAll();
}

void HttpServer::broadcastEvent(const QByteArray& event, const QByteArray& data) {
    if (sse_.isEmpty()) return;
    QByteArray msg = "event: " + event + "\n";
    for (const QByteArray& line : data.split('\n')) msg += "data: " + line + "\n";
    msg += "\n";
    for (QTcpSocket* s : QList<QTcpSocket*>(sse_)) {
        s->write(msg);
        if (s->bytesToWrite() > limits_.sseMaxBacklogBytes) dropConn(s);
    }
}

} // namespace lc
