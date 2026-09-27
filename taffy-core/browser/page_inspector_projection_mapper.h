// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PAGE_INSPECTOR_PROJECTION_MAPPER_H_
#define TAFFY_BROWSER_PAGE_INSPECTOR_PROJECTION_MAPPER_H_

#include "taffy/contracts/core-api/generated/mojom/core_api.mojom-forward.h"

namespace taffy {

struct ObservationEnvelope;

// Converts one browser-validated observation into the generated UI-safe Core
// API shape. Returns null on any bound, lifecycle, or projection violation.
core_api::mojom::PageInspectorSnapshotViewPtr ProjectPageInspectorObservation(
    const ObservationEnvelope& observation);

}  // namespace taffy

#endif  // TAFFY_BROWSER_PAGE_INSPECTOR_PROJECTION_MAPPER_H_
