// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/model/profile_model_broker.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "services/network/public/cpp/simple_url_loader.h"

namespace taffy {
namespace {

namespace service = core_service::mojom;

}  // namespace

void ProfileModelBroker::CancelTask(std::string_view task_id,
                                    uint64_t generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (task_id.empty() || generation == 0u) {
    // An empty task id belongs to a direct call that no task owns, and it is
    // held by every such call at once. Matching on it would cancel all of
    // them on behalf of a task that is not their owner.
    return;
  }

  // The identities are collected before any of them is answered. Answering
  // runs the effect broker's callback, which may journal and re-enter this
  // class synchronously, so a loop holding an iterator into `in_flight_` would
  // be walking a container the callback is allowed to change.
  std::vector<std::string> claimed;
  for (const auto& [effect_id, pending] : in_flight_) {
    if (pending->effect->operation->service_generation == generation &&
        pending->effect->model_request->task_id == task_id) {
      claimed.push_back(effect_id);
    }
  }

  for (const std::string& effect_id : claimed) {
    auto it = in_flight_.find(effect_id);
    if (it == in_flight_.end()) {
      continue;
    }
    // The whole of the distinction: a call still waiting on the secret store
    // has cost nothing and is cancelled, and one already on the wire has
    // possibly been received, answered and billed by the time this runs. The
    // second is `OUTCOME_UNKNOWN` because that is what the browser knows, and
    // a ledger told "cancelled" would be recording that no money was spent.
    const service::EffectStatus status =
        it->second->loader ? service::EffectStatus::kOutcomeUnknown
                           : service::EffectStatus::kCancelled;
    Finish(effect_id, MakeResult(*it->second->effect, status, {}));
  }
}

void ProfileModelBroker::CancelGeneration(uint64_t generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (generation == 0u) {
    return;
  }
  // Dropped rather than answered. `CoreEffectBroker::OnGenerationDisconnected`
  // resolves every effect it still holds from the lost generation — as
  // `OUTCOME_UNKNOWN` for a consequential one, which a model call is — and it
  // does that after calling here. Answering as well would be a second terminal
  // for one effect, which the broker counts as a late completion and which
  // would make a spend record look like two calls.
  //
  // What this does have to do is destroy the loaders, and it is the only thing
  // that will: a request nobody is waiting for otherwise runs to completion
  // and is billed against a generation that no longer exists.
  std::vector<std::string> lost;
  for (const auto& [effect_id, pending] : in_flight_) {
    if (pending->effect->operation->service_generation == generation) {
      lost.push_back(effect_id);
    }
  }
  for (const std::string& effect_id : lost) {
    auto it = in_flight_.find(effect_id);
    if (it == in_flight_.end()) {
      continue;
    }
    // Lifted out of the map before it is destroyed. Destroying the loader is
    // what runs its dropped-callback path, and that path looks this effect id
    // up again — in a container that must not be mid-erase when it does.
    std::unique_ptr<PendingCall> pending = std::move(it->second);
    in_flight_.erase(it);
    pending.reset();
  }

  // The endpoint probe goes the same way and for the same reason, with one
  // difference worth naming: nothing else will answer it. It never reaches
  // `CoreEffectBroker`, so no generation teardown there synthesizes a terminal
  // for it — the callback belongs to `CoreServiceManager::EmitEffect`, which
  // drops a reply from a generation that is gone. Dropping the probe destroys
  // the prober's loader, which is what stops a request nobody is waiting for
  // from running to completion against a person's own server.
  //
  // The held probe is dropped before the prober, so a callback that is already
  // on the stack finds nothing pending and returns rather than answering into
  // a generation that is gone. Destroying the prober from inside its own
  // callback is safe for the reason it is safe for a `SimpleURLLoader`:
  // `CustomEndpointProber::Finish` is the last statement on every path that
  // reaches it, so no frame touches the object after it returns.
  if (endpoint_probe_ &&
      endpoint_probe_->effect->operation->service_generation == generation) {
    endpoint_probe_.reset();
    endpoint_prober_.reset();
  }
}

}  // namespace taffy
