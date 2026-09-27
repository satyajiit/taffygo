// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/recovery/comparison_vertical_model_endpoint.h"

#include <string>
#include <utility>

#include "base/check.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::test {
namespace {

std::string RequestWith(const char* role, const char* text) {
  base::ListValue messages;
  messages.Append(base::DictValue().Set("role", role).Set("content", text));
  base::DictValue request;
  request.Set("messages", std::move(messages));
  std::string json;
  CHECK(base::JSONWriter::Write(request, &json));
  return json;
}

TEST(ComparisonVerticalModelEndpointTest, RequiresBothPagesInCurrentUserProjection) {
  constexpr char kBoth[] = "Cedar Phone at Orchard\nPrice: $349.00\n"
                           "Cedar Phone at Harbor\nPrice: $329.00";
  EXPECT_TRUE(HasBothComparisonPagesForTesting(RequestWith("user", kBoth)));
  EXPECT_FALSE(HasBothComparisonPagesForTesting(RequestWith("tool", kBoth)));
  EXPECT_FALSE(HasBothComparisonPagesForTesting(RequestWith("system", kBoth)));
  EXPECT_FALSE(HasBothComparisonPagesForTesting(RequestWith(
      "user", "Cedar Phone at Orchard\nPrice: $349.00")));
  EXPECT_FALSE(HasBothComparisonPagesForTesting(RequestWith(
      "user", "Cedar Phone at Orchard\nPrice: $349.00\nCedar Phone at Harbor")));
  EXPECT_FALSE(HasBothComparisonPagesForTesting("malformed JSON"));
}

}  // namespace
}  // namespace taffy::test
