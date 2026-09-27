// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_ADAPTERS_LAYOUT_VISIBILITY_ADAPTER_H_
#define TAFFY_RENDERER_ADAPTERS_LAYOUT_VISIBILITY_ADAPTER_H_

#include <string_view>

#include "taffy/renderer/adapters/adapter.h"

namespace taffy {

// CAP-PI-006. Visibility, occlusion, and bounds for nodes other adapters
// already described.
//
// Two rules govern everything in this module.
//
// FIRST: bounds are diagnostic, never identity. Protocol sections 3.2 and 7.3
// both say so, and protocol section 12 forbids resolving a node by "nearest
// coordinates". Nothing here allocates an identity, nothing here is an action
// target, and the bounds this adapter records exist so that a target which
// MOVED between observation and dispatch is a visible precondition change -
// not so that anything can be found by where it is.
//
// SECOND: "we did not look" is not "not obscured". A visibility check that
// silently returns "fine" when it could not run is worse than no check,
// because an action policy would then treat its silence as a pass. So this
// adapter reports three outcomes per node and the third is explicit:
//
//   * determined visible - the node has area, it intersects the viewport, the
//     accessibility tree does not consider it ignored or invisible, and an
//     accessibility hit test at its centre reached the node itself or one of
//     its descendants;
//   * determined not visible, off-screen, or obscured - one of those checks
//     positively failed, and the corresponding state is asserted;
//   * not determined - the probe budget ran out, the node has no element to
//     measure, or the hit test could not run. NEITHER state is asserted and
//     the annotation does not claim an occlusion determination, so an action
//     precondition that forbids occlusion is refused rather than passed.
//
// Whether an occlusion test of this shape is reliable enough to gate a
// consequential action on Android is `[Open (OD-054)]`. This adapter is built
// so that the answer changes one function, and so that until it is settled
// the honest outcome - refusal - is the one that happens by default.
class LayoutVisibilityAdapter final : public Adapter {
 public:
  LayoutVisibilityAdapter();
  ~LayoutVisibilityAdapter() override;

  AdapterKind kind() const override;
  std::string_view name() const override;
  uint32_t extraction_rule_version() const override;
  bool annotates_existing_nodes() const override;
  AdapterResult Run(ExtractionContext& context) override;
};

}  // namespace taffy

#endif  // TAFFY_RENDERER_ADAPTERS_LAYOUT_VISIBILITY_ADAPTER_H_
