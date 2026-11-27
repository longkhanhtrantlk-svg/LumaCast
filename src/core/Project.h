#pragma once
#include "core/Channel.h"
#include <QColor>

namespace lc {

// Mo ta mot nguon de luu/mo project (id on dinh de Layer tham chieu).
struct SourceDef {
    QString id;
    QString type;               // "video" | "image" | "color" | "testpattern" | "ndi" | "camera"
    QString path;               // duong dan tuyet doi (video/image)
    QString label;
    QColor color{Qt::black};
    QSize size{1920, 1080};     // testpattern; camera: do phan giai yeu cau (0x0 = de thiet bi chon)
    QString device;             // ndi: ten nguon NDI; camera: id thiet bi (Windows: symbolic link)
    QString deviceName;         // camera: ten hien thi, de nhan ra lai khi id doi
    QString url;                // ndi: dia chi nguon (tuy chon)
    double fps = 0;             // camera: fps yeu cau (0 = de thiet bi chon)
    std::shared_ptr<Source> source;
};

struct Project {
    std::vector<SourceDef> sources;
    std::vector<std::shared_ptr<Scene>> scenes;
    std::vector<std::shared_ptr<Channel>> channels;
    std::vector<OutputConfig> outputs;
    double fps = 60.0;
    QString filePath;           // file project hien tai (rong = chua luu)
    QString audioDevice;        // id thiet bi am thanh ra mong muon (rong = mac dinh cua he thong); khong co tren may nay -> dung mac dinh
    QStringList loadWarnings;   // loi khong lam hong viec mo project (vd. thieu file media); khong luu vao file

    // Them nguon (tao Source neu def.source rong); tra ve nullptr neu loi.
    std::shared_ptr<Source> addSource(SourceDef def, QString* err = nullptr);
    const SourceDef* findSource(const Source* s) const;
};

std::shared_ptr<Source> createSource(const SourceDef& def, QString* err = nullptr);

// Project dang JSON (media luu theo duong dan tuong doi so voi file project).
bool saveProject(const Project& p, const QString& path, QString* err = nullptr);
bool loadProject(Project& out, const QString& path, QString* err = nullptr);

// Video wall: chia mot Channel rong cho cols*rows Output bang crop.
// Bu khe (OUT-04): gap = so diem anh canvas nam "sau" vien/khe giua hai man ke nhau (ngang, doc). Canvas MO RONG thanh
// cols*cell.w + (cols-1)*gap.w (tuong tu theo chieu cao), moi Output van lay dung cell.w x cell.h diem anh (1:1) va phan
// sau khe khong hien o dau ca. Khe chi nam trong kich thuoc canvas + crop, khong co truong rieng trong file project.
constexpr int kMaxWallGap = 4096;           // diem anh
constexpr int kMaxWallCanvas = 16384;       // canh canvas lon nhat khi co khe (gioi han texture GPU)
QSize wallCanvas(int cols, int rows, QSize cell, QSize gap = QSize(0, 0));
QRectF wallCrop(int col, int row, int cols, int rows, QSize cell, QSize gap = QSize(0, 0));
std::vector<OutputConfig> makeWall(int channel, int cols, int rows, QSize cell, int firstScreen = 0,
                                   bool fullscreen = false, QSize gap = QSize(0, 0));

// Wall nhan ra tu cau hinh dang co (ke ca project cu): cac Output man hinh cua mot Channel co cung kich thuoc va
// crop khop dung mot luoi cols x rows (co the co khe) tren canvas.
struct WallLayout {
    struct Cell { int output, col, row; };  // output = chi so trong danh sach Output
    int cols = 0, rows = 0;
    QSize cell;                             // diem anh canvas moi o
    QSize gap{0, 0};
    std::vector<Cell> cells;
    // Khe lon nhat theo mot chieu de canvas khong vuot kMaxWallCanvas (0 neu chieu do chi co mot o).
    QSize maxGap() const;
};
// Bo qua Output NDI, Output cua Channel khac va Output lay ca canvas (vd. man giam sat). false = khong phai wall.
bool detectWall(const std::vector<OutputConfig>& outputs, int channel, QSize canvas, WallLayout& out);

} // namespace lc
