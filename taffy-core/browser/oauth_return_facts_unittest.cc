// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/oauth_return_facts.h"

#include <string>
#include <type_traits>

#include "taffy/components/intelligence/content/origin_codec.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

// PAR-AUTH-004's second half: the callback's parameters do not leak.
//
// The strongest assertion in this file is the one that reads as an absence.
// There is no accessor for the query or the fragment, so the test cannot
// demonstrate that they are empty — it can only demonstrate that the object
// was built from a URL that had them and carries no way to get at them. That
// is the design: a value nobody stored is a value nobody can leak.

namespace taffy {
namespace {

class OAuthReturnFactsTest : public testing::Test {
 protected:
  void TearDown() override { OriginCodec::Get().ClearForTesting(); }
  content::BrowserTaskEnvironment task_environment_;
};

TEST_F(OAuthReturnFactsTest, OnlyTheOriginAndPathSurvive) {
  const GURL callback(
      "https://primary.taffy.test/auth/callback"
      "?code=AUTHORIZATION_CODE_VALUE&state=OPAQUE_STATE_VALUE");

  const OAuthReturnFacts facts =
      OAuthReturnFacts::FromCallbackUrl(callback, TabId{"tab_oauth"});

  EXPECT_EQ("https://primary.taffy.test",
            facts.callback_origin().serialization);
  EXPECT_EQ("/auth/callback", facts.callback_path());
  EXPECT_TRUE(facts.carried_callback_parameters());
  EXPECT_EQ("tab_oauth", facts.tab_id().value);
}

TEST_F(OAuthReturnFactsTest, ImplicitFlowFragmentIsAlsoDropped) {
  // The implicit flows put the access token and the identity token in the
  // fragment. It is read once to answer "was there anything there" and then it
  // is out of scope.
  const GURL callback(
      "https://primary.taffy.test/auth/return"
      "#access_token=TOKEN_VALUE&id_token=IDENTITY_TOKEN_VALUE");

  const OAuthReturnFacts facts =
      OAuthReturnFacts::FromCallbackUrl(callback, TabId{"tab_oauth"});

  EXPECT_EQ("/auth/return", facts.callback_path());
  EXPECT_TRUE(facts.carried_callback_parameters());
}

TEST_F(OAuthReturnFactsTest, ACallbackWithNoParametersSaysSo) {
  const OAuthReturnFacts facts = OAuthReturnFacts::FromCallbackUrl(
      GURL("https://primary.taffy.test/auth/done"), TabId{"tab_oauth"});

  EXPECT_FALSE(facts.carried_callback_parameters());
  EXPECT_EQ("/auth/done", facts.callback_path());
}

TEST_F(OAuthReturnFactsTest, ThereIsNoWayToBuildOneFromAString) {
  // The only producer is FromCallbackUrl, which drops the query and the
  // fragment before any member exists to hold them. A future call site cannot
  // construct a "full" version because there is no constructor that would.
  static_assert(!std::is_default_constructible_v<OAuthReturnFacts>,
                "OAuthReturnFacts must come from a committed callback URL.");
  static_assert(!std::is_constructible_v<OAuthReturnFacts, GURL>,
                "The public constructor would bypass the parameter drop.");
  static_assert(!std::is_constructible_v<OAuthReturnFacts, std::string>,
                "A string constructor would let a full callback URL in.");
}

TEST_F(OAuthReturnFactsTest, AnOpaqueCallbackOriginStaysOpaque) {
  // A sandboxed document's callback commits an opaque origin. Two opaque
  // origins are equal only when their session-local identifiers match, which
  // is why the continuity check compares Origin values rather than strings.
  const OAuthReturnFacts facts = OAuthReturnFacts::FromCallbackUrl(
      GURL("data:text/html,<p>not a callback"), TabId{"tab_oauth"});

  EXPECT_TRUE(facts.callback_origin().is_opaque());
  EXPECT_TRUE(facts.callback_origin().is_valid());
}

}  // namespace
}  // namespace taffy
