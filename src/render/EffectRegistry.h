#pragma once
#include "render/EffectDef.h"
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace lc {

struct PluginInfo {
    QString id, name, version, author, description, dir;
    EffectDef::Kind kind = EffectDef::Effect;
    bool builtin = false;
    bool enabled = true;
    bool ok = true;            // false = manifest/shader loi (khong nap duoc)
    QString error;
};

// Kho Effect/Transition dung chung giua UI thread (nap plug-in, hien danh sach) va render thread (bien dich, ve).
// Du lieu bat bien duoc thay the nguyen khoi duoi mutex; nguoi doc lay shared_ptr nen khong bi doi giua chung.
class EffectRegistry {
public:
    using DefPtr = std::shared_ptr<const EffectDef>;

    void setDefs(std::vector<EffectDef> defs, std::vector<PluginInfo> infos);
    void loadBuiltins();                                       // chi Effect/Transition dung san
    DefPtr find(const QString& id) const;                      // nullptr neu khong co/da tat
    std::vector<DefPtr> all(EffectDef::Kind kind) const;       // dang bat, theo thu tu
    std::vector<PluginInfo> infos() const;

    // Render thread bao loi bien dich shader; UI doc de hien thi. Loi gan theo (id, revision).
    void setCompileError(const QString& id, int revision, const QString& msg);
    QString compileError(const QString& id) const;

private:
    mutable std::mutex m_;
    std::vector<DefPtr> defs_;
    std::vector<PluginInfo> infos_;
    std::map<QString, std::pair<int, QString>> errors_;
};

} // namespace lc
