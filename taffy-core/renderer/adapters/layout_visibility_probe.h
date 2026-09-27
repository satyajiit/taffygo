// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_ADAPTERS_LAYOUT_VISIBILITY_PROBE_H_
#define TAFFY_RENDERER_ADAPTERS_LAYOUT_VISIBILITY_PROBE_H_

#include <stdint.h>

#include <optional>
#include <vector>

#include "taffy/renderer/node_precondition_checker.h"
#include "taffy/renderer/observation_limits.h"
#include "taffy/renderer/semantic_graph.h"
#include "taffy/renderer/semantic_graph_store.h"
#include "third_party/blink/public/web/web_ax_context.h"
#include "third_party/blink/public/web/web_ax_object.h"
#include "third_party/blink/public/web/web_document.h"
#include "ui/gfx/geometry/rect.h"

namespace blink {
class WebLocalFrame;
}  // namespace blink

namespace taffy {

// What one node's visibility check concluded. The third member is the whole
// reason this module is written the way it is: see LayoutVisibilityAdapter.
enum class LayoutVisibility {
  kVisible,
  kNotVisible,
  kOffscreen,
  kObscured,
  kNotDetermined,
};

// The states a determination asserts. Off-screen is a kind of not-visible,
// and an action policy that asks for "visible" must not be satisfied by an
// element that is merely scrolled out of view, so both members are asserted
// and neither reading has to be inferred from the other. Not determined
// asserts nothing.
std::vector<NodeState> StatesFor(LayoutVisibility visibility);

// The viewport every off-screen decision is made against, in the same space
// as the bounds below. Nothing when the frame cannot reach its widget.
std::optional<gfx::Rect> LayoutViewportRect(blink::WebLocalFrame* frame);

// The one definition of whether a node is in view: the layout adapter asks
// it about every node of a reading, and a live reader asks it about the one
// node it is about to act on or verify.
//
// It holds the accessibility context a hit test needs for as long as it
// lives, so one probe serves a whole pass.
class LayoutVisibilityProbe {
 public:
  struct Measurement {
    LayoutVisibility visibility = LayoutVisibility::kNotDetermined;
    // The node's bounds, when it has an element to measure.
    std::optional<gfx::Rect> bounds;
    // Whether an occlusion test actually decided the answer.
    bool occlusion_determined = false;
    // A hit test was spent on this node.
    bool probed = false;
    // A hit test was needed and `may_probe` said there was no budget for it.
    bool probe_budget_reached = false;
    // A hit test was needed and the accessibility cache would not come clean.
    bool cache_not_clean = false;
  };

  explicit LayoutVisibilityProbe(const blink::WebDocument& document);
  LayoutVisibilityProbe(const LayoutVisibilityProbe&) = delete;
  LayoutVisibilityProbe& operator=(const LayoutVisibilityProbe&) = delete;
  ~LayoutVisibilityProbe();

  // Whether an occlusion hit test can run in this document at all.
  bool can_probe_occlusion() const { return can_probe_occlusion_; }

  Measurement Measure(const SemanticGraphStore::DomNodeKey& key,
                      const gfx::Rect& viewport_rect,
                      const LayoutLimits& limits,
                      uint32_t max_depth,
                      bool may_probe);

 private:
  const blink::WebDocument document_;
  blink::WebAXContext ax_context_;
  blink::WebAXObject root_;
  bool can_probe_occlusion_ = false;
};

// The visibility states `key` has in `frame` now, measured with the process's
// own limits, or nothing when it cannot be told now.
//
// A reading records where a node was. No reading follows a scroll, so a node
// read below the fold keeps saying so after it has been brought into view,
// and an action that requires it to be visible was refused on a phone for a
// field in plain sight (decision 0250). A caller that gets nothing keeps
// what the reading recorded.
std::optional<MeasuredVisibility> MeasureLiveVisibility(
    blink::WebLocalFrame* frame,
    const SemanticGraphStore::DomNodeKey& key);

}  // namespace taffy

#endif  // TAFFY_RENDERER_ADAPTERS_LAYOUT_VISIBILITY_PROBE_H_
