// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The composer seam of the profile facade: one request out, one push back.
//
// Nothing here is kept. The request carries a bounded prefix and suffix of
// what the person is typing, and the answer is pushed at whatever surface is
// attached and then forgotten — there is no store, no journal and no replay,
// because a suggestion about text that has since changed is worse than no
// suggestion at all. The `delivered` flag the core is answered with is decided
// in CoreServiceManager::DeliverComposerCompletion, which knows whether any
// surface was watching; this file only knows about its own observer.

#include <optional>
#include <string>
#include <utility>

#include "base/time/time.h"
#include "taffy/browser/core_api/profile_core_api_facade.h"

namespace taffy {
namespace {

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

}  // namespace

void ProfileCoreApiFacade::RequestComposerCompletion(
    const std::string& request_id,
    const std::string& prefix,
    const std::optional<std::string>& suffix,
    RequestComposerCompletionCallback callback) {
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(
      factory.BuildRequestComposerCompletion(
          request_id, prefix, suffix,
          manager_ ? manager_->service_generation() : 0u, NowMonotonicMillis()),
      std::move(callback));
}

// The withdrawal (decision 0097 section 3).
//
// One submission that dispatches nothing and names what to stop. The browser
// needs no new leg for it: the core already knows which dispatch a newer
// request displaced and already names it, so a withdrawal is the shape this
// process already handles for a superseded request. What it adds is the half a
// surface can state on its own — a person who stopped typing, or moved the
// caret, has stopped wanting an answer without wanting a different one, and
// before this the only way to say so was to mint a newer identity and pay for
// a model call that would be discarded on arrival.
//
// No answer is pushed for a withdrawn request, and that is the point rather
// than an omission: `OnComposerCompletion` below is the only way one arrives,
// and a request that was stopped produces none.
void ProfileCoreApiFacade::CancelComposerCompletion(
    const std::string& request_id,
    CancelComposerCompletionCallback callback) {
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(
      factory.BuildCancelComposerCompletion(
          request_id, manager_ ? manager_->service_generation() : 0u,
          NowMonotonicMillis()),
      std::move(callback));
}

void ProfileCoreApiFacade::OnComposerCompletion(
    const std::string& request_id,
    const std::optional<std::string>& text) {
  // Straight through, with no state kept here, for the reason
  // `OnAssetProgress` gives: the value is only worth anything while it is
  // fresh, and an observer that is not connected yet has missed nothing it
  // could still use.
  if (observer_) {
    observer_->OnComposerCompletion(request_id, text);
  }
}

void ProfileCoreApiFacade::OnTaskAnswerDelta(
    const std::string& task_id,
    const std::string& call_id,
    uint32_t sequence,
    const std::optional<std::string>& text,
    bool terminal,
    bool complete) {
  // The isolated core is the only producer, but this facade is still the
  // public profile boundary. Refuse a malformed push whole rather than hand a
  // platform surface an event it would have to repair into a different one.
  if (task_id.empty() ||
      task_id.size() > core_api::mojom::kMaxIdentifierBytes ||
      call_id.empty() ||
      call_id.size() > core_api::mojom::kMaxIdentifierBytes ||
      terminal != !text.has_value() ||
      (complete && !terminal) ||
      (text && (text->empty() ||
                text->size() > core_api::mojom::kMaxTaskAnswerDeltaBytes))) {
    return;
  }
  if (observer_) {
    observer_->OnTaskAnswerDelta(task_id, call_id, sequence, text, terminal,
                                 complete);
  }
}

}  // namespace taffy
