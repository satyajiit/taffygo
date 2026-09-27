// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef TAFFY_BROWSER_CORE_API_SAVED_FLOW_QUERY_PROJECTION_H_
#define TAFFY_BROWSER_CORE_API_SAVED_FLOW_QUERY_PROJECTION_H_
#include <string>

#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
namespace taffy {
core_api::mojom::SavedFlowQueryResultPtr ProjectSavedFlowQuery(
    const std::string& request_id,
    uint64_t generation,
    core_service::mojom::SavedFlowQueryResultPtr result);
}
#endif
