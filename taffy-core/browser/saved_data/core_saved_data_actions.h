// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_SAVED_DATA_CORE_SAVED_DATA_ACTIONS_H_
#define TAFFY_BROWSER_SAVED_DATA_CORE_SAVED_DATA_ACTIONS_H_

#include <stdint.h>

#include <optional>
#include <string>

#include "base/functional/callback_forward.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom-forward.h"

namespace taffy {

class CoreServiceManager;

// Narrow mutation surface for Chromium-owned saved personal data. Keeping it
// outside CoreServiceManager's broad protocol header prevents UI callers from
// depending on the manager's lifecycle machinery.
class CoreSavedDataActions {
 public:
  CoreSavedDataActions() = delete;

  static void UpsertDetail(
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
      base::OnceCallback<void(core_api::mojom::CoreApiSubmissionStatus)>
          callback);
  static void DeleteDetail(
      CoreServiceManager* manager,
      const std::string& detail_id,
      uint64_t expected_revision,
      base::OnceCallback<void(core_api::mojom::CoreApiSubmissionStatus)>
          callback);
  static void DeleteSignIn(
      CoreServiceManager* manager,
      const std::string& sign_in_id,
      uint64_t expected_revision,
      base::OnceCallback<void(core_api::mojom::CoreApiSubmissionStatus)>
          callback);
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_SAVED_DATA_CORE_SAVED_DATA_ACTIONS_H_
