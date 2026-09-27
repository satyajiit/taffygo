// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_BIP_MOJOM_CONVERSIONS_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_BIP_MOJOM_CONVERSIONS_H_

#include <optional>

#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"
#include "taffy/common/public/bip_action.h"
#include "taffy/common/public/bip_budget.h"
#include "taffy/common/public/bip_delta.h"
#include "taffy/common/public/bip_identity.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/common/public/bip_protocol_support.h"
#include "taffy/common/public/bip_subscription.h"

// Conversions between the mojom projection of the BIP contract and the owned
// value types in //taffy/common/public.
//
// Every conversion here is written as a switch with no default case. That is
// the point of the file: adding a member to the contract, and therefore to the
// mojom, becomes a compile error at this boundary instead of a silent
// fall-through to "unsupported" at run time. A silent fall-through is exactly
// how a new action type would quietly become undeniable.
//
// Direction matters for trust. The public-to-mojom direction converts values
// the browser already decided. The mojom-to-public direction reads a renderer
// reply, so it converts only the small set of scalars in ResolvedNodeFacts and
// nothing else: the semantic graph never becomes C++ structs
// (bip_observation.h explains why).

namespace taffy {

mojom::OriginPtr ToMojom(const Origin& origin);
Origin FromMojom(const mojom::Origin& origin);

mojom::ContentDigestPtr ToMojom(const ContentDigest& digest);
std::optional<ContentDigest> FromMojom(const mojom::ContentDigest& digest);

mojom::ActionType ToMojom(ActionType type);
std::optional<ActionType> FromMojom(mojom::ActionType type);

mojom::NodeState ToMojom(NodeState state);
std::optional<NodeState> FromMojom(mojom::NodeState state);

mojom::PreconditionKind ToMojom(PreconditionKind kind);

mojom::NodeHandlePtr ToMojom(const NodeHandle& handle);

// Null when the precondition is browser-only. The renderer is not asked to
// evaluate origin policy or budget: it is the party that would be lying about
// them. Browser-checked destination and content-trust expectations are
// forwarded only as narrowing guards against a later page mutation.
mojom::PreconditionPtr ToRendererPrecondition(const Precondition& precondition);

// The only renderer-supplied structure browser C++ reads field by field.
ResolvedNodeFacts FromMojom(const mojom::ResolvedNode& node);

ActionResultCode FromMojom(mojom::NodeResolutionCode code);
ActionResultCode FromMojom(mojom::RendererActionOutcome outcome);

// --- negotiation and stream types -------------------------------------------
//
// Everything below returns std::optional in the mojom-to-public direction and
// nullopt for a value this build does not know. That is protocol section 6.2's
// rule made mechanical: "every consumer handles unknown enum values as
// unsupported, never as the least restrictive known value". Dropping the value
// is what "unsupported" looks like in a list; coercing it to a neighbour is
// what a compromised or merely newer endpoint would want instead.

std::optional<AdapterKind> FromMojom(mojom::AdapterKind adapter);
mojom::AdapterKind ToMojom(AdapterKind adapter);

std::optional<ObservationScope> FromMojom(mojom::ObservationScope scope);
mojom::ObservationScope ToMojom(ObservationScope scope);

std::optional<BudgetKind> FromMojom(mojom::BudgetKind budget);

std::optional<DeltaClass> FromMojom(mojom::DeltaCategory category);

std::optional<BackpressureAction> FromMojom(mojom::BackpressureAction action);

std::optional<InvalidationCode> FromMojom(mojom::InvalidationReason reason);
mojom::InvalidationReason ToMojom(InvalidationCode code);

std::optional<DocumentLifecycleState> FromMojom(
    mojom::DocumentLifecycleState state);

// The endpoint's own reported ceiling. Read field by field rather than
// memcpy-ed, because the two structs are allowed to drift: ProcessBudgetLimits
// mirrors the contract's ProtocolLimits today and a compile error here is the
// intended signal if that stops being true.
ProcessBudgetLimits FromMojom(const mojom::ProtocolLimits& limits);

TruncationSummary FromMojom(const mojom::Truncation& truncation);

// The subscription options the broker sends. Built from a request that has
// already been clamped; this function does no narrowing of its own, so a
// reviewer looking for the clamp finds exactly one place with it.
mojom::PageSubscriptionOptionsPtr ToMojom(const SubscriptionRequest& clamped);

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_BIP_MOJOM_CONVERSIONS_H_
