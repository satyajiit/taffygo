// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A person's answer names the field each held value was minted for, one per
// value (decision 0238). The list is what lets the task put the values into
// the page without a model turn, so a list that disagrees with its own count
// is a fill the core would aim at the wrong field.

#include <string>
#include <vector>

#include "taffy/browser/core_api/core_api_command_factory.h"
#include "taffy/browser/core_service_command_validation.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

mojom::CoreServiceCommandPtr Answer(uint32_t supplied,
                                    std::vector<std::string> fields) {
  CoreApiCommandFactory factory("profile", CreateCoreApiEntropySource());
  return factory.BuildSupplyFieldValues(
      "task-1", "field-request-1", supplied,
      mojom::FieldValueAskOutcome::kAnswered, std::move(fields),
      /*task_revision=*/4u, /*service_generation=*/1u,
      /*now_monotonic_ms=*/100u);
}

bool IsValid(const mojom::CoreServiceCommandPtr& command) {
  return command && IsStructurallyValidCoreServiceCommand(
                        *command, mojom::kMaxCommandBytes);
}

TEST(CoreServiceCommandValidationFieldValuesTest,
     AdmitsOneFieldPerHeldValueAndNoneForNothing) {
  EXPECT_TRUE(IsValid(Answer(2u, {"node-7", "node-9"})));
  EXPECT_TRUE(IsValid(Answer(0u, {})));
}

TEST(CoreServiceCommandValidationFieldValuesTest,
     RefusesAListThatDisagreesWithItsCount) {
  EXPECT_FALSE(IsValid(Answer(2u, {"node-7"})));
  EXPECT_FALSE(IsValid(Answer(1u, {"node-7", "node-9"})));
  EXPECT_FALSE(IsValid(Answer(0u, {"node-7"})));
}

TEST(CoreServiceCommandValidationFieldValuesTest,
     RefusesARepeatedOrEmptyField) {
  // Two of the person's answers into one field, or an answer for no field.
  EXPECT_FALSE(IsValid(Answer(2u, {"node-7", "node-7"})));
  EXPECT_FALSE(IsValid(Answer(1u, {""})));
}

}  // namespace
}  // namespace taffy
