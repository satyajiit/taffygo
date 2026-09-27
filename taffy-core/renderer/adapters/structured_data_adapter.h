// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_ADAPTERS_STRUCTURED_DATA_ADAPTER_H_
#define TAFFY_RENDERER_ADAPTERS_STRUCTURED_DATA_ADAPTER_H_

#include <string_view>

#include "taffy/renderer/adapters/adapter.h"

namespace taffy {

// CAP-PI-004. What a page publishes about itself: JSON-LD blocks and
// microdata attributes.
//
// This used to live inside the metadata adapter. It is its own module now for
// two reasons that both matter: the feature catalog lists it as a capability
// of its own, and the two jobs have opposite trust postures. Document
// metadata is browser-owned fact; structured data is the least trustworthy
// source in the stack. Putting them in one file made it easy to write code
// that treated one like the other.
//
// Structured data is authored for search engines. It goes stale relative to
// what the page displays, it is invisible to the user, and it is the easiest
// place for a page to state something it does not show - a different price, a
// different availability, an instruction aimed at a model. So:
//
//   * every value is marked Transformation::kNormalized, never kNone,
//     because none of it is what the user can see;
//
//   * every candidate carries a CORROBORATION verdict against what the page
//     actually renders. That is the only freshness test a renderer can
//     honestly perform (protocol section 7.6 asks for freshness and
//     visibility checks): a structured price that matches the rendered price
//     is corroborated, one that appears nowhere on the page is uncorroborated,
//     and one that contradicts a rendered value for the same property is
//     contradicted - which is exactly the stale-JSON-LD shape the comparison
//     fixtures carry;
//
//   * disagreement is preserved, never resolved. Two candidates for one
//     property are emitted as two nodes with their own provenance, joined by
//     a SAME_ENTITY_AS edge, and the adapter reports kConflicted. Picking a
//     winner is the one thing this adapter may not do (protocol section 7.5);
//
//   * structured data never contributes an action. A destination that only
//     exists in JSON-LD is not something a user could have clicked, so these
//     nodes are allocated in the derived identity space, which
//     RendererActionExecutor refuses to resolve at all.
//
// Parsing untrusted JSON in C++ is allowed here specifically because this
// code runs in the sandboxed renderer: the Chromium Rule of Two permits an
// unsafe language on untrusted input only inside a sandbox, and the input is
// size- and depth-bounded before the parser sees it, from the one limits
// policy.
class StructuredDataAdapter final : public Adapter {
 public:
  StructuredDataAdapter();
  ~StructuredDataAdapter() override;

  AdapterKind kind() const override;
  std::string_view name() const override;
  uint32_t extraction_rule_version() const override;
  AdapterResult Run(ExtractionContext& context) override;
};

}  // namespace taffy

#endif  // TAFFY_RENDERER_ADAPTERS_STRUCTURED_DATA_ADAPTER_H_
