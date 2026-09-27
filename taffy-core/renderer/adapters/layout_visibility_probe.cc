// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/adapters/layout_visibility_probe.h"

#include <stdint.h>

#include <optional>
#include <vector>

#include "base/numerics/safe_conversions.h"
#include "third_party/blink/public/web/web_element.h"
#include "third_party/blink/public/web/web_frame_widget.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_node.h"
#include "ui/accessibility/ax_mode.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/size.h"

// VERIFY AT SP-04 - every geometry accessor here has moved between milestones
// at least once, and each one is a compile error rather than a silent
// behaviour change, which is the good kind of uncertainty:
//
//   * blink::WebElement::BoundsInWidget(). The name has been
//     BoundsInViewport; whichever it is at the pin, only BoundsFor()
//     below changes.
//
//   * blink::WebAXObject::GetBoundsInFrameCoordinates(). Used for nodes the
//     accessibility adapter produced, which have no WebElement of their own
//     in this process.
//
//   * blink::WebLocalFrame::FrameWidget() and blink::WebFrameWidget::Size().
//     This is the viewport rectangle every off-screen decision is made
//     against. If a local frame cannot reach its widget at the pin, the
//     viewport has to come from the browser in the request instead - and
//     until it does, ViewportSize() returns nothing and every node is
//     reported as not determined, which fails closed.
//
//   * blink::WebAXObject::HitTest(const gfx::Point&) - THE most important
//     item on this file's list. Confirm that it hit-tests the rendered
//     result, honours pointer-events, and returns the topmost object at the
//     point. If it does not, the occlusion determination is not a
//     determination and OcclusionOf() must return kNotDetermined
//     unconditionally until `[Open (OD-054)]` supplies a test that is.
//
//   * The cost of one hit test on the supported-device floor. The probe
//     budget in observation_limits.json is a placeholder; if a probe is
//     expensive, occlusion becomes opt-in per request rather than a default
//     pass over every node.

namespace taffy {

namespace {

// Bounds for whichever identity space the node was allocated in. A node in
// the derived space - a structured-data statement, the document node, the
// selection region - has no element and therefore no bounds, and saying so is
// the correct answer rather than a missing one.
std::optional<gfx::Rect> BoundsFor(const blink::WebDocument& document,
                                   const SemanticGraphStore::DomNodeKey& key) {
  switch (key.space) {
    case SemanticGraphStore::IdentitySpace::kDom: {
      const blink::WebNode node =
          blink::WebNode::FromDomNodeId(static_cast<int>(key.dom_node_id));
      if (node.IsNull() || node.GetDocument() != document ||
          !node.IsElementNode()) {
        return std::nullopt;
      }
      return node.To<blink::WebElement>().BoundsInWidget();
    }
    case SemanticGraphStore::IdentitySpace::kAccessibility: {
      const blink::WebAXObject object = blink::WebAXObject::FromWebDocumentByID(
          document, static_cast<int>(key.dom_node_id));
      if (object.IsDetached()) {
        return std::nullopt;
      }
      return object.GetBoundsInFrameCoordinates();
    }
    case SemanticGraphStore::IdentitySpace::kDerived:
      return std::nullopt;
  }
}

// Re-establishes the precondition an accessibility hit test carries, and says
// whether it holds. `root` is read back out, because a commit may replace the
// object this pass was holding.
//
// blink::WebAXObject::HitTest takes a ScopedFreezeAXCache, and
// AXObjectCacheImpl::Freeze() asserts the cache is not dirty. Reading a node's
// geometry is precisely what breaks that assertion:
// blink::WebElement::BoundsInWidget() forces style and layout for the node,
// and forcing it inside a locked subtree - a closed <details>, anything under
// content-visibility - attaches layout objects that the accessibility cache
// then records as tree updates it has not processed yet. One
// UpdateAXForAllDocuments() before the loop therefore buys a clean cache only
// until the first BoundsFor() call, which happens one line before the first
// probe.
//
// MEASURED on 2026-08-21: without this, observing the hidden-and-offscreen
// fixture killed the renderer at the hit test below with
//
//   [FATAL:.../accessibility/ax_object_cache_impl.h:195] DCHECK failed:
//   !IsDirty().
//
// under blink::AXObjectCacheImpl::Freeze() <- blink::WebAXObject::HitTest.
// dcheck_always_on is set in the committed dev profiles, so that is a browser
// crash reachable from ordinary page markup rather than a debug-only
// annoyance - and the observation the browser then settles carries a failure
// code, because the process that was answering it is gone.
//
// Refreshing before the probe is what RenderAccessibilityImpl::HitTest does
// before every hit test it serves. The second check is this component's own:
// if the cache still will not come clean, the probe does not happen at all.
// Failing closed costs one node its occlusion determination, which the caller
// is told about; not failing closed costs the renderer.
bool ReadyRootForProbe(blink::WebAXContext& ax_context,
                       const blink::WebDocument& document,
                       blink::WebAXObject& root) {
  if (!ax_context.HasActiveDocument()) {
    return false;
  }
  if (blink::WebAXObject::IsDirty(document)) {
    ax_context.UpdateAXForAllDocuments();
    root = blink::WebAXObject::FromWebDocument(document);
  }
  return !root.IsDetached() && !blink::WebAXObject::IsDirty(document);
}

// True when `candidate` is `target` or a descendant of it, walking up from the
// candidate. Bounded by the depth budget: a hit test that returned something
// deep inside a large subtree must not make this walk unbounded.
bool IsSelfOrDescendant(const blink::WebAXObject& candidate,
                        const blink::WebAXObject& target,
                        uint32_t max_depth) {
  blink::WebAXObject current = candidate;
  for (uint32_t step = 0; step <= max_depth; ++step) {
    if (current.IsDetached()) {
      return false;
    }
    if (current.Equals(target)) {
      return true;
    }
    current = current.ParentObject();
  }
  return false;
}

// The accessibility object a node's identity names, or a detached object
// when it has none in the tree. A DOM-space node is named by its element,
// whose object Blink looks up directly; an object left out of the tree is
// never a hit test's answer, so it could only ever compare as obscured.
blink::WebAXObject AccessibilityObjectFor(
    const blink::WebDocument& document,
    const SemanticGraphStore::DomNodeKey& key) {
  switch (key.space) {
    case SemanticGraphStore::IdentitySpace::kAccessibility:
      return blink::WebAXObject::FromWebDocumentByID(
          document, static_cast<int>(key.dom_node_id));
    case SemanticGraphStore::IdentitySpace::kDom: {
      const blink::WebNode node =
          blink::WebNode::FromDomNodeId(static_cast<int>(key.dom_node_id));
      if (node.IsNull() || node.GetDocument() != document) {
        return blink::WebAXObject();
      }
      const blink::WebAXObject object = blink::WebAXObject::FromWebNode(node);
      return !object.IsDetached() && object.IsIncludedInTree()
                 ? object
                 : blink::WebAXObject();
    }
    case SemanticGraphStore::IdentitySpace::kDerived:
      return blink::WebAXObject();
  }
}

}  // namespace

std::vector<NodeState> StatesFor(LayoutVisibility visibility) {
  switch (visibility) {
    case LayoutVisibility::kVisible:
      return {NodeState::kVisible};
    case LayoutVisibility::kNotVisible:
      return {NodeState::kNotVisible};
    case LayoutVisibility::kOffscreen:
      return {NodeState::kOffscreen, NodeState::kNotVisible};
    case LayoutVisibility::kObscured:
      return {NodeState::kObscured};
    case LayoutVisibility::kNotDetermined:
      return {};
  }
}

std::optional<gfx::Rect> LayoutViewportRect(blink::WebLocalFrame* frame) {
  if (!frame) {
    return std::nullopt;
  }
  blink::WebFrameWidget* widget = frame->FrameWidget();
  if (!widget) {
    return std::nullopt;
  }
  const gfx::Size size = widget->Size();
  if (size.IsEmpty()) {
    return std::nullopt;
  }
  return gfx::Rect(size);
}

// The context has to outlive every WebAXObject taken from it. Without one
// alive the document has no accessibility cache at all, and
// WebAXObject::FromWebDocument DCHECKs on that rather than returning a null
// object:
//
//   [FATAL:.../web_ax_object.cc:1139] DCHECK failed: cache.
//
// MEASURED on 2026-08-19, on the first run renderer/test/ ever had. It did
// not fire in production because something else in the frame happened to be
// holding a context open; that is a coincidence of timing, not a guarantee,
// and an occlusion probe that crashes the renderer when it is not holding is
// a browser crash. UpdateAXForAllDocuments() is the second half: building
// the cache's relation table requires the document to be layout-clean.
LayoutVisibilityProbe::LayoutVisibilityProbe(const blink::WebDocument& document)
    : document_(document), ax_context_(document, ui::kAXModeBasic) {
  if (ax_context_.HasActiveDocument()) {
    ax_context_.UpdateAXForAllDocuments();
    root_ = blink::WebAXObject::FromWebDocument(document_);
    can_probe_occlusion_ = !root_.IsDetached();
  }
}

LayoutVisibilityProbe::~LayoutVisibilityProbe() = default;

LayoutVisibilityProbe::Measurement LayoutVisibilityProbe::Measure(
    const SemanticGraphStore::DomNodeKey& key,
    const gfx::Rect& viewport_rect,
    const LayoutLimits& limits,
    uint32_t max_depth,
    bool may_probe) {
  Measurement measurement;
  measurement.bounds = BoundsFor(document_, key);
  if (!measurement.bounds.has_value()) {
    // No element to measure. The document node, a structured-data
    // statement, and the selection region all land here, correctly.
    return measurement;
  }
  const gfx::Rect& bounds = measurement.bounds.value();

  const int64_t area = static_cast<int64_t>(bounds.width()) *
                       static_cast<int64_t>(bounds.height());
  if (area < static_cast<int64_t>(limits.min_visible_area_px())) {
    // Zero-size and near-zero-size elements are a documented decoy shape in
    // the corpus. An element with no area is not something a user can
    // perceive, whatever the accessibility tree says about it.
    measurement.visibility = LayoutVisibility::kNotVisible;
    return measurement;
  }
  gfx::Rect padded = viewport_rect;
  padded.Outset(base::checked_cast<int>(limits.viewport_margin_px()));
  if (!padded.Intersects(bounds)) {
    measurement.visibility = LayoutVisibility::kOffscreen;
    return measurement;
  }
  if (!can_probe_occlusion_) {
    return measurement;
  }
  if (!may_probe) {
    // The budget ran out. NOT "assume visible": an action precondition that
    // forbids occlusion is refused for this node instead, which is the whole
    // point of distinguishing "not determined" from "fine".
    measurement.probe_budget_reached = true;
    return measurement;
  }
  if (!ReadyRootForProbe(ax_context_, document_, root_)) {
    // The accessibility cache could not be brought to a state a hit test is
    // allowed to run against. The same answer as a spent budget: no
    // determination for this node, named in a warning, rather than a guess
    // or a crash.
    measurement.cache_not_clean = true;
    return measurement;
  }

  measurement.probed = true;
  const gfx::Rect clipped = gfx::IntersectRects(padded, bounds);
  const gfx::Point centre = clipped.CenterPoint();
  const blink::WebAXObject hit = root_.HitTest(centre);

  // The comparison is an identity one: did the hit test reach this object or
  // something inside it. That is the answer worth having, and it is had for
  // a DOM-space node too, through the accessibility object Blink keeps for
  // its element. The geometric comparison below used to be the whole answer
  // for those, and it compared a document-space rectangle with a viewport
  // one: they agree only while the page is not scrolled, so a field below the
  // first screen read as obscured after it was scrolled to (decision 0250).
  const blink::WebAXObject self = AccessibilityObjectFor(document_, key);

  if (hit.IsDetached()) {
    return measurement;
  }
  if (!self.IsDetached()) {
    const bool reached = IsSelfOrDescendant(hit, self, max_depth) ||
                         IsSelfOrDescendant(self, hit, max_depth);
    measurement.visibility =
        reached ? LayoutVisibility::kVisible : LayoutVisibility::kObscured;
    measurement.occlusion_determined = true;
    return measurement;
  }
  // A DOM-space node whose element has no accessibility object in the tree.
  // The hit test answers in accessibility objects, so the comparison is made
  // through the hit object's own bounds: an object whose bounds contain the
  // probe point and which is not this node's own box is something drawn on
  // top of it. The hit object's bounds are in the document's space and this
  // node's are in the viewport's, so this approximation holds only on an
  // unscrolled page, which is why it is now the fallback and not the rule.
  const gfx::Rect hit_bounds = hit.GetBoundsInFrameCoordinates();
  const bool same_box = hit_bounds == bounds;
  const bool hit_contains_node = hit_bounds.Contains(bounds);
  measurement.visibility = (same_box || hit_contains_node)
                               ? LayoutVisibility::kVisible
                               : LayoutVisibility::kObscured;
  measurement.occlusion_determined = true;
  return measurement;
}

std::optional<MeasuredVisibility> MeasureLiveVisibility(
    blink::WebLocalFrame* frame,
    const SemanticGraphStore::DomNodeKey& key) {
  const std::optional<gfx::Rect> viewport = LayoutViewportRect(frame);
  if (!viewport.has_value()) {
    return std::nullopt;
  }
  const blink::WebDocument document = frame->GetDocument();
  if (document.IsNull()) {
    return std::nullopt;
  }
  const ObservationLimits& limits = ObservationLimits::ProcessSafeCeiling();
  LayoutVisibilityProbe probe(document);
  const LayoutVisibilityProbe::Measurement measurement =
      probe.Measure(key, viewport.value(), limits.layout(),
                    limits.snapshot().max_depth(), /*may_probe=*/true);
  if (measurement.visibility == LayoutVisibility::kNotDetermined) {
    return std::nullopt;
  }
  MeasuredVisibility measured;
  measured.states = StatesFor(measurement.visibility);
  measured.occlusion_determined = measurement.occlusion_determined;
  return measured;
}

}  // namespace taffy
