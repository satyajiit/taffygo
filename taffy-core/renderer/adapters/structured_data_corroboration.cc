// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/adapters/structured_data_corroboration.h"

#include <algorithm>
#include <map>
#include <utility>

#include "base/strings/string_split.h"
#include "taffy/renderer/adapters/adapter.h"
#include "taffy/renderer/adapters/structured_data_vocabulary.h"

namespace taffy::structured_data_corroboration {

namespace {

// How far above an entry the block that contains it may be.
//
// The walk stops at the first block it meets, so a larger number here can only
// find a block for an entry buried deeper - it can never reach past a tighter
// one. Four hops covers the shapes a page uses to put a label beside a value:
// `<p>Price today: <strong>$129.00</strong></p>` leaves the label one hop under
// the paragraph and the value two, and a span or two of styling markup around
// either is ordinary. It still stops well short of the document, which is the
// point: with an unbounded walk, "in the same block" would decay into "on the
// same page", and any number anywhere would answer for any label.
constexpr size_t kMaxHopsToBlock = 4;

// The roles a page uses to group a label with its value. An entry's block is
// the smallest of these that contains it.
//
// kDocument and kRegion are deliberately absent. They are landmarks: a banner
// or a main region can hold an entire article, and treating one as a block
// would pair a word in its first paragraph with a number in its last.
bool IsBlockRole(SemanticRole role) {
  switch (role) {
    case SemanticRole::kHeading:
    case SemanticRole::kParagraph:
    case SemanticRole::kListItem:
    case SemanticRole::kTableRow:
    case SemanticRole::kTableCell:
      return true;
    default:
      return false;
  }
}

// Whether `form` appears in `text` as a whole token run rather than as part of
// a longer token, so that "12" is not found inside "129.00" and "price" is not
// found inside "priceless". Every occurrence is considered, not just the
// first: a string may carry the form as part of a longer token before it
// carries it on its own.
bool ContainsWholeRun(std::string_view text, std::string_view form) {
  if (form.empty() || form.size() > text.size()) {
    return false;
  }
  size_t at = text.find(form);
  while (at != std::string_view::npos) {
    const bool starts_clean = at == 0 || text[at - 1] == ' ';
    const size_t end = at + form.size();
    const bool ends_clean = end == text.size() || text[end] == ' ';
    if (starts_clean && ends_clean) {
      return true;
    }
    at = text.find(form, at + 1);
  }
  return false;
}

// The first quantity in one perceivable string that is not `comparison_form`.
// Token by token rather than whole string, because a page may write a label
// and its value into one text node ("Price today: $129.00" reaches here as
// "price today 129.00") or into two, and the same rule has to read both.
bool StatesADifferentQuantity(std::string_view text,
                              std::string_view comparison_form) {
  for (const std::string_view token : base::SplitStringPiece(
           text, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    if (token != comparison_form &&
        structured_data_vocabulary::IsMeasuredQuantity(token)) {
      return true;
    }
  }
  return false;
}

}  // namespace

void PerceivableIndex::Add(std::string_view text,
                           const SemanticNodeId& node_id) {
  std::string form = structured_data_vocabulary::ComparisonForm(text);
  if (form.empty()) {
    return;
  }
  entries.emplace_back(std::move(form), node_id);
}

std::optional<SemanticNodeId> PerceivableIndex::Find(
    std::string_view form,
    BudgetLedger& ledger) const {
  if (form.empty()) {
    return std::nullopt;
  }
  for (const auto& [text, node_id] : entries) {
    if (!ledger.CheckDeadline()) {
      return std::nullopt;
    }
    if (ContainsWholeRun(text, form)) {
      return node_id;
    }
  }
  return std::nullopt;
}

std::optional<SemanticNodeId> PerceivableIndex::FindDisagreement(
    const std::vector<std::string_view>& labels,
    std::string_view comparison_form,
    BudgetLedger& ledger) const {
  if (labels.empty() ||
      !structured_data_vocabulary::IsMeasuredQuantity(comparison_form)) {
    return std::nullopt;
  }

  std::optional<SemanticNodeId> found;
  size_t found_in_block_of_size = 0;

  for (const std::vector<size_t>& block : blocks) {
    if (!ledger.CheckDeadline()) {
      return std::nullopt;
    }
    // The tightest block that answers wins. A larger one may carry the same
    // label and the same number, but it carries more besides, and the node
    // this returns is the one the disagreement edge will point at.
    if (found.has_value() && block.size() >= found_in_block_of_size) {
      continue;
    }
    const bool block_names_the_property =
        std::ranges::any_of(block, [&](size_t index) {
          return std::ranges::any_of(labels, [&](std::string_view label) {
            return ContainsWholeRun(entries[index].first, label);
          });
        });
    if (!block_names_the_property) {
      continue;
    }
    for (const size_t index : block) {
      if (StatesADifferentQuantity(entries[index].first, comparison_form)) {
        found = entries[index].second;
        found_in_block_of_size = block.size();
        break;
      }
    }
  }
  return found;
}

PerceivableIndex BuildPerceivableIndex(const ExtractedGraph* accumulated,
                                       BudgetLedger& ledger) {
  PerceivableIndex index;
  if (!accumulated) {
    return index;
  }
  for (const SemanticNode& node : accumulated->nodes) {
    if (!ledger.CheckDeadline()) {
      return index;
    }
    if (node.name.has_value()) {
      index.Add(node.name.value(), node.node_id);
    }
    for (const TextRun& run : node.text_runs) {
      index.Add(run.text, node.node_id);
    }
  }

  std::map<SemanticNodeId, SemanticRole> role_of;
  for (const SemanticNode& node : accumulated->nodes) {
    if (!ledger.CheckDeadline()) {
      return index;
    }
    role_of.emplace(node.node_id, node.role);
  }
  std::map<SemanticNodeId, SemanticNodeId> parent_of;
  for (const SemanticEdge& edge : accumulated->edges) {
    if (!ledger.CheckDeadline()) {
      return index;
    }
    if (edge.relationship == EdgeType::kContains) {
      parent_of.emplace(edge.to_node_id, edge.from_node_id);
    }
  }

  std::map<SemanticNodeId, std::vector<size_t>> by_block;
  for (size_t i = 0; i < index.entries.size(); ++i) {
    if (!ledger.CheckDeadline()) {
      return index;
    }
    // Strictly above the entry's own node. A table cell carrying both a name
    // and a static-text child would otherwise be its own block, and a header
    // cell would never be grouped with the data cell beside it.
    SemanticNodeId walker = index.entries[i].second;
    for (size_t hop = 0; hop < kMaxHopsToBlock; ++hop) {
      const auto parent = parent_of.find(walker);
      if (parent == parent_of.end()) {
        break;
      }
      walker = parent->second;
      const auto role = role_of.find(walker);
      if (role != role_of.end() && IsBlockRole(role->second)) {
        by_block[walker].push_back(i);
        break;
      }
    }
    // An entry with no block within reach - a page built entirely from
    // unlabelled containers - takes no part in the disagreement rule. It is
    // still in `entries`, so it can still corroborate.
  }
  for (auto& [block_node, member_entries] : by_block) {
    index.blocks.push_back(std::move(member_entries));
  }
  return index;
}

}  // namespace taffy::structured_data_corroboration
