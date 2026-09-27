// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_CORE_RUST_CORE_COMPOSER_H_
#define TAFFY_SERVICES_CORE_RUST_CORE_COMPOSER_H_

#include <stdint.h>

#include <optional>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core.h"
#include "taffy/services/core/service_bridge.rs.h"
#include "taffy/services/core/service_bridge_composer_ffi.rs.h"

namespace taffy::core_service_internal {

// One composer request carried across the seam: the command projected, the
// ordered core asked, and its answer turned into the batch the service
// publishes (decision 0097).
//
// The runtime is handed in rather than reached for, so that `rust_core.cc`
// goes on owning one job — which bridge entry point a message belongs to —
// and everything the composer plane knows stays in this file.
CoreResponseBatch SubmitComposerCommand(
    core_bridge::ServiceBridge &runtime,
    const core_service::mojom::CoreServiceCommand &command,
    uint64_t now_monotonic_ms, uint64_t now_utc_millis);

// One withdrawal carried across the seam (decision 0097 section 3): the
// command projected, the ordered core asked, and its answer turned into the
// batch the service publishes.
//
// It answers with the same batch a submission does, and that is the decision
// rather than reuse. The browser already stops a dispatch a newer request
// displaced; a withdrawal is that instruction with nothing to dispatch beside
// it, so the batch carries no effect and a `superseded_effect_id`. A second
// answer shape would be a second way to stop an effect.
CoreResponseBatch CancelComposerCommand(
    core_bridge::ServiceBridge &runtime,
    const core_service::mojom::CoreServiceCommand &command,
    uint64_t now_monotonic_ms, uint64_t now_utc_millis);

// One request the surface has stopped waiting for, flattened for the bridge.
// Absent when the command is not a withdrawal or carries no body: a body that
// does not match its kind is refused rather than repaired.
std::optional<core_bridge::BridgeComposerCancel>
ToBridgeComposerCancel(const core_service::mojom::CoreServiceCommand &command);

// One model terminal offered to the composer. Absent when the composer was
// not waiting for it — the answer arrived for a request the person has typed
// past, it carried nothing usable, or it was another plane's call — which is
// the caller's signal to hand it on to the next delivery leg.
std::optional<CoreResponseBatch> DeliverComposerTerminal(
    core_bridge::ServiceBridge &runtime,
    const core_service::mojom::EffectResult &result, uint64_t now_utc_millis);

// One request for a continuation of what a person is typing, flattened for
// the bridge (decision 0097). Absent when the command is not a composer
// request or carries no body: a body that does not match its kind is refused
// rather than repaired.
//
// The suffix crosses as a flag and a string because cxx has no optional, and
// the two are not the same answer — a caret at the end of the text is not a
// caret before an empty string, and only the second gives the model anything
// to write towards.
std::optional<core_bridge::BridgeComposerCommand>
ToBridgeComposerCommand(const core_service::mojom::CoreServiceCommand &command);

// One composer suggestion's model effect, rebuilt from the bridge's flat
// record into the typed envelope the effect broker dispatches. Null is a
// refusal, not a repair, on the same terms as the key probe's projection and
// on two further ones that belong to this call alone:
//
//   A suggestion is never spent on the managed route (decision 0097 section
//   2), so the managed wire is refused and so is a request carrying no
//   credential handle — the managed route is entered with a token the browser
//   mints per call and holds no stored credential, so "there is a handle" is
//   what makes a route the person's own.
//
//   A suggestion discloses the person's own composer text and nothing else
//   (decision 0097 section 4), so any disclosure class other than
//   USER_SELECTED_CONTENT is a request nobody composed.
//
// The rebuilt request is task-less and not a probe by construction: no task
// owns a suggestion, which is what makes it unjournalled rather than merely
// unjournalled-so-far.
core_service::mojom::EffectEnvelopePtr
ToMojoComposerEffect(const core_bridge::BridgeComposerEffect &effect);

// One suggestion dispatch's terminal, flattened for the bridge. Absent when
// the result is not a model result. Unlike the probe's terminal this carries
// the reply body, because a probe needs a verdict and this needs the words.
//
// A model terminal is claimed by whichever plane composed it, and only the
// ordered core knows which that was: the caller offers every model result
// here first and hands on the ones the composer does not claim.
std::optional<core_bridge::BridgeComposerCompletion>
ToBridgeComposerCompletion(const core_service::mojom::EffectResult &result);

// One suggestion on its way to a surface, rebuilt into the typed envelope the
// browser carries out. Absent text projects as an absent optional rather than
// an empty string: a surface that was handed "" would draw nothing and go on
// waiting, which is the one thing decision 0097 asks this push to prevent.
core_service::mojom::EffectEnvelopePtr
ToMojoComposerDelivery(const core_bridge::BridgeComposerDelivery &delivery);

// The bridge's answer to one submission, as the batch the service publishes.
// The batch carries no state, because a suggestion is offered rather than
// true and never enters a status snapshot (decision 0097 section 6).
CoreResponseBatch
ToComposerSubmissionBatch(core_bridge::BridgeComposerSubmission submission);

// The bridge's answer to one claimed terminal, as the batch the service
// publishes: one push effect and, again, no state.
CoreResponseBatch
ToComposerDeliveryBatch(const core_bridge::BridgeComposerDelivery &delivery);

} // namespace taffy::core_service_internal

#endif // TAFFY_SERVICES_CORE_RUST_CORE_COMPOSER_H_
