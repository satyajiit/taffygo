// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_ADAPTERS_DOM_ADAPTER_H_
#define TAFFY_RENDERER_ADAPTERS_DOM_ADAPTER_H_

#include <string_view>

#include "taffy/renderer/adapters/adapter.h"

namespace taffy {

// CAP-PI-002. Structure and text from the light DOM of one frame: bounded
// text runs, typed relationships, and allowlisted typed attributes.
//
// What this adapter deliberately does not do:
//
//   * It emits no raw HTML, no script, no stylesheet, no event handler, and
//     no arbitrary attribute map. Attributes reach the graph only through
//     AttributeKey, which is an allowlist rather than a filter: a name with
//     no member cannot be emitted at all (protocol section 7.3).
//   * It does not cross a shadow boundary, open or closed. Composed-tree
//     semantics, retargeting, and slot assignment are things Blink's
//     accessibility layer already models correctly; reimplementing them here
//     from the public Web API would produce a second, worse answer and a new
//     way to see content that Chromium's own surfaces cannot. Shadow content
//     reaches the graph through AccessibilityAdapter, which is the capability
//     Chromium already has (protocol section 8.2).
//   * It does not cross a frame boundary. Each frame has its own endpoint,
//     epoch, and node namespace (protocol section 8.1); the browser broker
//     assembles the tree from browser-owned topology.
//   * It reads no form-control value. That is FormSchemaAdapter's territory,
//     and even there the answer is "no values before M5".
//   * It attaches no semantics to canvas pixels (protocol section 8.3).
class DomAdapter final : public Adapter {
 public:
  DomAdapter();
  ~DomAdapter() override;

  AdapterKind kind() const override;
  std::string_view name() const override;
  uint32_t extraction_rule_version() const override;
  AdapterResult Run(ExtractionContext& context) override;
};

}  // namespace taffy

#endif  // TAFFY_RENDERER_ADAPTERS_DOM_ADAPTER_H_
