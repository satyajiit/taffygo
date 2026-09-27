// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <utility>

#include "taffy/renderer/action_command_translator.h"
#include "taffy/renderer/observation_limits.h"
#include "taffy/renderer/page_intelligence_endpoint.h"
#include "taffy/renderer/wire_conversions.h"

namespace taffy {

void PageIntelligenceEndpoint::ExecuteRendererAction(
    mojom::RendererActionCommandPtr command,
    ExecuteRendererActionCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto result = mojom::RendererActionResult::New();
  result->schema_version = command->schema_version;
  result->command_id = command->command_id;

  if (std::ranges::find(cancelled_commands_, command->command_id) !=
      cancelled_commands_.end()) {
    result->outcome = mojom::RendererActionOutcome::kCancelled;
    std::move(callback).Run(std::move(result));
    return;
  }
  if (invalidated_ || !store_ ||
      lifecycle_state_ != mojom::DocumentLifecycleState::kActive) {
    result->outcome = mojom::RendererActionOutcome::kDocumentInactive;
    std::move(callback).Run(std::move(result));
    return;
  }
  result->observed_at_revision = store_->current_revision().value();

  const ActionCommandTranslator::Result translated =
      ActionCommandTranslator::Translate(*command);
  if (!translated.request.has_value()) {
    result->outcome = translated.refusal;
    result->failed_precondition = translated.failed_precondition;
    std::move(callback).Run(std::move(result));
    return;
  }

  const RendererActionExecutor::Result executed =
      action_executor_.Execute(*store_, frame_, translated.request.value());
  result->observed_at_revision = executed.revision_at_dispatch.value();
  if (executed.observed_state.has_value()) {
    result->observed_state = BuildResolvedNode(executed.observed_state.value(),
                                               executed.revision_at_dispatch,
                                               /*refresh_live_checked_state=*/
                                               false);
  }

  switch (executed.outcome) {
    case RendererActionExecutor::Outcome::kPerformed:
      // DISPATCHED, never VERIFIED. Only the browser-side verifier may call
      // an effect verified after observing the postcondition.
      result->outcome = mojom::RendererActionOutcome::kDispatched;
      break;
    case RendererActionExecutor::Outcome::kRequiresBrowserInputDispatch:
    case RendererActionExecutor::Outcome::kUnsupported:
      result->outcome = mojom::RendererActionOutcome::kUnsupported;
      break;
    case RendererActionExecutor::Outcome::kPreconditionFailed:
      result->outcome = wire::ToMojom(executed.precondition);
      break;
    case RendererActionExecutor::Outcome::kDispatchFailed:
      result->outcome = mojom::RendererActionOutcome::kNodeGone;
      break;
  }

  std::move(callback).Run(std::move(result));
}

void PageIntelligenceEndpoint::Cancel(const std::string& command_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Bounded: a browser that cancels without limit must not be able to grow
  // renderer memory. The browser also rejects any evicted command's late
  // reply because it consumed the capability when it cancelled.
  const size_t ceiling = ObservationLimits::ProcessSafeCeiling()
                             .delta()
                             .max_tracked_cancellations();
  cancelled_commands_.push_back(command_id);
  if (cancelled_commands_.size() > ceiling) {
    cancelled_commands_.pop_front();
  }
}

}  // namespace taffy
