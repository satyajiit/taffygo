// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/adapters/layout_visibility_adapter.h"

#include <inttypes.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/strings/stringprintf.h"
#include "taffy/renderer/adapters/layout_visibility_probe.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "ui/gfx/geometry/rect.h"

// The geometry and hit-test accessors this pass depends on live in
// layout_visibility_probe.cc, with the list of what to verify about them.

namespace taffy {

namespace {

constexpr uint32_t kLayoutRuleVersion = 1;
constexpr char kAdapterName[] = "layout";

}  // namespace

LayoutVisibilityAdapter::LayoutVisibilityAdapter() = default;
LayoutVisibilityAdapter::~LayoutVisibilityAdapter() = default;

AdapterKind LayoutVisibilityAdapter::kind() const {
  return AdapterKind::kLayout;
}

std::string_view LayoutVisibilityAdapter::name() const {
  return kAdapterName;
}

uint32_t LayoutVisibilityAdapter::extraction_rule_version() const {
  return kLayoutRuleVersion;
}

bool LayoutVisibilityAdapter::annotates_existing_nodes() const {
  return true;
}

AdapterResult LayoutVisibilityAdapter::Run(ExtractionContext& context) {
  AdapterResult result;

  const blink::WebDocument document = context.frame->GetDocument();
  if (document.IsNull()) {
    result.status = AdapterStatus::kUnsupported;
    result.warnings.emplace_back(WarningCode::kAdapterUnavailable,
                                 "no-document");
    return result;
  }
  if (!context.accumulated || context.accumulated->nodes.empty()) {
    result.status = AdapterStatus::kUnsupported;
    result.warnings.emplace_back(WarningCode::kAdapterUnavailable,
                                 "no-nodes-to-annotate");
    return result;
  }

  const std::optional<gfx::Rect> viewport = LayoutViewportRect(context.frame);
  if (!viewport.has_value()) {
    // Without a viewport rectangle there is nothing to call a node on-screen
    // or off-screen against. Every node would be "not determined", so the
    // honest report is that this adapter could not run at all rather than
    // that it ran and found nothing.
    result.status = AdapterStatus::kUnsupported;
    result.warnings.emplace_back(WarningCode::kAdapterUnavailable,
                                 "no-viewport-geometry");
    return result;
  }
  const gfx::Rect viewport_rect = viewport.value();

  // One probe for the whole pass: it holds the accessibility context every
  // hit test below needs (see LayoutVisibilityProbe).
  LayoutVisibilityProbe probe(document);
  const bool can_probe_occlusion = probe.can_probe_occlusion();

  const LayoutLimits& limits = context.limits->layout();
  const uint32_t max_depth = context.limits->snapshot().max_depth();
  uint32_t probes_used = 0;
  uint32_t determined = 0;
  uint32_t not_determined = 0;
  bool probe_budget_reached = false;
  bool cache_not_clean = false;

  bool deadline_reached = false;
  for (const SemanticNode& node : context.accumulated->nodes) {
    if (!context.ledger->CheckDeadline()) {
      // Every node after this one has no determination, and the caller is
      // told so by the flag rather than by an arithmetic count that could be
      // wrong. An action precondition that forbids occlusion is refused for
      // all of them.
      deadline_reached = true;
      break;
    }

    const SemanticGraphStore::LiveNode* live =
        context.store->FindLive(node.node_id);
    if (!live) {
      // Retired between the producing pass and this one. Not an error: the
      // node is gone and the delta stream is the thing that reports that.
      ++not_determined;
      continue;
    }

    const LayoutVisibilityProbe::Measurement measurement =
        probe.Measure(live->dom_key, viewport_rect, limits, max_depth,
                      probes_used < limits.max_occlusion_probes());
    probes_used += measurement.probed ? 1 : 0;
    probe_budget_reached |= measurement.probe_budget_reached;
    cache_not_clean |= measurement.cache_not_clean;
    const std::optional<gfx::Rect>& bounds = measurement.bounds;
    if (!bounds.has_value()) {
      // No element to measure. The document node, a structured-data
      // statement, and the selection region all land here, correctly.
      ++not_determined;
      continue;
    }
    const LayoutVisibility visibility = measurement.visibility;
    const bool occlusion_determined = measurement.occlusion_determined;

    if (visibility == LayoutVisibility::kNotDetermined) {
      ++not_determined;
      // An annotation is still emitted, carrying the bounds and no state.
      // Bounds are useful diagnostics even when visibility is unknown, and
      // recording them is what lets the precondition check notice that a
      // target moved.
      NodeAnnotation annotation;
      annotation.node_id = node.node_id;
      NodeBounds recorded;
      recorded.x = bounds->x();
      recorded.y = bounds->y();
      recorded.width = bounds->width();
      recorded.height = bounds->height();
      annotation.bounds = recorded;
      annotation.occlusion_determined = false;
      annotation.evidence.push_back(MakeEvidence(
          SemanticField::kBounds, SourceKind::kAdapter,
          base::StringPrintf("layout/%" PRId64, live->dom_key.dom_node_id),
          Transformation::kNone));
      context.store->AnnotateLiveNode(node.node_id, {}, recorded, false);
      result.annotations.push_back(std::move(annotation));
      continue;
    }

    ++determined;
    NodeAnnotation annotation;
    annotation.node_id = node.node_id;
    // Off-screen asserts not-visible beside it (StatesFor).
    annotation.states = StatesFor(visibility);
    NodeBounds recorded;
    recorded.x = bounds->x();
    recorded.y = bounds->y();
    recorded.width = bounds->width();
    recorded.height = bounds->height();
    annotation.bounds = recorded;
    annotation.occlusion_determined = occlusion_determined;
    annotation.evidence.push_back(MakeEvidence(
        SemanticField::kStates, SourceKind::kAdapter,
        base::StringPrintf("layout/%" PRId64, live->dom_key.dom_node_id),
        Transformation::kNone));
    annotation.evidence.push_back(MakeEvidence(
        SemanticField::kBounds, SourceKind::kAdapter,
        base::StringPrintf("layout/%" PRId64, live->dom_key.dom_node_id),
        Transformation::kNone));

    context.store->AnnotateLiveNode(node.node_id, annotation.states, recorded,
                                    occlusion_determined);
    result.annotations.push_back(std::move(annotation));
  }

  result.truncation = context.ledger->report();
  if (deadline_reached) {
    result.warnings.emplace_back(WarningCode::kDeadlineReached,
                                 "layout-pass-stopped-at-deadline");
  }
  if (probe_budget_reached) {
    result.warnings.emplace_back(WarningCode::kAdapterFailed,
                                 "occlusion-probe-budget-reached");
  }
  if (cache_not_clean) {
    result.warnings.emplace_back(WarningCode::kAdapterFailed,
                                 "occlusion-cache-not-clean");
  }
  if (!can_probe_occlusion) {
    result.warnings.emplace_back(WarningCode::kAdapterUnavailable,
                                 "occlusion-not-probed");
  }
  if (not_determined > 0 || deadline_reached) {
    // Named, always. A caller that reads "some nodes have no visibility
    // determination" will refuse an action on one of them; a caller that
    // reads nothing would assume they were fine.
    result.warnings.emplace_back(WarningCode::kAdapterUnavailable,
                                 "visibility-not-determined-for-some-nodes");
  }
  if (determined == 0) {
    result.status = AdapterStatus::kUnsupported;
    return result;
  }
  result.status =
      (not_determined > 0 || probe_budget_reached || cache_not_clean ||
       deadline_reached || !can_probe_occlusion || result.truncation.truncated)
          ? AdapterStatus::kIncomplete
          : AdapterStatus::kOk;
  return result;
}

}  // namespace taffy
