// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_ACCOUNT_EFFECT_TERMINAL_H_
#define TAFFY_BROWSER_CORE_ACCOUNT_EFFECT_TERMINAL_H_

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom-forward.h"

namespace taffy {

struct CanonicalSessionClearDecision {
  core_service::mojom::EffectStatus terminal_status;
  bool mark_completed_revoke_deleted = false;
};

// A sign-out may report a deterministic terminal only after the canonical
// platform vault confirms deletion. Otherwise the durable SQL half must retain
// its reconciliation barrier.
CanonicalSessionClearDecision ResolveCanonicalSessionClear(
    core_service::mojom::EffectStatus requested_terminal,
    core_service::mojom::EffectStatus storage_status, bool cleared);

// True only for an ambiguous provider operation that can create, rotate, or
// revoke the canonical profile session. This terminal retires its utility
// generation so bootstrap reconciliation runs before more account work.
bool RequiresAccountReconciliation(
    const core_service::mojom::EffectResult &result);

// Installs the exact tagged result body for an account effect terminal. The
// outer operation, effect id, status, and kind remain owned by the broker.
bool PopulateCoreAccountTerminal(
    const core_service::mojom::EffectEnvelope &effect,
    core_service::mojom::EffectResult *result);

} // namespace taffy

#endif // TAFFY_BROWSER_CORE_ACCOUNT_EFFECT_TERMINAL_H_
