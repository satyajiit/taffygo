// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_ADAPTERS_SELECTION_ADAPTER_H_
#define TAFFY_RENDERER_ADAPTERS_SELECTION_ADAPTER_H_

#include <string_view>

#include "taffy/renderer/adapters/adapter.h"

namespace taffy {

// CAP-PI-005. What the user currently has selected, and nothing else.
//
// A selection is the most explicit scope signal a user can give: they
// highlighted this and not that. It is also the narrowest, which is why the
// protocol has an ObservationScope member for it - a request scoped to the
// selection is a request the user themselves bounded.
//
// This adapter annotates rather than duplicates. The nodes inside a selection
// were already described by the accessibility, DOM, and form adapters; what
// this adds is the state "the user has this selected", an edge from a
// selection region to each of them, and the selected text as one bounded run.
// Emitting a second copy of the content would double the byte cost and give a
// consumer two node identities for one thing on the screen.
//
// Two refusals are structural rather than checked:
//
//   * It never allocates an identity. Annotations resolve through
//     SemanticGraphStore::Lookup(), which does not allocate, so a selection
//     covering something no producing adapter described is reported as
//     partial rather than minted into a node nobody can resolve later.
//
//   * It never reads selected text out of a prohibited control. A caret
//     inside a password field is a selection; SelectionAsText() would return
//     the secret. So the focused element is classified BEFORE any text is
//     read, and a prohibited one produces a structural placeholder region
//     with no text at all (protocol section 9.1).
class SelectionAdapter final : public Adapter {
 public:
  SelectionAdapter();
  ~SelectionAdapter() override;

  AdapterKind kind() const override;
  std::string_view name() const override;
  uint32_t extraction_rule_version() const override;
  bool annotates_existing_nodes() const override;
  AdapterResult Run(ExtractionContext& context) override;
};

}  // namespace taffy

#endif  // TAFFY_RENDERER_ADAPTERS_SELECTION_ADAPTER_H_
