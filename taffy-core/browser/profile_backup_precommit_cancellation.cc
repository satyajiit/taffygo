// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/profile_backup_precommit_cancellation.h"

#include <utility>

namespace taffy {

ProfileBackupPrecommitCancellationReceipt::
    ProfileBackupPrecommitCancellationReceipt(
        std::string operation_id,
        std::string source_profile_id,
        std::string target_profile_id,
        core_service::mojom::BackupRestoreBindingPtr binding)
    : valid_(!operation_id.empty() && !source_profile_id.empty() &&
             !target_profile_id.empty() &&
             (!binding ||
              (binding->owner_profile_id == source_profile_id &&
               binding->target &&
               binding->target->profile_id == target_profile_id))),
      operation_id_(std::move(operation_id)),
      source_profile_id_(std::move(source_profile_id)),
      target_profile_id_(std::move(target_profile_id)),
      binding_(std::move(binding)) {}

ProfileBackupPrecommitCancellationReceipt::
    ProfileBackupPrecommitCancellationReceipt(
        ProfileBackupPrecommitCancellationReceipt&& other) noexcept
    : valid_(other.valid_),
      operation_id_(std::move(other.operation_id_)),
      source_profile_id_(std::move(other.source_profile_id_)),
      target_profile_id_(std::move(other.target_profile_id_)),
      binding_(std::move(other.binding_)) {
  other.Invalidate();
}

ProfileBackupPrecommitCancellationReceipt&
ProfileBackupPrecommitCancellationReceipt::operator=(
    ProfileBackupPrecommitCancellationReceipt&& other) noexcept {
  if (this != &other) {
    valid_ = other.valid_;
    operation_id_ = std::move(other.operation_id_);
    source_profile_id_ = std::move(other.source_profile_id_);
    target_profile_id_ = std::move(other.target_profile_id_);
    binding_ = std::move(other.binding_);
    other.Invalidate();
  }
  return *this;
}

ProfileBackupPrecommitCancellationReceipt::~
    ProfileBackupPrecommitCancellationReceipt() = default;

void ProfileBackupPrecommitCancellationReceipt::Invalidate() {
  valid_ = false;
  operation_id_.clear();
  source_profile_id_.clear();
  target_profile_id_.clear();
  binding_.reset();
}

}  // namespace taffy
