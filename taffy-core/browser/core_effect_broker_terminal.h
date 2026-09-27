// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_EFFECT_BROKER_TERMINAL_H_
#define TAFFY_BROWSER_CORE_EFFECT_BROKER_TERMINAL_H_

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

// The typed bodies a refused, unavailable or never-dispatched effect still has
// to carry back.
//
// Every one of these fills a body the core requires to be present, so that the
// terminal names an outcome rather than arriving as a missing-body protocol
// error. They live beside the broker rather than inside it because the broker's
// own file is at the size where one more body makes it unreadable — the same
// split core_account_effect_terminal.h already makes for the account plane.

// A delivery effect the browser did not carry out. Reported as interrupted
// rather than refused by the origin: nothing was asked of an origin, and the
// delivery plane's retry rule for an interruption — resume from what is on
// disk, after a backoff — is the correct one for a browser that was busy,
// disconnected or shutting down.
void PopulateAssetDeliveryTerminal(
    const core_service::mojom::EffectEnvelope &effect,
    core_service::mojom::EffectResult *result);

// The three provider-plane effects the broker never routes. All are answered in
// CoreServiceManager::EmitEffect on the live path; this is what they carry if
// one ever reaches the broker instead, and it is the honest answer in every
// case — a listing nobody fetched, a completion nobody was shown, and an
// address nobody asked.
void PopulateProviderPlaneTerminal(
    const core_service::mojom::EffectEnvelope &effect,
    core_service::mojom::EffectResult *result);

// The two status vocabularies of a tool job, mapped both ways. A tool
// terminal answers in the tool runtime's words and the effect journal records
// the broker's; every member of each has one image in the other, and a
// value this build does not know maps to the invalid-result status rather
// than to success.
core_service::mojom::ToolTerminalStatus ToolTerminalStatusFor(
    core_service::mojom::EffectStatus status);
core_service::mojom::EffectStatus EffectStatusFor(
    core_service::mojom::ToolTerminalStatus status);

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_EFFECT_BROKER_TERMINAL_H_
