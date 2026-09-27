// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_CORE_RUST_CORE_TASK_EFFECT_TERMINAL_INTERNAL_H_
#define TAFFY_SERVICES_CORE_RUST_CORE_TASK_EFFECT_TERMINAL_INTERNAL_H_

#include <stdint.h>

#include <string_view>

#include "taffy/contracts/core-service/core_service.mojom.h"
#include "taffy/services/core/service_bridge.rs.h"

namespace taffy::core_service_internal {

// A non-empty identifier within the wire's bound.
bool BoundedIdentifier(std::string_view value);

// Whether two envelopes name the same operation, in every field. Shared so
// that the completion half and the refusal half correlate a terminal by one
// rule rather than by two copies of it.
bool SameOperationEnvelope(const core_service::mojom::OperationEnvelope& left,
                           const core_service::mojom::OperationEnvelope& right);

// The terminal every effect kind starts from: the effect's operation and
// identities, the status, and no facts yet.
core_bridge::BridgeTaskTerminal TaskTerminalBase(
    const core_service::mojom::TaskEffectBinding& effect,
    uint8_t status);

// The denial code of a dispatched action that did not verify, and nothing
// else. See `CopyRefusedObservationTerminal` below for why either exists.
bool CopyRefusedActionTerminal(
    const core_service::mojom::TaskEffectBinding& effect,
    const core_service::mojom::TaskEffectCompletion& completion,
    core_bridge::BridgeTaskTerminal& out);

// The denial code of a read that did not complete, and nothing else.
//
// A refused effect used to reach the reducer as the one generic policy
// denial, which its recovery table reads as "this will be decided the same
// way again — do not retry". A page that moved under the read is the
// commonest thing a live site does and the answer to it is to read it again,
// so an errand gave up on the first client-side route and handed the page
// back at whatever it happened to be looking at. Refuses anything that is not
// a correlated, content-free page-observation refusal.
bool CopyRefusedObservationTerminal(
    const core_service::mojom::TaskEffectBinding& effect,
    const core_service::mojom::TaskEffectCompletion& completion,
    core_bridge::BridgeTaskTerminal& out);

bool CopyTaskActionCompletion(
    const core_service::mojom::TaskEffectBinding& effect,
    const core_service::mojom::TaskEffectCompletion& completion,
    core_bridge::BridgeTaskTerminal& out);

bool CopyTaskTabCompletion(
    const core_service::mojom::TaskEffectBinding& effect,
    const core_service::mojom::TaskEffectCompletion& completion,
    core_bridge::BridgeTaskTerminal& out);

bool CopyTaskDownloadCompletion(
    const core_service::mojom::TaskEffectBinding& effect,
    const core_service::mojom::TaskEffectCompletion& completion,
    core_bridge::BridgeTaskTerminal& out);

// The rows of one attached-store read (decision 0133). Each row must already
// be a reference — a title, a host, a path with no query or fragment, and a
// time — bounded and free of control characters; anything else is refused
// here before the sandbox reads it.
bool CopyTaskStoreCompletion(
    const core_service::mojom::TaskEffectBinding& effect,
    const core_service::mojom::TaskEffectCompletion& completion,
    core_bridge::BridgeTaskTerminal& out);

}  // namespace taffy::core_service_internal

#endif  // TAFFY_SERVICES_CORE_RUST_CORE_TASK_EFFECT_TERMINAL_INTERNAL_H_
