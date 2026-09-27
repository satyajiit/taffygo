// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/action_accessibility_target.h"

#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>

#include "taffy/renderer/observation_limits.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/web/web_node.h"
#include "third_party/blink/public/web/web_option_element.h"
#include "third_party/blink/public/web/web_select_element.h"

namespace taffy {
namespace {

ResolvedAccessibilityTarget ExactNodeTarget(
    const blink::WebDocument& document,
    const SemanticGraphStore::DomNodeKey& key) {
  blink::WebAXObject object;
  switch (key.space) {
    case SemanticGraphStore::IdentitySpace::kAccessibility:
      object = blink::WebAXObject::FromWebDocumentByID(
          document, static_cast<int>(key.dom_node_id));
      break;
    case SemanticGraphStore::IdentitySpace::kDom: {
      const blink::WebNode node =
          blink::WebNode::FromDomNodeId(static_cast<int>(key.dom_node_id));
      if (node.IsNull() || node.GetDocument() != document) {
        return {.status = AccessibilityTargetStatus::kNodeGone};
      }
      object = blink::WebAXObject::FromWebNode(node);
      break;
    }
    case SemanticGraphStore::IdentitySpace::kDerived:
      return {.status = AccessibilityTargetStatus::kNodeGone};
  }
  return object.IsDetached()
             ? ResolvedAccessibilityTarget{.status = AccessibilityTargetStatus::
                                               kNodeGone}
             : ResolvedAccessibilityTarget{
                   .status = AccessibilityTargetStatus::kOk, .object = object};
}

ResolvedAccessibilityTarget ExactOptionTarget(
    const blink::WebDocument& document,
    const SemanticGraphStore::DomNodeKey& key,
    std::string_view requested_value) {
  if (key.space != SemanticGraphStore::IdentitySpace::kDom) {
    return {.status = AccessibilityTargetStatus::kUnsupported};
  }
  const blink::WebNode node =
      blink::WebNode::FromDomNodeId(static_cast<int>(key.dom_node_id));
  if (node.IsNull() || node.GetDocument() != document) {
    return {.status = AccessibilityTargetStatus::kNodeGone};
  }
  const blink::WebSelectElement select =
      node.DynamicTo<blink::WebSelectElement>();
  if (select.IsNull()) {
    return {.status = AccessibilityTargetStatus::kUnsupported};
  }

  const ObservationLimits& limits = ObservationLimits::ProcessSafeCeiling();
  const uint32_t max_nodes = limits.snapshot().max_nodes();
  const uint32_t max_value_bytes = limits.snapshot().max_text_bytes();
  if (requested_value.empty() || requested_value.size() > max_value_bytes) {
    return {.status = AccessibilityTargetStatus::kUnsupported};
  }
  std::optional<blink::WebOptionElement> match;
  uint32_t visited = 0;
  for (blink::WebNode current = select.FirstChild(); !current.IsNull();
       current = current.NextInFlatTree(select)) {
    if (visited >= max_nodes) {
      return {.status = AccessibilityTargetStatus::kUnsupported};
    }
    ++visited;
    blink::WebOptionElement option =
        current.DynamicTo<blink::WebOptionElement>();
    if (option.IsNull()) {
      continue;
    }
    const blink::WebString value = option.Value();
    // The first comparison bounds the UTF-8 conversion itself (at worst four
    // bytes per UTF-16 code unit); the second enforces the configured byte
    // contract exactly.
    if (value.IsNull() || value.length() > max_value_bytes) {
      return {.status = AccessibilityTargetStatus::kUnsupported};
    }
    const std::string value_utf8 = value.Utf8();
    if (value_utf8.size() > max_value_bytes) {
      return {.status = AccessibilityTargetStatus::kUnsupported};
    }
    if (value_utf8 != requested_value) {
      continue;
    }
    // Duplicate values make a value-only request ambiguous. Choosing the
    // first would turn DOM order into authority, so refuse the entire action.
    // The option default action toggles an already-selected option off. A
    // state-setting SELECT_OPTION must never turn into that inverse action;
    // the browser can re-observe rather than treating a no-op as a write.
    if (match.has_value() || !option.IsEnabled() || option.IsSelected()) {
      return {.status = AccessibilityTargetStatus::kUnsupported};
    }
    match = option;
  }
  if (!match.has_value()) {
    return {.status = AccessibilityTargetStatus::kUnsupported};
  }
  blink::WebAXObject object = blink::WebAXObject::FromWebNode(*match);
  return object.IsDetached()
             ? ResolvedAccessibilityTarget{.status = AccessibilityTargetStatus::
                                               kNodeGone}
             : ResolvedAccessibilityTarget{
                   .status = AccessibilityTargetStatus::kOk, .object = object};
}

}  // namespace

ResolvedAccessibilityTarget ResolveAccessibilityTarget(
    const blink::WebDocument& document,
    const SemanticGraphStore::DomNodeKey& key,
    const AccessibilityAction& action) {
  switch (action.target) {
    case AccessibilityTarget::kExactNode:
      return ExactNodeTarget(document, key);
    case AccessibilityTarget::kOptionWithValue:
      return ExactOptionTarget(document, key, action.value);
  }
  return {.status = AccessibilityTargetStatus::kUnsupported};
}

}  // namespace taffy
