// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_ADAPTERS_STRUCTURED_DATA_CLAIMS_H_
#define TAFFY_RENDERER_ADAPTERS_STRUCTURED_DATA_CLAIMS_H_

// What a page states about itself in machine-readable markup, read out of the
// document and bounded, before anything decides what it is worth.
//
// Its own translation unit because collecting and judging are different jobs
// with different hazards. Everything here reads attacker-controlled input: a
// JSON-LD block is authored for search engines, parsed in the renderer, and
// arbitrarily deep unless something stops it, and a microdata walk is a
// stack-depth attack a hostile fixture will absolutely try. So every loop here
// is bounded before it starts, and every skip is recorded rather than
// swallowed. Nothing here looks at what the page displays, assigns a
// confidence, or emits a node - those are the corroboration rule and the
// adapter, and keeping them out of this file is what lets a reviewer check the
// bounds without reading past them.

#include <optional>
#include <string>
#include <vector>

#include "taffy/renderer/adapters/structured_data_corroboration.h"
#include "taffy/renderer/semantic_graph.h"

namespace blink {
class WebDocument;
}  // namespace blink

namespace taffy {

// Defined in taffy/renderer/adapters/adapter.h, which is not
// included here for the same reason structured_data_corroboration.h does not
// include it: adapter.h carries the whole adapter contract, and this file is a
// leaf of that contract rather than a peer of it.
struct ExtractionContext;

}  // namespace taffy

namespace taffy::structured_data_claims {

// One observed structured statement, before it becomes a node.
//
// The verdict fields are here rather than in a second structure because a
// claim is one thing all the way through the adapter: collected, judged
// against what the page shows, then emitted. Splitting the record would mean
// keeping two vectors in step, and a claim judged under the wrong index is a
// confidence attached to the wrong value.
struct StructuredClaim {
  StructuredClaim();
  StructuredClaim(const StructuredClaim&);
  StructuredClaim(StructuredClaim&&);
  StructuredClaim& operator=(const StructuredClaim&);
  StructuredClaim& operator=(StructuredClaim&&);
  ~StructuredClaim();

  std::string property;
  std::string value;
  std::string comparison_form;
  SourceKind source = SourceKind::kJsonLd;
  std::string locator;
  std::string entity_type;
  // True when the value was authored only for machines rather than read from
  // the element's rendered text. Microdata can be either; JSON-LD is always
  // machine-authored.
  bool authored_for_machines = true;
  structured_data_corroboration::Corroboration corroboration =
      structured_data_corroboration::Corroboration::kUncorroborated;
  std::optional<SemanticNodeId> corroborating_node;
  // The perceivable node that shows a DIFFERENT value for this property, when
  // one does. Held apart from `corroborating_node` because the two draw
  // different edges: a node that agrees is a SOURCE_FOR, and a node that
  // disagrees is a second candidate for the same property, joined with
  // SAME_ENTITY_AS so a consumer sees one disputed price rather than two
  // unrelated ones.
  std::optional<SemanticNodeId> contradicting_node;
};

// The key two claims must share before they can be called claims about the
// same thing. A bare property name is not that key: one Product carries a
// `name`, its Brand carries a `name`, and an Offer carries a `price` while a
// nested shipping Offer carries another. Grouping on the property alone reads
// those as one property stated twice, which turns "Lumen Arc desk lamp" and
// "Lumen" into a disagreement about the product's name, and would join them
// with a kSameEntityAs edge - an edge whose own name says it must not be drawn
// between two different entities.
//
// `entity_type` is already scoped correctly by both collectors: the JSON-LD
// walk passes the enclosing `@type` down each branch, and the microdata walk
// carries `itemtype` in the stack entry for exactly this reason. This function
// is only the place that finally uses it. The unit separator is a byte no
// schema.org type or property name may contain, so no pair of distinct
// (type, property) values can collide into one key.
std::string EntityScopedProperty(const StructuredClaim& claim);

// Everything one document stated, together with what had to be skipped to stay
// inside the limits policy. The three flags are reported rather than returned
// as a failure: a page with one oversized block still has the rest to say, and
// an adapter that dropped something must be able to say so (protocol section
// 15).
struct CollectedClaims {
  CollectedClaims();
  CollectedClaims(const CollectedClaims&) = delete;
  CollectedClaims& operator=(const CollectedClaims&) = delete;
  CollectedClaims(CollectedClaims&&);
  CollectedClaims& operator=(CollectedClaims&&);
  ~CollectedClaims();

  std::vector<StructuredClaim> claims;
  bool oversized_block_skipped = false;
  bool malformed_block_skipped = false;
  bool block_limit_reached = false;
  bool claim_limit_reached = false;
  bool traversal_limit_reached = false;
};

// Reads every JSON-LD block and every microdata attribute this document
// carries, charging the shared budget as it goes and stopping when the budget
// or the deadline says so.
CollectedClaims Collect(ExtractionContext& context,
                        const blink::WebDocument& document);

}  // namespace taffy::structured_data_claims

#endif  // TAFFY_RENDERER_ADAPTERS_STRUCTURED_DATA_CLAIMS_H_
