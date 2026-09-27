// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "taffy/browser/core_api/profile_core_api_facade.h"
#include "taffy/browser/saved_data/core_saved_data_actions.h"

namespace taffy {

void ProfileCoreApiFacade::UpsertSavedDetail(
    uint64_t expected_revision,
    const std::optional<std::string>& detail_id,
    const std::string& given_name,
    const std::string& family_name,
    const std::string& email,
    const std::string& phone,
    const std::string& address,
    const std::string& postcode,
    const std::string& country,
    UpsertSavedDetailCallback callback) {
  if (!manager_) {
    std::move(callback).Run(SubmissionStatus::kCoreUnavailable);
    return;
  }
  CoreSavedDataActions::UpsertDetail(
      manager_, expected_revision, detail_id, given_name, family_name, email,
      phone, address, postcode, country, std::move(callback));
}

void ProfileCoreApiFacade::DeleteSavedDetail(
    const std::string& detail_id,
    uint64_t expected_revision,
    DeleteSavedDetailCallback callback) {
  if (!manager_) {
    std::move(callback).Run(SubmissionStatus::kCoreUnavailable);
    return;
  }
  CoreSavedDataActions::DeleteDetail(manager_, detail_id, expected_revision,
                                     std::move(callback));
}

void ProfileCoreApiFacade::DeleteSavedSignIn(
    const std::string& sign_in_id,
    uint64_t expected_revision,
    DeleteSavedSignInCallback callback) {
  if (!manager_) {
    std::move(callback).Run(SubmissionStatus::kCoreUnavailable);
    return;
  }
  CoreSavedDataActions::DeleteSignIn(manager_, sign_in_id, expected_revision,
                                     std::move(callback));
}

}  // namespace taffy
