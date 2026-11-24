#pragma once
#include "render/Engine.h"
#include <QJsonObject>
#include <QSize>
#include <QString>

namespace lc {

// Benchmark render co the chay lai (M6, NFR-01/02/03): dung RenderEngine that + N "Output ngoai man hinh" (FBO WxH, nhu cua so 4K nhung khong can man hinh),
// do thoi gian tung giai doan tren ca doan do va ghi p50/p99/roi khung ra JSON.
//   lumacast --benchmark "outputs=4,size=3840x2160,source=synthetic-yuv,seconds=10" --bench-json ket_qua.json
// SO LIEU TREN BO RENDER PHAN MEM (llvmpipe) KHONG DAI DIEN CHO GPU THAT - chi dung de so sanh tuong doi / kiem tra khong loi.
struct BenchOptions {
    int outputs = 4;                    // so Output (moi Output = 1 FBO size.w x size.h)
    QSize size{3840, 2160};             // do phan giai moi Output
    QSize canvas;                       // do phan giai Scene/Channel; rong = bang size
    int channels = 1;                   // so Channel (Output chia deu theo vong tron)
    QString source = "synthetic-yuv";   // "synthetic-yuv" (NV12) | "synthetic-rgba" | "color" | "file:<duong dan>" (giai ma that)
    QSize sourceSize{3840, 2160};       // do phan giai nguon tong hop
    double sourceFps = 30;              // nhip nguon tong hop
    int layers = 1;                     // so Layer nguon moi Scene
    bool transition = false;            // chay Transition Fade lien tuc giua hai Scene (do nhanh ba tren)
    double seconds = 10;                // do dai doan do
    double warmup = 2;                  // bo qua giay dau (bien dich shader, cap texture)
    double fps = 60;
    bool pbo = true;                    // false: upload truc tiep (so sanh)
    bool gpuColor = true;               // file video: false = sws_scale CPU
    bool syncStages = false;            // glFinish moi giai doan -> thoi gian GPU that (cham hon, chi dung khi do)
    bool hwDecode = false;              // file video: bat giai ma phan cung ("auto")
};

struct BenchStage { double p50 = 0, p99 = 0, max = 0, mean = 0; };
struct BenchResult {
    bool ok = false;
    QString error;
    QString gpu;
    bool softwareRenderer = false;      // llvmpipe/softpipe/... -> so lieu khong dai dien
    BenchOptions options;
    int frames = 0;
    double measuredSeconds = 0;
    double fps = 0;                     // khung/giay thuc cua Engine
    uint64_t dropped = 0;               // khung ket thuc sau han chot (Engine::droppedFrames())
    int overBudget = 0;                 // so khung ve (khong tinh ngu) lau hon 1/fps
    double budgetMs = 0;
    BenchStage frame, upload, scene, transition, monitor, output;
    uint64_t presented = 0;             // tong khung da dua ra tren moi Output trong doan do
    QJsonObject toJson() const;
    QString toText() const;
};

BenchResult runBenchmark(const BenchOptions& opt);
// "key=value,key=value" -> BenchOptions. Khoa: outputs, size (WxH), canvas, channels, source, srcsize, srcfps, layers, transition (0/1), seconds, warmup, fps, pbo, gpucolor, sync, hwdec.
bool parseBenchOptions(const QString& spec, BenchOptions& out, QString* err);
// Cho dong lenh: chay, in ket qua ra stdout, ghi JSON neu jsonPath khong rong. Tra ma thoat (0 = chay duoc).
int runBenchmarkCli(const QString& spec, const QString& jsonPath);

} // namespace lc
