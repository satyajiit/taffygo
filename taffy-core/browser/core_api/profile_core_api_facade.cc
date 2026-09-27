// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_api/profile_core_api_facade.h"

#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/time/time.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;
namespace service = core_service::mojom;

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

uint64_t NowUtcMillis() {
  const int64_t value = base::Time::Now().InMillisecondsSinceUnixEpoch();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

api::CoreAvailability
ProjectAvailability(CoreServiceManager::Availability availability) {
  switch (availability) {
  case CoreServiceManager::Availability::kStarting:
    return api::CoreAvailability::kStarting;
  case CoreServiceManager::Availability::kReady:
    return api::CoreAvailability::kReady;
  case CoreServiceManager::Availability::kCircuitOpen:
    return api::CoreAvailability::kCircuitOpen;
  case CoreServiceManager::Availability::kStopped:
  case CoreServiceManager::Availability::kUnavailable:
  case CoreServiceManager::Availability::kShuttingDown:
    return api::CoreAvailability::kUnavailable;
  }
  return api::CoreAvailability::kUnavailable;
}

} // namespace

ProfileCoreApiFacade::SubmissionStatus ProfileCoreApiFacade::ProjectAdmission(
    service::AdmissionStatus status) {
  switch (status) {
  case service::AdmissionStatus::kAccepted:
    return api::CoreApiSubmissionStatus::kAccepted;
  case service::AdmissionStatus::kStaleGeneration:
    return api::CoreApiSubmissionStatus::kStaleGeneration;
  case service::AdmissionStatus::kStaleRevision:
    return api::CoreApiSubmissionStatus::kStaleRevision;
  case service::AdmissionStatus::kDeadlineExceeded:
    return api::CoreApiSubmissionStatus::kDeadlineExceeded;
  case service::AdmissionStatus::kBackpressure:
    return api::CoreApiSubmissionStatus::kBackpressure;
  case service::AdmissionStatus::kInvalidCommand:
    return api::CoreApiSubmissionStatus::kInvalidRequest;
  case service::AdmissionStatus::kCoreUnavailable:
    return api::CoreApiSubmissionStatus::kCoreUnavailable;
  case service::AdmissionStatus::kDuplicate:
    return api::CoreApiSubmissionStatus::kDuplicate;
  }
  return api::CoreApiSubmissionStatus::kInvalidRequest;
}

ProfileCoreApiFacade::ProfileCoreApiFacade(CoreServiceManager *manager)
    : manager_(manager) {
  CHECK(manager_);
}

ProfileCoreApiFacade::~ProfileCoreApiFacade() {
  if (observing_ && manager_) {
    manager_->RemoveObserver(this);
  }
}

void ProfileCoreApiFacade::Observe(
    mojo::PendingRemote<api::TaffyProfileCoreApiObserver> observer) {
  if (!manager_ || !observer.is_valid() || observer_.is_bound()) {
    return;
  }
  // A surface that is watching has to be able to make the core speak once.
  //
  // Without this the observer attached, `AddObserver` replayed the stored
  // availability, and the stored availability at startup is `kStopped` —
  // which projects to a snapshot with no payload at all. `EnsureStarted`
  // returns early while every pending queue is empty, and nothing the Core
  // API alone does fills one, so the core stayed stopped and every snapshot
  // after the first said the same thing.
  //
  // Sign-in is where that became visible and it is the case that proves the
  // rule: screen SCR-701 draws from `CoreStatus.auth_state`, and every
  // command it could send is refused until that projection arrives, so the
  // one surface that could have started the core was the one surface that
  // could not. It waited for the life of the process. The account view, the
  // workspace list and the delivery view arrive by the same route and were
  // all equally absent; sign-in is simply the one with nothing else to draw.
  //
  // `RetryCore` below already prepares for exactly this reason, and
  // `IsQuiescent` already refuses to idle down while an observer is attached,
  // so this starts the core once per profile graph rather than holding a
  // service open that nothing is watching.
  manager_->PrepareForCoreApi(base::DoNothing());
  observer_.Bind(std::move(observer));
  observing_ = true;
  manager_->AddObserver(this);
}

void ProfileCoreApiFacade::RetryCore(uint64_t observed_generation,
                                     RetryCoreCallback callback) {
  if (!manager_) {
    std::move(callback).Run(SubmissionStatus::kCoreUnavailable);
    return;
  }
  if (observed_generation != manager_->service_generation()) {
    std::move(callback).Run(SubmissionStatus::kStaleGeneration);
    return;
  }
  manager_->RetryExplicitly();
  manager_->PrepareForCoreApi(base::DoNothing());
  std::move(callback).Run(SubmissionStatus::kAccepted);
}

void ProfileCoreApiFacade::OnCoreAvailabilityChanged(
    CoreServiceManager::Availability availability) {
  if (availability != CoreServiceManager::Availability::kReady) {
    pending_task_artifact_exports_.clear();
    ++saved_flow_query_ticket_;
  }
  const api::CoreAvailability projected = ProjectAvailability(availability);
  // A snapshot that says ready has to carry the state it is ready with.
  //
  // `status_payload` is documented as absent only when no complete state
  // exists, so `kReady` with nothing attached is not a thin notification -- it
  // is a contradiction, and the surface treats it as the protocol violation it
  // is: `MissingReadyPayload` replaces whatever state it was holding with an
  // unavailable one.
  //
  // That is exactly what happened here, and it happened *after* the good
  // state arrived. `OnCoreState` publishes the first payload while the launch
  // is still finishing, and `SetAvailability(kReady)` follows it a moment
  // later; this method then overwrote a decoded status with an empty one. A
  // ready core is quiescent, so no further state was due and nothing ever
  // corrected it: every core-backed surface stayed empty for the life of the
  // process. The start page was the visible one, waiting on a delivery view
  // that had already been delivered and thrown away.
  //
  // Readiness is therefore announced only by the snapshot that carries the
  // state. Every other availability is a fact about the core rather than
  // about its contents, and still goes out by itself.
  if (observer_.is_bound() && projected != api::CoreAvailability::kReady) {
    observer_->OnSnapshot(projected,
                          manager_ ? manager_->service_generation() : 0u, 0u,
                          0u, std::nullopt);
  }
  if (availability == CoreServiceManager::Availability::kShuttingDown &&
      observing_ && manager_) {
    manager_->RemoveObserver(this);
    observing_ = false;
    manager_ = nullptr;
  }
}

void ProfileCoreApiFacade::OnCoreState(const service::CoreStateUpdate &state) {
  if (!observer_.is_bound()) {
    return;
  }
  observer_->OnSnapshot(api::CoreAvailability::kReady, state.service_generation,
                        state.sequence, state.core_status_schema_version,
                        state.payload);
}

void ProfileCoreApiFacade::OnCorePermissionRequest(
    const std::string &request_id, api::PlatformPermission permission) {
  if (observer_.is_bound()) {
    observer_->OnPermissionRequest(request_id, permission);
  }
}

void ProfileCoreApiFacade::SubmitProjected(
    std::optional<ProjectedCoreCommand> projected,
    SubmissionCallback callback) {
  if (!manager_) {
    std::move(callback).Run(SubmissionStatus::kCoreUnavailable);
    return;
  }
  if (!projected || !projected->core_service_command) {
    // The factory refused and named its own clause; this says the refusal
    // reached the surface as an invalid request rather than being lost.
    LOG(WARNING) << "[taffy_core_api_refused] at=projection";
    std::move(callback).Run(SubmissionStatus::kInvalidRequest);
    return;
  }
  manager_->Submit(std::move(projected->core_service_command),
                   base::BindOnce(&ProfileCoreApiFacade::OnSubmitted,
                                  weak_factory_.GetWeakPtr(),
                                  std::move(callback)));
}

void ProfileCoreApiFacade::OnSubmitted(SubmissionCallback callback,
                                       service::AdmissionPtr admission) {
  const SubmissionStatus status = admission
                                      ? ProjectAdmission(admission->status)
                                      : SubmissionStatus::kCoreUnavailable;
  // The one place every admitted-or-not answer passes on its way to a
  // surface. The status is a closed enumeration and names no content.
  if (status != SubmissionStatus::kAccepted) {
    LOG(WARNING) << "[taffy_core_api_refused] at=admission status="
                 << static_cast<int>(status);
  }
  std::move(callback).Run(status);
}

CoreApiCommandFactory ProfileCoreApiFacade::NewFactory() const {
  return CoreApiCommandFactory(manager_ ? manager_->browser_profile_id()
                                        : std::string(),
                               CreateCoreApiEntropySource());
}

ProfileCoreApiFacade::SubmissionStatus
ProfileCoreApiFacade::UnavailableOrStaleRevision() const {
  return !manager_ || manager_->availability() !=
                          CoreServiceManager::Availability::kReady
             ? SubmissionStatus::kCoreUnavailable
             : SubmissionStatus::kStaleRevision;
}

void ProfileCoreApiFacade::SetAssistantConfiguration(
    uint64_t expected_revision,
    const std::vector<api::AssistantAbilityView>& disabled_abilities,
    api::PersonalityPresetView preset,
    uint32_t pace,
    uint32_t length,
    uint32_t check_in,
    SetAssistantConfigurationCallback callback) {
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(factory.BuildSetAssistantConfiguration(
                      expected_revision, disabled_abilities, preset,
                      pace, length, check_in,
                      manager_ ? manager_->service_generation() : 0u,
                      NowMonotonicMillis()),
                  std::move(callback));
}

void ProfileCoreApiFacade::MutateSiteSkill(
    api::SiteSkillMutationKind kind,
    const std::string& skill_id,
    uint32_t expected_version,
    const std::string& origin,
    std::vector<api::SiteSkillObservedClausePtr> clauses,
    std::vector<api::SiteSkillObservedStepPtr> steps,
    uint32_t admitted,
    bool enabled,
    MutateSiteSkillCallback callback) {
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(
      factory.BuildMutateSiteSkill(
          kind, skill_id, expected_version, origin, std::move(clauses),
          std::move(steps), admitted, enabled, NowUtcMillis(),
          manager_ ? manager_->service_generation() : 0u,
          NowMonotonicMillis()),
      std::move(callback));
}

} // namespace taffy
