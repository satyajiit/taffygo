// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_API_CORE_API_START_TASK_SHAPE_H_
#define TAFFY_BROWSER_CORE_API_CORE_API_START_TASK_SHAPE_H_

#include <optional>
#include <string>
#include <vector>

#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {
namespace start_task_shape {

// Whether a caller-supplied value could be an identifier at all.
bool IsIdentifier(const std::string& value);
bool IsOptionalIdentifier(const std::optional<std::string>& value);

// Whether the allowlist this task was asked for is a list somebody could have
// composed on purpose. It names no tools: the vocabulary lives in the
// sandboxed core, and the compiled-in action-class join refuses the effect a
// name would produce. Empty is the clause that matters — reducer guard
// evaluation reads an empty allowlist as everything the milestone has
// reached, so letting `{}` through would compose a wider task than any
// explicit list could ask for.
bool IsWellFormedToolAllowlist(const std::vector<std::string>& tools);

// The Core API vocabulary projected onto the Core Service one. Each answers
// nothing for a value this build will not start a task for.
std::optional<core_service::mojom::TaskTemplateId> ProjectTemplate(
    core_api::mojom::TaskTemplateId template_id);
std::optional<core_service::mojom::TaskProviderRoute> ProjectProviderRoute(
    core_api::mojom::TaskProviderRoute route);
std::optional<std::string> ProviderRouteId(
    core_service::mojom::TaskProviderRoute route);

// Whether the resolved sources are a list the browser itself could have
// produced: sorted, deduplicated by identifier, tab and document, and each one
// an exact non-opaque HTTP(S) origin that serializes back to what it claims.
bool AreWellFormedResolvedSources(
    const std::vector<core_service::mojom::TaskConsentSourcePtr>& sources);

// Whether the hosts the caller asked for are exactly the hosts that resolved.
bool IsCanonicalSourceHost(const std::string& host);
bool HasExactResolvedHosts(
    const core_api::mojom::TaskConsentPreview& intent,
    const core_service::mojom::TaskConsentPreview& resolved);

}  // namespace start_task_shape
}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_API_CORE_API_START_TASK_SHAPE_H_
