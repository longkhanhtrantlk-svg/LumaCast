// Cong cu THU TAY (khong phai ctest) cho T-UPD: chep vao <ban cai>\current\ roi chay de goi dung backend Velopack cua app.
//   lumacast_update_probe <noi lay ban> status | check | download | apply | rollback
// "apply" va "rollback" khoi dong trinh cap nhat roi thoat ngay: trinh cap nhat doi ban va mo lai LumaCast.
#include "core/Update.h"
#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

using namespace lc;

int main(int argc, char** argv) {
    update::runStartupHooks();
    QCoreApplication app(argc, argv);
    QFile outFile;
    outFile.open(stdout, QIODevice::WriteOnly);
    QTextStream out(&outFile);
    const QStringList args = app.arguments();
    if (args.size() < 3) {
        out << "lumacast_update_probe <noi lay ban> status|check|download|apply|rollback\n";
        return 2;
    }
    auto backend = update::createBackend(args[1]);
    const QString what = args[2];
    QString why;
    const bool supported = backend->supported(&why);
    out << "supported=" << (supported ? "yes" : "no") << " version=" << backend->currentVersion() << " previous=" << backend->previousVersion() << "\n"
        << "installDir=" << backend->installDir() << "\n";
    if (!supported) {
        out << why << "\n";
        return 1;
    }
    if (what == "status") return 0;
    QString err;
    if (what == "rollback") {
        const bool ok = backend->rollback(&err);
        out << "rollback=" << (ok ? "started" : "FAILED ") << err << "\n";
        return ok ? 0 : 1;
    }
    update::Release r;
    const update::CheckResult c = backend->check(r, &err);
    if (c == update::CheckResult::Failed) { out << "check=FAILED " << err << "\n"; return 1; }
    if (c == update::CheckResult::UpToDate) { out << "check=up-to-date\n"; return 0; }
    out << "check=available " << r.version << " delta=" << (r.delta ? "yes" : "no") << " bytes=" << r.downloadBytes << "\n";
    if (what == "check") return 0;
    int last = -1;
    if (!backend->download([&](int p) { if (p / 25 != last / 25) { out << "download " << p << "%\n"; out.flush(); } last = p; }, &err)) {
        out << "download=FAILED " << err << "\n";
        return 1;
    }
    out << "download=ok\n";
    if (what == "download") return 0;
    const bool ok = backend->applyAndRestart({}, &err);
    out << "apply=" << (ok ? "started" : "FAILED ") << err << "\n";
    return ok ? 0 : 1;
}
