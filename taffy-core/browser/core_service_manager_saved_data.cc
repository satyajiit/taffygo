// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <limits>
#include <utility>

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/saved_data/core_saved_data_actions.h"
#include "taffy/browser/saved_data/profile_saved_data_broker.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;
namespace service = core_service::mojom;

uint64_t SavedDataNowMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

uint64_t SavedDataDeadline(uint64_t now) {
  constexpr uint64_t kLifetimeMillis = 30'000u;
  return now > std::numeric_limits<uint64_t>::max() - kLifetimeMillis
             ? std::numeric_limits<uint64_t>::max()
             : now + kLifetimeMillis;
}

api::CoreApiSubmissionStatus ProjectMutationStatus(
    ProfileSavedDataBroker::MutationStatus status) {
  switch (status) {
    case ProfileSavedDataBroker::MutationStatus::kAccepted:
      return api::CoreApiSubmissionStatus::kAccepted;
    case ProfileSavedDataBroker::MutationStatus::kStaleRevision:
      return api::CoreApiSubmissionStatus::kStaleRevision;
    case ProfileSavedDataBroker::MutationStatus::kInvalidRequest:
      return api::CoreApiSubmissionStatus::kInvalidRequest;
    case ProfileSavedDataBroker::MutationStatus::kUnavailable:
      return api::CoreApiSubmissionStatus::kCoreUnavailable;
  }
  return api::CoreApiSubmissionStatus::kInvalidRequest;
}

service::ReplaceSavedDataSnapshotCommandPtr UnavailableSnapshot() {
  auto snapshot = service::ReplaceSavedDataSnapshotCommand::New();
  snapshot->sign_ins_availability =
      service::SavedDataAvailability::kUnavailable;
  snapshot->sign_ins_revision = 0;
  snapshot->details_availability = service::SavedDataAvailability::kUnavailable;
  snapshot->details_revision = 0;
  return snapshot;
}

}  // namespace

void CoreServiceManager::InitializeSavedDataBroker() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (saved_data_broker_) {
    saved_data_broker_->Start(base::BindRepeating(
        &CoreServiceManager::OnSavedDataSnapshot, weak_factory_.GetWeakPtr()));
  } else if (!private_profile_) {
    OnSavedDataSnapshot(UnavailableSnapshot());
  }
}

void CoreServiceManager::OnSavedDataSnapshot(
    service::ReplaceSavedDataSnapshotCommandPtr snapshot) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutdown_started_ || !snapshot) {
    return;
  }
  latest_saved_data_snapshot_ = std::move(snapshot);
  if (availability_ == Availability::kReady) {
    ReplaySavedDataSnapshot();
  }
}

void CoreServiceManager::ReplaySavedDataSnapshot() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutdown_started_ || availability_ != Availability::kReady ||
      !latest_saved_data_snapshot_) {
    return;
  }
  const base::Uuid operation_uuid = base::Uuid::GenerateRandomV4();
  const base::Uuid idempotency_uuid = base::Uuid::GenerateRandomV4();
  if (!operation_uuid.is_valid() || !idempotency_uuid.is_valid()) {
    return;
  }
  const uint64_t now = SavedDataNowMillis();
  auto command = service::CoreServiceCommand::New();
  command->operation = service::OperationEnvelope::New(
      "saved-data-" + operation_uuid.AsLowercaseString(), service_generation_,
      0u, SavedDataDeadline(now),
      "saved-data-key-" + idempotency_uuid.AsLowercaseString());
  command->kind = service::CoreServiceCommandKind::kReplaceSavedDataSnapshot;
  command->replace_saved_data_snapshot = latest_saved_data_snapshot_.Clone();
  Submit(std::move(command), base::BindOnce([](service::AdmissionPtr) {}));
}

void CoreSavedDataActions::UpsertDetail(
    CoreServiceManager* manager,
    uint64_t expected_revision,
    const std::optional<std::string>& detail_id,
    std::string given_name,
    std::string family_name,
    std::string email,
    std::string phone,
    std::string address,
    std::string postcode,
    std::string country,
    base::OnceCallback<void(api::CoreApiSubmissionStatus)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager->sequence_checker_);
  if (manager->shutdown_started_ || !manager->saved_data_broker_) {
    std::move(callback).Run(api::CoreApiSubmissionStatus::kCoreUnavailable);
    return;
  }
  manager->saved_data_broker_->UpsertDetail(
      expected_revision, detail_id,
      ProfileSavedDataBroker::SavedDetailInput{
          .given_name = std::move(given_name),
          .family_name = std::move(family_name),
          .email = std::move(email),
          .phone = std::move(phone),
          .address = std::move(address),
          .postcode = std::move(postcode),
          .country = std::move(country),
      },
      base::BindOnce(
          [](base::OnceCallback<void(api::CoreApiSubmissionStatus)> reply,
             ProfileSavedDataBroker::MutationStatus status) {
            std::move(reply).Run(ProjectMutationStatus(status));
          },
          std::move(callback)));
}

void CoreSavedDataActions::DeleteDetail(
    CoreServiceManager* manager,
    const std::string& detail_id,
    uint64_t expected_revision,
    base::OnceCallback<void(api::CoreApiSubmissionStatus)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager->sequence_checker_);
  if (manager->shutdown_started_ || !manager->saved_data_broker_) {
    std::move(callback).Run(api::CoreApiSubmissionStatus::kCoreUnavailable);
    return;
  }
  manager->saved_data_broker_->DeleteDetail(
      expected_revision, detail_id,
      base::BindOnce(
          [](base::OnceCallback<void(api::CoreApiSubmissionStatus)> reply,
             ProfileSavedDataBroker::MutationStatus status) {
            std::move(reply).Run(ProjectMutationStatus(status));
          },
          std::move(callback)));
}

void CoreSavedDataActions::DeleteSignIn(
    CoreServiceManager* manager,
    const std::string& sign_in_id,
    uint64_t expected_revision,
    base::OnceCallback<void(api::CoreApiSubmissionStatus)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager->sequence_checker_);
  if (manager->shutdown_started_ || !manager->saved_data_broker_) {
    std::move(callback).Run(api::CoreApiSubmissionStatus::kCoreUnavailable);
    return;
  }
  manager->saved_data_broker_->DeleteSignIn(
      expected_revision, sign_in_id,
      base::BindOnce(
          [](base::OnceCallback<void(api::CoreApiSubmissionStatus)> reply,
             ProfileSavedDataBroker::MutationStatus status) {
            std::move(reply).Run(ProjectMutationStatus(status));
          },
          std::move(callback)));
}

}  // namespace taffy
