#pragma once

#include "aero/core/RsAliases.hpp"
#include "aero/runtime/ScriptBackendFacade.hpp"

namespace aero {

// Registers the stateless thread-local XML DOM scratch parser intrinsics
// (Xml_ParseDocument, Xml_ClearDocument, Xml_NodeMeta, Xml_NodeRawName,
// Xml_NodeLocalName, Xml_NodeText, Xml_AttrName, Xml_AttrValue).
bool RegisterNativeXmlDomBindings(
    BindingRegistry& bindings,
    const ScriptBackendView& backend);

} // namespace aero
