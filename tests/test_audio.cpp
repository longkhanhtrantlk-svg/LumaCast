// Test M5a: ring am thanh, mixer (bus/Channel, mute, am luong, doi he so muot, meter), trong so crossfade theo transition,
// ke hoach tron tu Project, luu/mo project, backend Null + mixer thread, giai ma am thanh FFmpeg va hinh theo dong ho am thanh.
#include "audio/AudioDevice.h"
#include "audio/AudioMixer.h"
#include "audio/AudioPlan.h"
#include "core/Project.h"
#include "media/FFmpegSource.h"
#include "media/StaticSources.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <thread>

using namespace lc;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { ++g_fail; std::cerr << "FAIL: " << msg << "  (" #cond ")\n"; } else std::cout << "ok:   " << msg << "\n"; } while (0)
static bool near(double a, double b, double tol = 1e-4) { return std::abs(a - b) <= tol; }
static void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// Nguon thu: am thanh do test tu day vao stream (khong co hinh).
class ToneSource : public Source {
public:
    explicit ToneSource(size_t cap = kAudioRate) : st(cap) {}
    QString name() const override { return "tone"; }
    TexRef acquire(GL&) override { return {}; }
    AudioStream* audio() override { return silent ? nullptr : &st; }
    void fill(float l, float r, size_t frames) {
        std::vector<float> v(frames * 2);
        for (size_t i = 0; i < frames; ++i) { v[i * 2] = l; v[i * 2 + 1] = r; }
        st.push(v.data(), frames);
    }
    AudioStream st;
    bool silent = false;
};

static std::shared_ptr<AudioPlan> plan1(std::shared_ptr<Source> s, int ch, float g, int nch = 1) {
    auto p = std::make_shared<AudioPlan>();
    p->channels = nch;
    p->entries.push_back({std::move(s), ch, g});
    return p;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    // ---------- AudioStream ----------
    {
        AudioStream st(8);
        float in[20], out[20];
        for (int i = 0; i < 20; ++i) in[i] = float(i + 1);
        CHECK(st.push(in, 3) == 3 && st.queued() == 3 && st.writable() == 5, "Ring: ghi 3 khung, con 5 cho");
        CHECK(st.pull(out, 2) == 2 && out[0] == 1 && out[3] == 4 && st.queued() == 1, "Ring: rut 2 khung theo thu tu");
        CHECK(st.push(in, 10) == 7, "Ring: ghi qua suc chua chi nhan phan con cho (khong ghi de)");
        CHECK(st.pull(out, 8) == 8 && out[0] == 5 && out[2] == 1 && out[14] == 13, "Ring: quay vong dung thu tu");
        CHECK(st.pull(out, 4) == 0 && out[0] == 0 && out[7] == 0 && st.underruns() == 1, "Ring: rong -> dien im lang va dem underrun");

        AudioStream pos(kAudioRate);
        std::vector<float> one(2 * 4800, 0.1f);
        pos.push(one.data(), 4800);
        pos.pull(one.data(), 2400);
        CHECK(near(pos.position(), 0.05, 1e-6), "Dong ho: vi tri = so mau da rut / 48000");
        pos.setOutputLatency(0.03);
        CHECK(near(pos.heardPosition(), 0.02, 1e-6), "Dong ho: vi tri nghe duoc = vi tri - do tre dau ra");
        pos.flush(10.0);
        CHECK(pos.queued() == 0 && near(pos.position(), 10.0, 1e-6), "flush: bo du lieu cho va dat lai goc thoi gian");
        pos.push(one.data(), 2400);
        pos.pull(one.data(), 1200);
        CHECK(near(pos.position(), 10.025, 1e-6), "Sau flush: dong ho dem tiep tu goc moi");
        pos.holdUntilFlush();
        pos.pull(one.data(), 100);
        CHECK(pos.queued() == 0 && one[0] == 0, "holdUntilFlush: im lang va bo du lieu cu ngay (restart)");
        pos.flush(0);
        pos.push(one.data(), 100);
        CHECK(pos.pull(one.data(), 100) == 100, "Sau flush: het che do giu im lang");
        pos.pull(one.data(), 1);
        CHECK(pos.consumerActive(1000), "Vua rut xong -> consumerActive");
    }

    // ---------- Mixer: bus, mute, am luong, meter ----------
    {
        auto a = std::make_shared<ToneSource>();
        auto b = std::make_shared<ToneSource>();
        AudioMixer mx;
        std::vector<float> out(kMixBlock * 2);
        auto p = std::make_shared<AudioPlan>();
        p->channels = 2;
        p->entries = {{a, 0, 0.5f}, {b, 1, 1.0f}};
        mx.setPlan(p);
        a->fill(0.5f, 0.25f, kMixBlock * 6);
        b->fill(0.2f, 0.2f, kMixBlock * 6);
        mx.renderBlock(out.data(), kMixBlock);          // khoi dau: nguon moi vao ke hoach tang tu 0 (khong click)
        CHECK(out[0] < 0.01f && near(out[kMixBlock * 2 - 2], 0.45, 1e-3), "Nguon moi vao ke hoach: tang tu 0 den dich trong mot khoi (khong click)");
        mx.renderBlock(out.data(), kMixBlock);
        CHECK(near(out[0], 0.5 * 0.5 + 0.2) && near(out[1], 0.25 * 0.5 + 0.2) && near(out[kMixBlock * 2 - 1], 0.325),
              "Mixer: master = tong cac bus, moi bus = nguon x he so (L va R rieng)");
        CHECK(near(mx.channelMeter(0).peak[0], 0.25) && near(mx.channelMeter(0).peak[1], 0.125) && near(mx.channelMeter(0).rms[0], 0.25),
              "Meter: dinh/RMS cua bus Channel 1 theo tung kenh trai/phai");
        CHECK(near(mx.channelMeter(1).peak[0], 0.2) && near(mx.masterMeter().peak[0], 0.45), "Meter: bus Channel 2 doc lap; master = tong");
        CHECK(near(mx.channelMeter(5).peak[0], 0), "Meter: bus khong dung = 0");

        // mute Channel 1 = he so 0: bus im lang, meter ve 0 sau khi het giu dinh; nguon van duoc rut (dong ho chay)
        const size_t q0 = a->st.queued();
        p = std::make_shared<AudioPlan>();
        p->channels = 2;
        p->entries = {{a, 0, 0.0f}, {b, 1, 1.0f}};
        mx.setPlan(p);
        mx.renderBlock(out.data(), kMixBlock);          // khoi nay doi tu 0.5 -> 0 (khong click)
        mx.renderBlock(out.data(), kMixBlock);
        CHECK(near(out[0], 0.2) && near(out[kMixBlock * 2 - 2], 0.2), "Mute: he so 0 -> Channel 1 im lang, Channel 2 giu nguyen");
        CHECK(a->st.queued() == q0 - 2 * kMixBlock, "Mute: nguon van duoc rut deu (dong ho am thanh/hinh khong dung)");
        CHECK(near(mx.channelMeter(0).rms[0], 0.0), "Meter: Channel mute -> RMS 0");

        // doi he so muot: 0 -> 1 trong mot khoi, tang don dieu, khong nhay bac
        auto c = std::make_shared<ToneSource>();
        AudioMixer m2;
        m2.setPlan(plan1(c, 0, 0.0f));
        c->fill(1.0f, 1.0f, kMixBlock * 4);
        m2.renderBlock(out.data(), kMixBlock);          // khoi dau: he so 0
        m2.setPlan(plan1(c, 0, 1.0f));
        m2.renderBlock(out.data(), kMixBlock);
        bool mono = true;
        float maxStep = 0;
        for (int i = 1; i < kMixBlock; ++i) { mono = mono && out[i * 2] >= out[(i - 1) * 2]; maxStep = std::max(maxStep, out[i * 2] - out[(i - 1) * 2]); }
        CHECK(mono && maxStep < 0.01f && near(out[(kMixBlock - 1) * 2], 1.0, 1e-3) && out[0] < 0.01f,
              "Doi he so 0->1: doi tuyen tinh trong khoi (khong tieng lach), cuoi khoi dat dung dich");

        // nguon xuat hien o 2 Channel: chi rut MOT lan
        auto d = std::make_shared<ToneSource>();
        AudioMixer m3;
        auto p3 = std::make_shared<AudioPlan>();
        p3->channels = 2;
        p3->entries = {{d, 0, 1.0f}, {d, 1, 0.5f}};
        m3.setPlan(p3);
        d->fill(0.4f, 0.4f, kMixBlock * 3);
        m3.renderBlock(out.data(), kMixBlock);
        m3.renderBlock(out.data(), kMixBlock);
        CHECK(near(m3.channelMeter(0).peak[0], 0.4) && near(m3.channelMeter(1).peak[0], 0.2) && d->st.queued() == kMixBlock && d->st.underruns() == 0,
              "Cung nguon o 2 Channel: ca hai bus deu co tieng, nguon chi bi rut mot lan");

        // tran bien: master bi cat ve +-1, dem so khoi bi cat
        auto e = std::make_shared<ToneSource>();
        AudioMixer m4;
        m4.setPlan(plan1(e, 0, 2.0f));
        e->fill(0.9f, -0.9f, kMixBlock * 2);
        m4.renderBlock(out.data(), kMixBlock);
        m4.renderBlock(out.data(), kMixBlock);
        CHECK(out[0] == 1.0f && out[1] == -1.0f && m4.clippedBlocks() >= 1, "Master: gioi han +-1 va dem so khoi bi cat");

        // nguon khong co am thanh (anh/mau) hoac thieu du lieu: im lang, khong loi
        auto s = std::make_shared<ToneSource>();
        s->silent = true;
        AudioMixer m5;
        m5.setPlan(plan1(s, 0, 1.0f));
        m5.renderBlock(out.data(), kMixBlock);
        CHECK(out[0] == 0 && near(m5.masterMeter().peak[0], 0), "Nguon khong co am thanh: im lang, khong loi");
        AudioMixer m6;
        auto f = std::make_shared<ToneSource>();
        m6.setPlan(plan1(f, 0, 1.0f));
        m6.renderBlock(out.data(), kMixBlock);
        CHECK(out[5] == 0 && f->st.underruns() == 1, "Nguon thieu du lieu: im lang va dem underrun");
        std::vector<float> big(20000 * 2);
        m6.renderBlock(big.data(), 20000);
        CHECK(big[39999] == 0 && m6.blocksRendered() >= 5, "renderBlock khoi lon hon 1 khoi noi bo van chay (tach khoi), khong tran bo dem");
    }

    // ---------- Trong so crossfade theo transition ----------
    auto mkScene = [](std::shared_ptr<Source> src, float layerVol = 1.0f) {
        auto sc = std::make_shared<Scene>();
        sc->name = "S";
        Layer l;
        l.source = std::move(src);
        l.volume = layerVol;
        sc->layers.push_back(l);
        return sc;
    };
    {
        auto sa = std::make_shared<ToneSource>(), sb = std::make_shared<ToneSource>();
        auto ch = std::make_shared<Channel>();
        ch->scenes = {mkScene(sa), mkScene(sb)};
        ch->program = 0;
        ch->preview = 1;
        std::vector<float> w;
        std::shared_ptr<Source> sting;
        ch->audioWeights(5.0, w, sting);
        CHECK(w.size() == 2 && w[0] == 1 && w[1] == 0, "Trong so: khong chuyen -> Program 1, Preview 0 (Preview khong ra tieng)");
        TransitionSpec fade;
        fade.type = TransitionType::Fade;
        fade.duration = 2.0;
        fade.easing = Easing::Linear;
        ch->startTransition(fade, 10.0);
        ch->audioWeights(10.0, w, sting);
        CHECK(near(w[0], 1) && near(w[1], 0), "Fade bat dau: van Program");
        ch->audioWeights(10.5, w, sting);
        CHECK(near(w[0], 0.75) && near(w[1], 0.25), "Fade 25%: Program 0.75 / Preview 0.25 (dung tien do hinh)");
        ch->audioWeights(11.0, w, sting);
        CHECK(near(w[0], 0.5) && near(w[1], 0.5) && near(w[0] + w[1], 1.0), "Fade 50%: tong he so = 1 (khong to, khong nho)");
        ch->audioWeights(12.5, w, sting);
        CHECK(near(w[0], 0) && near(w[1], 1), "Qua han: Preview 1 (khong am)");

        // easing: EaseInOut tai 25% thoi gian
        Channel c2;
        c2.scenes = ch->scenes;
        c2.program = 0; c2.preview = 1;
        TransitionSpec ez = fade;
        ez.easing = Easing::EaseInOut;
        c2.startTransition(ez, 0.0);
        c2.audioWeights(0.5, w, sting);
        CHECK(near(w[1], applyEasing(Easing::EaseInOut, 0.25)), "Crossfade theo easing giong het hinh");

        // Dip to black
        Channel c3;
        c3.scenes = ch->scenes;
        c3.program = 0; c3.preview = 1;
        TransitionSpec dip = fade;
        dip.type = TransitionType::DipToBlack;
        c3.startTransition(dip, 0.0);
        c3.audioWeights(0.5, w, sting);
        CHECK(near(w[0], 0.5) && near(w[1], 0), "Dip: nua dau Program giam ve 0, Preview chua vao");
        c3.audioWeights(1.0, w, sting);
        CHECK(near(w[0], 0) && near(w[1], 0), "Dip: giua chung = im lang hoan toan (cung luc man hinh den)");
        c3.audioWeights(1.5, w, sting);
        CHECK(near(w[0], 0) && near(w[1], 0.5), "Dip: nua sau Preview tang len");

        // T-bar
        Channel c4;
        c4.scenes = ch->scenes;
        c4.program = 0; c4.preview = 1;
        TransitionSpec tb;
        tb.type = TransitionType::Fade;
        c4.setManual(tb, 0.3f);
        c4.audioWeights(123.0, w, sting);
        CHECK(near(w[0], 0.7, 1e-6) && near(w[1], 0.3, 1e-6), "T-bar: he so theo dung vi tri thanh keo (khong phu thuoc thoi gian)");
        c4.setManual(tb, 0.0f);
        c4.audioWeights(123.0, w, sting);
        CHECK(near(w[0], 1) && near(w[1], 0), "T-bar keo ve 0 (huy): tro lai Program");
        c4.setManual(tb, 0.6f);
        c4.setManual(tb, 1.0f);
        c4.audioWeights(0, w, sting);
        CHECK(c4.program == 1 && near(w[1], 1) && near(w[0], 0), "T-bar hoan tat: Program va Preview hoan doi, tieng theo Program moi");

        // Stinger: cat cung o cutPoint, stinger co tieng rieng
        auto stSrc = std::make_shared<ToneSource>();
        Channel c5;
        c5.scenes = ch->scenes;
        c5.program = 0; c5.preview = 1;
        TransitionSpec sp;
        sp.type = TransitionType::Stinger;
        sp.stinger = stSrc;
        sp.duration = 2.0;
        sp.cutPoint = 0.5;
        c5.startTransition(sp, 0.0);
        c5.audioWeights(0.5, w, sting);
        CHECK(near(w[0], 1) && near(w[1], 0) && sting == stSrc, "Stinger truoc diem cat: van Program; am thanh stinger duoc dua vao");
        c5.audioWeights(1.5, w, sting);
        CHECK(near(w[0], 0) && near(w[1], 1), "Stinger sau diem cat: doi sang Preview (cat cung nhu hinh)");
    }

    // ---------- Ke hoach tron tu Project ----------
    {
        auto sa = std::make_shared<ToneSource>(), sb = std::make_shared<ToneSource>();
        Project p;
        p.scenes = {mkScene(sa, 0.5f), mkScene(sb)};
        auto ch = std::make_shared<Channel>();
        ch->scenes = p.scenes;
        ch->program = 0;
        ch->preview = 1;
        p.channels = {ch};
        auto gainOf = [](const AudioPlan& pl, const std::shared_ptr<Source>& s) { for (auto& e : pl.entries) if (e.src == s) return e.gain; return -1.0f; };
        auto pl = buildAudioPlan(p, 0);
        CHECK(pl->channels == 1 && pl->entries.size() == 2 && near(gainOf(*pl, sa), 0.5) && near(gainOf(*pl, sb), 0.0),
              "Ke hoach: moi nguon cua moi Scene deu co mat (Preview he so 0); am luong Layer nhan vao");
        ch->volume = 0.5f;
        pl = buildAudioPlan(p, 0);
        CHECK(near(gainOf(*pl, sa), 0.25), "Ke hoach: am luong Channel x am luong Layer");
        ch->mute = true;
        pl = buildAudioPlan(p, 0);
        CHECK(near(gainOf(*pl, sa), 0) && pl->entries.size() == 2, "Ke hoach: Mute Channel -> he so 0 nhung nguon van co mat de dong ho chay");
        ch->mute = false;
        ch->volume = 1.0f;
        p.scenes[0]->layers[0].visible = false;
        pl = buildAudioPlan(p, 0);
        CHECK(near(gainOf(*pl, sa), 0), "Ke hoach: Layer an -> im lang");
        p.scenes[0]->layers[0].visible = true;
        ch->ftbAmount = 0.75f;
        pl = buildAudioPlan(p, 0);
        CHECK(near(gainOf(*pl, sa), 0.5 * 0.25, 1e-6), "Ke hoach: Fade-To-Black keo ca am thanh ve 0");
        ch->ftbAmount = 0.0f;
        auto same = buildAudioPlan(p, 0);
        CHECK(same->sameAs(*buildAudioPlan(p, 1.0)) && !same->sameAs(*pl), "AudioPlan::sameAs: bo qua khung khong doi, nhan ra thay doi");
        // cung nguon o hai Scene cua mot Channel -> he so cong lai (khong bi doi tang x2 khi dang crossfade)
        p.scenes[1]->layers[0].source = sa;
        TransitionSpec f;
        f.duration = 1.0; f.easing = Easing::Linear;
        ch->startTransition(f, 0.0);
        pl = buildAudioPlan(p, 0.5);
        CHECK(pl->entries.size() == 1 && near(gainOf(*pl, sa), 0.5 * 0.5 + 1.0 * 0.5), "Cung nguon o ca hai Scene: he so cong (0.5*Program + 1.0*Preview)");
    }

    // ---------- Luu/mo project ----------
    {
        QTemporaryDir dir;
        Project p;
        SourceDef d;
        d.id = "c1"; d.type = "color"; d.color = Qt::red;
        QString err;
        p.addSource(d, &err);
        auto sc = std::make_shared<Scene>();
        Layer l;
        l.source = p.sources[0].source;
        l.volume = 0.4f;
        sc->layers.push_back(l);
        p.scenes = {sc};
        auto ch = std::make_shared<Channel>();
        ch->scenes = {sc};
        ch->volume = 0.7f;
        ch->mute = true;
        p.channels = {ch};
        p.outputs.push_back(OutputConfig{});
        p.audioDevice = "{0.0.0.00000000}.{abc}";
        const QString path = dir.filePath("a.json");
        CHECK(saveProject(p, path, &err), "Luu project co am thanh: " + err.toStdString());
        Project q;
        CHECK(loadProject(q, path, &err), "Mo lai: " + err.toStdString());
        CHECK(near(q.channels[0]->volume, 0.7) && q.channels[0]->mute && near(q.scenes[0]->layers[0].volume, 0.4) && q.audioDevice == p.audioDevice,
              "Round-trip: am luong + mute Channel, am luong Layer, thiet bi am thanh");
        QFile f(path);
        f.open(QIODevice::ReadOnly);
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        CHECK(root["version"].toInt() == 1, "Khong tang version project");
        ch->volume = 1.0f; ch->mute = false; sc->layers[0].volume = 1.0f; p.audioDevice.clear();
        f.close();      // Windows: QSaveFile khong ghi de duoc file dang mo
        CHECK(saveProject(p, path, &err), "Luu de project: " + err.toStdString());
        QFile f2(path);
        f2.open(QIODevice::ReadOnly);
        const QByteArray js = f2.readAll();
        CHECK(!js.contains("volume") && !js.contains("mute") && !js.contains("\"audio\""), "Gia tri mac dinh khong ghi ra file (file gon, tuong thich nguoc)");
        Project r;
        CHECK(loadProject(r, path, &err) && near(r.channels[0]->volume, 1) && !r.channels[0]->mute && near(r.scenes[0]->layers[0].volume, 1) && r.audioDevice.isEmpty(),
              "Project cu (khong co truong am thanh) mo duoc, mac dinh: 100%, khong mute, thiet bi mac dinh");
        QByteArray bad = js;
        bad.replace("\"program\"", "\"volume\": 99, \"mute\": \"x\", \"program\"");
        bad.replace("\"source\"", "\"volume\": -3, \"source\"");
        QFile fb(dir.filePath("bad.json"));
        fb.open(QIODevice::WriteOnly); fb.write(bad); fb.close();
        Project b;
        CHECK(loadProject(b, dir.filePath("bad.json"), &err) && b.channels[0]->volume <= 2.0f && b.scenes[0]->layers[0].volume >= 0.0f,
              "File viet tay: am luong ngoai khoang bi ep ve 0..2");
    }

    // ---------- Backend Null + mixer thread ----------
    {
        auto devs = listAudioDevices();
        CHECK(!devs.empty() && devs[0].id.isEmpty() && devs[0].isDefault, "Liet ke thiet bi: luon co muc mac dinh (id rong) dau tien");
        auto nd = createAudioDevice("null");
        CHECK(nd && nd->name().contains("Null"), "createAudioDevice(\"null\") tra backend Null");

        auto tone = std::make_shared<ToneSource>(kAudioRate * 2);
        AudioMixer mx;
        mx.setPlan(plan1(tone, 0, 0.5f));
        std::atomic<bool> feed{true};
        std::thread feeder([&] {
            while (feed) { if (tone->st.writable() > 2400) tone->fill(0.6f, 0.6f, 2400); sleepMs(5); }
        });
        QString err;
        mx.start("null", &err);
        CHECK(mx.running() && mx.deviceName().contains("Null"), "Mixer: chay thread rieng voi thiet bi Null");
        sleepMs(700);
        CHECK(near(mx.channelMeter(0).peak[0], 0.3, 1e-3), "Mixer thread: meter doc duoc muc 0.6 x 0.5 = 0.3 (qua ring dau ra va thiet bi)");
        CHECK(mx.blocksRendered() > 40 && mx.blocksRendered() < 140, "Mixer thread chay theo nhip thiet bi (~100 khoi/giay, khong chay tran)");
        CHECK(mx.outputUnderruns() == 0, "Mixer thread: khong underrun o dau ra khi nguon day du");
        const double pos = tone->st.position();
        CHECK(pos > 0.6 && pos < 1.0, "Dong ho nguon chay theo thiet bi (~0.7 s sau 700 ms)");
        CHECK(tone->st.heardPosition() < pos && tone->st.heardPosition() > pos - 0.2, "Vi tri nghe duoc tre hon vi tri rut mot do tre dau ra hop ly");
        // doi thiet bi giua chung
        mx.start("null", &err);
        sleepMs(150);
        CHECK(mx.running() && mx.blocksRendered() > 0, "Doi/khoi dong lai thiet bi khi dang chay khong vo");
        feed = false;
        feeder.join();
        mx.stop();
        CHECK(!mx.running(), "Dung mixer: thread va thiet bi dong sach");
        mx.releasePlan();
        CHECK(tone.use_count() == 1, "releasePlan: mixer tra lai tham chieu toi nguon");
    }

    // ---------- FFmpeg: giai ma am thanh, hinh theo dong ho am thanh ----------
    {
        QTemporaryDir dir;
        const QString av = dir.filePath("av.mkv"), vo = dir.filePath("video_only.mkv");
        const int rc1 = std::system(QString("ffmpeg -y -loglevel error -f lavfi -i testsrc2=s=160x90:r=50:d=1.5 -f lavfi -i \"sine=frequency=440:sample_rate=44100:duration=1.5\" "
                                            "-c:v mpeg4 -c:a pcm_s16le -ac 1 \"%1\"").arg(av).toLocal8Bit().constData());
        const int rc2 = std::system(QString("ffmpeg -y -loglevel error -f lavfi -i testsrc2=s=160x90:r=50:d=1 -c:v mpeg4 \"%1\"").arg(vo).toLocal8Bit().constData());
        if (rc1 != 0 || rc2 != 0) {
            std::cout << "skip: khong co ffmpeg de tao media thu\n";
        } else {
            // nguon khong co am thanh
            {
                FFmpegSource v(vo);
                sleepMs(300);
                CHECK(v.error().isEmpty() && !v.hasAudio() && v.audio() == nullptr, "Video khong co am thanh: im lang, khong loi");
            }
            // doi am thanh 44.1k mono -> 48k stereo
            auto src = std::make_shared<FFmpegSource>(av);
            for (int i = 0; i < 100 && !src->hasAudio(); ++i) sleepMs(20);
            CHECK(src->error().isEmpty() && src->hasAudio() && src->audio() != nullptr, "Video co am thanh: nhan ra stream am thanh");
            {   // chua ai rut -> chay theo dong ho tuong, khong ket, ring khong day vo han
                sleepMs(700);
                CHECK(!src->followingAudioClock() && src->presentedTime() > 0.3, "Chua co mixer: hinh chay theo dong ho tuong (khong ket)");
                CHECK(src->audio()->queued() <= size_t(kAudioRate / 10) + 4096, "Chua co mixer: chi giu ~100 ms am thanh moi nhat (khong phat am thanh cu khi mixer bat dau)");
            }
            AudioMixer mx;
            mx.setPlan(plan1(src, 0, 1.0f));
            QString err;
            mx.start("null", &err);
            sleepMs(600);
            CHECK(src->followingAudioClock(), "Co mixer rut: hinh chuyen sang theo dong ho am thanh");
            const float pk = mx.channelMeter(0).peak[0];
            CHECK(pk > 0.07f && pk < 0.11f, "Am thanh giai ma + doi dinh dang ra bus: sine 0.125 mono -> stereo -3 dB = ~0.088 (peak=" + std::to_string(pk) + ")");
            CHECK(std::abs(mx.channelMeter(0).peak[0] - mx.channelMeter(0).peak[1]) < 1e-3, "Mono duoc nhan doi ra ca hai kenh");

            // do lech hinh-tieng trong ~4 s (2+ vong lap), do doc lap tu thread test
            const uint64_t und0 = src->audio()->underruns();
            double minD = 1e9, maxD = -1e9, lastHeard = -1;
            bool heardMonotonic = true;
            int samples = 0;
            for (int i = 0; i < 160; ++i) {
                const double heard = src->audio()->heardPosition(), shown = src->presentedTime();
                if (heard < lastHeard - 1e-9) heardMonotonic = false;
                lastHeard = heard;
                const double d = heard - shown;
                minD = std::min(minD, d); maxD = std::max(maxD, d);
                ++samples;
                sleepMs(25);
            }
            std::cout << "      lech hinh-tieng (nghe - hinh): " << int(minD * 1000) << " .. " << int(maxD * 1000) << " ms tren " << samples << " mau, vi tri nghe cuoi " << lastHeard << " s\n";
            CHECK(minD > -0.015 && maxD < 0.045, "Hinh-tieng: hinh luon trong khoang [-15, +45] ms so voi tieng (1 khung 20 ms + do tre doc) qua nhieu vong lap");
            CHECK(heardMonotonic && lastHeard > 4.0, "Dong ho am thanh lien tuc qua vong lap (khong nhay ve 0)");
            CHECK(src->audio()->underruns() == und0, "Lap noi lien: khong underrun (khong ho giua cac vong)");
            CHECK(src->presentedTime() > 4.0, "Dong thoi gian cua hinh cung lien tuc qua vong lap (khong tich luy lech)");

            // mute (he so 0) khong lam dung dong ho
            mx.setPlan(plan1(src, 0, 0.0f));
            const double t0 = src->presentedTime();
            sleepMs(500);
            CHECK(src->followingAudioClock() && src->presentedTime() > t0 + 0.3, "Mute Channel: nguon van duoc rut, hinh van chay dung");

            // restart(): am thanh + hinh ve 0, khong nghe not cu
            const double before = src->audio()->position();
            src->restart();
            sleepMs(250);
            CHECK(src->audio()->position() < 0.5 && before > 1.0, "restart(): dong ho am thanh ve dau");
            for (int i = 0; i < 50 && src->presentedTime() > 0.8; ++i) sleepMs(20);
            CHECK(src->presentedTime() < 0.8, "restart(): hinh ve dau, theo dong ho am thanh moi");

            // mixer dung -> nguon tu ve dong ho tuong, khong ket
            mx.stop();
            mx.releasePlan();
            sleepMs(500);
            const double t1 = src->presentedTime();
            sleepMs(300);
            CHECK(!src->followingAudioClock() && (src->presentedTime() > t1 + 0.15 || src->presentedTime() < t1), "Mixer dung: hinh tu ve dong ho tuong, van chay");
        }
    }

    std::cout << (g_fail ? "THAT BAI " : "TAT CA DAT ") << g_fail << " loi\n";
    return g_fail ? 1 : 0;
}
