// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/saved_data/profile_saved_data_validation.h"

#include <algorithm>
#include <string_view>

#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "components/autofill/core/browser/data_model/addresses/autofill_profile.h"
#include "components/autofill/core/browser/field_types.h"
#include "components/password_manager/core/browser/ui/credential_ui_entry.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "url/gurl.h"

namespace taffy::saved_data_internal {
namespace {

namespace mojom = core_service::mojom;

bool ValidText(const std::string& value, size_t max_bytes, bool allow_newline) {
  if (value.size() > max_bytes || !base::IsStringUTF8(value)) {
    return false;
  }
  return std::ranges::all_of(value, [allow_newline](unsigned char character) {
    return character >= 0x20 ||
           (allow_newline && (character == '\n' || character == '\r'));
  });
}

}  // namespace

std::string NewOpaqueId(std::string_view prefix) {
  const base::Uuid uuid = base::Uuid::GenerateRandomV4();
  return uuid.is_valid() ? std::string(prefix) + uuid.AsLowercaseString()
                         : std::string();
}

std::string CredentialKey(
    const password_manager::CredentialUIEntry& credential) {
  std::string key = credential.GetFirstSignonRealm();
  key.push_back('\0');
  key.append(base::UTF16ToUTF8(credential.username));
  key.push_back('\0');
  for (password_manager::PasswordForm::Store store : credential.stored_in) {
    key.push_back(static_cast<char>(store));
  }
  return key;
}

std::string CredentialSite(
    const password_manager::CredentialUIEntry& credential) {
  GURL url = credential.GetURL();
  if (url.host().empty() && !credential.GetAffiliatedWebRealm().empty()) {
    url = GURL(credential.GetAffiliatedWebRealm());
  }
  return url.is_valid() ? std::string(url.host()) : std::string();
}

uint64_t LastUsedMillis(base::Time time) {
  if (time.is_null()) {
    return 0;
  }
  const int64_t value = time.InMillisecondsFSinceUnixEpoch();
  return value > 0 ? static_cast<uint64_t>(value) : 0;
}

bool IsEditableProfile(const autofill::AutofillProfile& profile) {
  return profile.record_type() ==
             autofill::AutofillProfile::RecordType::kLocalOrSyncable ||
         profile.record_type() ==
             autofill::AutofillProfile::RecordType::kAccount;
}

bool IsValidSignIn(const std::string& site, const std::string& username) {
  return !site.empty() &&
         ValidText(site, mojom::kMaxSavedSignInSiteBytes, false) &&
         ValidText(username, mojom::kMaxSavedSignInUsernameBytes, false);
}

bool IsValidDetail(const ProfileSavedDataBroker::SavedDetailInput& input) {
  const bool any_value = !input.given_name.empty() ||
                         !input.family_name.empty() || !input.email.empty() ||
                         !input.phone.empty() || !input.address.empty() ||
                         !input.postcode.empty() || !input.country.empty();
  return any_value &&
         ValidText(input.given_name, mojom::kMaxSavedDetailNameBytes, false) &&
         ValidText(input.family_name, mojom::kMaxSavedDetailNameBytes, false) &&
         ValidText(input.email, mojom::kMaxSavedDetailEmailBytes, false) &&
         ValidText(input.phone, mojom::kMaxSavedDetailPhoneBytes, false) &&
         ValidText(input.address, mojom::kMaxSavedDetailAddressBytes, true) &&
         ValidText(input.postcode, mojom::kMaxSavedDetailPostcodeBytes,
                   false) &&
         ValidText(input.country, mojom::kMaxSavedDetailCountryBytes, false);
}

ProfileSavedDataBroker::SavedDetailInput ReadDetail(
    const autofill::AutofillProfile& profile) {
  return {
      .given_name = base::UTF16ToUTF8(profile.GetRawInfo(autofill::NAME_FIRST)),
      .family_name = base::UTF16ToUTF8(profile.GetRawInfo(autofill::NAME_LAST)),
      .email = base::UTF16ToUTF8(profile.GetRawInfo(autofill::EMAIL_ADDRESS)),
      .phone = base::UTF16ToUTF8(
          profile.GetRawInfo(autofill::PHONE_HOME_WHOLE_NUMBER)),
      .address = base::UTF16ToUTF8(
          profile.GetRawInfo(autofill::ADDRESS_HOME_STREET_ADDRESS)),
      .postcode =
          base::UTF16ToUTF8(profile.GetRawInfo(autofill::ADDRESS_HOME_ZIP)),
      .country =
          base::UTF16ToUTF8(profile.GetRawInfo(autofill::ADDRESS_HOME_COUNTRY)),
  };
}

void ApplyDetail(const ProfileSavedDataBroker::SavedDetailInput& input,
                 autofill::AutofillProfile* profile) {
  profile->SetRawInfo(autofill::NAME_FIRST,
                      base::UTF8ToUTF16(input.given_name));
  profile->SetRawInfo(autofill::NAME_LAST,
                      base::UTF8ToUTF16(input.family_name));
  profile->SetRawInfo(autofill::EMAIL_ADDRESS, base::UTF8ToUTF16(input.email));
  profile->SetRawInfo(autofill::PHONE_HOME_WHOLE_NUMBER,
                      base::UTF8ToUTF16(input.phone));
  profile->SetRawInfo(autofill::ADDRESS_HOME_STREET_ADDRESS,
                      base::UTF8ToUTF16(input.address));
  profile->SetRawInfo(autofill::ADDRESS_HOME_ZIP,
                      base::UTF8ToUTF16(input.postcode));
  profile->SetRawInfo(autofill::ADDRESS_HOME_COUNTRY,
                      base::UTF8ToUTF16(input.country));
}

mojom::SavedDetailRecordPtr ProjectDetail(
    const std::string& opaque_id,
    const ProfileSavedDataBroker::SavedDetailInput& input) {
  return mojom::SavedDetailRecord::New(
      opaque_id, input.given_name, input.family_name, input.email, input.phone,
      input.address, input.postcode, input.country);
}

}  // namespace taffy::saved_data_internal
