// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_ASSISTANT_CONFIGURATION_H_
#define TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_ASSISTANT_CONFIGURATION_H_

#include "sql/database.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom-forward.h"

namespace taffy {

// Reads only the non-secret assistant record, never a whole bootstrap.
bool LoadAssistantConfigurationRecord(
    sql::Database* database,
    core_service::mojom::AssistantConfigurationPtr* configuration);

// Restores the optional whole-profile assistant configuration. Absence is the
// compiled default and is therefore a valid first-run state.
bool LoadAssistantConfiguration(sql::Database* database,
                                core_service::mojom::CoreBootstrap* bootstrap);

// Commits one whole-record compare-and-set. An exact effect replay is
// idempotent; stale revisions and reused effect identities with different
// contents are refused.
bool CommitAssistantConfiguration(
    sql::Database* database,
    const core_service::mojom::EffectEnvelope& effect);

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_ASSISTANT_CONFIGURATION_H_
