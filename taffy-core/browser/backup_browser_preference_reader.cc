// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stddef.h>

#include <string_view>
#include <utility>

#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "base/values.h"
#include "components/prefs/pref_service.h"
#include "crypto/secure_util.h"
#include "taffy/browser/backup_browser_record_adapters.h"
#include "taffy/browser/backup_browser_record_adapters_internal.h"
#include "taffy/browser/profile_preferences.h"
#include "third_party/icu/source/common/unicode/uloc.h"

namespace taffy {
namespace {

// Do not copy an administrator's or extension's effective setting as if the
// person chose it. The four literal callers below are the entire read
// allowlist.
//
// The value read here is the effective one, which is deliberate and is the
// only thing a reader can honestly obtain. A stored value whose type does not
// match the registration is discarded by PrefValueStore before any reader
// sees it, so the browser is already running on the registered default and so
// is the person looking at the screen; recording that default is reporting the
// choice in force, not substituting for one. The accessor that can see the
// discarded bytes, PrefService::GetUserPrefValue, is not an alternative: its
// wrong-type branch is DUMP_WILL_BE_NOTREACHED, and GetDumpSeverity answers
// LOGGING_FATAL outside an official build, so reaching it aborts the browser
// on every build this project produces rather than returning the null its
// signature promises. A profile whose Preferences file had one mistyped entry
// therefore crashed at backup time instead of being backed up.
base::expected<const base::Value*, storage::backup::BackupSnapshotError>
ReadChoice(const PrefService& preferences, std::string_view name) {
  using storage::backup::BackupSnapshotError;
  const auto* preference = preferences.FindPreference(name);
  if (!preference) {
    return base::unexpected(BackupSnapshotError::kInvalidRecord);
  }
  if (!preference->IsUserModifiable()) {
    return base::unexpected(BackupSnapshotError::kUnsupportedSelection);
  }
  const auto* value = preference->GetValue();
  if (!value) {
    return base::unexpected(BackupSnapshotError::kInvalidRecord);
  }
  return value;
}

}  // namespace

bool ValidateBrowserPreferenceBackupRecord(
    const storage::backup::BackupBrowserPreferenceRecord& record) {
  auto encoded = storage::backup::EncodeBrowserPreferenceRecordV1(record);
  if (!encoded) {
    return false;
  }
  crypto::SecureZeroBuffer(*encoded);
  // ICU answers a null-terminated vector of pointers and publishes no count
  // for it, so this terminator scan is the one place the C array becomes a
  // bounds-carrying span; the membership read after it is span-checked.
  const char* const* const codes = uloc_getISOCountries();
  size_t count = 0;
  while (UNSAFE_BUFFERS(codes[count]) != nullptr) {
    ++count;
  }
  const auto countries =
      UNSAFE_BUFFERS(base::span<const char* const>(codes, count));
  for (const char* country : countries) {
    if (record.region_code == country) {
      return true;
    }
  }
  return false;
}

storage::backup::BackupSnapshotResult ReadBrowserPreferenceBackupRecords(
    const PrefService* preferences) {
  using namespace storage::backup;
  namespace prefs = profile_preferences;
  if (!preferences) {
    return base::unexpected(BackupSnapshotError::kUnavailable);
  }
  const auto theme = ReadChoice(*preferences, prefs::kTheme);
  const auto language = ReadChoice(*preferences, prefs::kAppLanguage);
  const auto region = ReadChoice(*preferences, prefs::kRegionCode);
  const auto dark_web = ReadChoice(*preferences, prefs::kForceDarkWeb);
  for (const auto* choice : {&theme, &language, &region, &dark_web}) {
    if (!choice->has_value()) {
      return base::unexpected(choice->error());
    }
  }
  if (!(*theme)->is_string() || !(*language)->is_string() ||
      !(*region)->is_string() || !(*dark_web)->is_bool()) {
    return base::unexpected(BackupSnapshotError::kInvalidRecord);
  }
  BackupBrowserPreferenceRecord values{
      (*theme)->GetString(), (*language)->GetString(), (*region)->GetString(),
      (*dark_web)->GetBool()};
  if (!ValidateBrowserPreferenceBackupRecord(values)) {
    return base::unexpected(BackupSnapshotError::kInvalidRecord);
  }
  auto record = backup_browser_internal::MakeSnapshotRecord(
      core_service::mojom::BackupRecordKind::kBrowserPreference,
      kBrowserPreferenceBackupStableId,
      EncodeBrowserPreferenceRecordV1(values));
  if (!record) {
    return base::unexpected(record.error());
  }
  std::vector<BackupSnapshotRecord> result;
  result.push_back(std::move(*record));
  return result;
}

}  // namespace taffy
