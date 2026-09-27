// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What the browser records about a credential it announced, and what it
// refuses to record.
//
// The file exists for one property: this register is what a credential
// survives a core generation by (decision 0117), and every way a row can fail
// to read back is a provider that silently stops working at the next start.
// So a torn row is dropped rather than repaired, a member this build cannot
// spell drops its whole row rather than one field, and neither enumeration is
// stored as a number.

#include "taffy/browser/model/provider_credential_announcement_store.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/values.h"
#include "components/prefs/testing_pref_service.h"
#include "taffy/browser/profile_preferences.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;

class ProviderCredentialAnnouncementStoreTest : public testing::Test {
protected:
  ProviderCredentialAnnouncementStoreTest() {
    profile_preferences::RegisterProfilePreferences(prefs_.registry());
  }

  // The stored dictionary as it actually is on disk, for the cases that are
  // about a value this store did not write.
  void PutRaw(base::DictValue stored) {
    prefs_.SetDict(profile_preferences::kProviderCredentialAnnouncements,
                   std::move(stored));
  }

  std::vector<ProviderCredentialAnnouncement> Read() {
    return ReadProviderCredentialAnnouncements(prefs_);
  }

  TestingPrefServiceSimple prefs_;
};

TEST_F(ProviderCredentialAnnouncementStoreTest, AnAnnouncementRoundTrips) {
  ASSERT_TRUE(WriteProviderCredentialAnnouncement(
      &prefs_, "anthropic", api::ProviderAuthMethodView::kApiKey, "anthropic"));
  const std::vector<ProviderCredentialAnnouncement> announced = Read();
  ASSERT_EQ(1u, announced.size());
  EXPECT_EQ("anthropic", announced[0].provider_id);
  EXPECT_EQ(api::ProviderAuthMethodView::kApiKey, announced[0].auth_method);
  EXPECT_EQ("anthropic", announced[0].handle);
  EXPECT_EQ(api::ProviderCredentialStateView::kUsable, announced[0].state);
}

// A sign-in is a different fact about a credential from a pasted key, and the
// core applies a different rule to each. Losing the method at a restart would
// file somebody's subscription as a typed key.
TEST_F(ProviderCredentialAnnouncementStoreTest, TheMethodIsCarried) {
  ASSERT_TRUE(WriteProviderCredentialAnnouncement(
      &prefs_, "openai", api::ProviderAuthMethodView::kOauth, "openai"));
  const std::vector<ProviderCredentialAnnouncement> announced = Read();
  ASSERT_EQ(1u, announced.size());
  EXPECT_EQ(api::ProviderAuthMethodView::kOauth, announced[0].auth_method);
}

// A save states a credential meant to work. This is the half that keeps a
// replacement from inheriting the previous credential's trouble.
TEST_F(ProviderCredentialAnnouncementStoreTest, ASaveFilesTheCredentialUsable) {
  ASSERT_TRUE(WriteProviderCredentialAnnouncement(
      &prefs_, "openai", api::ProviderAuthMethodView::kOauth, "openai"));
  ASSERT_TRUE(WriteProviderCredentialAnnouncementState(
      &prefs_, "openai", api::ProviderCredentialStateView::kNeedsSignIn));
  ASSERT_TRUE(WriteProviderCredentialAnnouncement(
      &prefs_, "openai", api::ProviderAuthMethodView::kOauth, "openai"));
  const std::vector<ProviderCredentialAnnouncement> announced = Read();
  ASSERT_EQ(1u, announced.size());
  EXPECT_EQ(api::ProviderCredentialStateView::kUsable, announced[0].state);
}

// The state has to survive, or a restart answers somebody with a request that
// fails instead of the sign-in they can act on.
TEST_F(ProviderCredentialAnnouncementStoreTest, AReportedStateIsHeld) {
  ASSERT_TRUE(WriteProviderCredentialAnnouncement(
      &prefs_, "openai", api::ProviderAuthMethodView::kOauth, "openai"));
  ASSERT_TRUE(WriteProviderCredentialAnnouncementState(
      &prefs_, "openai", api::ProviderCredentialStateView::kRefreshFailed));
  const std::vector<ProviderCredentialAnnouncement> announced = Read();
  ASSERT_EQ(1u, announced.size());
  EXPECT_EQ(api::ProviderCredentialStateView::kRefreshFailed,
            announced[0].state);
}

// A state is a fact about a credential, and there is none here to state it
// about. Filing one would announce a handle nothing ever sealed.
TEST_F(ProviderCredentialAnnouncementStoreTest,
       AStateForNoCredentialIsRefused) {
  EXPECT_FALSE(WriteProviderCredentialAnnouncementState(
      &prefs_, "openai", api::ProviderCredentialStateView::kNeedsSignIn));
  EXPECT_TRUE(Read().empty());
}

TEST_F(ProviderCredentialAnnouncementStoreTest, ForgettingRemovesTheRow) {
  ASSERT_TRUE(WriteProviderCredentialAnnouncement(
      &prefs_, "anthropic", api::ProviderAuthMethodView::kApiKey, "anthropic"));
  EXPECT_TRUE(ForgetProviderCredentialAnnouncement(&prefs_, "anthropic"));
  EXPECT_TRUE(Read().empty());
  // A provider that never had one, because the state afterwards is the state
  // that was asked for.
  EXPECT_TRUE(ForgetProviderCredentialAnnouncement(&prefs_, "mistral"));
}

TEST_F(ProviderCredentialAnnouncementStoreTest, OneProviderIsAnsweredOnItsOwn) {
  ASSERT_TRUE(WriteProviderCredentialAnnouncement(
      &prefs_, "anthropic", api::ProviderAuthMethodView::kApiKey, "anthropic"));
  const std::optional<ProviderCredentialAnnouncement> found =
      ReadProviderCredentialAnnouncement(prefs_, "anthropic");
  ASSERT_TRUE(found);
  EXPECT_EQ("anthropic", found->handle);
  EXPECT_FALSE(ReadProviderCredentialAnnouncement(prefs_, "mistral"));
  EXPECT_FALSE(ReadProviderCredentialAnnouncement(prefs_, std::string()));
}

// Every one of these is a row that parsed. Repairing any of them would
// announce a credential nobody saved, under a method or a state nobody
// reported.
TEST_F(ProviderCredentialAnnouncementStoreTest, ATornRowIsDropped) {
  base::DictValue stored;
  // Not a record at all.
  stored.Set("plain", "anthropic");
  // A record missing the handle.
  base::DictValue no_handle;
  no_handle.Set("auth_method", "API_KEY");
  no_handle.Set("state", "USABLE");
  stored.Set("no-handle", std::move(no_handle));
  // A record whose handle is empty, which is not the same answer as absent.
  base::DictValue empty_handle;
  empty_handle.Set("auth_method", "API_KEY");
  empty_handle.Set("handle", "");
  empty_handle.Set("state", "USABLE");
  stored.Set("empty-handle", std::move(empty_handle));
  // A method this build cannot spell. Defaulted, it would file somebody's
  // subscription as a pasted key or the other way round.
  base::DictValue unknown_method;
  unknown_method.Set("auth_method", "PASSKEY");
  unknown_method.Set("handle", "unknown-method");
  unknown_method.Set("state", "USABLE");
  stored.Set("unknown-method", std::move(unknown_method));
  // A state this build cannot spell. Defaulted to usable, it would send a
  // request through a credential the browser knows is not.
  base::DictValue unknown_state;
  unknown_state.Set("auth_method", "OAUTH");
  unknown_state.Set("handle", "unknown-state");
  unknown_state.Set("state", "REVOKED");
  stored.Set("unknown-state", std::move(unknown_state));
  // A record with no state at all, which is not usable by default either.
  base::DictValue no_state;
  no_state.Set("auth_method", "OAUTH");
  no_state.Set("handle", "no-state");
  stored.Set("no-state", std::move(no_state));
  // One good row, so the test proves the reader works rather than that it
  // refuses everything.
  base::DictValue whole;
  whole.Set("auth_method", "OAUTH");
  whole.Set("handle", "openai");
  whole.Set("state", "NEEDS_SIGN_IN");
  stored.Set("openai", std::move(whole));
  PutRaw(std::move(stored));

  const std::vector<ProviderCredentialAnnouncement> announced = Read();
  ASSERT_EQ(1u, announced.size());
  EXPECT_EQ("openai", announced[0].provider_id);
  EXPECT_EQ(api::ProviderAuthMethodView::kOauth, announced[0].auth_method);
  EXPECT_EQ(api::ProviderCredentialStateView::kNeedsSignIn,
            announced[0].state);
}

// A number on disk is an ordinal, and an enumeration that is ever renumbered
// would read every row back as a different member with nothing to say so.
TEST_F(ProviderCredentialAnnouncementStoreTest, NeitherEnumerationIsANumber) {
  ASSERT_TRUE(WriteProviderCredentialAnnouncement(
      &prefs_, "openai", api::ProviderAuthMethodView::kOauth, "openai"));
  ASSERT_TRUE(WriteProviderCredentialAnnouncementState(
      &prefs_, "openai", api::ProviderCredentialStateView::kRefreshFailed));
  const base::DictValue &stored =
      prefs_.GetDict(profile_preferences::kProviderCredentialAnnouncements);
  const base::DictValue *row = stored.FindDict("openai");
  ASSERT_TRUE(row);
  const std::string *method = row->FindString("auth_method");
  const std::string *state = row->FindString("state");
  ASSERT_TRUE(method);
  ASSERT_TRUE(state);
  EXPECT_EQ("OAUTH", *method);
  EXPECT_EQ("REFRESH_FAILED", *state);
}

TEST_F(ProviderCredentialAnnouncementStoreTest, NothingIsWrittenWithoutAName) {
  EXPECT_FALSE(WriteProviderCredentialAnnouncement(
      &prefs_, std::string(), api::ProviderAuthMethodView::kApiKey, "handle"));
  EXPECT_FALSE(WriteProviderCredentialAnnouncement(
      &prefs_, "anthropic", api::ProviderAuthMethodView::kApiKey,
      std::string()));
  EXPECT_FALSE(WriteProviderCredentialAnnouncement(
      nullptr, "anthropic", api::ProviderAuthMethodView::kApiKey, "anthropic"));
  EXPECT_TRUE(Read().empty());
}

}  // namespace
}  // namespace taffy
