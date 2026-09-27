// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_ADAPTERS_ACCESSIBILITY_ADAPTER_H_
#define TAFFY_RENDERER_ADAPTERS_ACCESSIBILITY_ADAPTER_H_

#include <string_view>

#include "taffy/renderer/adapters/adapter.h"

namespace taffy {

// CAP-PI-003. What a user can perceive and operate, taken from Blink's
// accessibility representation: roles, computed names, states, and the
// relationships a user's assistive technology already sees.
//
// This adapter is the reason the DOM adapter can be conservative. The
// accessibility tree already solves the problems that make a naive DOM walk
// wrong: composed-tree traversal across shadow boundaries, retargeting,
// aria-owns reparenting, computed accessible names, ignored and invisible
// subtrees, and the difference between "in the DOM" and "presented to the
// user". Chromium computes all of that for TalkBack already, so using it is
// both cheaper and narrower than a second implementation - and, per spec
// section 8.2, it means closed shadow content is reached only through a
// capability Chromium already has rather than through a bypass added for AI.
//
// It is also the adapter that costs the most, because building an
// accessibility tree on a page that does not already have one is real work on
// a mid-range device. The budget applies here first.
class AccessibilityAdapter final : public Adapter {
 public:
  AccessibilityAdapter();
  ~AccessibilityAdapter() override;

  AdapterKind kind() const override;
  std::string_view name() const override;
  uint32_t extraction_rule_version() const override;
  AdapterResult Run(ExtractionContext& context) override;
};

}  // namespace taffy

#endif  // TAFFY_RENDERER_ADAPTERS_ACCESSIBILITY_ADAPTER_H_
