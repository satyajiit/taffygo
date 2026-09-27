// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <limits>
#include <string>

#include "taffy/browser/core_api/core_api_command_factory.h"
#include "taffy/browser/core_service_command_validation.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;
namespace mojom = core_service::mojom;

bool IsValid(const mojom::CoreServiceCommand& command,
             size_t ceiling = mojom::kMaxCommandBytes) {
  return IsStructurallyValidCoreServiceCommand(command, ceiling);
}

mojom::CoreServiceCommandPtr ManagerSnapshot() {
  auto command = mojom::CoreServiceCommand::New();
  command->operation = mojom::OperationEnvelope::New(
      "saved-data-operation", 4u, 0u, 100u, "saved-data-key");
  command->kind = mojom::CoreServiceCommandKind::kReplaceSavedDataSnapshot;
  command->replace_saved_data_snapshot =
      mojom::ReplaceSavedDataSnapshotCommand::New();
  auto& snapshot = *command->replace_saved_data_snapshot;
  snapshot.sign_ins_availability = mojom::SavedDataAvailability::kReady;
  snapshot.sign_ins_revision = 7u;
  snapshot.sign_ins.push_back(mojom::SavedSignInMetadata::New(
      "saved-sign-in-1", "accounts.example", "person@example.test", 20u));
  snapshot.details_availability = mojom::SavedDataAvailability::kReady;
  snapshot.details_revision = 5u;
  snapshot.details.push_back(mojom::SavedDetailRecord::New(
      "saved-detail-1", "Ada", "Lovelace", "ada@example.test", "+1 555",
      "12 Example Road\nSecond floor", "10001", "US"));
  return command;
}

TEST(CoreServiceCommandValidationProfileTest,
     AdmitsFactoryConfigurationAndManagerSnapshot) {
  CoreApiCommandFactory factory("profile", CreateCoreApiEntropySource());
  auto configuration = factory.BuildSetAssistantConfiguration(
      3u, {api::AssistantAbilityView::kForm,
           api::AssistantAbilityView::kDownloads},
      api::PersonalityPresetView::kQuickShopper, 2u, 1u, 0u, 4u, 10u);

  ASSERT_TRUE(configuration);
  EXPECT_TRUE(IsValid(*configuration->core_service_command));
  EXPECT_TRUE(IsValid(*ManagerSnapshot()));

  auto unavailable = ManagerSnapshot();
  auto& snapshot = *unavailable->replace_saved_data_snapshot;
  snapshot.sign_ins_availability = mojom::SavedDataAvailability::kUnavailable;
  snapshot.sign_ins_revision = 0u;
  snapshot.sign_ins.clear();
  snapshot.details_availability = mojom::SavedDataAvailability::kLoading;
  snapshot.details_revision = 0u;
  snapshot.details.clear();
  EXPECT_TRUE(IsValid(*unavailable));
}

TEST(CoreServiceCommandValidationProfileTest,
     RefusesConfigurationOrderDuplicatesAndInvalidScales) {
  CoreApiCommandFactory factory("profile", CreateCoreApiEntropySource());
  auto projected = factory.BuildSetAssistantConfiguration(
      3u, {api::AssistantAbilityView::kForm,
           api::AssistantAbilityView::kDownloads},
      api::PersonalityPresetView::kQuickShopper, 2u, 1u, 0u, 4u, 10u);
  ASSERT_TRUE(projected);
  const auto& valid = projected->core_service_command;

  auto reversed = valid.Clone();
  std::reverse(reversed->set_assistant_configuration->disabled_abilities.begin(),
               reversed->set_assistant_configuration->disabled_abilities.end());
  EXPECT_FALSE(IsValid(*reversed));

  auto duplicate = valid.Clone();
  duplicate->set_assistant_configuration->disabled_abilities[1] =
      mojom::AssistantAbility::kForm;
  EXPECT_FALSE(IsValid(*duplicate));

  auto invalid_scale = valid.Clone();
  invalid_scale->set_assistant_configuration->check_in =
      mojom::kMaxPersonalityScale + 1u;
  EXPECT_FALSE(IsValid(*invalid_scale));

  auto invalid_preset = valid.Clone();
  invalid_preset->set_assistant_configuration->preset =
      static_cast<mojom::PersonalityPreset>(99u);
  EXPECT_FALSE(IsValid(*invalid_preset));
}

TEST(CoreServiceCommandValidationProfileTest,
     RefusesMalformedAndOversizedSavedRecords) {
  auto invalid_id = ManagerSnapshot();
  invalid_id->replace_saved_data_snapshot->sign_ins[0]->id = "not an id";
  EXPECT_FALSE(IsValid(*invalid_id));

  auto invalid_host = ManagerSnapshot();
  invalid_host->replace_saved_data_snapshot->sign_ins[0]->site =
      "https://accounts.example/path";
  EXPECT_FALSE(IsValid(*invalid_host));

  auto invalid_time = ManagerSnapshot();
  invalid_time->replace_saved_data_snapshot->sign_ins[0]
      ->last_used_epoch_ms = std::numeric_limits<uint64_t>::max();
  EXPECT_FALSE(IsValid(*invalid_time));

  auto empty_detail = ManagerSnapshot();
  auto& detail = *empty_detail->replace_saved_data_snapshot->details[0];
  detail.given_name.clear();
  detail.family_name.clear();
  detail.email.clear();
  detail.phone.clear();
  detail.address.clear();
  detail.postcode.clear();
  detail.country.clear();
  EXPECT_FALSE(IsValid(*empty_detail));

  auto oversized = ManagerSnapshot();
  oversized->replace_saved_data_snapshot->sign_ins[0]->username =
      std::string(mojom::kMaxSavedSignInUsernameBytes + 1u, 'x');
  EXPECT_FALSE(IsValid(*oversized));
}

TEST(CoreServiceCommandValidationProfileTest,
     RefusesDuplicateRowsAvailabilityMismatchAndAggregateOverflow) {
  auto duplicate = ManagerSnapshot();
  duplicate->replace_saved_data_snapshot->sign_ins.push_back(
      duplicate->replace_saved_data_snapshot->sign_ins[0].Clone());
  EXPECT_FALSE(IsValid(*duplicate));

  auto records_while_loading = ManagerSnapshot();
  records_while_loading->replace_saved_data_snapshot->sign_ins_availability =
      mojom::SavedDataAvailability::kLoading;
  records_while_loading->replace_saved_data_snapshot->sign_ins_revision = 0u;
  EXPECT_FALSE(IsValid(*records_while_loading));

  auto zero_ready_revision = ManagerSnapshot();
  zero_ready_revision->replace_saved_data_snapshot->details_revision = 0u;
  EXPECT_FALSE(IsValid(*zero_ready_revision));

  auto bounded = ManagerSnapshot();
  EXPECT_FALSE(IsValid(*bounded, 8u));
}

}  // namespace
}  // namespace taffy
