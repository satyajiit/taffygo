// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PAGE_SCREENSHOT_FALLBACK_H_
#define TAFFY_BROWSER_PAGE_SCREENSHOT_FALLBACK_H_

#include "taffy/common/public/bip_observation.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

// Browser-owned gate for the generic visual fallback. A task asking for the
// tool is necessary but not sufficient: structured evidence must be unusable,
// the observation must be complete enough to prove the safety boundary, and
// the capture may cover only one ordinary main frame.
bool PageScreenshotFallbackIsEligible(const ObservationEnvelope& observation);

bool PageScreenshotResultMatchesObservation(
    const ObservationEnvelope& observation,
    const core_service::mojom::MediaObservationResult& media);

}  // namespace taffy

#endif  // TAFFY_BROWSER_PAGE_SCREENSHOT_FALLBACK_H_
