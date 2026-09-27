// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_TASK_ACTION_RECONCILIATION_H_
#define TAFFY_BROWSER_CORE_TASK_ACTION_RECONCILIATION_H_

#include "base/functional/callback_forward.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom-forward.h"

namespace taffy {

class CoreStorageBroker;

using TaskActionReconciliationCallback = base::OnceCallback<void(
    core_service::mojom::TaskEffectCompletionPtr)>;

// Reads one exact durable dispatch witness. Missing/open/read-failed rows stay
// outcome-unknown; only a terminal result is returned, and a successful result
// that lost required observation/source/tab/download payload is not
// overstated. Only a payload-free page mutation may recover VERIFIED.
void ReconcileTaskAction(CoreStorageBroker* storage,
                         core_service::mojom::TaskEffectBindingPtr effect,
                         TaskActionReconciliationCallback callback);

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_TASK_ACTION_RECONCILIATION_H_
