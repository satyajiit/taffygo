// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_SAVED_DATA_PROFILE_SAVED_DATA_VALIDATION_H_
#define TAFFY_BROWSER_SAVED_DATA_PROFILE_SAVED_DATA_VALIDATION_H_

#include <stdint.h>

#include <string>
#include <string_view>

#include "base/time/time.h"
#include "taffy/browser/saved_data/profile_saved_data_broker.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom-forward.h"

namespace autofill {
class AutofillProfile;
}

namespace password_manager {
struct CredentialUIEntry;
}

namespace taffy::saved_data_internal {

std::string NewOpaqueId(std::string_view prefix);
std::string CredentialKey(
    const password_manager::CredentialUIEntry& credential);
std::string CredentialSite(
    const password_manager::CredentialUIEntry& credential);
uint64_t LastUsedMillis(base::Time time);

bool IsEditableProfile(const autofill::AutofillProfile& profile);
bool IsValidSignIn(const std::string& site, const std::string& username);
bool IsValidDetail(const ProfileSavedDataBroker::SavedDetailInput& input);

ProfileSavedDataBroker::SavedDetailInput ReadDetail(
    const autofill::AutofillProfile& profile);
void ApplyDetail(const ProfileSavedDataBroker::SavedDetailInput& input,
                 autofill::AutofillProfile* profile);

core_service::mojom::SavedDetailRecordPtr ProjectDetail(
    const std::string& opaque_id,
    const ProfileSavedDataBroker::SavedDetailInput& input);

}  // namespace taffy::saved_data_internal

#endif  // TAFFY_BROWSER_SAVED_DATA_PROFILE_SAVED_DATA_VALIDATION_H_
