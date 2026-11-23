#pragma once
#include "core/Channel.h"
#include <QColor>

namespace lc {

// Mo ta mot nguon de luu/mo project (id on dinh de Layer tham chieu).
struct SourceDef {
    QString id;
    QString type;               // "video" | "image" | "color" | "testpattern"
    QString path;               // duong dan tuyet doi (video/image)
    QString label;
    QColor color{Qt::black};
    QSize size{1920, 1080};     // testpattern
    std::shared_ptr<Source> source;
};

struct Project {
    std::vector<SourceDef> sources;
    std::vector<std::shared_ptr<Scene>> scenes;
    std::vector<std::shared_ptr<Channel>> channels;
    std::vector<OutputConfig> outputs;
    double fps = 60.0;
    QString filePath;           // file project hien tai (rong = chua luu)

    // Them nguon (tao Source neu def.source rong); tra ve nullptr neu loi.
    std::shared_ptr<Source> addSource(SourceDef def, QString* err = nullptr);
    const SourceDef* findSource(const Source* s) const;
};

std::shared_ptr<Source> createSource(const SourceDef& def, QString* err = nullptr);

// Project dang JSON (media luu theo duong dan tuong doi so voi file project).
bool saveProject(const Project& p, const QString& path, QString* err = nullptr);
bool loadProject(Project& out, const QString& path, QString* err = nullptr);

// Video wall: chia mot Channel rong (cols*cell.w x rows*cell.h) cho cols*rows Output bang crop.
std::vector<OutputConfig> makeWall(int channel, int cols, int rows, QSize cell, int firstScreen = 0,
                                   bool fullscreen = false);

} // namespace lc
