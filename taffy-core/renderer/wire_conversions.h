// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_WIRE_CONVERSIONS_H_
#define TAFFY_RENDERER_WIRE_CONVERSIONS_H_

#include <optional>

#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"
#include "taffy/renderer/adapters/adapter.h"
#include "taffy/renderer/node_precondition_checker.h"
#include "taffy/renderer/semantic_graph.h"

// The only translation between the renderer's internal graph types and the
// BIP wire types.
//
// It is its own module for three reasons.
//
//   * Every function here is a switch with no default case. A member added to
//     the mojom, or to the internal mirror in semantic_graph.h, is a compile
//     error rather than a silent fall-through. For an action type or a
//     sensitivity class, a silent fall-through is a security bug rather than
//     a defect - so this property is worth protecting, and it is easier to
//     protect in a file that contains nothing else.
//
//   * It is where the enumerations that are deliberately shaped differently
//     are handled, and that has to be visible. Internal AdapterKind has seven
//     members and mojom::AdapterKind has eleven: every internal member has a
//     wire member, and four wire members name adapters this process does not
//     have. So ToMojom(AdapterKind) is total and FromMojom is not - the
//     asymmetry is the trust boundary, not an oversight.
//
//   * Keeping it out of the endpoint leaves the endpoint readable. Seven
//     hundred lines of switch statements around the interface methods made
//     the authority boundary hard to see, and the authority boundary is the
//     part a reviewer is there to check.

namespace taffy::wire {

// --- enum conversions -------------------------------------------------------

mojom::SourceKind ToMojom(SourceKind kind);
mojom::AdapterStatus ToMojom(AdapterStatus status);
mojom::Transformation ToMojom(Transformation transformation);
mojom::ContentTrust ToMojom(RendererContentTrust trust);
mojom::ContentSignal ToMojom(ContentSignal signal);
mojom::SemanticField ToMojom(SemanticField field);
mojom::Sensitivity ToMojom(Sensitivity sensitivity);
mojom::ChallengeKind ToMojom(ChallengeKind challenge);
mojom::SemanticRole ToMojom(SemanticRole role);
mojom::NodeState ToMojom(NodeState state);
mojom::RelationshipKind ToMojom(EdgeType type);
mojom::ActionType ToMojom(ActionKind action);
mojom::ValueKind ToMojom(ValueKind kind);
mojom::AttributeName ToMojom(AttributeKey key);
mojom::WarningCode ToMojom(WarningCode code);
mojom::BudgetKind ToMojom(BudgetKind kind);
mojom::ObservationScope ToMojom(ExtractionScope scope);
mojom::DocumentLifecycleState ToMojom(DocumentLifecycle lifecycle);
mojom::RendererActionOutcome ToMojom(PreconditionCode code);

// Every internal adapter kind has a wire member, so this cannot fail. It
// could once: selection and layout had none until the additive minor contract
// step recorded in taffy-core/contracts/bip/schema/bip.version.json appended
// SELECTION and LAYOUT. Returning a value rather than an optional is the honest
// signature now, and it is what stops a caller writing a fallback for a case
// that can no longer happen.
mojom::AdapterKind ToMojom(AdapterKind kind);

// The other direction still can fail. Four wire members name adapters this
// process does not have, and nullopt is what "this endpoint cannot serve
// that" looks like at a call site - never the nearest adapter it does have.
std::optional<AdapterKind> FromMojom(mojom::AdapterKind kind);
std::optional<Sensitivity> FromMojom(mojom::Sensitivity sensitivity);
std::optional<ChallengeKind> FromMojom(mojom::ChallengeKind challenge);
SemanticRole FromMojom(mojom::SemanticRole role);
NodeState FromMojom(mojom::NodeState state);
ExtractionScope FromMojom(mojom::ObservationScope scope);
DocumentLifecycle FromMojom(mojom::DocumentLifecycleState state);

// The one place an incoming action type is interpreted.
//
// It returned an optional until protocol 0.8, because the four writing
// operations were reserved wire values and decoded to nothing at all. They are
// specified operations now, so every member of the wire enumeration has an
// internal counterpart and this cannot fail - the same honest-signature
// argument as ToMojom(AdapterKind) above. What refuses a write is no longer a
// gap in a translation table; it is the milestone that owns the tool, the
// action class no milestone authorizes, the browser dispatcher, and the
// node's own advertised actions[], each of which refuses for a reason it can
// state.
ActionKind FromMojom(mojom::ActionType action);

// --- struct conversions -----------------------------------------------------

mojom::SnapshotWarningPtr ToMojom(const ObservationWarning& warning);
mojom::FieldEvidencePtr ToMojom(const FieldEvidence& evidence);
mojom::SemanticNodePtr ToMojom(const SemanticNode& node);
mojom::SemanticEdgePtr ToMojom(const SemanticEdge& edge);

// Destinations leave this process at origin granularity only.
//
// How much of a URL a caller may see is task and provider policy (protocol
// section 7.2). The isolated core service makes that decision; the browser
// validates and narrows its grant against the committed navigation record.
// A renderer that guessed generously would be making a disclosure decision
// it has no basis for, so it emits the most conservative representation.
// Returns null for a URL that is not an ordinary web navigation.
mojom::DestinationPtr ToMojom(const Destination& destination);

}  // namespace taffy::wire

#endif  // TAFFY_RENDERER_WIRE_CONVERSIONS_H_
