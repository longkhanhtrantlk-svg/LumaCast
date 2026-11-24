#pragma once
#include <QByteArray>
#include <QList>
#include <QString>
#include <QVariant>

namespace lc {

// Mot thong diep OSC 1.0: dia chi + tham so (int32 -> int, float32 -> double, string -> QString, T/F -> bool, N -> invalid QVariant).
struct OscMessage {
    QString address;
    QList<QVariant> args;
};

// Phan tich mot goi UDP (thong diep don hoac bundle long nhau). Goi hong -> tra false va `err` (khong crash, khong doc ngoai bien).
// Bundle: timetag bi bo qua (thuc thi ngay). Gioi han: goi <= kMaxPacket, do sau bundle <= kMaxDepth, <= kMaxMessages thong diep.
bool parseOscPacket(const QByteArray& data, QList<OscMessage>& out, QString* err = nullptr);

constexpr int kOscMaxPacket = 4096;
constexpr int kOscMaxDepth = 4;
constexpr int kOscMaxMessages = 64;

// Dung goi OSC (cho test va cho cong cu): chi ho tro int/double(float32)/string/bool.
QByteArray buildOscMessage(const QString& address, const QList<QVariant>& args = {});
QByteArray buildOscBundle(const QList<QByteArray>& elements);

} // namespace lc
