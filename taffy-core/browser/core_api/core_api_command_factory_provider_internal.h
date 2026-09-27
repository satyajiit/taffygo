// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_API_CORE_API_COMMAND_FACTORY_PROVIDER_INTERNAL_H_
#define TAFFY_BROWSER_CORE_API_CORE_API_COMMAND_FACTORY_PROVIDER_INTERNAL_H_

#include <optional>
#include <string>

#include "taffy/browser/core_api/core_api_command_factory.h"

namespace taffy {

// The provider field rules, declared once because more than one file builds a
// provider command. Two of them are endpoint rules and they are two on
// purpose — see the pair below.
//
// They are shared rather than repeated for the reason
// core_api_command_factory_provider_refusal_unittest.cc asserts across every
// entry point: they all reach the same store, so a request one of them accepts
// and another refuses would be a provider a person can create and cannot
// delete. A second copy of the alphabet is how that drift starts.
//
// All of them are defined in core_api_command_factory_provider.cc, beside the
// host classification the endpoint rule depends on.

// The provider identity rule: `[a-z0-9][a-z0-9-]{0,63}`.
ProviderRequestRefusal CheckProviderId(const std::string &value);

// The opaque secure-store reference. Measured, never read (decision 0049).
ProviderRequestRefusal CheckProviderCredentialHandle(const std::string &value);

// The row a person reads. Blank is refused, not only empty.
ProviderRequestRefusal CheckProviderDisplayName(const std::string &value);

// The catalog endpoint rule: an https origin, publicly routable, spelled
// exactly as an origin. It is the host refusal decision 0049 assigns to the
// browser, and it is the rule a *served* address is held to.
//
// It has no caller among the builders today, and that is a statement rather
// than an oversight. Every endpoint that reaches this factory arrives from a
// person configuring their own provider, so every one of them is asked the
// register question below instead; a catalog endpoint reaches the browser
// through the served catalog document and is judged where a request is sent,
// by `IsHttpsOrigin` in core_model_effect_validation.cc. This function is the
// same rule stated at the seam where an address is *accepted* rather than
// spent, kept whole and kept under its own test
// (`ACatalogEndpointIsAnOriginAndOneSpelling`) so that the day a catalog
// endpoint does arrive here it meets the rule that has always applied to one —
// and so that relaxing the register rule below can never have relaxed it.
ProviderRequestRefusal
CheckCatalogProviderEndpoint(const std::string &value);

// The register rule (decision 0096 section 3): may a person register this as
// their own model server's address? Ports and base paths are theirs, plain
// http is admitted to a literal local machine and nowhere else, and the whole
// decision is `ClassifyCustomProviderEndpoint`'s — this function only names
// the refusal a surface is shown.
//
// Deliberately not written in terms of the catalog rule above, and not a
// relaxation of it: it is a different question, asked of an address named by a
// different authority, and answered by a different file.
ProviderRequestRefusal CheckCustomProviderEndpoint(const std::string &value);

// The wire family, or nothing when the view names one a person's own provider
// may not claim. Both reserved spellings are refused here and independently in
// the core's provider plane (`ReservedWireApi` in provider_commands.rs).
std::optional<core_service::mojom::ProviderWireApi>
ProjectProviderWireApi(core_api::mojom::ProviderWireApiView wire_api);

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_API_CORE_API_COMMAND_FACTORY_PROVIDER_INTERNAL_H_
