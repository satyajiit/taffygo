// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_STORAGE_BROWSER_BACKUP_RECOVERY_KEY_CODEC_H_
#define TAFFY_COMPONENTS_STORAGE_BROWSER_BACKUP_RECOVERY_KEY_CODEC_H_

#include <array>
#include <optional>

#include "base/containers/span.h"
#include "taffy/components/storage/browser/encrypted_backup_crypto.h"

namespace taffy::storage::backup {

// Full 256-bit key, not a password: TAFFY1- then eight eight-digit hex groups.
// Fixed punctuation/version; hex digits may be entered in either ASCII case.
// There is deliberately no whitespace folding, Unicode normalization, lossy
// truncation or account credential fallback. Archive authentication remains
// the proof of a correct key; this codec only checks its representation.
inline constexpr size_t kRecoveryKeyTextChars = 78;
using RecoveryKeyText = std::array<char16_t, kRecoveryKeyTextChars>;

// Sensitive output belongs only to the trusted, non-persisted Android key
// surface. Callers wipe both the text and decoded Secret after use. Neither
// value is suitable for a Core API/Core Service record, log or saved UI state.
std::optional<RecoveryKeyText> FormatRecoveryKeyForDisplay(
    base::span<const uint8_t> key);
std::optional<Secret> ParseRecoveryKeyFromInput(
    base::span<const char16_t> text);

}  // namespace taffy::storage::backup

#endif  // TAFFY_COMPONENTS_STORAGE_BROWSER_BACKUP_RECOVERY_KEY_CODEC_H_
