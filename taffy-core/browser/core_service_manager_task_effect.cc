// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/time/time.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_task_action.h"
#include "taffy/browser/core_task_action_reconciliation.h"
#include "taffy/browser/core_task_action_route.h"
#include "taffy/browser/core_task_artifact_export.h"
#include "taffy/browser/core_task_effect.h"
#include "taffy/browser/field_value_request_coordinator.h"
#include "taffy/browser/profile_tool_artifact_broker.h"
#include "taffy/browser/taffy_page_intelligence_host.h"

namespace taffy {
namespace {

namespace service_mojom = core_service::mojom;

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

uint64_t NowUtcMillis() {
  const int64_t value = base::Time::Now().InMillisecondsSinceUnixEpoch();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

service_mojom::EffectEnvelopePtr BuildObservationEnvelope(
    const service_mojom::TaskEffectBinding& binding) {
  const service_mojom::TaskActionEffect& action = *binding.action;
  auto envelope = service_mojom::EffectEnvelope::New();
  envelope->operation = binding.operation.Clone();
  envelope->effect_id = binding.effect_id;
  envelope->kind = service_mojom::EffectKind::kPageObservation;
  envelope->retry_class = service_mojom::RetryClass::kIdempotent;
  envelope->page_observation = service_mojom::PageObservationEffect::New();
  envelope->page_observation->tab_id = action.executable->tab_id;
  envelope->page_observation->frame_id = action.document->frame_id;
  envelope->page_observation->page_epoch = action.document->page_epoch;
  envelope->page_observation->scope = action.observation->scope;
  envelope->page_observation->max_bytes = action.observation->max_bytes;
  envelope->page_observation->task_id = binding.task_id;
  envelope->page_observation->action_id = action.action_id;
  envelope->page_observation->capability_id = action.capability_id;
  envelope->page_observation->proposal_digest = action.proposal_digest;
  envelope->page_observation->idempotency_key = action.idempotency_key;
  envelope->page_observation->authority_subject =
      service_mojom::AuthoritySubject::New();
  envelope->page_observation->authority_subject->kind =
      service_mojom::AuthoritySubjectKind::kTask;
  envelope->page_observation->authority_subject->authority_subject_id =
      binding.task_id;
  envelope->page_observation->max_nodes = action.observation->max_nodes;
  envelope->page_observation->max_text_bytes =
      action.observation->max_text_bytes;
  envelope->page_observation->max_frames = action.observation->max_frames;
  envelope->page_observation->deadline_ms = action.observation->deadline_ms;
  envelope->page_observation->expected_graph_revision =
      action.document->graph_revision;
  return envelope;
}

service_mojom::EffectEnvelopePtr BuildModelEnvelope(
    const service_mojom::TaskEffectBinding& binding) {
  auto envelope = service_mojom::EffectEnvelope::New();
  envelope->operation = binding.operation.Clone();
  envelope->effect_id = binding.effect_id;
  envelope->kind = service_mojom::EffectKind::kModelRequest;
  // A model call is consequential: it may already have been billed by the time
  // anything here learns what happened to it (decision 0052).
  envelope->retry_class = service_mojom::RetryClass::kConsequential;
  envelope->model_request = binding.model->request.Clone();
  return envelope;
}

}  // namespace

void CoreServiceManager::ExecuteTaskEffect(
    service_mojom::TaskEffectBindingPtr effect,
    ExecuteTaskEffectCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutdown_started_ || availability_ != Availability::kReady ||
      !session_.is_bound()) {
    std::move(callback).Run(MakeTaskEffectCompletion(
        effect.get(), service_mojom::TaskEffectCompletionStatus::kUnavailable));
    return;
  }
  const std::optional<uint64_t> revision =
      effect ? FindTaskRevision(effect->task_id) : std::nullopt;
  const uint64_t now_monotonic_ms = NowMonotonicMillis();
  const uint64_t now_utc_ms = NowUtcMillis();
  const bool structurally_valid =
      effect && revision &&
      IsStructurallyValidTaskEffectBinding(*effect, service_generation_,
                                           *revision, now_monotonic_ms);
  if (!structurally_valid) {
    // One boolean over every effect kind and, for a dispatch action, over
    // forty more clauses. It is the last refusal on this path that named
    // nothing, and it is the one that had been refusing every
    // `browser.link.open` (decision 0175). Every value here is a compiled-in
    // enumeration member or a flag.
    LOG(WARNING) << "[taffy_task_effect_refused] at=structure"
                 << " kind=" << (effect ? static_cast<int>(effect->kind) : -1)
                 << " revision=" << (revision ? 1 : 0)
                 << " op="
                 << (effect && effect->action && effect->action->executable
                         ? static_cast<int>(
                               effect->action->executable->operation_kind)
                         : -1)
                 << " tool="
                 << (effect && effect->action && effect->action->executable
                         ? effect->action->executable->tool_name
                         : std::string());
    // And the lease it was granted a moment ago. A structurally refused action
    // never runs, and the registry allows one mutating lease per tab, so
    // keeping it stops every later move on that tab (decision 0174).
    if (effect && effect->action) {
      CapabilityGrant capability;
      if (capabilities_.CopyRegisteredCapability(
              CapabilityReference{effect->action->capability_id}, capability)) {
        actor_leases_.Release(capability.actor_lease_id);
      }
    }
    std::move(callback).Run(MakeTaskEffectCompletion(
        effect.get(), service_mojom::TaskEffectCompletionStatus::kRefused));
    return;
  }

  // Some task effects use the durable effect broker, while tab, navigation,
  // download and page actions have purpose-built browser executors. Hold one
  // generation-bound identity around every path so idle release cannot race
  // an executor merely because that executor has a different transport seam.
  const std::string effect_id = effect->effect_id;
  if (!pending_host_task_effect_ids_.insert(effect_id).second) {
    std::move(callback).Run(MakeTaskEffectCompletion(
        effect.get(), service_mojom::TaskEffectCompletionStatus::kRefused));
    return;
  }
  RefreshIdleTeardown();
  callback = base::BindOnce(
      [](base::WeakPtr<CoreServiceManager> manager, uint64_t generation,
         std::string tracked_effect_id,
         ExecuteTaskEffectCallback original_callback,
         service_mojom::TaskEffectCompletionPtr completion) {
        if (manager && generation == manager->service_generation_) {
          if (manager->pending_host_task_effect_ids_.erase(tracked_effect_id) !=
              1u) {
            ++manager->late_reply_count_;
          }
          manager->RefreshIdleTeardown();
        }
        std::move(original_callback).Run(std::move(completion));
      },
      weak_factory_.GetWeakPtr(), service_generation_, effect_id,
      std::move(callback));

  // ASK_POLICY is deliberately not multiplexed through this method. The core
  // service retains the full PolicyEvaluationResult from EvaluateTaskPolicy so
  // Rust can durably record the exact decision and capability identity.
  if (effect->kind == service_mojom::TaskReducerEffectKind::kAskPolicy) {
    std::move(callback).Run(MakeTaskEffectCompletion(
        effect.get(), service_mojom::TaskEffectCompletionStatus::kRefused));
    return;
  }

  if ((effect->kind ==
           service_mojom::TaskReducerEffectKind::kRunLibraryTool &&
       effect->library_tool) ||
      (effect->kind ==
           service_mojom::TaskReducerEffectKind::kRunMemoryTool &&
       effect->memory_tool)) {
    // The sandboxed core executes the aggregate Library or Memory operation.
    // This correlated terminal is only the trigger; a write returns a
    // separate exact-revision browser storage effect before the reducer
    // records its outcome. Keeping the two core-owned stores on the same
    // trigger path prevents an otherwise valid Memory tool from falling
    // through to the generic unavailable terminal below.
    std::move(callback).Run(MakeTaskEffectCompletion(
        effect.get(), service_mojom::TaskEffectCompletionStatus::kSucceeded));
    return;
  }

  if (effect->kind ==
          service_mojom::TaskReducerEffectKind::kPrepareDiscoveryTab &&
      effect->discovery_bootstrap) {
    ExecuteTaskDiscoveryBootstrap(std::move(effect), std::move(callback));
    return;
  }

  if (effect->kind == service_mojom::TaskReducerEffectKind::kRevokeAuthority) {
    AbandonOpenHandover(effect->task_id);
    actor_leases_.RevokeTask(effect->task_id, service_generation_);
    capabilities_.RevokeTask(effect->task_id, service_generation_);
    // A value outliving the errand it was given for is the same defect as a
    // lease outliving it. The vault takes no generation because a value is
    // only ever held under the active one.
    value_references_.RevokeTask(TaskId{effect->task_id});
    if (field_value_requests_) {
      // And the sheet, if one was still open over this errand. A person
      // typing into a form for a task that has ended is a person whose
      // answers nothing will ever spend.
      field_value_requests_->CloseRequestsForTask(effect->task_id);
    }
    RevokePreapprovedFormActionsForTask(effect->task_id);
    accepted_approvals_.RevokeTaskApprovals(effect->task_id,
                                            service_generation_);
    const auto disposition =
        effect->revocation->reason ==
                service_mojom::TaskRevocationReason::kTaskCancelled
            ? ProfileToolArtifactBroker::TaskDisposition::kAbandoned
            : ProfileToolArtifactBroker::TaskDisposition::kPaused;
    tool_artifact_broker_->SettleTask(effect->task_id, service_generation_,
                                      disposition);
    effect_broker_->CancelTask(effect->task_id, service_generation_);
    std::move(callback).Run(MakeTaskEffectCompletion(
        effect.get(), service_mojom::TaskEffectCompletionStatus::kSucceeded));
    return;
  }

  if (effect->kind == service_mojom::TaskReducerEffectKind::kRequestApproval &&
      effect->approval) {
    const std::optional<PendingApprovalLookup> pending =
        state_bindings_.FindPendingApproval(effect->task_id,
                                            effect->approval->action_id);
    if (!pending || pending->service_generation != service_generation_ ||
        pending->task_revision != effect->operation->task_revision ||
        pending->proposal_digest != effect->approval->proposal_digest) {
      std::move(callback).Run(MakeTaskEffectCompletion(
          effect.get(), service_mojom::TaskEffectCompletionStatus::kRefused));
      return;
    }
    QueueTaskSurface(std::move(effect), std::move(callback));
    return;
  }

  if (effect->kind ==
          service_mojom::TaskReducerEffectKind::kRequestPermission &&
      effect->permission) {
    const std::optional<PendingPermissionLookup> pending =
        state_bindings_.FindPendingPermission(effect->permission->request_id);
    if (!pending || pending->task_id != effect->task_id ||
        pending->service_generation != service_generation_ ||
        pending->task_revision != effect->operation->task_revision ||
        pending->permission != effect->permission->permission ||
        pending->deadline_monotonic_ms !=
            effect->permission->deadline_monotonic_ms ||
        pending->deadline_utc_ms != effect->permission->deadline_utc_ms ||
        pending->browser_session_id != effect->permission->browser_session_id ||
        effect->permission->browser_session_id != browser_session_id_ ||
        effect->permission->deadline_utc_ms <= now_utc_ms) {
      std::move(callback).Run(MakeTaskEffectCompletion(
          effect.get(), service_mojom::TaskEffectCompletionStatus::kRefused));
      return;
    }
    QueueTaskSurface(std::move(effect), std::move(callback));
    return;
  }

  if (effect->kind ==
          service_mojom::TaskReducerEffectKind::kRequestFieldValues &&
      effect->field_values) {
    // A surface effect, so it parks with the approval and permission ones
    // rather than answering here: it succeeds when the state that carries it
    // has reached a native owner, not when the browser has read the message.
    //
    // What is checked first is the tab. The core names a form in a tab, and
    // the browser owns whether this task was ever consented to that tab and
    // what document is in it now — exactly the pair the observe-page arm
    // above checks, for the same reason. A sheet opened over a page the task
    // has no business in is a person being asked to type into a form Taffy
    // could not have been looking at.
    const std::optional<TaskPolicyDocumentContext> live =
        ResolveTaskPolicyDocument(browser_context_.get(),
                                  effect->field_values->tab_id);
    const char* refused_at =
        !live ? "no-live-document"
        : !accepted_approvals_.IsTaskSourceAuthorized(
              effect->task_id, effect->field_values->tab_id,
              *effect->operation, live->origin, service_generation_)
            ? "source-not-authorized"
            : nullptr;
    if (refused_at) {
      // The bridge treats a sheet's terminal as the sheet having reached its
      // owner, whatever its status, and the task then waits for the person's
      // answer. Refused here, nothing ever drew a sheet, so that answer could
      // never come: on a phone an errand on myAadhaar's form sat under
      // "Taffy needs something" with nothing to fill in, for as long as
      // anyone left it. So the refusal is answered the way the coordinator
      // answers a sheet it could not draw — the person supplied nothing, and
      // the outcome says to name another field or hand the page over, which
      // is the move that still gives the person the form.
      LOG(WARNING) << "[taffy_field_request_refused] at=" << refused_at;
      const std::string task_id = effect->task_id;
      const std::string request_id = effect->field_values->request_id;
      std::move(callback).Run(MakeTaskEffectCompletion(
          effect.get(), service_mojom::TaskEffectCompletionStatus::kRefused));
      // `ValidateCommand` binds a count to a request this browser emitted,
      // and a refused one never was, so without this the answer is refused
      // too and the task waits exactly as before. The request is entered for
      // the length of the one submission that settles it, and only if this
      // is what entered it.
      const bool entered =
          emitted_field_value_requests_.insert(request_id).second;
      SubmitSuppliedFieldValues(
          task_id, request_id, 0u,
          service_mojom::FieldValueAskOutcome::kCannotBeShown,
          /*field_node_ids=*/{});
      if (entered) {
        emitted_field_value_requests_.erase(request_id);
      }
      return;
    }
    QueueTaskSurface(std::move(effect), std::move(callback));
    return;
  }

  // AWAIT_IN_FLIGHT_WORK is owned exclusively by
  // CompleteRegisteredTaskSettlements(). CoreServiceImpl must not multiplex
  // it here: doing so would race a second reducer transition against the
  // durable CompleteTaskSettlement response.
  if (effect->kind ==
      service_mojom::TaskReducerEffectKind::kAwaitInFlightWork) {
    std::move(callback).Run(MakeTaskEffectCompletion(
        effect.get(), service_mojom::TaskEffectCompletionStatus::kRefused));
    return;
  }

  if (effect->kind ==
          service_mojom::TaskReducerEffectKind::kReconcileAction &&
      effect->reconcile) {
    ReconcileTaskAction(storage_broker_.get(), std::move(effect),
                        std::move(callback));
    return;
  }

  if (effect->kind == service_mojom::TaskReducerEffectKind::kReleaseTaskTabs &&
      effect->release_tabs) {
    ExecuteTaskTabRelease(std::move(effect), std::move(callback));
    return;
  }

  if (effect->kind == service_mojom::TaskReducerEffectKind::kGenerateArtifact &&
      effect->generate_artifact) {
    // Artifact rendering is deliberately inside the sandboxed Rust core. By
    // the time this immutable binding is published, Generate has reproduced
    // the exact cited workspace revision and Export has additionally
    // published those same bounded bytes in CoreStatus.latest_export. The
    // browser owns only the correlated terminal here; it must neither render
    // model prose nor manufacture a second byte representation.
    std::move(callback).Run(MakeTaskEffectCompletion(
        effect.get(), service_mojom::TaskEffectCompletionStatus::kSucceeded));
    return;
  }

  if (effect->kind == service_mojom::TaskReducerEffectKind::kExportArtifact &&
      effect->export_artifact) {
    const bool browser_custodied =
        effect->export_artifact->content.empty() &&
        ProfileToolArtifactBroker::RetainsArtifactKind(
            effect->export_artifact->kind);
    const bool delivered =
        browser_custodied
            ? tool_artifact_broker_->DeliverArtifact(
                  observers_, effect->task_id, service_generation_,
                  *effect->export_artifact)
            : DeliverTaskArtifactExport(observers_, effect->task_id,
                                        *effect->export_artifact);
    std::move(callback).Run(MakeTaskEffectCompletion(
        effect.get(),
        delivered ? service_mojom::TaskEffectCompletionStatus::kSucceeded
                  : service_mojom::TaskEffectCompletionStatus::kRefused));
    return;
  }

  const TaskActionExecutor action_executor =
      effect->kind == service_mojom::TaskReducerEffectKind::kDispatchAction &&
              effect->action && effect->action->executable
          ? TaskActionExecutorForOperation(
                effect->action->executable->operation_kind)
          : TaskActionExecutor::kNotDispatchedByBrowser;

  if (action_executor == TaskActionExecutor::kTaskTabs) {
    ExecuteTaskTabAction(std::move(effect), std::move(callback));
    return;
  }

  if (action_executor == TaskActionExecutor::kDownloads) {
    ExecuteTaskDownloadAction(std::move(effect), std::move(callback));
    return;
  }

  if (action_executor == TaskActionExecutor::kProfileStores) {
    ExecuteTaskStoreAction(std::move(effect), std::move(callback));
    return;
  }

  if (action_executor == TaskActionExecutor::kPageObservation) {
    const service_mojom::TaskActionEffect& action = *effect->action;
    const std::optional<TaskPolicyDocumentContext> live =
        ResolveTaskPolicyDocument(browser_context_.get(),
                                  action.executable->tab_id);
    // Six facts refused a read here and all six answered one silent word.
    // Nothing logged, and the completion named no code, so the bridge settled
    // every one of them as `DeniedByPolicy` — `DoNotRetry`. A phone spent an
    // errand on that: a read of a page that was still settling came back to
    // the model as a standing prohibition, and it handed the page back rather
    // than look again at something it had already read twice (decision 0207).
    //
    // Each clause now says which fact it is and answers the code for that
    // fact. Five of them are `ReobserveThenRetry`, because a document that
    // moved is exactly what a fresh look fixes. Only an unauthorized source
    // is a standing no, and `kEgressNotAuthorized` is the code the policy
    // path already answers for it.
    const char* refused_at = nullptr;
    service_mojom::TaskActionResultCode refused_code =
        service_mojom::TaskActionResultCode::kDocumentInactive;
    if (!live) {
      refused_at = "no-live-document";
      refused_code = service_mojom::TaskActionResultCode::kDocumentInactive;
    } else if (live->frame_id != action.document->frame_id) {
      refused_at = "frame-changed";
      refused_code = service_mojom::TaskActionResultCode::kFrameGone;
    } else if (live->page_epoch != action.document->page_epoch) {
      refused_at = "epoch-changed";
      refused_code = service_mojom::TaskActionResultCode::kStalePageEpoch;
    } else if (live->origin != action.document->normalized_origin) {
      refused_at = "origin-changed";
      refused_code = service_mojom::TaskActionResultCode::kOriginChanged;
    } else if (live->graph_revision < action.document->graph_revision) {
      refused_at = "graph-behind-the-action";
      refused_code =
          service_mojom::TaskActionResultCode::kGraphMovedDuringPreflight;
    } else if (!accepted_approvals_.IsTaskSourceAuthorized(
                   effect->task_id, action.executable->tab_id,
                   *effect->operation, live->origin, service_generation_)) {
      refused_at = "source-not-authorized";
      refused_code = service_mojom::TaskActionResultCode::kEgressNotAuthorized;
    } else if (!capabilities_.TaskObservationMatchesRegisteredGrant(action)) {
      // The grant this read was minted against is not the one it is spending.
      refused_at = "grant-does-not-match";
      refused_code = service_mojom::TaskActionResultCode::kCapabilityExpired;
    }
    if (refused_at) {
      LOG(WARNING) << "[taffy_task_observation_refused] at=" << refused_at;
      std::move(callback).Run(
          MakeRefusedTaskActionCompletion(*effect, refused_code));
      return;
    }
    service_mojom::TaskEffectBindingPtr retained = effect.Clone();
    effect_broker_->Dispatch(
        BuildObservationEnvelope(*effect),
        base::BindOnce(&CoreServiceManager::OnTaskObservationCompleted,
                       weak_factory_.GetWeakPtr(), std::move(retained),
                       std::move(callback)));
    return;
  }

  if (action_executor == TaskActionExecutor::kPageAction) {
    ExecuteTaskPageAction(std::move(effect), std::move(callback));
    return;
  }

  if (action_executor == TaskActionExecutor::kNavigation) {
    ExecuteTaskNavigate(std::move(effect), std::move(callback));
    return;
  }

  if (effect->kind == service_mojom::TaskReducerEffectKind::kCallModel &&
      effect->model && effect->model->request) {
    service_mojom::TaskEffectBindingPtr retained = effect.Clone();
    effect_broker_->Dispatch(
        BuildModelEnvelope(*effect),
        base::BindOnce(&CoreServiceManager::OnTaskModelCompleted,
                       weak_factory_.GetWeakPtr(), std::move(retained),
                       std::move(callback)));
    return;
  }

  if (effect->kind == service_mojom::TaskReducerEffectKind::kRunToolJob &&
      effect->tool_job &&
      (effect->tool_job->runtime == service_mojom::ToolRuntimeKind::kPython ||
       effect->tool_job->runtime == service_mojom::ToolRuntimeKind::kMedia) &&
      effect->tool_job->job) {
    ExecuteTaskToolJob(std::move(effect), std::move(callback));
    return;
  }

  if (effect->kind == service_mojom::TaskReducerEffectKind::kAwaitHandover &&
      effect->handover) {
    ExecuteTaskHandover(std::move(effect), std::move(callback));
    return;
  }

  // Each remaining family gets its own reviewed executor. Until that executor
  // exists, return one correlated terminal; never acknowledge work that did
  // not run and never choose another effect family as a fallback.
  std::move(callback).Run(MakeTaskEffectCompletion(
      effect.get(), service_mojom::TaskEffectCompletionStatus::kUnavailable));
}

}  // namespace taffy
