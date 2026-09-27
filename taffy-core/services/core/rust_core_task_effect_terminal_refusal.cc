// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What a dispatched action that did not verify carries back, and nothing
//! else.
//!
//! Split from the completion half because the two answer opposite questions
//! and the rules differ: a completion copies facts the browser observed, and
//! a refusal copies one code and proves that nothing else came with it.

#include <string_view>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core_task_effect_terminal_internal.h"

namespace taffy::core_service_internal {
namespace {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;

// The closed code a refused read carries, mirroring the ledger's own reading
// of the same status in //taffy/browser/core_page_observation_broker.cc. Every
// one of these is a fact about the document rather than a decision about the
// proposal, which is exactly what makes them retryable after a fresh look.
mojom::TaskActionResultCode TaskActionResultCodeForRefusedObservation(
    mojom::BipObservationStatus status) {
  switch (status) {
    case mojom::BipObservationStatus::kOk:
    case mojom::BipObservationStatus::kIncomplete:
    case mojom::BipObservationStatus::kConflicted:
      // Not a refusal; the caller only reaches here for a status that is.
      // `kConflicted` joined these two when the broker stopped refusing a
      // reading for a page that disagrees with itself (decision 0207).
      return mojom::TaskActionResultCode::kInternalError;
    case mojom::BipObservationStatus::kUnsupported:
      return mojom::TaskActionResultCode::kUnsupported;
    case mojom::BipObservationStatus::kStalePageEpoch:
      return mojom::TaskActionResultCode::kStalePageEpoch;
    case mojom::BipObservationStatus::kDocumentInactive:
      return mojom::TaskActionResultCode::kDocumentInactive;
    case mojom::BipObservationStatus::kBudgetExceeded:
    case mojom::BipObservationStatus::kResourcePressure:
      return mojom::TaskActionResultCode::kBudgetExceeded;
    case mojom::BipObservationStatus::kCancelled:
      return mojom::TaskActionResultCode::kCancelledByUser;
    case mojom::BipObservationStatus::kDeadlineExceeded:
    case mojom::BipObservationStatus::kInternalError:
      return mojom::TaskActionResultCode::kInternalError;
  }
}

// The correlation every refusal carrying a result must pass first: this
// binding, this operation, a dispatched action, and a result that did not
// complete. A completion that fails any of it is a message about work this
// binding never asked for.
bool RefusalIsForThisDispatch(const mojom::TaskEffectBinding& effect,
                              const mojom::TaskEffectCompletion& completion) {
  return effect.kind == mojom::TaskReducerEffectKind::kDispatchAction &&
         effect.operation && effect.action && effect.action->executable &&
         completion.effect_result && completion.effect_result->operation &&
         SameOperationEnvelope(*effect.operation,
                       *completion.effect_result->operation) &&
         completion.effect_result->effect_id == effect.effect_id &&
         completion.effect_result->status != mojom::EffectStatus::kCompleted;
}

}  // namespace

bool CopyRefusedActionTerminal(const mojom::TaskEffectBinding& effect,
                               const mojom::TaskEffectCompletion& completion,
                               bridge::BridgeTaskTerminal& out) {
  if (!RefusalIsForThisDispatch(effect, completion) ||
      completion.effect_result->kind != mojom::EffectKind::kBrowserAction ||
      !completion.effect_result->browser_action ||
      completion.effect_result->browser_action->outcome !=
          mojom::BrowserActionOutcome::kRefused ||
      !completion.effect_result->browser_action->refused_code) {
    return false;
  }
  const mojom::BrowserActionEffectResult& action =
      *completion.effect_result->browser_action;
  // A refusal is a refusal and nothing else: no landing, no tab list, no
  // download, no store rows. Only the code and the dispatch it belongs to.
  if (action.discovered_source || action.discovery_tab_id ||
      action.browser_session_id || action.task_tab || action.task_download ||
      action.task_store ||
      action.refused_code->code == mojom::TaskActionResultCode::kVerified) {
    return false;
  }
  out.has_denial_code = true;
  out.denial_code = static_cast<uint8_t>(action.refused_code->code);
  return true;
}

bool CopyRefusedObservationTerminal(
    const mojom::TaskEffectBinding& effect,
    const mojom::TaskEffectCompletion& completion,
    bridge::BridgeTaskTerminal& out) {
  if (!RefusalIsForThisDispatch(effect, completion) ||
      effect.action->executable->action_class !=
          mojom::PolicyActionClass::kObservePage ||
      completion.effect_result->kind != mojom::EffectKind::kPageObservation ||
      !completion.effect_result->observation) {
    return false;
  }
  const mojom::ObservationEffectResult& observation =
      *completion.effect_result->observation;
  // A refusal carries its own word and no page. The graph, the media and the
  // two counts must all be absent: the ledger settled this capability against
  // a code that is not Verified, so bytes arriving beside it would be page
  // content the task was refused. The browser sends nothing else, and this is
  // what makes that a checked property rather than a promise.
  if (!observation.graph_payload.empty() || observation.media ||
      observation.node_count != 0u || observation.total_bytes != 0u ||
      observation.graph_encoding != mojom::BipGraphEncoding::kNone) {
    return false;
  }
  out.has_denial_code = true;
  out.denial_code = static_cast<uint8_t>(
      TaskActionResultCodeForRefusedObservation(observation.status));
  return true;
}


}  // namespace taffy::core_service_internal
