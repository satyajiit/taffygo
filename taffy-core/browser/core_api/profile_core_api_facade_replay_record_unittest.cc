// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What the provider handlers write to the browser's own file so a new core
// generation can be told about it (decision 0117).
//
// The core's provider plane holds a person's credentials and their own
// providers for exactly as long as the utility process it lives in, and
// nothing refills it. So every one of these records is the difference between
// a provider that works after a restart and one whose row reads connected
// while every request through it is refused — a disagreement no gate can see,
// because it exists only in a second generation and no host lane starts one.
//
// The core is shut down in every case, and that is not a test of shutdown: it
// is how a well-formed provider command can be submitted here at all, because
// a manager that has shut down answers Submit without reaching EnsureStarted.
// What is under test is what happened to the file *before* the command was
// forwarded, and that half is the same on either side of a shutdown.

#include "taffy/browser/core_api/profile_core_api_facade.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "taffy/browser/core_api/profile_core_api_facade_test_support.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_test_support.h"
#include "taffy/browser/model/custom_provider_endpoint_store.h"
#include "taffy/browser/model/provider_credential_announcement_store.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;

using core_api_test::RecordedStatus;

class ProfileCoreApiFacadeReplayRecordTest : public testing::Test {
 protected:
  ProfileCoreApiFacadeReplayRecordTest()
      : browser_context_(std::make_unique<content::TestBrowserContext>()),
        manager_(core_api_test::MakeFacadeTestManager(
            tail_, browser_context_.get())),
        facade_(manager_.get()) {}

  void SetUp() override { manager_->Shutdown(); }

  api::TaffyProfileCoreApi& surface() {
    return static_cast<api::TaffyProfileCoreApi&>(facade_);
  }

  std::vector<CustomProviderDefinition> Defined() {
    return ReadCustomProviderDefinitions(*manager_->profile_prefs());
  }

  std::optional<ProviderCredentialAnnouncement> Announced(
      const std::string& provider_id) {
    return ReadProviderCredentialAnnouncement(*manager_->profile_prefs(),
                                              provider_id);
  }

  content::BrowserTaskEnvironment task_environment_;
  // Before the manager: the filtering service the tail builds watches the
  // tail's pref service for the manager's whole lifetime.
  test::QuietManagerTail tail_;
  std::unique_ptr<content::TestBrowserContext> browser_context_;
  std::unique_ptr<CoreServiceManager> manager_;
  ProfileCoreApiFacade facade_;
};

// The whole definition, not the address alone. An address is enough to
// authorize a request and not enough to say the provider exists, so a save
// that wrote only one would leave the provider working in this generation and
// absent from the next with nothing to say why.
TEST_F(ProfileCoreApiFacadeReplayRecordTest, ASaveRecordsTheWholeDefinition) {
  std::vector<api::CustomModelSpecViewPtr> models;
  models.push_back(api::CustomModelSpecView::New(
      "local-reasoner", "local-reasoner", 32768u, 4096u, /*reasoning=*/true,
      /*tool_calling=*/false));

  RecordedStatus saved;
  surface().SaveCustomProvider(
      "my-gateway", "My gateway", "http://192.168.1.9:11434/v1",
      api::ProviderWireApiView::kOpenAiCompletions,
      std::optional<std::string>("my-gateway"), std::move(models),
      api::DetectedServerView::New(api::ServerKindView::kVllm), saved.Bind());

  const std::vector<CustomProviderDefinition> defined = Defined();
  ASSERT_EQ(1u, defined.size());
  EXPECT_EQ("my-gateway", defined[0].provider_id);
  EXPECT_EQ("My gateway", defined[0].display_name);
  EXPECT_EQ("http://192.168.1.9:11434/v1", defined[0].endpoint);
  EXPECT_EQ(std::optional<std::string>("my-gateway"),
            defined[0].credential_handle);
  ASSERT_TRUE(defined[0].detected_server);
  EXPECT_EQ(api::ServerKindView::kVllm, *defined[0].detected_server);
  ASSERT_EQ(1u, defined[0].models.size());
  EXPECT_EQ("local-reasoner", defined[0].models[0].model_id);

  // And its credential is announced the way every other one is, so a state
  // reported about it later has a row to land on.
  const std::optional<ProviderCredentialAnnouncement> announced =
      Announced("my-gateway");
  ASSERT_TRUE(announced);
  EXPECT_EQ(api::ProviderAuthMethodView::kApiKey, announced->auth_method);
  EXPECT_EQ("my-gateway", announced->handle);
}

// A save carrying no credential clears any announcement, because the plane
// drops the credential on exactly the same command. A row left standing would
// announce a key the person removed.
TEST_F(ProfileCoreApiFacadeReplayRecordTest, ASaveWithNoKeyAnnouncesNone) {
  RecordedStatus first;
  surface().SaveCustomProvider(
      "my-gateway", "My gateway", "http://192.168.1.9:11434/v1",
      api::ProviderWireApiView::kOpenAiCompletions,
      std::optional<std::string>("my-gateway"),
      std::vector<api::CustomModelSpecViewPtr>(), api::DetectedServerViewPtr(),
      first.Bind());
  ASSERT_TRUE(Announced("my-gateway"));

  RecordedStatus second;
  surface().SaveCustomProvider(
      "my-gateway", "My gateway", "http://192.168.1.9:11434/v1",
      api::ProviderWireApiView::kOpenAiCompletions,
      std::optional<std::string>(), std::vector<api::CustomModelSpecViewPtr>(),
      api::DetectedServerViewPtr(), second.Bind());

  EXPECT_FALSE(Announced("my-gateway"));
  EXPECT_EQ(1u, Defined().size());
}

TEST_F(ProfileCoreApiFacadeReplayRecordTest, ARemovalWithdrawsBothRecords) {
  RecordedStatus saved;
  surface().SaveCustomProvider(
      "my-gateway", "My gateway", "http://192.168.1.9:11434/v1",
      api::ProviderWireApiView::kOpenAiCompletions,
      std::optional<std::string>("my-gateway"),
      std::vector<api::CustomModelSpecViewPtr>(), api::DetectedServerViewPtr(),
      saved.Bind());
  ASSERT_EQ(1u, Defined().size());

  RecordedStatus removed;
  surface().RemoveCustomProvider("my-gateway", removed.Bind());

  EXPECT_TRUE(Defined().empty());
  EXPECT_FALSE(Announced("my-gateway"));
}

// The three credential handlers, which are the whole of why a saved key
// survives a restart at all.
TEST_F(ProfileCoreApiFacadeReplayRecordTest, ACredentialIsRecordedAndCleared) {
  RecordedStatus saved;
  surface().SaveProviderCredential("anthropic",
                                   api::ProviderAuthMethodView::kApiKey,
                                   "anthropic", saved.Bind());
  std::optional<ProviderCredentialAnnouncement> announced =
      Announced("anthropic");
  ASSERT_TRUE(announced);
  EXPECT_EQ(api::ProviderAuthMethodView::kApiKey, announced->auth_method);
  EXPECT_EQ(api::ProviderCredentialStateView::kUsable, announced->state);

  RecordedStatus reported;
  surface().SetProviderCredentialState(
      "anthropic", api::ProviderCredentialStateView::kNeedsSignIn,
      reported.Bind());
  announced = Announced("anthropic");
  ASSERT_TRUE(announced);
  EXPECT_EQ(api::ProviderCredentialStateView::kNeedsSignIn, announced->state);

  RecordedStatus forgotten;
  surface().ForgetProviderCredential("anthropic", forgotten.Bind());
  EXPECT_FALSE(Announced("anthropic"));
}

// A state is a fact about a credential, and there is none here to state it
// about. Filing one would replay a handle nothing ever sealed.
TEST_F(ProfileCoreApiFacadeReplayRecordTest, AStateForNoCredentialIsRefused) {
  RecordedStatus reported;
  surface().SetProviderCredentialState(
      "anthropic", api::ProviderCredentialStateView::kNeedsSignIn,
      reported.Bind());

  ASSERT_TRUE(reported.value().has_value());
  EXPECT_EQ(*reported.value(), api::CoreApiSubmissionStatus::kInvalidRequest);
  EXPECT_FALSE(Announced("anthropic"));
}

}  // namespace
}  // namespace taffy
