#include "Osc.h"
#include "core/I18n.h"
#include <cstring>

namespace lc {

namespace {

struct Reader {
    const uchar* p;
    int size;
    int pos = 0;
    bool fail = false;

    int left() const { return size - pos; }
    bool need(int n) { if (n < 0 || n > left()) { fail = true; return false; } return true; }
    quint32 u32() {
        if (!need(4)) return 0;
        const quint32 v = (quint32(p[pos]) << 24) | (quint32(p[pos + 1]) << 16) | (quint32(p[pos + 2]) << 8) | quint32(p[pos + 3]);
        pos += 4;
        return v;
    }
    // Chuoi ket thuc bang '\0', can len boi so cua 4 byte. Phan dem phai du va deu la 0.
    QString str(int maxLen = 512) {
        int end = pos;
        while (end < size && p[end] != 0) {
            if (end - pos > maxLen) { fail = true; return {}; }
            ++end;
        }
        if (end >= size) { fail = true; return {}; }                     // khong thay '\0'
        const QString s = QString::fromUtf8(reinterpret_cast<const char*>(p + pos), end - pos);
        int next = end + 1;
        next = (next + 3) & ~3;
        if (next > size) { fail = true; return {}; }
        for (int i = end + 1; i < next; ++i) if (p[i] != 0) { fail = true; return {}; }
        pos = next;
        return s;
    }
};

bool parseMessage(const uchar* d, int n, OscMessage& m, QString* err) {
    Reader r{d, n};
    m.address = r.str();
    if (r.fail || !m.address.startsWith('/')) { if (err) *err = LC_TR("địa chỉ OSC không hợp lệ"); return false; }
    if (r.left() == 0) return true;                                      // OSC cu: khong co chuoi kieu
    const QString tags = r.str();
    if (r.fail || !tags.startsWith(',')) { if (err) *err = LC_TR("thiếu chuỗi kiểu tham số"); return false; }
    if (tags.size() > 33) { if (err) *err = LC_TR("quá nhiều tham số"); return false; }
    for (int i = 1; i < tags.size(); ++i) {
        switch (tags[i].unicode()) {
        case 'i': m.args << qint32(r.u32()); break;
        case 'f': { const quint32 u = r.u32(); float f; std::memcpy(&f, &u, 4); m.args << double(f); break; }
        case 's': m.args << r.str(); break;
        case 'T': m.args << true; break;
        case 'F': m.args << false; break;
        case 'N': m.args << QVariant(); break;
        case 'I': m.args << true; break;
        default: if (err) *err = QString(LC_TR("kiểu tham số OSC không hỗ trợ: '%1'")).arg(tags[i]); return false;
        }
        if (r.fail) { if (err) *err = LC_TR("gói OSC bị cụt"); return false; }
    }
    return true;
}

bool parseRec(const uchar* d, int n, QList<OscMessage>& out, QString* err, int depth) {
    if (n <= 0) { if (err) *err = LC_TR("gói rỗng"); return false; }
    if (d[0] == '#') {
        if (depth >= kOscMaxDepth) { if (err) *err = LC_TR("bundle lồng quá sâu"); return false; }
        static const char kTag[8] = {'#', 'b', 'u', 'n', 'd', 'l', 'e', 0};
        if (n < 16 || std::memcmp(d, kTag, 8) != 0) { if (err) *err = LC_TR("bundle hỏng"); return false; }
        Reader r{d, n, 16};                                               // bo qua timetag 8 byte
        while (r.left() > 0) {
            const int len = int(r.u32());
            if (r.fail || len <= 0 || len % 4 != 0 || len > r.left()) { if (err) *err = LC_TR("phần tử bundle sai kích thước"); return false; }
            if (!parseRec(d + r.pos, len, out, err, depth + 1)) return false;
            r.pos += len;
            if (out.size() > kOscMaxMessages) { if (err) *err = LC_TR("quá nhiều thông điệp trong một gói"); return false; }
        }
        return true;
    }
    OscMessage m;
    if (!parseMessage(d, n, m, err)) return false;
    out.append(m);
    return true;
}

void pad4(QByteArray& b) { while (b.size() % 4) b.append('\0'); }
void putStr(QByteArray& b, const QString& s) { b += s.toUtf8(); b.append('\0'); pad4(b); }
void putU32(QByteArray& b, quint32 v) { b.append(char(v >> 24)); b.append(char(v >> 16)); b.append(char(v >> 8)); b.append(char(v)); }

} // namespace

bool parseOscPacket(const QByteArray& data, QList<OscMessage>& out, QString* err) {
    out.clear();
    if (data.size() > kOscMaxPacket) { if (err) *err = LC_TR("gói OSC quá lớn"); return false; }
    if (data.size() < 4 || data.size() % 4 != 0) { if (err) *err = LC_TR("gói OSC sai kích thước"); return false; }
    QList<OscMessage> tmp;
    if (!parseRec(reinterpret_cast<const uchar*>(data.constData()), int(data.size()), tmp, err, 0)) return false;
    if (tmp.size() > kOscMaxMessages) { if (err) *err = LC_TR("quá nhiều thông điệp trong một gói"); return false; }
    out = tmp;                                                            // chi giao ra khi ca goi hop le
    return true;
}

QByteArray buildOscMessage(const QString& address, const QList<QVariant>& args) {
    QByteArray b;
    putStr(b, address);
    QString tags = ",";
    QByteArray payload;
    for (const QVariant& a : args) {
        switch (a.typeId()) {
        case QMetaType::Bool: tags += a.toBool() ? 'T' : 'F'; break;
        case QMetaType::Int: case QMetaType::LongLong: tags += 'i'; putU32(payload, quint32(qint32(a.toInt()))); break;
        case QMetaType::Double: case QMetaType::Float: {
            tags += 'f';
            const float f = a.toFloat(); quint32 u; std::memcpy(&u, &f, 4); putU32(payload, u);
            break;
        }
        case QMetaType::QString: tags += 's'; putStr(payload, a.toString()); break;
        default: tags += 'N'; break;
        }
    }
    putStr(b, tags);
    return b + payload;
}

QByteArray buildOscBundle(const QList<QByteArray>& elements) {
    QByteArray b("#bundle", 7);
    b.append('\0');
    putU32(b, 0);
    putU32(b, 1);                                                         // timetag "ngay lap tuc"
    for (const QByteArray& e : elements) { putU32(b, quint32(e.size())); b += e; }
    return b;
}

} // namespace lc
