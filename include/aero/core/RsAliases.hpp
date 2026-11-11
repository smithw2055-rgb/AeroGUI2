#pragma once

#include "realscript/runtime/ExecutionDomain.h"
#include "realscript/runtime/Runtime.h"
#include "realscript/semantic/Semantic.h"

namespace realscript {
namespace runtime {}
namespace semantic {}
namespace bytecode {}
namespace diagnostics {}
namespace compiler {}
namespace hot_reload {}
namespace mir {}
namespace optimization {}
namespace aot {}
namespace game {
using PropertySlotHandle = ::realscript::runtime::PropertySlotHandle;
} // namespace game
} // namespace realscript

namespace aero {

// Short namespace aliases for RealScript subsystems
namespace rs = ::realscript;
namespace rs_rt = ::realscript::runtime;
namespace rs_sem = ::realscript::semantic;
namespace rs_bc = ::realscript::bytecode;
namespace rs_diag = ::realscript::diagnostics;
namespace rs_game = ::realscript::game;
namespace rs_comp = ::realscript::compiler;
namespace rs_hr = ::realscript::hot_reload;
namespace rs_mir = ::realscript::mir;
namespace rs_opt = ::realscript::optimization;
namespace rs_aot = ::realscript::aot;

// Semantic & Symbol identity aliases
using SymbolId = rs_sem::SymbolId;
using PrimitiveType = rs_sem::PrimitiveType;

// Unified 16-byte value model (RealScript RuntimeAbiMajor = 2) & primitive wrappers
using UnifiedValue = rs_rt::Value;
using ValueTag = rs_rt::ValueTag;
using Float4Value = rs_rt::Float4Value;
using FloatValue = rs_rt::FloatValue;
using StringValue = rs_rt::StringValue;
using StructValue = rs_rt::StructValue;
using NullObject = rs_rt::NullObject;
using NullString = rs_rt::NullString;
using NullArray = rs_rt::NullArray;

// Managed heap references, generational weak references & zero-hash native handles
using ObjectRef = rs_rt::ObjectRef;
using ObjectKind = rs_rt::ObjectKind;
using WeakRef = rs_rt::WeakRef;
using NativeHandle = rs_rt::NativeHandle;
using NativeHandleRegistry = rs_rt::NativeHandleRegistry;
using ManagedHeap = rs_rt::ManagedHeap;
using PersistentRoot = rs_rt::PersistentRoot;
using ShadowStack = rs_rt::ShadowStack;
using NativeRawColumnView = rs_rt::NativeRawColumnView;
using NativeFieldLayout = rs_rt::NativeFieldLayout;
using NativeFieldKind = rs_rt::NativeFieldKind;

// VM program image, interpreter & host binding registry
using ProgramImage = rs_rt::ProgramImage;
using Interpreter = rs_rt::Interpreter;
using BindingRegistry = rs_rt::BindingRegistry;
using ValueSpan = rs_rt::ValueSpan;
using RuntimeError = rs_rt::RuntimeError;
using ErrorCode = rs_rt::ErrorCode;

// Dual execution domains (SimDomain + UiDomain) & zero-boxing property slot handle
using DomainKind = rs_rt::DomainKind;
using ExecutionDomainContext = rs_rt::ExecutionDomainContext;
using SimCommandEnvelope = rs_rt::SimCommandEnvelope;
using PropertySlotHandle = rs_game::PropertySlotHandle;
using CollectionChangeKind = rs_rt::CollectionChangeKind;
using CollectionChangeRecord = rs_rt::CollectionChangeRecord;
using ObservableListHelper = rs_rt::ObservableListHelper;

} // namespace aero
