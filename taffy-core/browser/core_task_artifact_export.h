// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_TASK_ARTIFACT_EXPORT_H_
#define TAFFY_BROWSER_CORE_TASK_ARTIFACT_EXPORT_H_

#include <string>

#include "base/observer_list.h"
#include "taffy/browser/core_service_manager_types.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom-forward.h"

namespace taffy {

// Offers validated transient artifact bytes to every profile surface and
// reports whether at least one surface accepted browser custody. The caller
// uses that result as the core's exact succeeded/refused terminal.
bool DeliverTaskArtifactExport(
    base::ObserverList<CoreServiceObserver>& observers,
    const std::string& task_id,
    const core_service::mojom::TaskArtifactEffect& artifact);

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_TASK_ARTIFACT_EXPORT_H_
