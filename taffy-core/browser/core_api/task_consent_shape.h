// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_API_TASK_CONSENT_SHAPE_H_
#define TAFFY_BROWSER_CORE_API_TASK_CONSENT_SHAPE_H_

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <string>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

// Product bounds, intentionally narrower than the generic Core Service
// contract. These are consent limits, not authority: every row still names an
// exact browser-issued source.
inline constexpr size_t kMaxSelectedTaskSources = 8u;
inline constexpr uint32_t kMaxErrandNewSourceCap = 8u;

// Validates the exact consent shape of a newly submitted task, including the
// template-specific cardinality and the provider-route identifier carried
// beside the preview.
bool IsAdmittedInitialTaskConsentShape(
    core_service::mojom::TaskTemplateId template_id,
    const core_service::mojom::TaskConsentPreview& preview,
    const std::optional<std::string>& provider_route_id,
    const std::optional<std::string>& skill_version_id = std::nullopt);

// Validates the union of consent shapes that can be restored from a durable
// browser binding. The binding contract does not carry the task template, so
// this deliberately cannot infer comparison-vs-summary cardinality. It still
// rejects every shape that no admitted template can have.
bool IsAdmittedDurableTaskConsentShape(
    const core_service::mojom::TaskConsentPreview& preview);

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_API_TASK_CONSENT_SHAPE_H_
