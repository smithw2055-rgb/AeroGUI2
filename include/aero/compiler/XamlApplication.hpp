#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "aero/animation/AnimationEngine.hpp"
#include "aero/binding/DualTrackBindingEngine.hpp"
#include "aero/bridge/ScriptUiBridge.hpp"
#include "aero/compiler/AeroCompiler.hpp"
#include "aero/controls/Controls.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/input/InputEventRouter.hpp"
#include "aero/style/StyleTriggerEngine.hpp"

namespace aero {

struct XamlApplicationOptions {
    std::string appOrWindowXamlPath;
    std::vector<std::string> extraScriptPaths;
    std::vector<std::string> resourceSearchPaths;
    float viewportWidth = 1280.0f;
    float viewportHeight = 720.0f;
    bool autoDiscoverScripts = true;
};

// WPF-style one-line application & window loader (App.xaml StartupUri + automatic *.xaml.rs / ViewModels discovery)
class XamlApplication {
public:
    XamlApplication();
    ~XamlApplication();

    XamlApplication(const XamlApplication&) = delete;
    XamlApplication& operator=(const XamlApplication&) = delete;

    bool Load(const XamlApplicationOptions& options, std::string* outError = nullptr);
    bool Load(std::string_view appOrWindowOrDirPath, std::string* outError = nullptr);

    void Tick(float dtSeconds = 1.0f / 60.0f);

    [[nodiscard]] UIElement* MainWindowElement() const noexcept { return rootElement_.Get(); }
    [[nodiscard]] ObjectRef CodeBehindObject() const noexcept { return codeBehindObj_; }
    [[nodiscard]] ObjectRef DataContextObject() const noexcept { return viewModelObj_; }
    [[nodiscard]] ExecutionDomainContext& Domains() noexcept { return domains_; }
    [[nodiscard]] DualTrackBindingEngine& BindingEngine() noexcept { return *bindingEngine_; }
    [[nodiscard]] StyleTriggerEngine& StyleEngine() noexcept { return *styleEngine_; }
    [[nodiscard]] AnimationEngine& Animations() noexcept { return *animationEngine_; }
    [[nodiscard]] InputEventRouter& InputRouter() noexcept { return *inputRouter_; }
    [[nodiscard]] Interpreter* ScriptInterpreter() const noexcept { return interpreter_.get(); }
    [[nodiscard]] const ProgramImage* Program() const noexcept { return compileResult_.programImage.get(); }
    [[nodiscard]] const AxbPackage& MainPackage() const noexcept { return compileResult_.axbPackage; }
    [[nodiscard]] const std::string& StartupUri() const noexcept { return resolvedStartupUri_; }
    [[nodiscard]] const std::vector<std::string>& Diagnostics() const noexcept { return compileResult_.diagnostics; }

private:
    ExecutionDomainContext domains_;
    AeroCompileResult compileResult_{};
    std::shared_ptr<BindingRegistry> nativeBindings_;
    std::unique_ptr<ScriptUiBridge> scriptBridge_;
    std::unique_ptr<Interpreter> interpreter_;
    std::unique_ptr<DualTrackBindingEngine> bindingEngine_;
    std::unique_ptr<AnimationEngine> animationEngine_;
    std::unique_ptr<StyleTriggerEngine> styleEngine_;
    std::unique_ptr<InputEventRouter> inputRouter_;
    IntrusivePtr<UIElement> rootElement_;
    ObjectRef codeBehindObj_{};
    ObjectRef viewModelObj_{};
    PersistentRoot codeBehindPin_;
    PersistentRoot vmPin_;
    std::string resolvedStartupUri_;
    float viewportW_ = 1280.0f;
    float viewportH_ = 720.0f;
};

} // namespace aero
