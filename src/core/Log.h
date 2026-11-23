#pragma once
#include <QString>

namespace lc {

// Ghi log ra file (cac qDebug/qInfo/qWarning/qCritical), xoay vong khi file vuot maxBytes, giu `keep` file cu.
// File: <dir>/lumacast.log, lumacast.log.1 ... Moi dong: "yyyy-MM-dd HH:mm:ss.zzz [I] noi dung".
void initLogging(const QString& dir, qint64 maxBytes = 2 * 1024 * 1024, int keep = 3);
void shutdownLogging();
QString logFilePath();

} // namespace lc
