// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/saved_data/profile_saved_data_broker.h"

#include <algorithm>
#include <limits>
#include <tuple>
#include <utility>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/affiliations/affiliation_service_factory.h"
#include "chrome/browser/autofill/personal_data_manager_factory.h"
#include "chrome/browser/password_manager/factories/account_password_store_factory.h"
#include "chrome/browser/password_manager/factories/profile_password_store_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/autofill/core/browser/data_manager/addresses/address_data_manager.h"
#include "components/autofill/core/browser/data_manager/personal_data_manager.h"
#include "components/autofill/core/browser/data_model/addresses/autofill_profile.h"
#include "components/keyed_service/core/keyed_service_base_factory.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/password_manager/core/browser/password_store/password_store_interface.h"
#include "components/password_manager/core/browser/ui/saved_passwords_presenter.h"
#include "mojo/public/cpp/bindings/clone_traits.h"
#include "taffy/browser/saved_data/profile_saved_data_validation.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
namespace internal = saved_data_internal;

class ProfileSavedDataBrokerImpl final
    : public ProfileSavedDataBroker,
      public password_manager::SavedPasswordsPresenter::Observer,
      public autofill::AddressDataManager::Observer {
 public:
  ProfileSavedDataBrokerImpl(
      autofill::PersonalDataManager* personal_data,
      affiliations::AffiliationService* affiliation_service,
      scoped_refptr<password_manager::PasswordStoreInterface> profile_store,
      scoped_refptr<password_manager::PasswordStoreInterface> account_store)
      : personal_data_(personal_data),
        presenter_(affiliation_service,
                   std::move(profile_store),
                   std::move(account_store)) {}

  ~ProfileSavedDataBrokerImpl() override {
    if (started_) {
      presenter_.RemoveObserver(this);
    }
    if (started_ && personal_data_) {
      personal_data_->address_data_manager().RemoveObserver(this);
    }
  }

  void Start(SnapshotCallback callback) override {
    if (!callback_.is_null()) {
      return;
    }
    started_ = true;
    callback_ = std::move(callback);
    presenter_.AddObserver(this);
    if (personal_data_) {
      personal_data_->address_data_manager().AddObserver(this);
      if (personal_data_->IsDataLoaded()) {
        RefreshDetails();
      }
    } else {
      MarkDetailsUnavailable();
    }
    presenter_.Init(
        base::BindOnce(&ProfileSavedDataBrokerImpl::OnPasswordsLoaded,
                       weak_factory_.GetWeakPtr()));
    Publish();
  }

  void UpsertDetail(uint64_t expected_revision,
                    const std::optional<std::string>& detail_id,
                    SavedDetailInput input,
                    MutationCallback callback) override {
    if (!personal_data_ || details_state_ != StoreState::kReady) {
      return FinishMutation(std::move(callback), MutationStatus::kUnavailable);
    }
    if (expected_revision != details_revision_) {
      return FinishMutation(std::move(callback),
                            MutationStatus::kStaleRevision);
    }
    if (!internal::IsValidDetail(input)) {
      return FinishMutation(std::move(callback),
                            MutationStatus::kInvalidRequest);
    }
    auto& manager = personal_data_->address_data_manager();
    if (detail_id) {
      const auto found = detail_ids_.find(*detail_id);
      if (found == detail_ids_.end()) {
        return FinishMutation(std::move(callback),
                              MutationStatus::kInvalidRequest);
      }
      const autofill::AutofillProfile* current =
          manager.GetProfileByGUID(found->second);
      if (!current || !internal::IsEditableProfile(*current)) {
        return FinishMutation(std::move(callback),
                              MutationStatus::kInvalidRequest);
      }
      autofill::AutofillProfile replacement(*current);
      internal::ApplyDetail(input, &replacement);
      manager.UpdateProfile(replacement);
    } else {
      autofill::AutofillProfile created(
          autofill::AutofillProfile::RecordType::kLocalOrSyncable,
          autofill::AddressCountryCode(input.country));
      internal::ApplyDetail(input, &created);
      manager.AddProfile(created);
    }
    details_state_ = StoreState::kLoading;
    Publish();
    FinishMutation(std::move(callback), MutationStatus::kAccepted);
  }

  void DeleteDetail(uint64_t expected_revision,
                    const std::string& detail_id,
                    MutationCallback callback) override {
    if (!personal_data_ || details_state_ != StoreState::kReady) {
      return FinishMutation(std::move(callback), MutationStatus::kUnavailable);
    }
    if (expected_revision != details_revision_) {
      return FinishMutation(std::move(callback),
                            MutationStatus::kStaleRevision);
    }
    const auto found = detail_ids_.find(detail_id);
    if (found == detail_ids_.end()) {
      return FinishMutation(std::move(callback),
                            MutationStatus::kInvalidRequest);
    }
    personal_data_->address_data_manager().RemoveProfile(found->second);
    details_state_ = StoreState::kLoading;
    Publish();
    FinishMutation(std::move(callback), MutationStatus::kAccepted);
  }

  void DeleteSignIn(uint64_t expected_revision,
                    const std::string& sign_in_id,
                    MutationCallback callback) override {
    if (sign_ins_state_ != StoreState::kReady) {
      return FinishMutation(std::move(callback), MutationStatus::kUnavailable);
    }
    if (expected_revision != sign_ins_revision_) {
      return FinishMutation(std::move(callback),
                            MutationStatus::kStaleRevision);
    }
    const auto found = sign_in_ids_.find(sign_in_id);
    if (found == sign_in_ids_.end()) {
      return FinishMutation(std::move(callback),
                            MutationStatus::kInvalidRequest);
    }
    for (const auto& credential : presenter_.GetSavedPasswords()) {
      if (internal::CredentialKey(credential) == found->second) {
        const bool removed = presenter_.RemoveCredential(credential);
        if (!removed) {
          return FinishMutation(std::move(callback),
                                MutationStatus::kInvalidRequest);
        }
        sign_ins_state_ = StoreState::kLoading;
        Publish();
        return FinishMutation(std::move(callback), MutationStatus::kAccepted);
      }
    }
    FinishMutation(std::move(callback), MutationStatus::kInvalidRequest);
  }

 private:
  enum class StoreState { kLoading, kReady, kUnavailable };

  void OnPasswordsLoaded() {
    RefreshSignIns();
    Publish();
  }

  void OnSavedPasswordsChanged(
      const password_manager::PasswordStoreChangeList&) override {
    if (!presenter_.IsWaitingForPasswordStore()) {
      RefreshSignIns();
      Publish();
    }
  }

  void OnAddressDataChanged() override {
    RefreshDetails();
    Publish();
  }

  void RefreshSignIns() {
    sign_ins_.clear();
    sign_in_ids_.clear();
    auto credentials = presenter_.GetSavedPasswords();
    if (credentials.size() > mojom::kMaxSavedSignIns ||
        sign_ins_revision_ == std::numeric_limits<uint64_t>::max()) {
      sign_ins_state_ = StoreState::kUnavailable;
      return;
    }
    ++sign_ins_revision_;
    base::flat_map<std::string, std::string> current_opaque_ids;
    for (const auto& credential : credentials) {
      const std::string site = internal::CredentialSite(credential);
      const std::string username = base::UTF16ToUTF8(credential.username);
      if (!internal::IsValidSignIn(site, username)) {
        MarkSignInsUnavailable();
        return;
      }
      const std::string key = internal::CredentialKey(credential);
      std::string opaque_id = credential_opaque_ids_[key];
      if (opaque_id.empty()) {
        opaque_id = internal::NewOpaqueId("saved-sign-in-");
      }
      if (opaque_id.empty()) {
        MarkSignInsUnavailable();
        return;
      }
      auto record = mojom::SavedSignInMetadata::New();
      record->id = opaque_id;
      record->site = site;
      record->username = username;
      record->last_used_epoch_ms =
          internal::LastUsedMillis(credential.last_used_time);
      current_opaque_ids.emplace(key, opaque_id);
      sign_in_ids_.emplace(opaque_id, key);
      sign_ins_.push_back(std::move(record));
    }
    credential_opaque_ids_ = std::move(current_opaque_ids);
    sign_ins_state_ = StoreState::kReady;
    std::ranges::sort(sign_ins_, {}, [](const auto& record) {
      return std::tie(record->site, record->username, record->id);
    });
  }

  void RefreshDetails() {
    details_.clear();
    detail_ids_.clear();
    if (!personal_data_ || !personal_data_->IsDataLoaded() ||
        details_revision_ == std::numeric_limits<uint64_t>::max()) {
      if (personal_data_ && !personal_data_->IsDataLoaded()) {
        details_state_ = StoreState::kLoading;
      } else {
        MarkDetailsUnavailable();
      }
      return;
    }
    const auto profiles =
        personal_data_->address_data_manager().GetProfilesForSettings();
    const size_t editable_count = std::ranges::count_if(
        profiles, [](const autofill::AutofillProfile* profile) {
          return profile && internal::IsEditableProfile(*profile);
        });
    if (editable_count > mojom::kMaxSavedDetails) {
      MarkDetailsUnavailable();
      return;
    }
    ++details_revision_;
    base::flat_map<std::string, std::string> current_opaque_ids;
    for (const autofill::AutofillProfile* profile : profiles) {
      if (!profile || !internal::IsEditableProfile(*profile)) {
        continue;
      }
      SavedDetailInput input = internal::ReadDetail(*profile);
      if (!internal::IsValidDetail(input)) {
        MarkDetailsUnavailable();
        return;
      }
      std::string opaque_id = detail_opaque_ids_[profile->guid()];
      if (opaque_id.empty()) {
        opaque_id = internal::NewOpaqueId("saved-detail-");
      }
      if (opaque_id.empty()) {
        MarkDetailsUnavailable();
        return;
      }
      current_opaque_ids.emplace(profile->guid(), opaque_id);
      detail_ids_.emplace(opaque_id, profile->guid());
      details_.push_back(internal::ProjectDetail(opaque_id, input));
    }
    detail_opaque_ids_ = std::move(current_opaque_ids);
    details_state_ = StoreState::kReady;
    std::ranges::sort(details_, {}, [](const auto& record) {
      return std::tie(record->family_name, record->given_name, record->id);
    });
  }

  void Publish() {
    if (callback_.is_null()) {
      return;
    }
    auto snapshot = mojom::ReplaceSavedDataSnapshotCommand::New();
    snapshot->sign_ins_availability = ProjectAvailability(sign_ins_state_);
    snapshot->sign_ins_revision =
        sign_ins_state_ == StoreState::kReady ? sign_ins_revision_ : 0;
    snapshot->sign_ins = sign_ins_state_ == StoreState::kReady
                             ? mojo::Clone(sign_ins_)
                             : std::vector<mojom::SavedSignInMetadataPtr>();
    snapshot->details_availability = ProjectAvailability(details_state_);
    snapshot->details_revision =
        details_state_ == StoreState::kReady ? details_revision_ : 0;
    snapshot->details = details_state_ == StoreState::kReady
                            ? mojo::Clone(details_)
                            : std::vector<mojom::SavedDetailRecordPtr>();
    callback_.Run(std::move(snapshot));
  }

  static mojom::SavedDataAvailability ProjectAvailability(StoreState state) {
    switch (state) {
      case StoreState::kLoading:
        return mojom::SavedDataAvailability::kLoading;
      case StoreState::kReady:
        return mojom::SavedDataAvailability::kReady;
      case StoreState::kUnavailable:
        return mojom::SavedDataAvailability::kUnavailable;
    }
    return mojom::SavedDataAvailability::kUnavailable;
  }

  void MarkSignInsUnavailable() {
    sign_ins_state_ = StoreState::kUnavailable;
    sign_ins_.clear();
    sign_in_ids_.clear();
    credential_opaque_ids_.clear();
  }

  void MarkDetailsUnavailable() {
    details_state_ = StoreState::kUnavailable;
    details_.clear();
    detail_ids_.clear();
    detail_opaque_ids_.clear();
  }

  static void FinishMutation(MutationCallback callback, MutationStatus status) {
    std::move(callback).Run(status);
  }

  const raw_ptr<autofill::PersonalDataManager> personal_data_;
  password_manager::SavedPasswordsPresenter presenter_;
  SnapshotCallback callback_;
  bool started_ = false;
  StoreState sign_ins_state_ = StoreState::kLoading;
  StoreState details_state_ = StoreState::kLoading;
  uint64_t sign_ins_revision_ = 0;
  uint64_t details_revision_ = 0;
  std::vector<mojom::SavedSignInMetadataPtr> sign_ins_;
  std::vector<mojom::SavedDetailRecordPtr> details_;
  base::flat_map<std::string, std::string> credential_opaque_ids_;
  base::flat_map<std::string, std::string> detail_opaque_ids_;
  base::flat_map<std::string, std::string> sign_in_ids_;
  base::flat_map<std::string, std::string> detail_ids_;
  base::WeakPtrFactory<ProfileSavedDataBrokerImpl> weak_factory_{this};
};

}  // namespace

// static
std::vector<KeyedServiceBaseFactory*>
ProfileSavedDataBroker::GetFactoryDependencies() {
  return {
      autofill::PersonalDataManagerFactory::GetInstance(),
      AffiliationServiceFactory::GetInstance(),
      ProfilePasswordStoreFactory::GetInstance(),
      AccountPasswordStoreFactory::GetInstance(),
  };
}

std::unique_ptr<ProfileSavedDataBroker> ProfileSavedDataBroker::Create(
    Profile* profile) {
  if (!profile || profile->IsOffTheRecord()) {
    return nullptr;
  }
  auto profile_store = ProfilePasswordStoreFactory::GetForProfile(
      profile, ServiceAccessType::EXPLICIT_ACCESS);
  if (!profile_store) {
    return nullptr;
  }
  return std::make_unique<ProfileSavedDataBrokerImpl>(
      autofill::PersonalDataManagerFactory::GetForBrowserContext(profile),
      AffiliationServiceFactory::GetForProfile(profile),
      std::move(profile_store),
      AccountPasswordStoreFactory::GetForProfile(
          profile, ServiceAccessType::EXPLICIT_ACCESS));
}

}  // namespace taffy
