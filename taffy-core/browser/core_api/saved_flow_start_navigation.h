// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef TAFFY_BROWSER_CORE_API_SAVED_FLOW_START_NAVIGATION_H_
#define TAFFY_BROWSER_CORE_API_SAVED_FLOW_START_NAVIGATION_H_
#include <optional>

#include "base/functional/callback.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom-forward.h"
#include "url/gurl.h"
namespace content {
class WebContents;
}
namespace taffy {
std::optional<GURL> ReviewedSavedFlowStartAddress(
    const core_service::mojom::SavedFlowReview& flow);
// Manual user-requested navigation only; reports the actual exact commit.
void NavigateSavedFlowStart(content::WebContents* contents,
                            const GURL& address,
                            base::OnceCallback<void(bool)> callback);
}  // namespace taffy
#endif
