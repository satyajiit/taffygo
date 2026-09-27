// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/callback_helpers.h"
#include "base/time/time.h"
#include "taffy/browser/core_api/core_api_command_factory.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/field_value_request_coordinator.h"

// The two things the browser tells the isolated core about a form a person
// filled in: how many values they supplied, and what became of the ask.
//
// It is a file of its own because it is the far end of decision 0088's
// crossing, and the whole property being claimed is visible in one screen:
// there is no parameter here for a value, no local that holds one, and no
// call that could produce one. What arrives is a count that the coordinator
// composed after every mint had already happened, plus a closed member it
// chose from its own control flow, and what leaves is a command carrying
// both and two identities.
//
// The second fact is new and the argument for it has to be made rather than
// assumed, because "one small enum" is how a content-free crossing stops
// being one. `FieldValueAskOutcome` has seven members fixed at build time;
// none is chosen from anything on the page; and a count of zero without it
// reached the assistant as a fact with six possible meanings, which on the
// myAadhaar CAPTCHA cost eleven paid model calls over a picture that was
// merely below the fold (decision 0215).

namespace taffy {
namespace {

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

}  // namespace

void CoreServiceManager::BindFieldValueSurface(
    mojo::PendingReceiver<browser::field_values::mojom::TaffyFieldValueSurface>
        receiver) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!field_value_requests_ || !receiver.is_valid()) {
    return;
  }
  field_value_requests_->Bind(std::move(receiver));
}

void CoreServiceManager::OpenFieldValueRequest(
    const std::string& request_id,
    const std::string& task_id,
    const std::string& tab_id,
    const std::string& node_id,
    const std::vector<std::string>& companion_node_ids) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!field_value_requests_) {
    return;
  }
  field_value_requests_->OnCoreFieldValueRequest(request_id, task_id, tab_id,
                                                 node_id, companion_node_ids);
  RefreshIdleTeardown();
}

void CoreServiceManager::OnFieldValueRequestClosed(
    const std::string& request_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  emitted_field_value_requests_.erase(request_id);
  RefreshIdleTeardown();
}

void CoreServiceManager::SubmitSuppliedFieldValues(
    const std::string& task_id,
    const std::string& request_id,
    uint32_t supplied,
    core_service::mojom::FieldValueAskOutcome outcome,
    const std::vector<std::string>& field_node_ids) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutdown_started_ || availability_ != Availability::kReady) {
    // Dropped rather than queued. A count is a fact about a vault this
    // generation owns; a generation that is going takes the values with it,
    // so a command delivered afterwards would name references that no longer
    // exist.
    return;
  }
  const std::optional<uint64_t> revision = FindTaskRevision(task_id);
  if (!revision) {
    return;
  }
  CoreApiCommandFactory factory(browser_profile_id_,
                                CreateCoreApiEntropySource());
  core_service::mojom::CoreServiceCommandPtr command =
      factory.BuildSupplyFieldValues(task_id, request_id, supplied, outcome,
                                     field_node_ids, *revision,
                                     service_generation_, NowMonotonicMillis());
  if (!command) {
    return;
  }
  // base::DoNothing on the admission for the reason the handover expiry gives:
  // there is no surface waiting on this and nothing a refusal could be told
  // to. The values are already held either way, and the errand's own reducer
  // is what decides what happens next.
  Submit(std::move(command), base::DoNothing());
}

}  // namespace taffy
