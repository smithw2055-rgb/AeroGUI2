#include "aero/core/UiResourceContext.hpp"

#include <atomic>
#include <unordered_map>

namespace aero {

namespace {

thread_local UiResourceContext* gActiveThreadResourceContext = nullptr;
thread_local ScopedUiResourceContext* gTopScopedResourceFrame = nullptr;
std::atomic<std::size_t> gLiveResourceContextCount{0u};

struct DomainResourceBindingTable {
    std::mutex mutex;
    std::unordered_map<const ExecutionDomainContext*, std::weak_ptr<UiResourceContext>> entries;

    static DomainResourceBindingTable& Instance() noexcept {
        static DomainResourceBindingTable table;
        return table;
    }
};

} // namespace

UiResourceContext::UiResourceContext()
    : templates_()
    , fontAtlas_(FontAtlas::kDefaultAtlasSize)
    , imageAtlas_()
    , gradientBrushes_() {
    templates_.SetOwnerContext(this);
    gLiveResourceContextCount.fetch_add(1u, std::memory_order_relaxed);
}

UiResourceContext::UiResourceContext(std::uint16_t fontAtlasSize)
    : templates_()
    , fontAtlas_(fontAtlasSize)
    , imageAtlas_()
    , gradientBrushes_() {
    templates_.SetOwnerContext(this);
    gLiveResourceContextCount.fetch_add(1u, std::memory_order_relaxed);
}

UiResourceContext::~UiResourceContext() {
    if (gActiveThreadResourceContext == this) {
        gActiveThreadResourceContext = nullptr;
    }
    for (ScopedUiResourceContext* frame = gTopScopedResourceFrame; frame != nullptr; frame = frame->prevFrame_) {
        if (frame->previous_ == this) {
            frame->previous_ = nullptr;
        }
        if (frame->pushed_ == this) {
            frame->pushed_ = nullptr;
        }
    }
    templates_.SetOwnerContext(nullptr);
    ClearAll();
    gLiveResourceContextCount.fetch_sub(1u, std::memory_order_relaxed);
}

UiResourceContext& UiResourceContext::Default() noexcept {
    static UiResourceContext sDefaultContext;
    return sDefaultContext;
}

UiResourceContext& UiResourceContext::Current() noexcept {
    if (gActiveThreadResourceContext != nullptr) {
        return *gActiveThreadResourceContext;
    }
    return Default();
}

UiResourceContext* UiResourceContext::ActiveOverride() noexcept {
    return gActiveThreadResourceContext;
}

bool UiResourceContext::HasActiveThreadOverride() noexcept {
    return gActiveThreadResourceContext != nullptr;
}

std::size_t UiResourceContext::LiveInstanceCount() noexcept {
    return gLiveResourceContextCount.load(std::memory_order_relaxed);
}

void UiResourceContext::BindToDomains(
    const ExecutionDomainContext* domains,
    std::shared_ptr<UiResourceContext> context) {
    if (!domains) {
        return;
    }
    auto& table = DomainResourceBindingTable::Instance();
    std::lock_guard<std::mutex> lock(table.mutex);
    if (!context) {
        table.entries.erase(domains);
    } else {
        table.entries[domains] = context;
    }
}

void UiResourceContext::UnbindFromDomains(const ExecutionDomainContext* domains) noexcept {
    if (!domains) {
        return;
    }
    auto& table = DomainResourceBindingTable::Instance();
    std::lock_guard<std::mutex> lock(table.mutex);
    table.entries.erase(domains);
}

std::shared_ptr<UiResourceContext> UiResourceContext::FindForDomains(
    const ExecutionDomainContext* domains) noexcept {
    if (!domains) {
        return nullptr;
    }
    auto& table = DomainResourceBindingTable::Instance();
    std::lock_guard<std::mutex> lock(table.mutex);
    const auto it = table.entries.find(domains);
    if (it == table.entries.end()) {
        return nullptr;
    }
    if (auto locked = it->second.lock()) {
        return locked;
    }
    table.entries.erase(it);
    return nullptr;
}

void UiResourceContext::AddRiveSearchDirectory(std::string_view directory) {
    if (directory.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(riveDirMutex_);
    for (const auto& existing : riveSearchDirs_) {
        if (existing == directory) {
            return;
        }
    }
    riveSearchDirs_.emplace_back(directory);
}

void UiResourceContext::ClearRiveSearchDirectories() noexcept {
    std::lock_guard<std::mutex> lock(riveDirMutex_);
    riveSearchDirs_.clear();
}

std::vector<std::string> UiResourceContext::RiveSearchDirectoriesSnapshot() const {
    std::lock_guard<std::mutex> lock(riveDirMutex_);
    return riveSearchDirs_;
}

void UiResourceContext::AddXamlSearchDirectory(std::string_view directory) {
    if (directory.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(xamlDirMutex_);
    for (const auto& existing : xamlSearchDirs_) {
        if (existing == directory) {
            return;
        }
    }
    xamlSearchDirs_.emplace_back(directory);
}

void UiResourceContext::ClearXamlSearchDirectories() noexcept {
    std::lock_guard<std::mutex> lock(xamlDirMutex_);
    xamlSearchDirs_.clear();
}

std::vector<std::string> UiResourceContext::XamlSearchDirectoriesSnapshot() const {
    std::lock_guard<std::mutex> lock(xamlDirMutex_);
    return xamlSearchDirs_;
}

void UiResourceContext::ClearAll() noexcept {
    templates_.Clear();
    fontAtlas_.ClearCustomFonts();
    imageAtlas_.Clear();
    gradientBrushes_.Clear();
    ClearRiveSearchDirectories();
    ClearXamlSearchDirectories();
}

ScopedUiResourceContext::ScopedUiResourceContext(UiResourceContext& context) noexcept
    : prevFrame_(gTopScopedResourceFrame)
    , previous_(gActiveThreadResourceContext)
    , pushed_(&context)
    , active_(true) {
    gTopScopedResourceFrame = this;
    gActiveThreadResourceContext = &context;
}

ScopedUiResourceContext::ScopedUiResourceContext(UiResourceContext* context) noexcept {
    if (context != nullptr) {
        prevFrame_ = gTopScopedResourceFrame;
        previous_ = gActiveThreadResourceContext;
        pushed_ = context;
        active_ = true;
        gTopScopedResourceFrame = this;
        gActiveThreadResourceContext = context;
    }
}

ScopedUiResourceContext::ScopedUiResourceContext(std::shared_ptr<UiResourceContext> context) noexcept {
    if (context != nullptr) {
        pinned_ = std::move(context);
        prevFrame_ = gTopScopedResourceFrame;
        previous_ = gActiveThreadResourceContext;
        pushed_ = pinned_.get();
        active_ = true;
        gTopScopedResourceFrame = this;
        gActiveThreadResourceContext = pushed_;
    }
}

ScopedUiResourceContext::~ScopedUiResourceContext() noexcept {
    if (active_) {
        gActiveThreadResourceContext = previous_;
        if (gTopScopedResourceFrame == this) {
            gTopScopedResourceFrame = prevFrame_;
        } else {
            for (ScopedUiResourceContext* cur = gTopScopedResourceFrame; cur != nullptr; cur = cur->prevFrame_) {
                if (cur->prevFrame_ == this) {
                    cur->prevFrame_ = prevFrame_;
                    break;
                }
            }
        }
    }
}

} // namespace aero
