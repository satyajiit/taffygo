// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_ADAPTERS_ADAPTER_REGISTRY_H_
#define TAFFY_RENDERER_ADAPTERS_ADAPTER_REGISTRY_H_

#include <memory>
#include <string_view>
#include <vector>

#include "taffy/renderer/adapters/adapter.h"

namespace taffy {

// The one place the adapter set and its order are stated.
//
// There are two orders and conflating them is a real bug, so both are named
// here:
//
//   EXECUTION ORDER is which adapter runs first, and it matters because the
//   adapters share one budget ledger. An adapter that runs late can find the
//   node and byte budget already spent.
//
//   PRECEDENCE (protocol section 7.6) is which source wins when two of them
//   describe the same field, and it is decided downstream from the `sources`
//   and `evidence` a node carries. The renderer never resolves a conflict; it
//   preserves it.
//
// The two are deliberately kept identical for the five adapters that produce
// field values, because letting a lower-precedence adapter spend the budget
// first would starve a higher-precedence one and change the answer for
// budget reasons rather than evidence reasons. The two annotating adapters
// run last: they describe nodes the producers made, so there is nothing for
// them to do until the producers have run.
class AdapterRegistry {
 public:
  struct Entry {
    Entry();
    Entry(const Entry&) = delete;
    Entry& operator=(const Entry&) = delete;
    Entry(Entry&&);
    Entry& operator=(Entry&&);
    ~Entry();

    AdapterKind kind = AdapterKind::kDom;
    std::unique_ptr<Adapter> adapter;
    // Position in the protocol section 7.6 precedence list, 1 highest. Zero
    // means "not a source of field values": an annotator states visibility or
    // selection, never what a field's value is, so it has no place in a
    // precedence argument.
    uint32_t precedence = 0;
  };

  // A fresh, ordered adapter set. Adapters hold no state between runs, so
  // building them per extraction costs nothing and removes the question of
  // what a reused adapter might remember about the previous document.
  static std::vector<Entry> CreateAll();

  // Stable identifier for an adapter kind, used in capability reports and in
  // warning detail codes. Never user-facing text.
  static std::string_view NameOf(AdapterKind kind);

  // Protocol section 7.6 precedence for a kind; zero for the annotators.
  static uint32_t PrecedenceOf(AdapterKind kind);
};

}  // namespace taffy

#endif  // TAFFY_RENDERER_ADAPTERS_ADAPTER_REGISTRY_H_
