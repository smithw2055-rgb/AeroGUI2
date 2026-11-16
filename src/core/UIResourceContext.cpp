#include "aero/core/UIResourceContext.hpp"

#include <atomic>
#include <mutex>
#include <unordered_map>

#include "aero/asset/AxbFormat.hpp"
#include "aero/asset/AxbTemplateRegistry.hpp"
#include "aero/render/FontAtlas.hpp"
#include "aero/render/ImageAtlas.hpp"

namespace aero {

struct UIResourceContext::Impl {
    AxbTemplateRegistry templates;
    FontAtlas fontAtlas;
    ImageAtlas imageAtlas;
    GradientBrushRegistry gradientBrushes;
    mutable std::mutex riveDirMutex;
    std::vector<std::string> riveSearchDirs;
    mutable std::mutex xamlDirMutex;
    std::vector<std::string> xamlSearchDirs;

    explicit Impl(std::uint16_t fontAtlasSize)
        : templates()
        , fontAtlas(fontAtlasSize)
        , imageAtlas()
        , gradientBrushes() {}
};

namespace {

thread_local UIResourceContext* gActiveThreadResourceContext = nullptr;
thread_local ScopedUIResourceContext* gTopScopedResourceFrame = nullptr;
std::atomic<std::size_t> gLiveResourceContextCount{0u};

struct DomainResourceBindingTable {
    std::mutex mutex;
    std::unordered_map<const ExecutionDomainContext*, std::weak_ptr<UIResourceContext>> entries;

    static DomainResourceBindingTable& Instance() noexcept {
        static DomainResourceBindingTable table;
        return table;
    }
};

} // namespace

UIResourceContext::UIResourceContext()
    : UIResourceContext(FontAtlas::kDefaultAtlasSize) {}

UIResourceContext::UIResourceContext(std::uint16_t fontAtlasSize)
    : impl_(std::make_unique<Impl>(fontAtlasSize)) {
    impl_->templates.SetOwnerContext(this);
    gLiveResourceContextCount.fetch_add(1u, std::memory_order_relaxed);
}

UIResourceContext::~UIResourceContext() {
    if (gActiveThreadResourceContext == this) {
        gActiveThreadResourceContext = nullptr;
    }
    for (ScopedUIResourceContext* frame = gTopScopedResourceFrame; frame != nullptr; frame = frame->prevFrame_) {
        if (frame->previous_ == this) {
            frame->previous_ = nullptr;
        }
        if (frame->pushed_ == this) {
            frame->pushed_ = nullptr;
        }
    }
    if (impl_) {
        impl_->templates.SetOwnerContext(nullptr);
        ClearAll();
    }
    gLiveResourceContextCount.fetch_sub(1u, std::memory_order_relaxed);
}

UIResourceContext& UIResourceContext::Default() noexcept {
    static UIResourceContext sDefaultContext;
    return sDefaultContext;
}

UIResourceContext& UIResourceContext::Current() noexcept {
    if (gActiveThreadResourceContext != nullptr) {
        return *gActiveThreadResourceContext;
    }
    return Default();
}

UIResourceContext* UIResourceContext::ActiveOverride() noexcept {
    return gActiveThreadResourceContext;
}

bool UIResourceContext::HasActiveThreadOverride() noexcept {
    return gActiveThreadResourceContext != nullptr;
}

std::size_t UIResourceContext::LiveInstanceCount() noexcept {
    return gLiveResourceContextCount.load(std::memory_order_relaxed);
}

void UIResourceContext::BindToDomains(
    const ExecutionDomainContext* domains,
    std::shared_ptr<UIResourceContext> context) {
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

void UIResourceContext::UnbindFromDomains(const ExecutionDomainContext* domains) noexcept {
    if (!domains) {
        return;
    }
    auto& table = DomainResourceBindingTable::Instance();
    std::lock_guard<std::mutex> lock(table.mutex);
    table.entries.erase(domains);
}

std::shared_ptr<UIResourceContext> UIResourceContext::FindForDomains(
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

AxbTemplateRegistry& UIResourceContext::Templates() noexcept {
    return impl_->templates;
}

const AxbTemplateRegistry& UIResourceContext::Templates() const noexcept {
    return impl_->templates;
}

FontAtlas& UIResourceContext::Fonts() noexcept {
    return impl_->fontAtlas;
}

const FontAtlas& UIResourceContext::Fonts() const noexcept {
    return impl_->fontAtlas;
}

FontAtlas& UIResourceContext::GetFontAtlas() noexcept {
    return impl_->fontAtlas;
}

const FontAtlas& UIResourceContext::GetFontAtlas() const noexcept {
    return impl_->fontAtlas;
}

ImageAtlas& UIResourceContext::Images() noexcept {
    return impl_->imageAtlas;
}

const ImageAtlas& UIResourceContext::Images() const noexcept {
    return impl_->imageAtlas;
}

ImageAtlas& UIResourceContext::GetImageAtlas() noexcept {
    return impl_->imageAtlas;
}

const ImageAtlas& UIResourceContext::GetImageAtlas() const noexcept {
    return impl_->imageAtlas;
}

GradientBrushRegistry& UIResourceContext::GradientBrushes() noexcept {
    return impl_->gradientBrushes;
}

const GradientBrushRegistry& UIResourceContext::GradientBrushes() const noexcept {
    return impl_->gradientBrushes;
}

void UIResourceContext::AddRiveSearchDirectory(std::string_view directory) {
    if (directory.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(impl_->riveDirMutex);
    for (const auto& existing : impl_->riveSearchDirs) {
        if (existing == directory) {
            return;
        }
    }
    impl_->riveSearchDirs.emplace_back(directory);
}

void UIResourceContext::ClearRiveSearchDirectories() noexcept {
    std::lock_guard<std::mutex> lock(impl_->riveDirMutex);
    impl_->riveSearchDirs.clear();
}

std::vector<std::string> UIResourceContext::RiveSearchDirectoriesSnapshot() const {
    std::lock_guard<std::mutex> lock(impl_->riveDirMutex);
    return impl_->riveSearchDirs;
}

void UIResourceContext::AddXamlSearchDirectory(std::string_view directory) {
    if (directory.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(impl_->xamlDirMutex);
    for (const auto& existing : impl_->xamlSearchDirs) {
        if (existing == directory) {
            return;
        }
    }
    impl_->xamlSearchDirs.emplace_back(directory);
}

void UIResourceContext::ClearXamlSearchDirectories() noexcept {
    std::lock_guard<std::mutex> lock(impl_->xamlDirMutex);
    impl_->xamlSearchDirs.clear();
}

std::vector<std::string> UIResourceContext::XamlSearchDirectoriesSnapshot() const {
    std::lock_guard<std::mutex> lock(impl_->xamlDirMutex);
    return impl_->xamlSearchDirs;
}

void UIResourceContext::ClearAll() noexcept {
    impl_->templates.Clear();
    impl_->fontAtlas.ClearCustomFonts();
    impl_->imageAtlas.Clear();
    impl_->gradientBrushes.Clear();
    ClearRiveSearchDirectories();
    ClearXamlSearchDirectories();
}

ScopedUIResourceContext::ScopedUIResourceContext(UIResourceContext& context) noexcept
    : prevFrame_(gTopScopedResourceFrame)
    , previous_(gActiveThreadResourceContext)
    , pushed_(&context)
    , active_(true) {
    gTopScopedResourceFrame = this;
    gActiveThreadResourceContext = &context;
}

ScopedUIResourceContext::ScopedUIResourceContext(UIResourceContext* context) noexcept {
    if (context != nullptr) {
        prevFrame_ = gTopScopedResourceFrame;
        previous_ = gActiveThreadResourceContext;
        pushed_ = context;
        active_ = true;
        gTopScopedResourceFrame = this;
        gActiveThreadResourceContext = context;
    }
}

ScopedUIResourceContext::ScopedUIResourceContext(std::shared_ptr<UIResourceContext> context) noexcept {
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

ScopedUIResourceContext::~ScopedUIResourceContext() noexcept {
    if (active_) {
        gActiveThreadResourceContext = previous_;
        if (gTopScopedResourceFrame == this) {
            gTopScopedResourceFrame = prevFrame_;
        } else {
            for (ScopedUIResourceContext* cur = gTopScopedResourceFrame; cur != nullptr; cur = cur->prevFrame_) {
                if (cur->prevFrame_ == this) {
                    cur->prevFrame_ = prevFrame_;
                    break;
                }
            }
        }
    }
}

} // namespace aero
