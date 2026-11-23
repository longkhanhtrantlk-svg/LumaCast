#include "EffectRegistry.h"

namespace lc {

void EffectRegistry::setDefs(std::vector<EffectDef> defs, std::vector<PluginInfo> infos) {
    std::vector<DefPtr> v;
    for (auto& d : defs) v.push_back(std::make_shared<const EffectDef>(std::move(d)));
    std::lock_guard lk(m_);
    defs_ = std::move(v);
    infos_ = std::move(infos);
}

void EffectRegistry::loadBuiltins() {
    std::vector<EffectDef> defs = builtinEffects();
    std::vector<PluginInfo> infos;
    for (const auto& d : defs) {
        PluginInfo i;
        i.id = d.id; i.name = d.name; i.version = d.version; i.author = d.author; i.description = d.description;
        i.kind = d.kind; i.builtin = true;
        infos.push_back(i);
    }
    setDefs(std::move(defs), std::move(infos));
}

EffectRegistry::DefPtr EffectRegistry::find(const QString& id) const {
    std::lock_guard lk(m_);
    for (const auto& d : defs_) if (d->id == id) return d;
    return nullptr;
}

std::vector<EffectRegistry::DefPtr> EffectRegistry::all(EffectDef::Kind kind) const {
    std::lock_guard lk(m_);
    std::vector<DefPtr> v;
    for (const auto& d : defs_) if (d->kind == kind) v.push_back(d);
    return v;
}

std::vector<PluginInfo> EffectRegistry::infos() const {
    std::lock_guard lk(m_);
    std::vector<PluginInfo> v = infos_;
    for (auto& i : v) {
        auto it = errors_.find(i.id);
        if (it != errors_.end() && i.ok) { i.error = it->second.second; i.ok = it->second.second.isEmpty(); }
    }
    return v;
}

void EffectRegistry::setCompileError(const QString& id, int revision, const QString& msg) {
    std::lock_guard lk(m_);
    errors_[id] = {revision, msg};
}

QString EffectRegistry::compileError(const QString& id) const {
    std::lock_guard lk(m_);
    auto it = errors_.find(id);
    return it == errors_.end() ? QString() : it->second.second;
}

} // namespace lc
