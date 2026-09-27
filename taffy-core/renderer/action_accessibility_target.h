// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_ACTION_ACCESSIBILITY_TARGET_H_
#define TAFFY_RENDERER_ACTION_ACCESSIBILITY_TARGET_H_

#include "taffy/renderer/action_accessibility_mapping.h"
#include "taffy/renderer/semantic_graph_store.h"
#include "third_party/blink/public/web/web_ax_object.h"
#include "third_party/blink/public/web/web_document.h"

namespace taffy {

enum class AccessibilityTargetStatus {
  kOk,
  kNodeGone,
  kUnsupported,
};

struct ResolvedAccessibilityTarget {
  AccessibilityTargetStatus status = AccessibilityTargetStatus::kUnsupported;
  blink::WebAXObject object;
};

// Resolves the exact accessibility object a mapped operation may act on.
//
// The ordinary path is an identity lookup. The option path is deliberately
// deeper: it performs a bounded, exact and unique option-value match beneath
// the already-resolved select, then returns that option's AX object. It never
// writes a DOM property and never falls back to a label or position.
ResolvedAccessibilityTarget ResolveAccessibilityTarget(
    const blink::WebDocument& document,
    const SemanticGraphStore::DomNodeKey& key,
    const AccessibilityAction& action);

}  // namespace taffy

#endif  // TAFFY_RENDERER_ACTION_ACCESSIBILITY_TARGET_H_
