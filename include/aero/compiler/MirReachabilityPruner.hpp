#pragma once

#include <cstddef>
#include <vector>

#include "aero/core/RsAliases.hpp"
#include "realscript/mir/Mir.h"

namespace aero {

struct MirPruneStats {
    std::size_t totalTypesBefore = 0u;
    std::size_t totalFunctionsBefore = 0u;
    std::size_t retainedTypes = 0u;
    std::size_t prunedTypes = 0u;
    std::size_t retainedFunctions = 0u;
    std::size_t prunedFunctions = 0u;
    bool fellBackToUnpruned = false;
};

/// Performs sound reachability dead-code and dead-type pruning over `Aero.UI`
/// and `Aero.Native` MIR modules while preserving all non-stdlib modules,
/// host-dispatched `Aero.UI` runtime entry points, virtual/interface dispatch
/// slots, constructors, and reflection property accessors.
/// Verifies all pruned modules via `rs_mir::verifyModule`; if any diagnostic
/// triggers, restores the unpruned MIR cleanly and returns false.
bool PruneUnreachableStdlibMir(
    std::vector<rs_mir::Module>& mirModules,
    MirPruneStats* outStats = nullptr);

} // namespace aero
