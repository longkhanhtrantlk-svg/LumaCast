#pragma once
#include "core/I18n.h"
#include "core/Transition.h"
#include <QJsonObject>
#include <QString>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace lc {

class Channel;
struct Scene;

// Mot muc playlist: tro toi mot Scene cua project (anh/video nhap vao deu la mot Scene).
struct PlaylistItem {
    int id = 0;                      // on dinh trong phien (khong luu) de theo doi muc dang phat khi danh sach bi sua
    std::shared_ptr<Scene> scene;
    double seconds = 0;              // 0 = tu dong: Scene co video -> het video moi chuyen; con lai dung imageSeconds
};

// Phan cau hinh: sua duoc (hoan tac, bi khoa khi Khoa van hanh) va luu trong project.
struct PlaylistData {
    std::vector<PlaylistItem> items;
    double imageSeconds = 8.0;       // thoi luong cho muc "tu dong" khong co video; tinh tu luc chuyen vao XONG
    bool loop = true;
    TransitionType transition = TransitionType::Fade;     // khong ho tro Stinger
    QString pluginId;                                     // khi transition == Plugin
    double transitionSeconds = 1.0;

    bool isDefault() const;
    TransitionSpec spec() const;
};

constexpr double kPlaylistMinSeconds = 0.5, kPlaylistMaxSeconds = 3600.0;

int newPlaylistItemId();
// Do dai (giay) cua video dai nhat dang hien trong Scene; 0 = khong co video (hoac chua biet do dai).
double sceneClipSeconds(const Scene& s);

// JSON cua channels[].playlist. `scenes` = danh sach Scene goc cua project (muc luu theo chi so).
QJsonObject playlistToJson(const PlaylistData& d, const std::vector<std::shared_ptr<Scene>>& scenes);
PlaylistData playlistFromJson(const QJsonObject& o, const std::vector<std::shared_ptr<Scene>>& scenes);

// Playlist cua mot Channel + bo chay no. `data` va moi lenh play/pause/... chi duoc goi tren render thread (qua RenderEngine::post());
// UI chi doc state()/currentId()/remaining()/takeNotices(). Thoi gian la dong ho that (giay cua engine), khong dem khung.
// Playlist dua muc len Program qua Channel::startAuto (khong dung toi Preview). Khi nguoi van hanh CUT/TAKE/T-bar/dua Scene len Program
// (Channel::operatorOps doi) thi playlist TAM DUNG va giu nguyen hinh cua nguoi van hanh.
class Playlist {
public:
    enum class State { Stopped, Playing, Paused };
    static constexpr double kReadyTimeout = 3.0;      // video khong ra khung hinh trong khoang nay -> coi la loi, bo qua

    PlaylistData data;

    // Chay: tu dau (dang dung) / tiep tuc (dang tam dung). itemId != 0: chay tu muc do.
    void play(Channel& ch, double now, int itemId = 0);
    void pause(double now);
    void stop();                                      // giu nguyen Program
    void next(Channel& ch, double now) { step(ch, now, 1); }
    void prev(Channel& ch, double now) { step(ch, now, -1); }
    void tick(Channel& ch, double now);               // moi khung, tren render thread

    State state() const { return state_; }
    int currentId() const { return current_; }        // 0 = chua co muc nao len song
    double remaining() const { return remaining_; }   // giay con lai cua muc hien tai; < 0 = khong ap dung
    std::vector<QString> takeNotices();               // thong bao (muc bi bo qua, tam dung...) cho UI; goi tu thread bat ky

private:
    int indexOf(int itemId) const;
    void step(Channel& ch, double now, int dir);
    void cueFrom(Channel& ch, double now, int pos);   // tim tu `pos` muc phat duoc roi chuan bi (video: tua ve dau)
    void tryAir(Channel& ch, double now);             // muc dang chuan bi da san sang -> dua len Program
    void fail(Channel& ch, double now, int pos, const i18n::Text& why);
    double switchAt() const;                          // thoi diem (dong ho engine) chuyen sang muc ke tiep
    QString problemOf(const Channel& ch, const PlaylistItem& it) const;
    QString label(int pos) const;
    void notice(const Channel& ch, const i18n::Text& msg);
    void publish(double now);

    std::atomic<State> state_{State::Stopped};
    std::atomic<int> current_{0};
    std::atomic<double> remaining_{-1};

    int currentPos_ = 0;            // vi tri muc hien tai (dung khi muc do bi xoa khoi danh sach)
    int cue_ = 0, cuePos_ = 0;      // muc dang chuan bi
    double cueSince_ = 0;
    bool cueRestarted_ = false;     // da tua video ve dau luc chuan bi
    std::shared_ptr<Scene> onAir_;  // Scene cua muc hien tai
    double seconds_ = 0;            // thoi luong dat rieng cua muc hien tai (0 = tu dong)
    double started_ = 0;            // luc muc hien tai bat dau chuyen vao
    double inLen_ = 0;              // do dai hieu ung chuyen vao
    double pausedAt_ = 0;
    int fails_ = 0;                 // so muc loi lien tiep (du mot vong -> dung)
    uint32_t seenOps_ = 0;

    std::mutex noticeMutex_;
    std::vector<QString> notices_;
};

} // namespace lc
