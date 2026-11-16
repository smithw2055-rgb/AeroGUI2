#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "aero/asset/AxbFormat.hpp"
#include "aero/core/RsAliases.hpp"

namespace aero {

class UIElement;
class UIResourceContext;

// Self-contained template & MTSDF vector icon registry for automatic .axb4 mounting
struct DynamicResourceSubscription {
    UIElement* target = nullptr;
    UIElement* mountRoot = nullptr;
    std::uint16_t propId = 0u;
    std::uint8_t flags = AxbResourceFlag_DynamicResource;
    bool hadAncestorWhenBound = false;
    std::string resourceKey;
};

class AxbTemplateRegistry {
public:
    using WakeHostCallback = void (*)(void* userData) noexcept;

    static AxbTemplateRegistry& Instance() noexcept;
    [[nodiscard]] static bool IsLiveRegistry(const AxbTemplateRegistry* registry) noexcept;

    AxbTemplateRegistry() noexcept;
    ~AxbTemplateRegistry();

    AxbTemplateRegistry(const AxbTemplateRegistry&) = delete;
    AxbTemplateRegistry& operator=(const AxbTemplateRegistry&) = delete;

    void SetOwnerContext(UIResourceContext* owner) noexcept { ownerContext_ = owner; }
    [[nodiscard]] UIResourceContext* OwnerContext() const noexcept { return ownerContext_; }
    [[nodiscard]] bool HasAnyDynamicSubscriptions() const noexcept {
        return hasSubscriptions_.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool HasSubscriptionInSubtree(const UIElement* scopeRoot) const noexcept;

    void Register(std::string className, AxbPackage package);
    void RegisterResources(const AxbPackage& package);
    void RegisterResource(std::string key, UnifiedValue value);
    void RegisterMtsdfPath(AxbMtsdfPathEntry entry);
    void RegisterStyle(AxbStyleDefinition style);
    void RegisterStoryboard(AxbStoryboard storyboard);
    void RegisterDataTemplate(AxbDataTemplate dataTemplate);
    void RegisterControlTemplate(AxbControlTemplate controlTemplate);
    void MarkValuesInPools() const noexcept;
    [[nodiscard]] const UnifiedValue* FindResource(std::string_view key) const noexcept;
    [[nodiscard]] const UnifiedValue* FindDynamicResource(std::string_view key) const noexcept;
    [[nodiscard]] const UnifiedValue* FindResourceForElement(
        std::string_view key,
        const UIElement* target) const noexcept;
    [[nodiscard]] const UnifiedValue* FindDynamicResourceForElement(
        std::string_view key,
        const UIElement* target) const noexcept;
    [[nodiscard]] const AxbPackage* Find(std::string_view className) const noexcept;
    [[nodiscard]] const AxbMtsdfPathEntry* FindMtsdfPath(std::uint64_t pathHash) const noexcept;
    [[nodiscard]] const AxbMtsdfPathEntry* FindOrBakeMtsdfPath(std::string_view pathData);
    [[nodiscard]] const AxbStyleDefinition* FindStyle(std::string_view key) const noexcept;
    [[nodiscard]] const AxbStyleDefinition* FindImplicitStyle(std::string_view targetTypeName) const noexcept;
    [[nodiscard]] const AxbStoryboard* FindStoryboard(std::string_view key) const noexcept;
    [[nodiscard]] const AxbPackage* FindDataTemplate(std::string_view key) const noexcept;
    [[nodiscard]] const AxbDataTemplate* FindDataTemplateEntry(std::string_view key) const noexcept;
    [[nodiscard]] const AxbPackage* FindImplicitDataTemplate(std::string_view shortDataTypeName) const noexcept;
    [[nodiscard]] const AxbDataTemplate* FindImplicitDataTemplateEntry(std::string_view shortDataTypeName) const noexcept;
    [[nodiscard]] const AxbControlTemplate* FindControlTemplate(std::string_view key) const noexcept;
    [[nodiscard]] const AxbControlTemplate* FindImplicitControlTemplate(std::string_view targetTypeName) const noexcept;
    [[nodiscard]] std::vector<AxbStyleDefinition> RegisteredStyles() const;
    [[nodiscard]] std::vector<AxbStoryboard> RegisteredStoryboards() const;
    [[nodiscard]] std::vector<AxbControlTemplate> RegisteredControlTemplates() const;
    [[nodiscard]] std::size_t RegisteredMtsdfPathCount() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return mtsdfPaths_.size();
    }

    // P1-2: Weak DynamicResource / {noesis:Loc} subscription & sub-millisecond live dictionary hot-switching
    void SubscribeDynamicResource(
        UIElement* target,
        UIElement* mountRoot,
        std::uint16_t propId,
        std::string_view resourceKey,
        std::uint8_t flags = AxbResourceFlag_DynamicResource);
    void UnsubscribeElement(const UIElement* element) noexcept;
    std::size_t PruneStaleDynamicSubscriptions() noexcept;
    [[nodiscard]] std::size_t ActiveDynamicSubscriptionCount() const noexcept;

    std::size_t SetDynamicResource(
        std::string key,
        UnifiedValue value,
        UIElement* scopeRoot = nullptr);
    std::size_t ApplyDynamicDictionary(
        const AxbPackage& dictPkg,
        UIElement* scopeRoot = nullptr);

    void RegisterDictionary(std::string sourceUri, AxbPackage dictPkg);
    [[nodiscard]] const AxbPackage* FindDictionary(std::string_view sourceUri) const noexcept;
    std::size_t ApplyRegisteredDictionary(
        std::string_view sourceUri,
        UIElement* scopeRoot = nullptr);

    void SetWakeHostCallback(WakeHostCallback callback, void* userData = nullptr) noexcept;
    void RegisterHostWakeListener(const void* ownerKey, WakeHostCallback callback, void* userData = nullptr);
    void UnregisterHostWakeListener(const void* ownerKey) noexcept;

    static void ApplyResolvedResourceToElement(
        UIElement* target,
        std::uint16_t propId,
        const UnifiedValue& rawVal);

    void Clear() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        templates_.clear();
        resources_.clear();
        dynamicOverrides_.clear();
        scopedDynamicOverrides_.clear();
        registeredDictionaries_.clear();
        dynamicSubscriptions_.clear();
        mtsdfPaths_.clear();
        styles_.clear();
        implicitStyles_.clear();
        storyboards_.clear();
        dataTemplates_.clear();
        dataTemplateEntries_.clear();
        mtsdfLookupEpoch_.fetch_add(1u, std::memory_order_relaxed);
        implicitDataTemplates_.clear();
        implicitDataTemplateEntries_.clear();
        controlTemplates_.clear();
        implicitControlTemplates_.clear();
        customWakeCallback_ = nullptr;
        customWakeUserData_ = nullptr;
        nextMtsdfAtlasSlot_ = 0u;
        hasSubscriptions_.store(false, std::memory_order_release);
    }

private:
    struct HostWakeEntry {
        const void* ownerKey = nullptr;
        WakeHostCallback callback = nullptr;
        void* userData = nullptr;
    };
    struct PendingResourceUpdate;

    const AxbMtsdfPathEntry* InsertMtsdfPathLocked(AxbMtsdfPathEntry entry);
    void CollectDynamicDictionaryUpdates(
        const AxbPackage& dictPkg,
        UIElement* scopeRoot,
        std::vector<PendingResourceUpdate>& outUpdates,
        std::vector<HostWakeEntry>& outWakeList);
    [[nodiscard]] const UnifiedValue* FindScopedOverrideLocked(
        std::string_view key,
        const UIElement* target) const noexcept;
    static bool IsSubscriptionAttachedLocked(const DynamicResourceSubscription& sub) noexcept;
    static bool IsWithinScopeRoot(const UIElement* target, const UIElement* scopeRoot) noexcept;
    void SnapshotWakeListenersLocked(std::vector<HostWakeEntry>& outListeners) const;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, AxbPackage> templates_;
    std::unordered_map<std::string, UnifiedValue> resources_;
    std::unordered_map<std::string, UnifiedValue> dynamicOverrides_;
    std::unordered_map<const UIElement*, std::unordered_map<std::string, UnifiedValue>> scopedDynamicOverrides_;
    std::unordered_map<std::string, AxbPackage> registeredDictionaries_;
    std::vector<DynamicResourceSubscription> dynamicSubscriptions_;
    std::vector<HostWakeEntry> hostWakeListeners_;
    WakeHostCallback customWakeCallback_ = nullptr;
    void* customWakeUserData_ = nullptr;
    std::unordered_map<std::uint64_t, AxbMtsdfPathEntry> mtsdfPaths_;
    std::unordered_map<std::string, AxbStyleDefinition> styles_;
    std::unordered_map<std::string, AxbStyleDefinition> implicitStyles_;
    std::unordered_map<std::string, AxbStoryboard> storyboards_;
    std::unordered_map<std::string, std::shared_ptr<AxbPackage>> dataTemplates_;
    std::unordered_map<std::string, AxbDataTemplate> dataTemplateEntries_;
    std::atomic<std::uint64_t> mtsdfLookupEpoch_{1u};
    std::unordered_map<std::string, std::shared_ptr<AxbPackage>> implicitDataTemplates_;
    std::unordered_map<std::string, AxbDataTemplate> implicitDataTemplateEntries_;
    std::unordered_map<std::string, AxbControlTemplate> controlTemplates_;
    std::unordered_map<std::string, AxbControlTemplate> implicitControlTemplates_;
    std::uint32_t nextMtsdfAtlasSlot_ = 0u;
    UIResourceContext* ownerContext_ = nullptr;
    std::atomic<bool> hasSubscriptions_{false};
};

} // namespace aero
