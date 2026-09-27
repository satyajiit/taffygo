// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_ADAPTERS_STRUCTURED_DATA_CORROBORATION_H_
#define TAFFY_RENDERER_ADAPTERS_STRUCTURED_DATA_CORROBORATION_H_

// Whether a machine-readable claim is corroborated by something the user can
// actually perceive.
//
// Its own translation unit because it is the rule that decides how much a
// structured claim is worth. Protocol section 7.6 puts accessible and visible
// page state above structured metadata, so the index is built from what the
// higher-precedence adapters already emitted rather than by walking the
// document again. A claim the page never shows is reported as uncorroborated,
// never dropped and never promoted.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "taffy/renderer/semantic_graph.h"

namespace taffy {

// Defined in taffy/renderer/adapters/adapter.h, which is not
// included here: this header names it only through a pointer, and adapter.h
// carries the whole adapter contract - the corroboration rule is a leaf of that
// contract, so depending on it in the other direction would make every adapter
// rebuild when this rule changes.
struct ExtractedGraph;
class BudgetLedger;

}  // namespace taffy

namespace taffy::structured_data_corroboration {

// The verdict on one machine-readable claim, and the whole point of this
// translation unit.
//
// It never leaves the renderer: it is not on the wire, not in the JSON schema
// and not in the mojom. Its only effect is the confidence the adapter attaches
// to a value, because the protocol's rule is that corroboration is *evidence
// about* a claim rather than a filter on it. Nothing here drops a fact or picks
// a winner between two of them.
enum class Corroboration {
  // The page never showed this value. That may only mean the content was out of
  // the extraction's scope, so it lowers confidence rather than raising a
  // conflict. This is the default because absence of evidence is the starting
  // state, not a finding.
  kUncorroborated,

  // A perceivable node carries the same comparison form.
  kCorroborated,

  // The page shows a *different* value for the same property. That is the shape
  // a stale structured price has, and it is the only verdict that drives
  // confidence to zero.
  kContradicted,
};

// Every perceivable string this extraction has already collected, in
// comparison form.
struct PerceivableIndex {
  std::vector<std::pair<std::string, SemanticNodeId>> entries;

  // The entries the page groups together, as index positions into `entries`.
  //
  // A page states a property and its value in two places far more often than
  // in one: "Price today:" is one text node and "$129.00" is another inside a
  // <strong> beside it. Read one entry at a time, neither of them says
  // anything about the other. Grouped by the block the page put them both in,
  // they are a label and its value, which is what makes it possible to say
  // that the page shows a DIFFERENT price rather than merely not this one.
  //
  // Built from the containment edges the earlier adapters emitted, never from
  // a second walk of the document: a rule about what the page shows must be
  // computed from what was actually extracted, or the two can disagree.
  std::vector<std::vector<size_t>> blocks;

  void Add(std::string_view text, const SemanticNodeId& node_id);

  // The node that shows this comparison form, when one does. Exact match, or
  // the form appearing as a whole token run inside a longer perceivable
  // string - "129.00" inside "price 129.00 usd". Substring matching is
  // bounded to whole runs so that "12" does not corroborate "129.00".
  std::optional<SemanticNodeId> Find(std::string_view comparison_form,
                                     BudgetLedger& ledger) const;

  // The node showing a quantity that DISAGREES with `comparison_form`, inside
  // a block the page also labelled with one of `labels`.
  //
  // Only called once Find() has already answered no, so "the page does not
  // show this value" is established before this asks the narrower question
  // "and does it show another one in its place?". Both the claim and the page
  // value must be bare quantities (structured_data_vocabulary's
  // IsMeasuredQuantity), and the page must have named the property in the same
  // block; anything less certain than that is left uncorroborated instead,
  // because a conflict this component invents is worse than one it misses -
  // the miss lowers a confidence, the invention tells a caller the page is
  // lying.
  //
  // The tightest block wins, so the edge that records the disagreement points
  // at the price the reader sees rather than at the region containing it.
  std::optional<SemanticNodeId> FindDisagreement(
      const std::vector<std::string_view>& labels,
      std::string_view comparison_form,
      BudgetLedger& ledger) const;
};

// Builds the index from what the earlier adapters produced.
PerceivableIndex BuildPerceivableIndex(const ExtractedGraph* accumulated,
                                       BudgetLedger& ledger);

}  // namespace taffy::structured_data_corroboration

#endif  // TAFFY_RENDERER_ADAPTERS_STRUCTURED_DATA_CORROBORATION_H_
