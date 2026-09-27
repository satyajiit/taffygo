// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/backup_restore_profile_registry.h"

#include <string>

#include "base/json/values_util.h"
#include "base/values.h"
#include "components/prefs/testing_pref_service.h"
#include "taffy/browser/application_preferences.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

using Error = BackupRestoreProfileRegistryError;
using PhysicalState = BackupRestoreProfilePhysicalState;
using Quarantine = BackupRestoreProfileQuarantineStatus;

constexpr char kReservationId[] = "1c71e9e7-d965-4e7a-ae77-a1bb401fe0d2";
constexpr char kSecondReservationId[] = "52c50526-1105-470d-ac5f-059c10a9612b";
constexpr char kTargetProfileId[] = "9d146da0-9fe9-47cc-af6f-f2afe4e38ca1";
constexpr char kOtherTargetProfileId[] = "24b1327d-11ce-477f-acab-bae3ddf08917";

void RegisterApplicationPrefs(TestingPrefServiceSimple* prefs) {
  application_preferences::RegisterLocalStatePreferences(prefs->registry());
}

base::DictValue StoredEntry(std::string source,
                            std::string target,
                            std::string physical_state) {
  base::DictValue entry;
  entry.Set("version", 1);
  entry.Set("source_profile",
            base::FilePathToValue(base::FilePath(std::move(source))));
  entry.Set("target_profile",
            base::FilePathToValue(base::FilePath(std::move(target))));
  entry.Set("physical_state", std::move(physical_state));
  return entry;
}

base::DictValue StoredRegistry(std::string reservation_id,
                               base::DictValue entry) {
  base::DictValue registry;
  registry.Set(std::move(reservation_id), std::move(entry));
  return registry;
}

TEST(BackupRestoreProfileRegistryTest, RegisteredRegistryStartsEmpty) {
  TestingPrefServiceSimple local_state;
  RegisterApplicationPrefs(&local_state);

  auto reservations = ReadBackupRestoreProfileReservations(&local_state);

  ASSERT_TRUE(reservations.has_value());
  EXPECT_TRUE(reservations->empty());
}

TEST(BackupRestoreProfileRegistryTest,
     ReservationRoundTripsAndQuarantinesOnlyTarget) {
  TestingPrefServiceSimple local_state;
  RegisterApplicationPrefs(&local_state);

  auto reserved = ReserveBackupRestoreProfilePath(&local_state, kReservationId,
                                                  base::FilePath("Default"),
                                                  base::FilePath("Profile 2"));
  auto reservations = ReadBackupRestoreProfileReservations(&local_state);

  ASSERT_TRUE(reserved.has_value());
  ASSERT_TRUE(reservations.has_value());
  ASSERT_EQ(1u, reservations->size());
  EXPECT_EQ(kReservationId, reservations->front().reservation_id);
  EXPECT_EQ(base::FilePath("Default"),
            reservations->front().source_profile_base_name);
  EXPECT_EQ(base::FilePath("Profile 2"),
            reservations->front().target_profile_base_name);
  EXPECT_EQ(PhysicalState::kPathReserved, reservations->front().physical_state);
  EXPECT_FALSE(reservations->front().target_profile_id);
  const base::DictValue& stored = local_state.GetDict(
      application_preferences::kBackupRestoreProfileReservations);
  ASSERT_TRUE(stored.FindDict(kReservationId));
  EXPECT_EQ(1, stored.FindDict(kReservationId)->FindInt("version"));
  EXPECT_EQ(Quarantine::kQuarantined,
            BackupRestoreQuarantineForProfilePath(
                &local_state, base::FilePath("/profiles/Profile 2")));
  EXPECT_EQ(Quarantine::kNotQuarantined,
            BackupRestoreQuarantineForProfilePath(
                &local_state, base::FilePath("/profiles/Default")));
}

TEST(BackupRestoreProfileRegistryTest, OneInterruptedReservationOwnsCustody) {
  TestingPrefServiceSimple local_state;
  RegisterApplicationPrefs(&local_state);
  ASSERT_TRUE(ReserveBackupRestoreProfilePath(&local_state, kReservationId,
                                              base::FilePath("Default"),
                                              base::FilePath("Profile 2"))
                  .has_value());

  auto second = ReserveBackupRestoreProfilePath(
      &local_state, kSecondReservationId, base::FilePath("Default"),
      base::FilePath("Profile 3"));

  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(Error::kBusy, second.error());
  auto reservations = ReadBackupRestoreProfileReservations(&local_state);
  ASSERT_TRUE(reservations.has_value());
  ASSERT_EQ(1u, reservations->size());
  EXPECT_EQ(kReservationId, reservations->front().reservation_id);
}

TEST(BackupRestoreProfileRegistryTest, UnsafePhysicalIdentifiersAreRefused) {
  TestingPrefServiceSimple local_state;
  RegisterApplicationPrefs(&local_state);

  auto invalid_id = ReserveBackupRestoreProfilePath(
      &local_state, "not-a-uuid", base::FilePath("Default"),
      base::FilePath("Profile 2"));
  auto absolute = ReserveBackupRestoreProfilePath(
      &local_state, kReservationId, base::FilePath("Default"),
      base::FilePath("/profiles/Profile 2"));
  auto traversal = ReserveBackupRestoreProfilePath(
      &local_state, kReservationId, base::FilePath("Default"),
      base::FilePath("../Profile 2"));
  auto same = ReserveBackupRestoreProfilePath(&local_state, kReservationId,
                                              base::FilePath("Default"),
                                              base::FilePath("Default"));
  auto unrelated = ReserveBackupRestoreProfilePath(&local_state, kReservationId,
                                                   base::FilePath("Default"),
                                                   base::FilePath("Crashpad"));
  auto noncanonical = ReserveBackupRestoreProfilePath(
      &local_state, kReservationId, base::FilePath("Default"),
      base::FilePath("Profile 02"));
  auto control = ReserveBackupRestoreProfilePath(&local_state, kReservationId,
                                                 base::FilePath("Default"),
                                                 base::FilePath("Profile 2\n"));

  ASSERT_FALSE(invalid_id.has_value());
  ASSERT_FALSE(absolute.has_value());
  ASSERT_FALSE(traversal.has_value());
  ASSERT_FALSE(same.has_value());
  ASSERT_FALSE(unrelated.has_value());
  ASSERT_FALSE(noncanonical.has_value());
  ASSERT_FALSE(control.has_value());
  EXPECT_EQ(Error::kInvalidArgument, invalid_id.error());
  EXPECT_EQ(Error::kInvalidArgument, absolute.error());
  EXPECT_EQ(Error::kInvalidArgument, traversal.error());
  EXPECT_EQ(Error::kInvalidArgument, same.error());
  EXPECT_EQ(Error::kInvalidArgument, unrelated.error());
  EXPECT_EQ(Error::kInvalidArgument, noncanonical.error());
  EXPECT_EQ(Error::kInvalidArgument, control.error());
  auto reservations = ReadBackupRestoreProfileReservations(&local_state);
  ASSERT_TRUE(reservations.has_value());
  EXPECT_TRUE(reservations->empty());
}

TEST(BackupRestoreProfileRegistryTest,
     CreatedStateAndTargetIdentityAdvanceOnlyInOrder) {
  TestingPrefServiceSimple local_state;
  RegisterApplicationPrefs(&local_state);
  ASSERT_TRUE(ReserveBackupRestoreProfilePath(&local_state, kReservationId,
                                              base::FilePath("Default"),
                                              base::FilePath("Profile 2"))
                  .has_value());

  auto early_bind = BindBackupRestoreTargetProfileId(
      &local_state, kReservationId, kTargetProfileId);
  ASSERT_FALSE(early_bind.has_value());
  EXPECT_EQ(Error::kWrongPhysicalState, early_bind.error());

  EXPECT_TRUE(MarkBackupRestoreProfileCreated(&local_state, kReservationId)
                  .has_value());
  EXPECT_TRUE(MarkBackupRestoreProfileCreated(&local_state, kReservationId)
                  .has_value());
  EXPECT_TRUE(BindBackupRestoreTargetProfileId(&local_state, kReservationId,
                                               kTargetProfileId)
                  .has_value());
  EXPECT_TRUE(BindBackupRestoreTargetProfileId(&local_state, kReservationId,
                                               kTargetProfileId)
                  .has_value());

  auto rebind = BindBackupRestoreTargetProfileId(&local_state, kReservationId,
                                                 kOtherTargetProfileId);
  ASSERT_FALSE(rebind.has_value());
  EXPECT_EQ(Error::kWrongPhysicalState, rebind.error());

  auto reservations = ReadBackupRestoreProfileReservations(&local_state);
  ASSERT_TRUE(reservations.has_value());
  ASSERT_EQ(1u, reservations->size());
  EXPECT_EQ(PhysicalState::kProfileCreated,
            reservations->front().physical_state);
  EXPECT_EQ(kTargetProfileId, reservations->front().target_profile_id);
}

TEST(BackupRestoreProfileRegistryTest, UnknownReservationCannotAdvance) {
  TestingPrefServiceSimple local_state;
  RegisterApplicationPrefs(&local_state);

  auto created = MarkBackupRestoreProfileCreated(&local_state, kReservationId);
  auto bound = BindBackupRestoreTargetProfileId(&local_state, kReservationId,
                                                kTargetProfileId);

  ASSERT_FALSE(created.has_value());
  ASSERT_FALSE(bound.has_value());
  EXPECT_EQ(Error::kNotFound, created.error());
  EXPECT_EQ(Error::kNotFound, bound.error());
}

TEST(BackupRestoreProfileRegistryTest,
     MalformedPathFailsClosedWithoutLosingRawCustody) {
  TestingPrefServiceSimple local_state;
  RegisterApplicationPrefs(&local_state);
  base::DictValue malformed = StoredRegistry(
      kReservationId, StoredEntry("Default", "../Profile 2", "path-reserved"));
  local_state.SetDict(
      application_preferences::kBackupRestoreProfileReservations,
      malformed.Clone());

  auto reservations = ReadBackupRestoreProfileReservations(&local_state);

  ASSERT_FALSE(reservations.has_value());
  EXPECT_EQ(Error::kCorrupt, reservations.error());
  EXPECT_EQ(Quarantine::kRegistryCorrupt,
            BackupRestoreQuarantineForProfilePath(
                &local_state, base::FilePath("/profiles/Profile 2")));
  EXPECT_EQ(malformed,
            local_state.GetDict(
                application_preferences::kBackupRestoreProfileReservations));
}

TEST(BackupRestoreProfileRegistryTest,
     UnknownFieldsAndImpossibleMultiplicityFailClosed) {
  TestingPrefServiceSimple local_state;
  RegisterApplicationPrefs(&local_state);
  base::DictValue first = StoredEntry("Default", "Profile 2", "path-reserved");
  first.Set("semantic_permit", "must-not-exist");
  local_state.SetDict(
      application_preferences::kBackupRestoreProfileReservations,
      StoredRegistry(kReservationId, std::move(first)));
  auto unknown = ReadBackupRestoreProfileReservations(&local_state);
  ASSERT_FALSE(unknown.has_value());
  EXPECT_EQ(Error::kCorrupt, unknown.error());

  base::DictValue multiple;
  multiple.Set(kReservationId,
               StoredEntry("Default", "Profile 2", "path-reserved"));
  multiple.Set(kSecondReservationId,
               StoredEntry("Default", "Profile 3", "path-reserved"));
  local_state.SetDict(
      application_preferences::kBackupRestoreProfileReservations,
      std::move(multiple));
  auto duplicated = ReadBackupRestoreProfileReservations(&local_state);
  ASSERT_FALSE(duplicated.has_value());
  EXPECT_EQ(Error::kCorrupt, duplicated.error());
  EXPECT_EQ(Quarantine::kRegistryCorrupt,
            BackupRestoreQuarantineForProfilePath(
                &local_state, base::FilePath("/profiles/Profile 9")));
}

TEST(BackupRestoreProfileRegistryTest,
     VersionTwoWithoutPresentationFailsClosed) {
  TestingPrefServiceSimple local_state;
  RegisterApplicationPrefs(&local_state);
  base::DictValue entry = StoredEntry("Default", "Profile 2", "path-reserved");
  entry.Set("version", 2);
  local_state.SetDict(
      application_preferences::kBackupRestoreProfileReservations,
      StoredRegistry(kReservationId, std::move(entry)));

  auto reservations = ReadBackupRestoreProfileReservations(&local_state);

  ASSERT_FALSE(reservations.has_value());
  EXPECT_EQ(Error::kCorrupt, reservations.error());
}

TEST(BackupRestoreProfileRegistryTest, WrongPersistedTypeFailsClosed) {
  TestingPrefServiceSimple local_state;
  RegisterApplicationPrefs(&local_state);
  local_state.SetUserPref(
      application_preferences::kBackupRestoreProfileReservations,
      base::Value("not-a-registry"));

  auto reservations = ReadBackupRestoreProfileReservations(&local_state);

  ASSERT_FALSE(reservations.has_value());
  EXPECT_EQ(Error::kCorrupt, reservations.error());
  EXPECT_EQ(Quarantine::kRegistryCorrupt,
            BackupRestoreQuarantineForProfilePath(
                &local_state, base::FilePath("/profiles/Profile 2")));
}

TEST(BackupRestoreProfileRegistryTest,
     ExplicitlyStoredEmptyRegistryIsNotAnAbsentRegistry) {
  TestingPrefServiceSimple local_state;
  RegisterApplicationPrefs(&local_state);
  local_state.SetDict(
      application_preferences::kBackupRestoreProfileReservations,
      base::DictValue());

  auto reservations = ReadBackupRestoreProfileReservations(&local_state);

  ASSERT_FALSE(reservations.has_value());
  EXPECT_EQ(Error::kCorrupt, reservations.error());
}

TEST(BackupRestoreProfileRegistryTest,
     PersistedReservationReconstructsAfterStoreRestart) {
  TestingPrefServiceSimple first_store;
  RegisterApplicationPrefs(&first_store);
  ASSERT_TRUE(ReserveBackupRestoreProfilePath(&first_store, kReservationId,
                                              base::FilePath("Default"),
                                              base::FilePath("Profile 2"))
                  .has_value());
  base::DictValue persisted =
      first_store
          .GetDict(application_preferences::kBackupRestoreProfileReservations)
          .Clone();

  TestingPrefServiceSimple restarted_store;
  RegisterApplicationPrefs(&restarted_store);
  restarted_store.SetDict(
      application_preferences::kBackupRestoreProfileReservations,
      std::move(persisted));
  auto reconstructed = ReadBackupRestoreProfileReservations(&restarted_store);

  ASSERT_TRUE(reconstructed.has_value());
  ASSERT_EQ(1u, reconstructed->size());
  EXPECT_EQ(kReservationId, reconstructed->front().reservation_id);
  EXPECT_EQ(Quarantine::kQuarantined,
            BackupRestoreQuarantineForProfilePath(
                &restarted_store, base::FilePath("/profiles/Profile 2")));
}

TEST(BackupRestoreProfileRegistryTest, UnavailableStoreIsNeverReportedEmpty) {
  EXPECT_EQ(Error::kUnavailable,
            ReadBackupRestoreProfileReservations(nullptr).error());
  EXPECT_EQ(Quarantine::kRegistryUnavailable,
            BackupRestoreQuarantineForProfilePath(
                nullptr, base::FilePath("/profiles/Profile 2")));

  TestingPrefServiceSimple unregistered;
  auto reservations = ReadBackupRestoreProfileReservations(&unregistered);
  ASSERT_FALSE(reservations.has_value());
  EXPECT_EQ(Error::kUnavailable, reservations.error());
  EXPECT_EQ(Quarantine::kRegistryUnavailable,
            BackupRestoreQuarantineForProfilePath(
                &unregistered, base::FilePath("/profiles/Profile 2")));
}

TEST(BackupRestoreProfileRegistryTest, InvalidQueryPathsFailClosed) {
  TestingPrefServiceSimple local_state;
  RegisterApplicationPrefs(&local_state);

  EXPECT_EQ(
      Quarantine::kInvalidProfilePath,
      BackupRestoreQuarantineForProfilePath(&local_state, base::FilePath()));
  EXPECT_EQ(Quarantine::kInvalidProfilePath,
            BackupRestoreQuarantineForProfilePath(&local_state,
                                                  base::FilePath("Profile 2")));
  EXPECT_EQ(Quarantine::kInvalidProfilePath,
            BackupRestoreQuarantineForProfilePath(
                &local_state, base::FilePath("/profiles/Crashpad")));
}

}  // namespace
}  // namespace taffy
