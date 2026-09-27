// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_API_TASK_WORKFLOW_TOOLS_H_
#define TAFFY_BROWSER_CORE_API_TASK_WORKFLOW_TOOLS_H_

#include <optional>
#include <string>
#include <vector>

#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"

namespace taffy {

// Returns the exact reviewed allowlist for one start shape. An empty result is
// a refusal: the command factory never interprets empty as a useful default.
// An attached store adds its one reviewed group on a model route and nothing
// on any other (decision 0133).
std::vector<std::string> WorkflowToolsForStart(
    core_api::mojom::TaskTemplateId template_id,
    core_api::mojom::TaskProviderRoute route,
    const std::vector<core_api::mojom::TaskAttachedStore>& attached_stores,
    const std::optional<std::string>& skill_version_id = std::nullopt);

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_API_TASK_WORKFLOW_TOOLS_H_
