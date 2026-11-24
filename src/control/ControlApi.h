#pragma once
#include "control/HttpServer.h"
#include "control/Osc.h"
#include "core/Project.h"
#include "render/Engine.h"
#include "render/OutputSupervisor.h"
#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <functional>
#include <optional>

namespace lc {

// Ket qua mot lenh dieu khien ngoai (dung chung cho HTTP va OSC).
struct CmdResult {
    int status = 200;
    QString code = "ok";                 // ma may doc duoc: ok, channel_not_found, scene_not_found, bad_request...
    QString message;                     // mo ta cho nguoi (tieng Viet)
    QJsonObject data;
    bool ok() const { return status < 300; }
    static CmdResult success(QJsonObject d = {}) { CmdResult r; r.data = std::move(d); return r; }
    static CmdResult error(int status, const QString& code, const QString& msg) { CmdResult r; r.status = status; r.code = code; r.message = msg; return r; }
};

// Lop dieu khien ngoai khong gan voi mang: lenh (1-based nhu giao dien), trang thai JSON, dinh tuyen HTTP, anh xa OSC.
// Quy tac an toan giong giao dien: moi lenh di qua RenderEngine::post(); dat Preview khong bao gio doi Program;
// Den co pham vi ro (tung Output / tat ca); chi so sai tra 404/400 (khong crash); lenh SUA cau hinh bi tu choi (423) khi khoa van hanh.
// Chay tren UI thread; chi doc du lieu cua render thread qua cac truong atomic hoac ban sao (snapshot) lay tren UI thread.
class ControlApi : public QObject {
    Q_OBJECT
public:
    using RouteFn = std::function<CmdResult(const QStringList& params, const QJsonObject& body)>;

    ControlApi(Project& project, RenderEngine& engine, OutputSupervisor* supervisor = nullptr, QObject* parent = nullptr);

    // UI thread: lay lai danh sach Scene/ten sau khi project doi (nhap media, hoan tac nhap...).
    void refreshProject();
    void setLockProvider(std::function<bool()> f) { lock_ = std::move(f); }
    bool locked() const { return lock_ && lock_(); }

    // --- bao mat HTTP ---
    void setToken(const QByteArray& t) { token_ = t; }
    void setRequireToken(bool b) { requireToken_ = b; }           // true: khong co token thi tu choi moi yeu cau API
    void setStrictHost(bool b) { strictHost_ = b; }               // true (chi nghe loopback): Host phai la loopback (chong DNS rebinding)
    static bool constantTimeEquals(const QByteArray& a, const QByteArray& b);

    // Them tuyen. pattern dang "/api/x/{n}/y"; edit = true thi bi chan (423) khi dang khoa van hanh.
    void addRoute(const QByteArray& method, const QString& pattern, bool edit, RouteFn fn);
    HttpResponse handleHttp(const HttpRequest& req);

    // --- lenh (chi so 1-based) ---
    CmdResult setPreview(int channel, int scene);
    CmdResult cut(int channel);
    CmdResult take(int channel, const QJsonObject& opts);
    CmdResult ftb(int channel, std::optional<bool> on, double seconds = 1.0);
    enum class OutFlag { Black, Freeze, Pattern };
    CmdResult outputFlag(int output, OutFlag f, std::optional<bool> on);
    CmdResult blackAll(std::optional<bool> on);
    enum class Transport { Play, Pause, Stop, Seek };
    CmdResult sceneTransport(int scene, Transport t, double seconds = 0);
    CmdResult handleOsc(const OscMessage& m);

    QJsonObject state();
    QByteArray stateJson();                                       // gon, dung cho SSE (lam tron de khong day su kien thua)

    int rejectedCount() const { return rejected_; }
    void resetLogLimiter() { logWindowStart_ = 0; logInWindow_ = 0; logSuppressed_ = 0; }
    void noteRejected(const QString& who, const QString& what);   // ghi log co gioi han tan suat + dem

signals:
    void commandRejected(const QString& reason);

private:
    struct Route { QByteArray method; QStringList segs; bool edit; RouteFn fn; };
    struct OutInfo { QString name; int channel = 0; QSize size; quint64 lastPresented = 0; double fps = 0; };
    void buildDefaultRoutes();
    void sampleFps();
    std::shared_ptr<Channel> chan(int n, CmdResult& err) const;
    HttpResponse reply(const CmdResult& r);
    HttpResponse errorReply(int status, const QString& code, const QString& msg, const HttpRequest& req);
    HttpResponse page();

    Project& project_;
    RenderEngine& engine_;
    OutputSupervisor* sup_;
    std::function<bool()> lock_;
    QByteArray token_;
    bool requireToken_ = false, strictHost_ = true;
    std::vector<Route> routes_;
    std::vector<std::shared_ptr<Channel>> channels_;
    std::vector<std::vector<std::shared_ptr<Scene>>> chScenes_;
    std::vector<std::shared_ptr<Scene>> scenes_;
    std::vector<OutInfo> outs_;
    QTimer fpsTimer_;
    QElapsedTimer fpsClock_;
    int rejected_ = 0;
    qint64 logWindowStart_ = 0;
    int logInWindow_ = 0, logSuppressed_ = 0;
};

} // namespace lc
