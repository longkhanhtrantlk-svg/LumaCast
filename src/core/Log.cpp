#include "Log.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QTextStream>
#include <cstdio>

namespace lc {

namespace {
QMutex g_mutex;
QFile g_file;
QString g_path;
qint64 g_max = 0;
int g_keep = 3;
QtMessageHandler g_prev = nullptr;

void rotate() {
    g_file.close();
    QFile::remove(QString("%1.%2").arg(g_path).arg(g_keep));
    for (int i = g_keep - 1; i >= 1; --i) QFile::rename(QString("%1.%2").arg(g_path).arg(i), QString("%1.%2").arg(g_path).arg(i + 1));
    QFile::rename(g_path, g_path + ".1");
    g_file.setFileName(g_path);
    g_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

void handler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    const char* tag = type == QtDebugMsg ? "D" : type == QtInfoMsg ? "I" : type == QtWarningMsg ? "W" : "E";
    const QString line = QString("%1 [%2] %3\n").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz"), tag, msg);
    {
        QMutexLocker lk(&g_mutex);
        if (g_file.isOpen()) {
            if (g_file.size() + line.toUtf8().size() > g_max) rotate();
            g_file.write(line.toUtf8());
            g_file.flush();
        }
    }
    std::fputs(line.toLocal8Bit().constData(), stderr);
    Q_UNUSED(ctx);
    Q_UNUSED(g_prev);
}
} // namespace

void initLogging(const QString& dir, qint64 maxBytes, int keep) {
    QMutexLocker lk(&g_mutex);
    QDir().mkpath(dir);
    g_path = QDir(dir).filePath("lumacast.log");
    g_max = maxBytes;
    g_keep = std::max(1, keep);
    if (g_file.isOpen()) g_file.close();
    g_file.setFileName(g_path);
    g_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    qInstallMessageHandler(handler);
}

void shutdownLogging() {
    qInstallMessageHandler(nullptr);
    QMutexLocker lk(&g_mutex);
    if (g_file.isOpen()) g_file.close();
}

QString logFilePath() { return g_path; }

} // namespace lc
