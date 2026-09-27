// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "components/prefs/pref_service.h"
#include "taffy/browser/application_preferences.h"
#include "taffy/browser/backup_restore_recovery_journal_internal.h"
#include "taffy/browser/backup_restore_recovery_presentation.h"

namespace taffy::backup_restore_recovery_journal_internal {

namespace wire = core_service::mojom;
using Error = BackupRestoreProfileRegistryError;

base::expected<void, Error> Append(
    PrefService* local_state,
    const std::string& reservation_id,
    const wire::BackupRestoreRecoveryRecord& record) {
  auto history = ReadBackupRestoreRecoveryJournal(local_state, reservation_id);
  if (!history) {
    return base::unexpected(history.error());
  }
  if (history->size() >= wire::kMaxBackupRestoreRecoveryRecords ||
      record.sequence != history->size() + 1u) {
    return base::unexpected(Error::kBusy);
  }
  if (history->empty()) {
    auto presentation =
        ReadBackupRestoreRecoveryPresentation(local_state, reservation_id);
    if (presentation &&
        (!record.binding ||
         !backup_restore_recovery_presentation_internal::MatchesRecoveryBinding(
             *presentation, reservation_id, *record.binding))) {
      return base::unexpected(Error::kInvalidArgument);
    }
    // kNotFound is the conservative legacy-v1 shape. Every other read error
    // must fail before Local State is dirtied.
    if (!presentation && presentation.error() != Error::kNotFound) {
      return base::unexpected(presentation.error());
    }
  }
  const auto* preference = local_state->FindPreference(
      application_preferences::kBackupRestoreProfileReservations);
  if (!preference || !preference->IsUserModifiable()) {
    return base::unexpected(Error::kUnavailable);
  }
  auto encoded = EncodeBackupRestoreRecoveryRecord(record);
  if (!encoded) {
    return base::unexpected(encoded.error());
  }
  auto registry =
      local_state
          ->GetDict(application_preferences::kBackupRestoreProfileReservations)
          .Clone();
  auto* entry = registry.FindDict(reservation_id);
  if (!entry) {
    return base::unexpected(Error::kNotFound);
  }
  auto* stored = entry->FindList(kBackupRestoreRecoveryJournalKey);
  base::ListValue records = stored ? stored->Clone() : base::ListValue();
  records.Append(std::move(*encoded));
  const std::string* target = entry->FindString("target_profile_id");
  base::Value journal(std::move(records));
  if (!target ||
      !DecodeBackupRestoreRecoveryJournal(journal, reservation_id, *target)) {
    return base::unexpected(Error::kInvalidArgument);
  }
  entry->Set(kBackupRestoreRecoveryJournalKey, std::move(journal));
  local_state->SetDict(
      application_preferences::kBackupRestoreProfileReservations,
      std::move(registry));
  return base::ok();
}

bool IsExactHistory(const BackupRestoreRecoveryRecords& left,
                    const BackupRestoreRecoveryRecords& right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (size_t i = 0u; i < left.size(); ++i) {
    if (!left[i] || !right[i]) {
      return false;
    }
    auto encoded_left = EncodeBackupRestoreRecoveryRecord(*left[i]);
    auto encoded_right = EncodeBackupRestoreRecoveryRecord(*right[i]);
    if (!encoded_left || !encoded_right || *encoded_left != *encoded_right) {
      return false;
    }
  }
  return true;
}

}  // namespace taffy::backup_restore_recovery_journal_internal
