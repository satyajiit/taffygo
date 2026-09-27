// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_SAVED_DATA_PROFILE_SAVED_DATA_BROKER_H_
#define TAFFY_BROWSER_SAVED_DATA_PROFILE_SAVED_DATA_BROKER_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom-forward.h"

class Profile;
class KeyedServiceBaseFactory;

namespace taffy {

// The sole adapter from Chromium's password/address stores to bounded Taffy
// projections. Secret credential material has no method, field, or callback
// on this interface.
class ProfileSavedDataBroker {
 public:
  struct SavedDetailInput {
    std::string given_name;
    std::string family_name;
    std::string email;
    std::string phone;
    std::string address;
    std::string postcode;
    std::string country;
  };

  enum class MutationStatus {
    kAccepted,
    kStaleRevision,
    kInvalidRequest,
    kUnavailable,
  };

  using SnapshotCallback = base::RepeatingCallback<void(
      core_service::mojom::ReplaceSavedDataSnapshotCommandPtr)>;
  using MutationCallback = base::OnceCallback<void(MutationStatus)>;

  virtual ~ProfileSavedDataBroker() = default;

  ProfileSavedDataBroker(const ProfileSavedDataBroker&) = delete;
  ProfileSavedDataBroker& operator=(const ProfileSavedDataBroker&) = delete;

  // Returns null for private profiles or unavailable Chromium stores.
  static std::unique_ptr<ProfileSavedDataBroker> Create(Profile* profile);

  // The product adapter names the Chromium stores whose lifetime must precede
  // this broker. Standalone test binaries install an empty, unavailable
  // adapter instead of linking Chrome's full profile composition.
  static std::vector<KeyedServiceBaseFactory*> GetFactoryDependencies();

  // Starts store observation and publishes loading followed by complete
  // snapshots. Must be called exactly once.
  virtual void Start(SnapshotCallback callback) = 0;

  virtual void UpsertDetail(uint64_t expected_revision,
                            const std::optional<std::string>& detail_id,
                            SavedDetailInput input,
                            MutationCallback callback) = 0;
  virtual void DeleteDetail(uint64_t expected_revision,
                            const std::string& detail_id,
                            MutationCallback callback) = 0;
  virtual void DeleteSignIn(uint64_t expected_revision,
                            const std::string& sign_in_id,
                            MutationCallback callback) = 0;

 protected:
  ProfileSavedDataBroker() = default;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_SAVED_DATA_PROFILE_SAVED_DATA_BROKER_H_
