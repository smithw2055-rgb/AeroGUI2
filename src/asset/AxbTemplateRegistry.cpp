#include "aero/asset/AxbTemplateRegistry.hpp"
#include "aero/asset/AxbFormat.hpp"
#include "aero/binding/DualTrackBindingEngine.hpp"
#include "aero/core/RsAliases.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/core/UIResourceContext.hpp"
#include "aero/render/FontAtlas.hpp"
#include "aero/render/ImageAtlas.hpp"
#include "aero/style/StyleTriggerEngine.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace aero {

struct AxbTemplateRegistry::PendingResourceUpdate {
    IntrusivePtr<UIElement> target;
    std::uint16_t propId = 0u;
    UnifiedValue newValue{};
};

namespace {

std::string_view StripNamespacePrefix(std::string_view name) noexcept {
    if (const auto colon = name.rfind(':'); colon != std::string_view::npos) {
        name = name.substr(colon + 1);
    }
    if (const auto dot = name.rfind('.'); dot != std::string_view::npos) {
        name = name.substr(dot + 1);
    }
    return name;
}


const UIElement* NextResourceScopeAncestor(const UIElement* elem) noexcept {
    if (!elem) {
        return nullptr;
    }
    if (const UIElement* p = elem->Parent()) {
        return p;
    }
    return elem->TemplatedParent();
}

bool IsShadowedByCloserScopeLocked(
    const std::unordered_map<const UIElement*, std::unordered_map<std::string, UnifiedValue>>& scopedMap,
    const UIElement* target,
    const UIElement* outerScope,
    const std::string& key) noexcept {
    if (!target || !outerScope || scopedMap.empty()) {
        return false;
    }
    for (const UIElement* cur = target;
         cur != nullptr && cur != outerScope;
         cur = NextResourceScopeAncestor(cur)) {
        const auto scopeIt = scopedMap.find(cur);
        if (scopeIt != scopedMap.end() && scopeIt->second.find(key) != scopeIt->second.end()) {
            return true;
        }
    }
    return false;
}

void VisitPoolValue(const UnifiedValue& val, bool pinMode) noexcept {
    if (val.isTraceable()) {
        if (pinMode) {
            rs_rt::pinValueInPools(val);
        } else {
            rs_rt::markValueInPools(val);
        }
    }
}

void VisitStoryboardPoolValues(const AxbStoryboard& sb, bool pinMode) noexcept {
    for (const auto& tl : sb.timelines) {
        VisitPoolValue(tl.fromValue, pinMode);
        VisitPoolValue(tl.toValue, pinMode);
        for (const auto& kf : tl.keyFrames) {
            VisitPoolValue(kf.value, pinMode);
        }
    }
}

void VisitMultiBindingPoolValues(const AxbCompiledMultiBinding& mb, bool pinMode, int depth) noexcept;

void VisitBindingChainPoolValues(const AxbCompiledBindingChain& chain, bool pinMode, int depth) noexcept {
    if (depth > 32) {
        return;
    }
    VisitPoolValue(chain.fallbackValue, pinMode);
    VisitPoolValue(chain.targetNullValue, pinMode);
    if (chain.nestedMultiBinding) {
        VisitMultiBindingPoolValues(*chain.nestedMultiBinding, pinMode, depth + 1);
    }
}

void VisitMultiBindingPoolValues(const AxbCompiledMultiBinding& mb, bool pinMode, int depth) noexcept {
    if (depth > 32) {
        return;
    }
    VisitPoolValue(mb.fallbackValue, pinMode);
    for (const auto& src : mb.sources) {
        VisitBindingChainPoolValues(src, pinMode, depth + 1);
    }
}

void VisitInteractionActionPoolValues(const AxbInteractionAction& act, bool pinMode) noexcept {
    VisitPoolValue(act.value, pinMode);
    VisitStoryboardPoolValues(act.inlineStoryboard, pinMode);
    VisitPoolValue(act.commandParameter, pinMode);
    VisitBindingChainPoolValues(act.commandParameterBinding, pinMode, 0);
}

void VisitStyleTriggerPoolValues(const AxbStyleTrigger& tr, bool pinMode) noexcept {
    for (const auto& cond : tr.conditions) {
        VisitPoolValue(cond.expectedValue, pinMode);
    }
    for (const auto& s : tr.setters) {
        VisitPoolValue(s.value, pinMode);
    }
    for (const auto& sb : tr.enterStoryboards) {
        VisitStoryboardPoolValues(sb, pinMode);
    }
    for (const auto& sb : tr.exitStoryboards) {
        VisitStoryboardPoolValues(sb, pinMode);
    }
    for (const auto& act : tr.enterActions) {
        VisitInteractionActionPoolValues(act, pinMode);
    }
    for (const auto& act : tr.exitActions) {
        VisitInteractionActionPoolValues(act, pinMode);
    }
}

void VisitEventTriggerPoolValues(const AxbEventTrigger& et, bool pinMode) noexcept {
    for (const auto& sb : et.storyboards) {
        VisitStoryboardPoolValues(sb, pinMode);
    }
    for (const auto& act : et.actions) {
        VisitInteractionActionPoolValues(act, pinMode);
    }
}

void VisitVisualStateGroupPoolValues(const AxbVisualStateGroup& vsg, bool pinMode) noexcept {
    for (const auto& vs : vsg.states) {
        for (const auto& s : vs.setters) {
            VisitPoolValue(s.value, pinMode);
        }
        VisitStoryboardPoolValues(vs.storyboard, pinMode);
    }
}

void VisitPackagePoolValues(const AxbPackage& pkg, bool pinMode, int depth = 0) noexcept;

void VisitControlTemplatePoolValues(
    const AxbControlTemplate& ct,
    bool pinMode,
    int depth = 0) noexcept {
    if (depth > 32) {
        return;
    }
    if (ct.templatePackage) {
        VisitPackagePoolValues(*ct.templatePackage, pinMode, depth + 1);
    }
    for (const auto& tr : ct.triggers) {
        VisitStyleTriggerPoolValues(tr, pinMode);
    }
    for (const auto& et : ct.eventTriggers) {
        VisitEventTriggerPoolValues(et, pinMode);
    }
    for (const auto& vsg : ct.visualStateGroups) {
        VisitVisualStateGroupPoolValues(vsg, pinMode);
    }
}

void VisitStylePoolValues(
    const AxbStyleDefinition& st,
    bool pinMode,
    int depth = 0) noexcept {
    if (depth > 32) {
        return;
    }
    if (st.inlineControlTemplate) {
        VisitControlTemplatePoolValues(*st.inlineControlTemplate, pinMode, depth + 1);
    }
    for (const auto& s : st.setters) {
        VisitPoolValue(s.value, pinMode);
    }
    for (const auto& tr : st.triggers) {
        VisitStyleTriggerPoolValues(tr, pinMode);
    }
    for (const auto& et : st.eventTriggers) {
        VisitEventTriggerPoolValues(et, pinMode);
    }
    for (const auto& vsg : st.visualStateGroups) {
        VisitVisualStateGroupPoolValues(vsg, pinMode);
    }
}

void VisitPackagePoolValues(const AxbPackage& pkg, bool pinMode, int depth) noexcept {
    if (depth > 32) {
        return;
    }
    if (pinMode) {
        pkg.PinValuesInPools();
    } else {
        pkg.MarkValuesInPools();
    }
    for (const auto& kv : pkg.resources) {
        VisitPoolValue(kv.second, pinMode);
    }
    for (const auto& st : pkg.styles) {
        VisitStylePoolValues(st, pinMode, depth + 1);
    }
    for (const auto& sb : pkg.storyboards) {
        VisitStoryboardPoolValues(sb, pinMode);
    }
    for (const auto& dt : pkg.dataTemplates) {
        if (dt.templatePackage) {
            VisitPackagePoolValues(*dt.templatePackage, pinMode, depth + 1);
        }
    }
    for (const auto& ct : pkg.controlTemplates) {
        VisitControlTemplatePoolValues(ct, pinMode, depth + 1);
    }
}

struct LiveAxbTemplateRegistrySet {
    std::mutex mutex;
    std::vector<AxbTemplateRegistry*> registries;

    static LiveAxbTemplateRegistrySet& Instance() noexcept {
        static LiveAxbTemplateRegistrySet sSet;
        return sSet;
    }

    void Add(AxbTemplateRegistry* reg) {
        if (!reg) return;
        std::lock_guard<std::mutex> lock(mutex);
        for (auto* existing : registries) {
            if (existing == reg) return;
        }
        registries.push_back(reg);
    }

    bool Remove(AxbTemplateRegistry* reg) noexcept {
        if (!reg) return false;
        std::lock_guard<std::mutex> lock(mutex);
        registries.erase(
            std::remove(registries.begin(), registries.end(), reg),
            registries.end());
        return registries.empty();
    }

    bool Contains(const AxbTemplateRegistry* reg) noexcept {
        if (!reg) return false;
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto* existing : registries) {
            if (existing == reg) return true;
        }
        return false;
    }

    void UnsubscribeElementAll(const UIElement* elem) noexcept {
        if (!elem) return;
        std::lock_guard<std::mutex> lock(mutex);
        for (auto* reg : registries) {
            if (reg && reg->HasAnyDynamicSubscriptions()) {
                reg->UnsubscribeElement(elem);
            }
        }
    }

    template <typename Fn>
    void ForEachLocked(Fn&& fn) {
        std::lock_guard<std::mutex> lock(mutex);
        fn(registries);
    }
};

} // namespace

AxbTemplateRegistry& AxbTemplateRegistry::Instance() noexcept {
    return UIResourceContext::Current().Templates();
}

bool AxbTemplateRegistry::IsLiveRegistry(const AxbTemplateRegistry* registry) noexcept {
    return LiveAxbTemplateRegistrySet::Instance().Contains(registry);
}

AxbTemplateRegistry::AxbTemplateRegistry() noexcept {
    LiveAxbTemplateRegistrySet::Instance().Add(this);
    rs_rt::registerExternalValuePoolRootMarker(
        this,
        [this]() noexcept {
            MarkValuesInPools();
        });
    static std::once_flag sResourceHooksOnce;
    std::call_once(sResourceHooksOnce, []() noexcept {
        RegisterUIElementResourceHooks(
            [](const UIElement* elem) noexcept {
                if (!elem) {
                    return;
                }
                LiveAxbTemplateRegistrySet::Instance().UnsubscribeElementAll(elem);
            },
            [](UIElement* elem, std::string_view locSourceUri) {
                if (!elem || locSourceUri.empty()) {
                    return;
                }
                if (UIResourceContext::HasActiveThreadOverride()) {
                    (void)AxbTemplateRegistry::Instance().ApplyRegisteredDictionary(locSourceUri, elem);
                    return;
                }
                for (const UIElement* cur = elem;
                     cur != nullptr;
                     cur = NextResourceScopeAncestor(cur)) {
                    DualTrackBindingEngine* engine = cur->BoundTwoWayEngine();
                    if (!engine && cur->BoundStyleEngine()) {
                        engine = cur->BoundStyleEngine()->GetBindingEngine();
                    }
                    if (engine) {
                        if (std::shared_ptr<UIResourceContext> domainCtx =
                                UIResourceContext::FindForDomains(&engine->Domains())) {
                            ScopedUIResourceContext scope(std::move(domainCtx));
                            (void)AxbTemplateRegistry::Instance().ApplyRegisteredDictionary(locSourceUri, elem);
                            return;
                        }
                    }
                }

                std::vector<PendingResourceUpdate> updates;
                std::vector<HostWakeEntry> wakeList;
                bool handledUnderSetLock = false;
                LiveAxbTemplateRegistrySet::Instance().ForEachLocked(
                    [&](const std::vector<AxbTemplateRegistry*>& liveRegs) {
                        AxbTemplateRegistry* targetReg = nullptr;
                        for (auto* reg : liveRegs) {
                            if (reg && reg->HasSubscriptionInSubtree(elem) &&
                                reg->FindDictionary(locSourceUri) != nullptr) {
                                targetReg = reg;
                                break;
                            }
                        }
                        if (!targetReg) {
                            AxbTemplateRegistry& fallbackReg = AxbTemplateRegistry::Instance();
                            if (fallbackReg.FindDictionary(locSourceUri) != nullptr) {
                                targetReg = &fallbackReg;
                            }
                        }
                        if (!targetReg) {
                            for (auto* reg : liveRegs) {
                                if (reg && reg->FindDictionary(locSourceUri) != nullptr) {
                                    targetReg = reg;
                                    break;
                                }
                            }
                        }
                        if (!targetReg) {
                            return;
                        }
                        const AxbPackage* dictPtr = targetReg->FindDictionary(locSourceUri);
                        if (!dictPtr) {
                            return;
                        }
                        const AxbPackage dictCopy = *dictPtr;
                        targetReg->CollectDynamicDictionaryUpdates(dictCopy, elem, updates, wakeList);
                        for (const auto& upd : updates) {
                            if (upd.propId == PropId_PathData) {
                                if (const auto* sv = std::get_if<StringValue>(&upd.newValue)) {
                                    (void)targetReg->FindOrBakeMtsdfPath(sv->view());
                                }
                            }
                        }
                        handledUnderSetLock = true;
                    });
                if (!handledUnderSetLock) {
                    (void)AxbTemplateRegistry::Instance().ApplyRegisteredDictionary(locSourceUri, elem);
                    return;
                }
                for (const auto& upd : updates) {
                    ApplyResolvedResourceToElement(upd.target.Get(), upd.propId, upd.newValue);
                }
                for (const auto& wake : wakeList) {
                    if (wake.callback) {
                        wake.callback(wake.userData);
                    }
                }
            });
    });
}

AxbTemplateRegistry::~AxbTemplateRegistry() {
    (void)LiveAxbTemplateRegistrySet::Instance().Remove(this);
    rs_rt::unregisterExternalValuePoolRootMarker(this);
}

void AxbTemplateRegistry::MarkValuesInPools() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& kv : templates_) {
        VisitPackagePoolValues(kv.second, false);
    }
    for (const auto& kv : registeredDictionaries_) {
        VisitPackagePoolValues(kv.second, false);
    }
    for (const auto& kv : resources_) {
        VisitPoolValue(kv.second, false);
    }
    for (const auto& kv : dynamicOverrides_) {
        VisitPoolValue(kv.second, false);
    }
    for (const auto& scopeKv : scopedDynamicOverrides_) {
        for (const auto& resKv : scopeKv.second) {
            VisitPoolValue(resKv.second, false);
        }
    }
    for (const auto& kv : styles_) {
        VisitStylePoolValues(kv.second, false);
    }
    for (const auto& kv : implicitStyles_) {
        VisitStylePoolValues(kv.second, false);
    }
    for (const auto& kv : storyboards_) {
        VisitStoryboardPoolValues(kv.second, false);
    }
    for (const auto& kv : dataTemplates_) {
        if (kv.second) VisitPackagePoolValues(*kv.second, false);
    }
    for (const auto& kv : implicitDataTemplates_) {
        if (kv.second) VisitPackagePoolValues(*kv.second, false);
    }
    for (const auto& kv : controlTemplates_) {
        VisitControlTemplatePoolValues(kv.second, false);
    }
    for (const auto& kv : implicitControlTemplates_) {
        VisitControlTemplatePoolValues(kv.second, false);
    }
}

const AxbMtsdfPathEntry* AxbTemplateRegistry::InsertMtsdfPathLocked(AxbMtsdfPathEntry entry) {
    mtsdfLookupEpoch_.fetch_add(1u, std::memory_order_release);
    if (entry.pathHash == 0) {
        return nullptr;
    }
    if (const auto existing = mtsdfPaths_.find(entry.pathHash); existing != mtsdfPaths_.end()) {
        return &existing->second;
    }

    // Assign a globally unique, non-overlapping UV slot in the bottom half [0.5..1.0) of the unified UI/Glyph atlas
    const std::uint16_t safeCell = std::clamp<std::uint16_t>(
        entry.cellWidth == 0 ? 32u : entry.cellWidth, 8u, 128u);
    entry.cellWidth = safeCell;
    entry.cellHeight = safeCell;

    const std::uint32_t slot = nextMtsdfAtlasSlot_++;
    std::uint16_t atlasDim = 1024u;
    while (atlasDim < 4096u) {
        const std::uint32_t cols = std::max<std::uint32_t>(1u, atlasDim / safeCell);
        const std::uint32_t iconRows = std::max<std::uint32_t>(1u, cols / 2u);
        if (slot < cols * iconRows) {
            break;
        }
        atlasDim = static_cast<std::uint16_t>(atlasDim * 2u);
    }

    const std::uint32_t cols = std::max<std::uint32_t>(1u, atlasDim / safeCell);
    const std::uint32_t rowBase = std::max<std::uint32_t>(1u, cols / 2u);
    const std::uint32_t iconRows = std::max<std::uint32_t>(1u, cols - rowBase);
    const std::uint32_t slotCol = slot % cols;
    const std::uint32_t slotRow = rowBase + ((slot / cols) % iconRows);
    const float invAtlas = 1.0f / static_cast<float>(atlasDim);

    entry.atlasWidth = atlasDim;
    entry.atlasHeight = atlasDim;
    entry.u0 = (static_cast<float>(slotCol * safeCell) + 0.5f) * invAtlas;
    entry.v0 = (static_cast<float>(slotRow * safeCell) + 0.5f) * invAtlas;
    entry.u1 = (static_cast<float>((slotCol + 1u) * safeCell) - 0.5f) * invAtlas;
    entry.v1 = (static_cast<float>((slotRow + 1u) * safeCell) - 0.5f) * invAtlas;

    auto [insertedIt, _] = mtsdfPaths_.emplace(entry.pathHash, std::move(entry));
    return &insertedIt->second;
}

void AxbTemplateRegistry::Register(std::string className, AxbPackage package) {
    VisitPackagePoolValues(package, true);
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& kv : package.resources) {
        if (!kv.first.empty()) {
            resources_[kv.first] = kv.second;
        }
    }
    for (const auto& mp : package.mtsdfPaths) {
        if (mp.pathHash != 0) {
            (void)InsertMtsdfPathLocked(mp);
        }
    }
    for (const auto& st : package.styles) {
        if (!st.key.empty()) {
            styles_[st.key] = st;
        } else if (!st.targetTypeName.empty()) {
            implicitStyles_[std::string(StripNamespacePrefix(st.targetTypeName))] = st;
        }
    }
    for (const auto& sb : package.storyboards) {
        if (!sb.key.empty()) {
            storyboards_[sb.key] = sb;
        }
    }
    for (const auto& dt : package.dataTemplates) {
        if (!dt.templatePackage) continue;
        if (!dt.key.empty()) {
            dataTemplates_[dt.key] = dt.templatePackage;
            dataTemplateEntries_[dt.key] = dt;
        }
        if (!dt.dataTypeName.empty()) {
            const std::string shortType(StripNamespacePrefix(dt.dataTypeName));
            implicitDataTemplates_[shortType] = dt.templatePackage;
            implicitDataTemplateEntries_[shortType] = dt;
        }
        for (const auto& kv : dt.templatePackage->resources) {
            if (!kv.first.empty()) resources_[kv.first] = kv.second;
        }
        for (const auto& mp : dt.templatePackage->mtsdfPaths) {
            if (mp.pathHash != 0) (void)InsertMtsdfPathLocked(mp);
        }
        for (const auto& st : dt.templatePackage->styles) {
            if (!st.key.empty()) {
                styles_[st.key] = st;
            } else if (!st.targetTypeName.empty()) {
                implicitStyles_[std::string(StripNamespacePrefix(st.targetTypeName))] = st;
            }
        }
        for (const auto& sb : dt.templatePackage->storyboards) {
            if (!sb.key.empty()) storyboards_[sb.key] = sb;
        }
    }
    for (const auto& ct : package.controlTemplates) {
        if (!ct.templatePackage) continue;
        if (!ct.key.empty()) {
            controlTemplates_[ct.key] = ct;
        }
        for (const auto& kv : ct.templatePackage->resources) {
            if (!kv.first.empty()) resources_[kv.first] = kv.second;
        }
        for (const auto& mp : ct.templatePackage->mtsdfPaths) {
            if (mp.pathHash != 0) (void)InsertMtsdfPathLocked(mp);
        }
        for (const auto& st : ct.templatePackage->styles) {
            if (!st.key.empty()) {
                styles_[st.key] = st;
            } else if (!st.targetTypeName.empty()) {
                implicitStyles_[std::string(StripNamespacePrefix(st.targetTypeName))] = st;
            }
        }
        for (const auto& sb : ct.templatePackage->storyboards) {
            if (!sb.key.empty()) storyboards_[sb.key] = sb;
        }
        if (!ct.key.empty()) {
            controlTemplates_[ct.key] = ct;
        }
        if (!ct.targetTypeName.empty() && ct.key.empty()) {
            implicitControlTemplates_[std::string(StripNamespacePrefix(ct.targetTypeName))] = ct;
        }
    }
    if (className.empty()) {
        return;
    }
    const std::string shortName(StripNamespacePrefix(className));
    templates_[className] = package;
    if (shortName != className) {
        templates_[shortName] = std::move(package);
    }
}

void AxbTemplateRegistry::RegisterResources(const AxbPackage& package) {
    VisitPackagePoolValues(package, true);
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& kv : package.resources) {
        if (!kv.first.empty()) {
            resources_[kv.first] = kv.second;
        }
    }
    for (const auto& mp : package.mtsdfPaths) {
        if (mp.pathHash != 0) {
            (void)InsertMtsdfPathLocked(mp);
        }
    }
    for (const auto& st : package.styles) {
        if (!st.key.empty()) {
            styles_[st.key] = st;
        } else if (!st.targetTypeName.empty()) {
            implicitStyles_[std::string(StripNamespacePrefix(st.targetTypeName))] = st;
        }
    }
    for (const auto& sb : package.storyboards) {
        if (!sb.key.empty()) {
            storyboards_[sb.key] = sb;
        }
    }
    for (const auto& dt : package.dataTemplates) {
        if (!dt.templatePackage) continue;
        if (!dt.key.empty()) {
            dataTemplates_[dt.key] = dt.templatePackage;
            dataTemplateEntries_[dt.key] = dt;
        }
        if (!dt.dataTypeName.empty()) {
            const std::string shortType(StripNamespacePrefix(dt.dataTypeName));
            implicitDataTemplates_[shortType] = dt.templatePackage;
            implicitDataTemplateEntries_[shortType] = dt;
        }
        for (const auto& kv : dt.templatePackage->resources) {
            if (!kv.first.empty()) resources_[kv.first] = kv.second;
        }
        for (const auto& mp : dt.templatePackage->mtsdfPaths) {
            if (mp.pathHash != 0) (void)InsertMtsdfPathLocked(mp);
        }
        for (const auto& st : dt.templatePackage->styles) {
            if (!st.key.empty()) {
                styles_[st.key] = st;
            } else if (!st.targetTypeName.empty()) {
                implicitStyles_[std::string(StripNamespacePrefix(st.targetTypeName))] = st;
            }
        }
        for (const auto& sb : dt.templatePackage->storyboards) {
            if (!sb.key.empty()) storyboards_[sb.key] = sb;
        }
    }
    for (const auto& ct : package.controlTemplates) {
        if (!ct.templatePackage) continue;
        if (!ct.key.empty()) {
            controlTemplates_[ct.key] = ct;
        }
        for (const auto& kv : ct.templatePackage->resources) {
            if (!kv.first.empty()) resources_[kv.first] = kv.second;
        }
        for (const auto& mp : ct.templatePackage->mtsdfPaths) {
            if (mp.pathHash != 0) (void)InsertMtsdfPathLocked(mp);
        }
        for (const auto& st : ct.templatePackage->styles) {
            if (!st.key.empty()) {
                styles_[st.key] = st;
            } else if (!st.targetTypeName.empty()) {
                implicitStyles_[std::string(StripNamespacePrefix(st.targetTypeName))] = st;
            }
        }
        for (const auto& sb : ct.templatePackage->storyboards) {
            if (!sb.key.empty()) storyboards_[sb.key] = sb;
        }
        if (!ct.key.empty()) {
            controlTemplates_[ct.key] = ct;
        }
        if (!ct.targetTypeName.empty() && ct.key.empty()) {
            implicitControlTemplates_[std::string(StripNamespacePrefix(ct.targetTypeName))] = ct;
        }
    }
}

void AxbTemplateRegistry::RegisterStyle(AxbStyleDefinition style) {
    VisitStylePoolValues(style, true);
    std::lock_guard<std::mutex> lock(mutex_);
    if (!style.key.empty()) {
        styles_[style.key] = std::move(style);
    } else if (!style.targetTypeName.empty()) {
        const std::string shortType(StripNamespacePrefix(style.targetTypeName));
        implicitStyles_[shortType] = std::move(style);
    }
}

void AxbTemplateRegistry::RegisterStoryboard(AxbStoryboard storyboard) {
    if (!storyboard.key.empty()) {
        VisitStoryboardPoolValues(storyboard, true);
        std::lock_guard<std::mutex> lock(mutex_);
        storyboards_[storyboard.key] = std::move(storyboard);
    }
}

const AxbDataTemplate* AxbTemplateRegistry::FindDataTemplateEntry(std::string_view key) const noexcept {
    if (key.empty()) return nullptr;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = dataTemplateEntries_.find(std::string(key));
    return (it != dataTemplateEntries_.end()) ? &it->second : nullptr;
}

const AxbDataTemplate* AxbTemplateRegistry::FindImplicitDataTemplateEntry(
    std::string_view shortDataTypeName) const noexcept {
    if (shortDataTypeName.empty()) return nullptr;
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string shortName(StripNamespacePrefix(shortDataTypeName));
    const auto it = implicitDataTemplateEntries_.find(shortName);
    return (it != implicitDataTemplateEntries_.end()) ? &it->second : nullptr;
}

void AxbTemplateRegistry::RegisterDataTemplate(AxbDataTemplate dataTemplate) {
    if (!dataTemplate.key.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        dataTemplateEntries_[dataTemplate.key] = dataTemplate;
    }
    if (!dataTemplate.dataTypeName.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        implicitDataTemplateEntries_[std::string(StripNamespacePrefix(dataTemplate.dataTypeName))] =
            dataTemplate;
    }
    if (!dataTemplate.templatePackage) {
        return;
    }
    VisitPackagePoolValues(*dataTemplate.templatePackage, true);
    std::lock_guard<std::mutex> lock(mutex_);
    if (!dataTemplate.key.empty()) {
        dataTemplates_[dataTemplate.key] = dataTemplate.templatePackage;
    }
    if (!dataTemplate.dataTypeName.empty()) {
        implicitDataTemplates_[std::string(StripNamespacePrefix(dataTemplate.dataTypeName))] =
            dataTemplate.templatePackage;
    }
    for (const auto& kv : dataTemplate.templatePackage->resources) {
        if (!kv.first.empty()) resources_[kv.first] = kv.second;
    }
    for (const auto& mp : dataTemplate.templatePackage->mtsdfPaths) {
        if (mp.pathHash != 0) (void)InsertMtsdfPathLocked(mp);
    }
    for (const auto& st : dataTemplate.templatePackage->styles) {
        if (!st.key.empty()) {
            styles_[st.key] = st;
        } else if (!st.targetTypeName.empty()) {
            implicitStyles_[std::string(StripNamespacePrefix(st.targetTypeName))] = st;
        }
    }
    for (const auto& sb : dataTemplate.templatePackage->storyboards) {
        if (!sb.key.empty()) storyboards_[sb.key] = sb;
    }
}

const AxbPackage* AxbTemplateRegistry::FindDataTemplate(std::string_view key) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = dataTemplates_.find(std::string(key));
    if (it != dataTemplates_.end() && it->second) {
        return it->second.get();
    }
    return nullptr;
}

const AxbPackage* AxbTemplateRegistry::FindImplicitDataTemplate(
    std::string_view shortDataTypeName) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string shortName(StripNamespacePrefix(shortDataTypeName));
    const auto it = implicitDataTemplates_.find(shortName);
    if (it != implicitDataTemplates_.end() && it->second) {
        return it->second.get();
    }
    return nullptr;
}

const AxbStyleDefinition* AxbTemplateRegistry::FindStyle(std::string_view key) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = styles_.find(std::string(key));
    if (it != styles_.end()) {
        return &it->second;
    }
    return nullptr;
}

const AxbStyleDefinition* AxbTemplateRegistry::FindImplicitStyle(
    std::string_view targetTypeName) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string shortType(StripNamespacePrefix(targetTypeName));
    const auto it = implicitStyles_.find(shortType);
    if (it != implicitStyles_.end()) {
        return &it->second;
    }
    return nullptr;
}

const AxbStoryboard* AxbTemplateRegistry::FindStoryboard(std::string_view key) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = storyboards_.find(std::string(key));
    if (it != storyboards_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<AxbStyleDefinition> AxbTemplateRegistry::RegisteredStyles() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AxbStyleDefinition> out;
    out.reserve(styles_.size() + implicitStyles_.size());
    for (const auto& kv : styles_) out.push_back(kv.second);
    for (const auto& kv : implicitStyles_) out.push_back(kv.second);
    return out;
}

std::vector<AxbStoryboard> AxbTemplateRegistry::RegisteredStoryboards() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AxbStoryboard> out;
    out.reserve(storyboards_.size());
    for (const auto& kv : storyboards_) out.push_back(kv.second);
    return out;
}

void AxbTemplateRegistry::RegisterControlTemplate(AxbControlTemplate controlTemplate) {
    if (!controlTemplate.templatePackage) {
        return;
    }
    VisitControlTemplatePoolValues(controlTemplate, true);

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& kv : controlTemplate.templatePackage->resources) {
        if (!kv.first.empty()) resources_[kv.first] = kv.second;
    }
    for (const auto& mp : controlTemplate.templatePackage->mtsdfPaths) {
        if (mp.pathHash != 0) (void)InsertMtsdfPathLocked(mp);
    }
    for (const auto& st : controlTemplate.templatePackage->styles) {
        if (!st.key.empty()) {
            styles_[st.key] = st;
        } else if (!st.targetTypeName.empty()) {
            implicitStyles_[std::string(StripNamespacePrefix(st.targetTypeName))] = st;
        }
    }
    for (const auto& sb : controlTemplate.templatePackage->storyboards) {
        if (!sb.key.empty()) storyboards_[sb.key] = sb;
    }
    if (!controlTemplate.key.empty()) {
        controlTemplates_[controlTemplate.key] = std::move(controlTemplate);
    } else if (!controlTemplate.targetTypeName.empty()) {
        const std::string shortType(StripNamespacePrefix(controlTemplate.targetTypeName));
        implicitControlTemplates_[shortType] = std::move(controlTemplate);
    }
}

const AxbControlTemplate* AxbTemplateRegistry::FindControlTemplate(std::string_view key) const noexcept {
    if (key.empty()) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = controlTemplates_.find(std::string(key));
    if (it != controlTemplates_.end()) {
        return &it->second;
    }
    return nullptr;
}

const AxbControlTemplate* AxbTemplateRegistry::FindImplicitControlTemplate(
    std::string_view targetTypeName) const noexcept {
    const std::string shortName(StripNamespacePrefix(targetTypeName));
    if (shortName.empty()) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = implicitControlTemplates_.find(shortName);
    if (it != implicitControlTemplates_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<AxbControlTemplate> AxbTemplateRegistry::RegisteredControlTemplates() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AxbControlTemplate> out;
    out.reserve(controlTemplates_.size() + implicitControlTemplates_.size());
    for (const auto& kv : controlTemplates_) out.push_back(kv.second);
    for (const auto& kv : implicitControlTemplates_) out.push_back(kv.second);
    return out;
}

void AxbTemplateRegistry::RegisterResource(std::string key, UnifiedValue value) {
    if (!key.empty()) {
        VisitPoolValue(value, true);
        std::lock_guard<std::mutex> lock(mutex_);
        resources_[std::move(key)] = std::move(value);
    }
}

void AxbTemplateRegistry::RegisterMtsdfPath(AxbMtsdfPathEntry entry) {
    if (entry.pathHash != 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        (void)InsertMtsdfPathLocked(std::move(entry));
    }
}

const UnifiedValue* AxbTemplateRegistry::FindScopedOverrideLocked(
    std::string_view key,
    const UIElement* target) const noexcept {
    if (!target || key.empty() || scopedDynamicOverrides_.empty()) {
        return nullptr;
    }
    const std::string k(key);
    for (const UIElement* cur = target; cur != nullptr; cur = NextResourceScopeAncestor(cur)) {
        const auto scopeIt = scopedDynamicOverrides_.find(cur);
        if (scopeIt != scopedDynamicOverrides_.end()) {
            const auto resIt = scopeIt->second.find(k);
            if (resIt != scopeIt->second.end()) {
                return &resIt->second;
            }
        }
    }
    return nullptr;
}

const UnifiedValue* AxbTemplateRegistry::FindResource(std::string_view key) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string k(key);
    const auto dynIt = dynamicOverrides_.find(k);
    if (dynIt != dynamicOverrides_.end()) {
        return &dynIt->second;
    }
    const auto it = resources_.find(k);
    if (it != resources_.end()) {
        return &it->second;
    }
    return nullptr;
}

const UnifiedValue* AxbTemplateRegistry::FindDynamicResource(std::string_view key) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = dynamicOverrides_.find(std::string(key));
    if (it != dynamicOverrides_.end()) {
        return &it->second;
    }
    return nullptr;
}

const UnifiedValue* AxbTemplateRegistry::FindResourceForElement(
    std::string_view key,
    const UIElement* target) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (const UnifiedValue* scoped = FindScopedOverrideLocked(key, target)) {
        return scoped;
    }
    const std::string k(key);
    const auto dynIt = dynamicOverrides_.find(k);
    if (dynIt != dynamicOverrides_.end()) {
        return &dynIt->second;
    }
    const auto it = resources_.find(k);
    if (it != resources_.end()) {
        return &it->second;
    }
    return nullptr;
}

const UnifiedValue* AxbTemplateRegistry::FindDynamicResourceForElement(
    std::string_view key,
    const UIElement* target) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (const UnifiedValue* scoped = FindScopedOverrideLocked(key, target)) {
        return scoped;
    }
    const auto it = dynamicOverrides_.find(std::string(key));
    if (it != dynamicOverrides_.end()) {
        return &it->second;
    }
    return nullptr;
}

namespace {

bool IsPotentialColorLiteralString(std::string_view raw) noexcept {
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.front()))) {
        raw.remove_prefix(1);
    }
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.back()))) {
        raw.remove_suffix(1);
    }
    if (raw.empty()) {
        return false;
    }
    if (raw.front() == '#') {
        return true;
    }
    if (raw.size() > 3u &&
        (raw[0] == 's' || raw[0] == 'S') &&
        (raw[1] == 'c' || raw[1] == 'C') &&
        raw[2] == '#') {
        return true;
    }
    // File paths / URIs contain '.', '/', '\', or ':'
    if (raw.find('.') != std::string_view::npos ||
        raw.find('/') != std::string_view::npos ||
        raw.find('\\') != std::string_view::npos ||
        raw.find(':') != std::string_view::npos) {
        return false;
    }
    return true;
}

std::string NormalizeDictLookupKey(std::string_view uri) {
    while (!uri.empty() && std::isspace(static_cast<unsigned char>(uri.front()))) {
        uri.remove_prefix(1);
    }
    while (!uri.empty() && std::isspace(static_cast<unsigned char>(uri.back()))) {
        uri.remove_suffix(1);
    }
    std::string norm;
    norm.reserve(uri.size());
    for (char ch : uri) {
        norm.push_back(ch == '\\' ? '/' : ch);
    }
    if (const auto semi = norm.find(";component/"); semi != std::string::npos) {
        norm = norm.substr(semi + 11u);
    } else if (norm.rfind("pack://application:,,,/", 0) == 0) {
        norm = norm.substr(23u);
    } else if (norm.rfind("pack://siteoforigin:,,,/", 0) == 0) {
        norm = norm.substr(24u);
    }
    while (norm.rfind("./", 0) == 0) {
        norm.erase(0, 2u);
    }
    return norm;
}

std::string ExtractBasenameKey(std::string_view normPath) {
    const auto slash = normPath.rfind('/');
    return std::string(slash == std::string_view::npos ? normPath : normPath.substr(slash + 1u));
}

} // namespace

void AxbTemplateRegistry::ApplyResolvedResourceToElement(
    UIElement* target,
    std::uint16_t propId,
    const UnifiedValue& rawVal) {
    if (!target || std::holds_alternative<std::monostate>(rawVal)) {
        return;
    }

    if (propId == PropId_PathData) {
        if (const auto* sv = std::get_if<StringValue>(&rawVal)) {
            (void)AxbTemplateRegistry::Instance().FindOrBakeMtsdfPath(sv->view());
        }
        target->SetValue(propId, rawVal, ValueSourceRank::Local);
        target->InvalidateLayout();
        target->InvalidateRender();
        return;
    }

    if (propId == PropId_Background || propId == PropId_Foreground || propId == PropId_BorderBrush) {
        if (const auto* sv = std::get_if<StringValue>(&rawVal)) {
            const std::string_view text = sv->view();
            if (IsPotentialColorLiteralString(text)) {
                target->SetValue(
                    propId,
                    value::FromColor(ParseXamlColorLiteral(text)),
                    ValueSourceRank::Local);
            } else {
                // ImageBrush / BitmapImage URI assigned to Background (e.g. Localization Flag ImageBrush)
                target->SetValue(propId, rawVal, ValueSourceRank::Local);
                if (propId == PropId_Background) {
                    target->SetValue(PropId_ImageSource, rawVal, ValueSourceRank::Local);
                }
            }
        } else {
            target->SetValue(propId, rawVal, ValueSourceRank::Local);
        }
        target->InvalidateRender();
        return;
    }

    if (const auto* sv = std::get_if<StringValue>(&rawVal)) {
        switch (propId) {
            case PropId_Text:
            case PropId_Title:
            case PropId_Content:
            case PropId_Placeholder:
            case PropId_ImageSource:
            case PropId_FontFamily:
            case PropId_GroupName:
            case PropId_Style:
            case PropId_ItemTemplate:
            case PropId_MergedDictionarySource:
            case PropId_LocSource:
            case PropId_FlowDirection:
            case PropId_Tag:
                target->SetValue(propId, rawVal, ValueSourceRank::Local);
                break;
            default:
                target->SetValue(
                    propId,
                    ParseBuiltinLiteralPropertyValue(propId, sv->view()),
                    ValueSourceRank::Local);
                break;
        }
    } else {
        target->SetValue(propId, rawVal, ValueSourceRank::Local);
    }

    if (propId == PropId_Text || propId == PropId_Title || propId == PropId_Content ||
        propId == PropId_ImageSource || propId == PropId_FontSize ||
        propId == PropId_FontFamily || propId == PropId_FlowDirection ||
        propId == PropId_Width || propId == PropId_Height ||
        propId == PropId_Margin || propId == PropId_Padding ||
        propId == PropId_Visibility) {
        target->InvalidateLayout();
    }
    target->InvalidateRender();
}

bool AxbTemplateRegistry::IsSubscriptionAttachedLocked(const DynamicResourceSubscription& sub) noexcept {
    if (!sub.target) {
        return false;
    }
    if (sub.mountRoot != nullptr && sub.target != sub.mountRoot) {
        bool attachedToMountRoot = false;
        for (const UIElement* cur = NextResourceScopeAncestor(sub.target);
             cur != nullptr;
             cur = NextResourceScopeAncestor(cur)) {
            if (cur == sub.mountRoot) {
                attachedToMountRoot = true;
                break;
            }
        }
        if (!attachedToMountRoot) {
            return false;
        }
    } else if (sub.hadAncestorWhenBound &&
               sub.target->Parent() == nullptr &&
               !sub.target->IsConnectedToRoot()) {
        return false;
    }
    return true;
}

bool AxbTemplateRegistry::IsWithinScopeRoot(
    const UIElement* target,
    const UIElement* scopeRoot) noexcept {
    if (!scopeRoot) {
        return true;
    }
    for (const UIElement* cur = target; cur != nullptr; cur = NextResourceScopeAncestor(cur)) {
        if (cur == scopeRoot) {
            return true;
        }
    }
    return false;
}

void AxbTemplateRegistry::SnapshotWakeListenersLocked(std::vector<HostWakeEntry>& outListeners) const {
    outListeners.clear();
    outListeners.reserve(hostWakeListeners_.size() + (customWakeCallback_ ? 1u : 0u));
    for (const auto& entry : hostWakeListeners_) {
        if (entry.callback) {
            outListeners.push_back(entry);
        }
    }
    if (customWakeCallback_) {
        outListeners.push_back(HostWakeEntry{nullptr, customWakeCallback_, customWakeUserData_});
    }
}

void AxbTemplateRegistry::SubscribeDynamicResource(
    UIElement* target,
    UIElement* mountRoot,
    std::uint16_t propId,
    std::string_view resourceKey,
    std::uint8_t flags) {
    if (!target || resourceKey.empty() ||
        (flags & (AxbResourceFlag_DynamicResource | AxbResourceFlag_Loc)) == 0u) {
        return;
    }
    const bool hadParent = (target->Parent() != nullptr) || target->IsConnectedToRoot();
    UnifiedValue immediateScopedValue{};
    bool hasImmediateScoped = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (const UnifiedValue* scopedVal = FindScopedOverrideLocked(resourceKey, target)) {
            immediateScopedValue = *scopedVal;
            hasImmediateScoped = true;
        }
        bool updatedExisting = false;
        for (auto& existing : dynamicSubscriptions_) {
            if (existing.target == target && existing.propId == propId) {
                existing.mountRoot = mountRoot;
                existing.flags = flags;
                existing.hadAncestorWhenBound = hadParent;
                existing.resourceKey.assign(resourceKey);
                hasSubscriptions_.store(true, std::memory_order_release);
                updatedExisting = true;
                break;
            }
        }
        if (!updatedExisting) {
            DynamicResourceSubscription sub{};
            sub.target = target;
            sub.mountRoot = mountRoot;
            sub.propId = propId;
            sub.flags = flags;
            sub.hadAncestorWhenBound = hadParent;
            sub.resourceKey.assign(resourceKey);
            dynamicSubscriptions_.push_back(std::move(sub));
            hasSubscriptions_.store(true, std::memory_order_release);
        }
    }
    if (hasImmediateScoped) {
        ScopedUIResourceContext ownerScope(
            UIResourceContext::HasActiveThreadOverride() ? nullptr : ownerContext_);
        ApplyResolvedResourceToElement(target, propId, immediateScopedValue);
    }
}

bool AxbTemplateRegistry::HasSubscriptionInSubtree(const UIElement* scopeRoot) const noexcept {
    if (!scopeRoot || !hasSubscriptions_.load(std::memory_order_acquire)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (scopedDynamicOverrides_.find(scopeRoot) != scopedDynamicOverrides_.end()) {
        return true;
    }
    for (const auto& sub : dynamicSubscriptions_) {
        if (IsSubscriptionAttachedLocked(sub) && IsWithinScopeRoot(sub.target, scopeRoot)) {
            return true;
        }
    }
    return false;
}

void AxbTemplateRegistry::UnsubscribeElement(const UIElement* element) noexcept {
    if (!element || !hasSubscriptions_.load(std::memory_order_acquire)) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    scopedDynamicOverrides_.erase(element);
    dynamicSubscriptions_.erase(
        std::remove_if(
            dynamicSubscriptions_.begin(),
            dynamicSubscriptions_.end(),
            [element](const DynamicResourceSubscription& sub) noexcept {
                return sub.target == element || sub.mountRoot == element;
            }),
        dynamicSubscriptions_.end());
    if (dynamicSubscriptions_.empty() && scopedDynamicOverrides_.empty()) {
        hasSubscriptions_.store(false, std::memory_order_release);
    }
}

std::size_t AxbTemplateRegistry::PruneStaleDynamicSubscriptions() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t before = dynamicSubscriptions_.size();
    dynamicSubscriptions_.erase(
        std::remove_if(
            dynamicSubscriptions_.begin(),
            dynamicSubscriptions_.end(),
            [](const DynamicResourceSubscription& sub) noexcept {
                return !IsSubscriptionAttachedLocked(sub);
            }),
        dynamicSubscriptions_.end());
    if (dynamicSubscriptions_.empty() && scopedDynamicOverrides_.empty()) {
        hasSubscriptions_.store(false, std::memory_order_release);
    }
    return before - dynamicSubscriptions_.size();
}

std::size_t AxbTemplateRegistry::ActiveDynamicSubscriptionCount() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t active = 0u;
    for (const auto& sub : dynamicSubscriptions_) {
        if (IsSubscriptionAttachedLocked(sub)) {
            ++active;
        }
    }
    return active;
}

std::size_t AxbTemplateRegistry::SetDynamicResource(
    std::string key,
    UnifiedValue value,
    UIElement* scopeRoot) {
    if (key.empty()) {
        return 0u;
    }
    VisitPoolValue(value, true);

    std::vector<PendingResourceUpdate> updates;
    std::vector<HostWakeEntry> wakeList;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (scopeRoot == nullptr) {
            dynamicOverrides_[key] = value;
            resources_[key] = value;
        } else {
            scopedDynamicOverrides_[scopeRoot][key] = value;
            hasSubscriptions_.store(true, std::memory_order_release);
        }

        std::size_t writeIdx = 0u;
        for (std::size_t readIdx = 0u; readIdx < dynamicSubscriptions_.size(); ++readIdx) {
            DynamicResourceSubscription& sub = dynamicSubscriptions_[readIdx];
            if (!IsSubscriptionAttachedLocked(sub)) {
                continue;
            }
            if (sub.resourceKey == key &&
                IsWithinScopeRoot(sub.target, scopeRoot) &&
                (scopeRoot == nullptr ||
                 !IsShadowedByCloserScopeLocked(scopedDynamicOverrides_, sub.target, scopeRoot, key))) {
                updates.push_back(PendingResourceUpdate{
                    IntrusivePtr<UIElement>(sub.target, true),
                    sub.propId,
                    value});
            }
            if (writeIdx != readIdx) {
                dynamicSubscriptions_[writeIdx] = std::move(sub);
            }
            ++writeIdx;
        }
        dynamicSubscriptions_.resize(writeIdx);
        if (dynamicSubscriptions_.empty() && scopedDynamicOverrides_.empty()) {
            hasSubscriptions_.store(false, std::memory_order_release);
        }

        if (!updates.empty()) {
            SnapshotWakeListenersLocked(wakeList);
        }
    }

    {
        ScopedUIResourceContext ownerScope(
            UIResourceContext::HasActiveThreadOverride() ? nullptr : ownerContext_);
        for (const auto& upd : updates) {
            ApplyResolvedResourceToElement(upd.target.Get(), upd.propId, upd.newValue);
        }
    }
    for (const auto& wake : wakeList) {
        if (wake.callback) {
            wake.callback(wake.userData);
        }
    }
    return updates.size();
}

void AxbTemplateRegistry::CollectDynamicDictionaryUpdates(
    const AxbPackage& dictPkg,
    UIElement* scopeRoot,
    std::vector<PendingResourceUpdate>& outUpdates,
    std::vector<HostWakeEntry>& outWakeList) {
    VisitPackagePoolValues(dictPkg, true);

    std::lock_guard<std::mutex> lock(mutex_);
    std::unordered_map<std::string, UnifiedValue> incomingMap;
    incomingMap.reserve(dictPkg.resources.size());
    std::unordered_map<std::string, UnifiedValue>* scopedBucket =
        (scopeRoot != nullptr) ? &scopedDynamicOverrides_[scopeRoot] : nullptr;

    for (const auto& kv : dictPkg.resources) {
        if (!kv.first.empty()) {
            incomingMap[kv.first] = kv.second;
            if (scopeRoot == nullptr) {
                dynamicOverrides_[kv.first] = kv.second;
                resources_[kv.first] = kv.second;
            } else {
                (*scopedBucket)[kv.first] = kv.second;
            }
        }
    }
    if (scopedBucket != nullptr && !scopedBucket->empty()) {
        hasSubscriptions_.store(true, std::memory_order_release);
    }

    for (const auto& mp : dictPkg.mtsdfPaths) {
        if (mp.pathHash != 0) {
            (void)InsertMtsdfPathLocked(mp);
        }
    }
    if (scopeRoot == nullptr) {
        for (const auto& st : dictPkg.styles) {
            if (!st.key.empty()) {
                styles_[st.key] = st;
            } else if (!st.targetTypeName.empty()) {
                implicitStyles_[std::string(StripNamespacePrefix(st.targetTypeName))] = st;
            }
        }
        for (const auto& sb : dictPkg.storyboards) {
            if (!sb.key.empty()) {
                storyboards_[sb.key] = sb;
            }
        }
        for (const auto& dt : dictPkg.dataTemplates) {
            if (!dt.templatePackage) continue;
            if (!dt.key.empty()) {
                dataTemplates_[dt.key] = dt.templatePackage;
                dataTemplateEntries_[dt.key] = dt;
            }
            if (!dt.dataTypeName.empty()) {
                const std::string shortType(StripNamespacePrefix(dt.dataTypeName));
                implicitDataTemplates_[shortType] = dt.templatePackage;
                implicitDataTemplateEntries_[shortType] = dt;
            }
            for (const auto& kv : dt.templatePackage->resources) {
                if (!kv.first.empty()) resources_[kv.first] = kv.second;
            }
            for (const auto& mp : dt.templatePackage->mtsdfPaths) {
                if (mp.pathHash != 0) (void)InsertMtsdfPathLocked(mp);
            }
            for (const auto& st : dt.templatePackage->styles) {
                if (!st.key.empty()) {
                    styles_[st.key] = st;
                } else if (!st.targetTypeName.empty()) {
                    implicitStyles_[std::string(StripNamespacePrefix(st.targetTypeName))] = st;
                }
            }
            for (const auto& sb : dt.templatePackage->storyboards) {
                if (!sb.key.empty()) storyboards_[sb.key] = sb;
            }
        }
        for (const auto& ct : dictPkg.controlTemplates) {
            if (!ct.templatePackage) continue;
            for (const auto& kv : ct.templatePackage->resources) {
                if (!kv.first.empty()) resources_[kv.first] = kv.second;
            }
            for (const auto& mp : ct.templatePackage->mtsdfPaths) {
                if (mp.pathHash != 0) (void)InsertMtsdfPathLocked(mp);
            }
            for (const auto& st : ct.templatePackage->styles) {
                if (!st.key.empty()) {
                    styles_[st.key] = st;
                } else if (!st.targetTypeName.empty()) {
                    implicitStyles_[std::string(StripNamespacePrefix(st.targetTypeName))] = st;
                }
            }
            for (const auto& sb : ct.templatePackage->storyboards) {
                if (!sb.key.empty()) storyboards_[sb.key] = sb;
            }
            if (!ct.key.empty()) {
                controlTemplates_[ct.key] = ct;
            } else if (!ct.targetTypeName.empty()) {
                implicitControlTemplates_[std::string(StripNamespacePrefix(ct.targetTypeName))] = ct;
            }
        }
    }

    std::size_t writeIdx = 0u;
    for (std::size_t readIdx = 0u; readIdx < dynamicSubscriptions_.size(); ++readIdx) {
        DynamicResourceSubscription& sub = dynamicSubscriptions_[readIdx];
        if (!IsSubscriptionAttachedLocked(sub)) {
            continue;
        }
        if (IsWithinScopeRoot(sub.target, scopeRoot) &&
            (scopeRoot == nullptr ||
             !IsShadowedByCloserScopeLocked(
                 scopedDynamicOverrides_, sub.target, scopeRoot, sub.resourceKey))) {
            const auto it = incomingMap.find(sub.resourceKey);
            if (it != incomingMap.end()) {
                outUpdates.push_back(PendingResourceUpdate{
                    IntrusivePtr<UIElement>(sub.target, true),
                    sub.propId,
                    it->second});
            }
        }
        if (writeIdx != readIdx) {
            dynamicSubscriptions_[writeIdx] = std::move(sub);
        }
        ++writeIdx;
    }
    dynamicSubscriptions_.resize(writeIdx);
    if (dynamicSubscriptions_.empty() && scopedDynamicOverrides_.empty()) {
        hasSubscriptions_.store(false, std::memory_order_release);
    }

    if (!outUpdates.empty() ||
        (scopeRoot == nullptr && (!dictPkg.styles.empty() || !dictPkg.controlTemplates.empty()))) {
        SnapshotWakeListenersLocked(outWakeList);
    }
}

std::size_t AxbTemplateRegistry::ApplyDynamicDictionary(
    const AxbPackage& dictPkg,
    UIElement* scopeRoot) {
    std::vector<PendingResourceUpdate> updates;
    std::vector<HostWakeEntry> wakeList;
    CollectDynamicDictionaryUpdates(dictPkg, scopeRoot, updates, wakeList);

    for (const auto& upd : updates) {
        if (upd.propId == PropId_PathData) {
            if (const auto* sv = std::get_if<StringValue>(&upd.newValue)) {
                (void)FindOrBakeMtsdfPath(sv->view());
            }
        }
    }

    {
        ScopedUIResourceContext ownerScope(
            UIResourceContext::HasActiveThreadOverride() ? nullptr : ownerContext_);
        for (const auto& upd : updates) {
            ApplyResolvedResourceToElement(upd.target.Get(), upd.propId, upd.newValue);
        }
    }
    for (const auto& wake : wakeList) {
        if (wake.callback) {
            wake.callback(wake.userData);
        }
    }
    return updates.size();
}

void AxbTemplateRegistry::RegisterDictionary(std::string sourceUri, AxbPackage dictPkg) {
    if (sourceUri.empty()) {
        return;
    }
    VisitPackagePoolValues(dictPkg, true);
    const std::string norm = NormalizeDictLookupKey(sourceUri);
    const std::string base = ExtractBasenameKey(norm);
    std::lock_guard<std::mutex> lock(mutex_);
    registeredDictionaries_[sourceUri] = dictPkg;
    if (!norm.empty() && norm != sourceUri) {
        registeredDictionaries_[norm] = dictPkg;
    }
    if (!base.empty() && base != norm && base != sourceUri) {
        registeredDictionaries_[base] = std::move(dictPkg);
    }
}

const AxbPackage* AxbTemplateRegistry::FindDictionary(std::string_view sourceUri) const noexcept {
    if (sourceUri.empty()) {
        return nullptr;
    }
    const std::string raw(sourceUri);
    const std::string norm = NormalizeDictLookupKey(sourceUri);
    const std::string base = ExtractBasenameKey(norm);
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = registeredDictionaries_.find(raw);
    if (it != registeredDictionaries_.end()) {
        return &it->second;
    }
    if (!norm.empty()) {
        it = registeredDictionaries_.find(norm);
        if (it != registeredDictionaries_.end()) {
            return &it->second;
        }
    }
    if (!base.empty()) {
        it = registeredDictionaries_.find(base);
        if (it != registeredDictionaries_.end()) {
            return &it->second;
        }
    }
    return nullptr;
}

std::size_t AxbTemplateRegistry::ApplyRegisteredDictionary(
    std::string_view sourceUri,
    UIElement* scopeRoot) {
    AxbPackage dictCopy{};
    bool found = false;
    {
        const std::string raw(sourceUri);
        const std::string norm = NormalizeDictLookupKey(sourceUri);
        const std::string base = ExtractBasenameKey(norm);
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = registeredDictionaries_.find(raw);
        if (it == registeredDictionaries_.end() && !norm.empty()) {
            it = registeredDictionaries_.find(norm);
        }
        if (it == registeredDictionaries_.end() && !base.empty()) {
            it = registeredDictionaries_.find(base);
        }
        if (it != registeredDictionaries_.end()) {
            dictCopy = it->second;
            found = true;
        }
    }
    if (!found) {
        if (const auto compileHook = GetRuntimeXamlCompileHook()) {
            const auto searchDirs = UIResourceContext::Current().XamlSearchDirectoriesSnapshot();
            if (compileHook(sourceUri, searchDirs, dictCopy)) {
                RegisterDictionary(std::string(sourceUri), dictCopy);
                found = true;
            }
        }
    }
    if (!found) {
        return 0u;
    }
    return ApplyDynamicDictionary(dictCopy, scopeRoot);
}

void AxbTemplateRegistry::SetWakeHostCallback(
    WakeHostCallback callback,
    void* userData) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    customWakeCallback_ = callback;
    customWakeUserData_ = userData;
}

void AxbTemplateRegistry::RegisterHostWakeListener(
    const void* ownerKey,
    WakeHostCallback callback,
    void* userData) {
    if (!ownerKey || !callback) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : hostWakeListeners_) {
        if (entry.ownerKey == ownerKey) {
            entry.callback = callback;
            entry.userData = userData;
            return;
        }
    }
    hostWakeListeners_.push_back(HostWakeEntry{ownerKey, callback, userData});
}

void AxbTemplateRegistry::UnregisterHostWakeListener(const void* ownerKey) noexcept {
    if (!ownerKey) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    hostWakeListeners_.erase(
        std::remove_if(
            hostWakeListeners_.begin(),
            hostWakeListeners_.end(),
            [ownerKey](const HostWakeEntry& entry) noexcept {
                return entry.ownerKey == ownerKey;
            }),
        hostWakeListeners_.end());
}

const AxbPackage* AxbTemplateRegistry::Find(std::string_view className) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = templates_.find(std::string(className));
    if (it != templates_.end()) {
        return &it->second;
    }
    const std::string shortName(StripNamespacePrefix(className));
    it = templates_.find(shortName);
    if (it != templates_.end()) {
        return &it->second;
    }
    return nullptr;
}

const AxbMtsdfPathEntry* AxbTemplateRegistry::FindMtsdfPath(std::uint64_t pathHash) const noexcept {
    if (pathHash == 0u) {
        return nullptr;
    }
    struct TlsMtsdfSlot {
        const AxbTemplateRegistry* reg = nullptr;
        std::uint64_t epoch = 0u;
        std::uint64_t hash = 0u;
        const AxbMtsdfPathEntry* ptr = nullptr;
    };
    thread_local std::array<TlsMtsdfSlot, 128> tlsCache{};
    const std::uint64_t curEpoch = mtsdfLookupEpoch_.load(std::memory_order_acquire);
    auto& slot = tlsCache[static_cast<std::size_t>((pathHash ^ (pathHash >> 17u)) & 127u)];
    if (slot.reg == this && slot.epoch == curEpoch && slot.hash == pathHash) {
        return slot.ptr;
    }
    const AxbMtsdfPathEntry* resolvedPtr = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = mtsdfPaths_.find(pathHash);
        if (it != mtsdfPaths_.end()) {
            resolvedPtr = &it->second;
        }
    }
    slot.reg = this;
    slot.epoch = curEpoch;
    slot.hash = pathHash;
    slot.ptr = resolvedPtr;
    return resolvedPtr;
}

const AxbMtsdfPathEntry* AxbTemplateRegistry::FindOrBakeMtsdfPath(std::string_view pathData) {
    if (pathData.empty()) {
        return nullptr;
    }
    const std::uint64_t hash = ComputeSvgPathHash(pathData);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = mtsdfPaths_.find(hash);
        if (it != mtsdfPaths_.end()) {
            return &it->second;
        }
    }
    const std::uint16_t adaptiveCell = SelectAdaptiveMtsdfCellSize(pathData);
    AxbMtsdfPathEntry baked = BakeSvgPathToMtsdfEntry(pathData, adaptiveCell, 4.0f, 0u, 1024u);
    std::lock_guard<std::mutex> lock(mutex_);
    return InsertMtsdfPathLocked(std::move(baked));
}

} // namespace aero
