// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/sensitive_name_list.h"

#include <set>
#include <string>

#include "testing/gtest/include/gtest/gtest.h"

// The shared vocabulary of PAR-SEC-009 and REQ-SEC-001. Small, and worth its
// own test for one reason: it is a list three separate code paths consult, and
// the failure mode of a duplicate or a stray capital letter in it is that a
// name silently stops matching.

namespace taffy {
namespace {

TEST(SensitiveNameListTest, TheNamesRequirementsNameAreAllPresent) {
  // REQ-SEC-001 enumerates password, one-time code, card security code,
  // personal identification number, passkey, token, private key and seed
  // phrase. Each has at least one spelling here.
  EXPECT_TRUE(IsSensitiveValueName("password"));
  EXPECT_TRUE(IsSensitiveValueName("otp"));
  EXPECT_TRUE(IsSensitiveValueName("cvv"));
  EXPECT_TRUE(IsSensitiveValueName("pin"));
  EXPECT_TRUE(IsSensitiveValueName("token"));
  EXPECT_TRUE(IsSensitiveValueName("private_key"));
  EXPECT_TRUE(IsSensitiveValueName("mnemonic"));
  EXPECT_TRUE(IsSensitiveValueName("recovery_code"));
}

TEST(SensitiveNameListTest, TheAuthorizationFlowParametersAreCovered) {
  // The two that carry an authorization grant, and the two most ordinary
  // English words on the list. Over-redaction is the accepted cost.
  EXPECT_TRUE(IsSensitiveValueName("code"));
  EXPECT_TRUE(IsSensitiveValueName("state"));
  EXPECT_TRUE(IsSensitiveValueName("id_token"));
  EXPECT_TRUE(IsSensitiveValueName("refresh_token"));
}

TEST(SensitiveNameListTest, MatchingIgnoresCase) {
  EXPECT_TRUE(IsSensitiveValueName("PASSWORD"));
  EXPECT_TRUE(IsSensitiveValueName("Authorization"));
  EXPECT_TRUE(IsSensitiveValueName("Api_Key"));
}

TEST(SensitiveNameListTest, OrdinaryNamesAreNotOnTheList) {
  // A list that matched everything would redact every diagnostic, and a
  // diagnostic nobody reads is the same as no diagnostic.
  EXPECT_FALSE(IsSensitiveValueName("title"));
  EXPECT_FALSE(IsSensitiveValueName("count"));
  EXPECT_FALSE(IsSensitiveValueName("url"));
  EXPECT_FALSE(IsSensitiveValueName("name"));
  EXPECT_FALSE(IsSensitiveValueName(""));
}

TEST(SensitiveNameListTest, EveryEntryIsLowercaseAndUnique) {
  // The comparison lowercases the input, so an entry with a capital letter
  // would never match anything — a hole nobody would notice.
  std::set<std::string> seen;
  ASSERT_GT(GetSensitiveValueNameCount(), 0u);

  for (size_t i = 0; i < GetSensitiveValueNameCount(); ++i) {
    const std::string entry(GetSensitiveValueName(i));
    EXPECT_FALSE(entry.empty());
    for (char c : entry) {
      EXPECT_FALSE(c >= 'A' && c <= 'Z')
          << "entry \"" << entry << "\" has a capital letter and can never "
             "match";
    }
    EXPECT_TRUE(seen.insert(entry).second)
        << "entry \"" << entry << "\" appears twice";
    EXPECT_TRUE(IsSensitiveValueName(entry));
  }
}

TEST(SensitiveNameListTest, AnOutOfRangeIndexIsEmptyRatherThanUndefined) {
  EXPECT_TRUE(GetSensitiveValueName(GetSensitiveValueNameCount()).empty());
  EXPECT_TRUE(GetSensitiveValueName(1000000).empty());
}

}  // namespace
}  // namespace taffy
