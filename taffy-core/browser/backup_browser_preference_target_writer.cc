// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/backup_browser_target_writers.h"

#include <array>
#include <optional>
#include <string_view>
#include <utility>

#include "base/values.h"
#include "components/prefs/pref_service.h"
#include "taffy/browser/backup_browser_record_adapters.h"
#include "taffy/browser/profile_preferences.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {
namespace {

using storage::backup::BackupBrowserPreferenceRecord;
using storage::backup::BackupSnapshotError;

struct PreferenceShape {
  std::string_view name;
  base::Value::Type type;
};

constexpr std::array kPreferenceShapes = {
    PreferenceShape{profile_preferences::kTheme, base::Value::Type::STRING},
    PreferenceShape{profile_preferences::kAppLanguage,
                    base::Value::Type::STRING},
    PreferenceShape{profile_preferences::kRegionCode,
                    base::Value::Type::STRING},
    PreferenceShape{profile_preferences::kForceDarkWeb,
                    base::Value::Type::BOOLEAN}};

std::optional<BackupSnapshotError> PreferenceBoundaryError(
    const PrefService* preferences,
    bool require_pristine) {
  if (!preferences) {
    return BackupSnapshotError::kUnavailable;
  }
  const auto status = preferences->GetInitializationStatus();
  if (status == PrefService::INITIALIZATION_STATUS_WAITING ||
      status == PrefService::INITIALIZATION_STATUS_ERROR) {
    return BackupSnapshotError::kUnavailable;
  }
  if (preferences->ReadOnly()) {
    return BackupSnapshotError::kUnsupportedSelection;
  }
  for (const auto& shape : kPreferenceShapes) {
    const auto* preference = preferences->FindPreference(shape.name);
    if (!preference || preference->GetType() != shape.type) {
      return BackupSnapshotError::kInvalidRecord;
    }
    if (!preference->IsUserModifiable() || preference->IsManaged() ||
        preference->IsManagedByCustodian() ||
        preference->HasExtensionSetting() ||
        preference->GetRecommendedValue()) {
      return BackupSnapshotError::kUnsupportedSelection;
    }
    if (require_pristine &&
        (preference->HasUserSetting() || !preference->IsDefaultValue())) {
      return BackupSnapshotError::kUnsupportedSelection;
    }
  }
  return std::nullopt;
}

BackupBrowserPreferenceTargetReadback DecodePreferenceSnapshot(
    storage::backup::BackupSnapshotResult snapshot) {
  using core_service::mojom::BackupRecordKind;
  using storage::backup::DecodeBrowserPreferenceRecordV1;
  using storage::backup::kBrowserPreferenceBackupStableId;
  if (!snapshot) {
    return base::unexpected(snapshot.error());
  }
  if (snapshot->size() != 1u || !snapshot->front().descriptor ||
      snapshot->front().descriptor->kind !=
          BackupRecordKind::kBrowserPreference ||
      snapshot->front().descriptor->stable_id !=
          kBrowserPreferenceBackupStableId) {
    return base::unexpected(BackupSnapshotError::kInvalidRecord);
  }
  const auto& item = snapshot->front();
  auto decoded = DecodeBrowserPreferenceRecordV1(
      item.plaintext, item.descriptor->stable_id, item.descriptor->revision);
  if (!decoded) {
    return base::unexpected(BackupSnapshotError::kInvalidRecord);
  }
  return std::move(*decoded);
}

}  // namespace

ChromiumBackupBrowserPreferenceTargetWriter::
    ChromiumBackupBrowserPreferenceTargetWriter(PrefService* preferences)
    : preferences_(preferences) {}

ChromiumBackupBrowserPreferenceTargetWriter::
    ~ChromiumBackupBrowserPreferenceTargetWriter() = default;

BackupBrowserPreferenceTargetReadback
ChromiumBackupBrowserPreferenceTargetWriter::ReadBack() const {
  if (auto error = PreferenceBoundaryError(preferences_, false)) {
    return base::unexpected(*error);
  }
  return DecodePreferenceSnapshot(
      ReadBrowserPreferenceBackupRecords(preferences_));
}

BackupBrowserTargetWriteResult
ChromiumBackupBrowserPreferenceTargetWriter::WriteAndReadBack(
    const BackupBrowserPreferenceRecord& record) {
  if (PreferenceBoundaryError(preferences_, true) ||
      !ValidateBrowserPreferenceBackupRecord(record)) {
    return BackupBrowserTargetWriteResult::kRefusedBeforeMutation;
  }

  namespace prefs = profile_preferences;
  preferences_->SetString(prefs::kTheme, record.theme);
  preferences_->SetString(prefs::kAppLanguage, record.app_language);
  preferences_->SetString(prefs::kRegionCode, record.region_code);
  preferences_->SetBoolean(prefs::kForceDarkWeb, record.force_dark_web);

  auto observed = ReadBack();
  if (!observed || *observed != record) {
    return BackupBrowserTargetWriteResult::kOutcomeUnknown;
  }
  return BackupBrowserTargetWriteResult::kAppliedAndReadBack;
}

}  // namespace taffy
