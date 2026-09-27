// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_ACCOUNT_TRANSACTIONS_H_
#define TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_ACCOUNT_TRANSACTIONS_H_

#include <optional>
#include <string>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace sql {
class Database;
}

namespace taffy::storage_internal {

bool IsAccountSessionMutation(
    const core_service::mojom::EffectEnvelope &effect);
bool IsAccountSessionMutation(const core_service::mojom::EffectResult &result);
std::optional<bool>
HasAccountSessionPendingMarker(sql::Database *database,
                               const std::string &effect_id);
std::optional<bool> HasAnyAccountSessionPendingMarker(sql::Database *database);
bool PersistAccountResult(sql::Database *database,
                          const core_service::mojom::EffectResult &result,
                          bool has_pending_session_mutation);

} // namespace taffy::storage_internal

#endif // TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_ACCOUNT_TRANSACTIONS_H_
