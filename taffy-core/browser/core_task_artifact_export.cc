// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_task_artifact_export.h"

#include <stdint.h>

#include <vector>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

bool CoreServiceObserver::OnTaskArtifactExport(
    const std::string& task_id,
    const std::string& artifact_id,
    core_service::mojom::TaskArtifactKind kind,
    const std::vector<uint8_t>& content) {
  return false;
}

bool DeliverTaskArtifactExport(
    base::ObserverList<CoreServiceObserver>& observers,
    const std::string& task_id,
    const core_service::mojom::TaskArtifactEffect& artifact) {
  bool delivered = false;
  for (CoreServiceObserver& observer : observers) {
    delivered |= observer.OnTaskArtifactExport(
        task_id, artifact.artifact_id, artifact.kind, artifact.content);
  }
  return delivered;
}

}  // namespace taffy
