// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The refresh leg, which is not a flow: it runs against a credential already
// in force, files no terminal, and answers one caller with a rotated record.
// It is a file of its own because it shares nothing with the sign-in tests but
// the fixture, and because the rule it exists for — an omitted refresh token
// means "keep the one you have" — is the kind of thing that is repaired once
// and then quietly undone by somebody reading a flow test.

#include "taffy/browser/providerauth/profile_provider_auth_broker.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/memory/scoped_refptr.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "net/base/url_util.h"
#include "net/http/http_status_code.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "taffy/browser/account/fake_profile_platform_adapter.h"
#include "taffy/browser/account/profile_account_broker.h"
#include "taffy/browser/providerauth/provider_auth_configuration.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

// Every vendor answer here is dictated: authorization_pending, slow_down,
// access_denied, invalid_grant, a token grant. Each is one path through the
// broker's classification, and the observable facts are the one terminal per
// flow, what was sealed and stored through the platform adapter, and the
// events Android would render.
//
// Both shapes under test ride the scoped override. No shipping row carries a
// dated terms review, so no shipping row can start a flow at all
// (decision 0095 section 1), and a suite that depended on one would be a
// suite that stops working the day somebody does the honest thing.

namespace taffy {
namespace {

namespace account_mojom = browser::account::mojom;
namespace service = core_service::mojom;

constexpr char kDeviceCodeUrl[] = "https://vendor.example/device";
constexpr char kTokenUrl[] = "https://vendor.example/token";

constexpr ProviderAuthVendor kDeviceTestVendor = {
    "test-device",
    ProviderAuthFlowKind::kDeviceCode,
    ProviderAuthRedirectKind::kManualCode,
    ProviderAuthExchangeKind::kOauthTokenPair,
    "",
    kDeviceCodeUrl,
    kTokenUrl,
    "",
    "",
    "",
    "",
    "",
    "",
    "Someone else's command-line tool",
    {},
    {},
    "2026-08-29",
    /*authorization_displays_code=*/false,
    /*state_is_pkce_verifier=*/false,
    /*presents_client_identity=*/true,
    "test-client",
    "",
    "profile inference",
};

struct RecordedTerminal {
  uint64_t generation = 0;
  service::ProviderAuthCallbackCommandPtr command;
};

class ProfileProviderAuthBrokerRefreshTest : public testing::Test {
 protected:
  ProfileProviderAuthBrokerRefreshTest()
      : task_environment_(base::test::TaskEnvironment::TimeSource::MOCK_TIME) {
    account_ = std::make_unique<ProfileAccountBroker>(&browser_context_);
    account_->BindPlatformAdapter(adapter_.BindNewPipeAndPassRemote());
    broker_ = std::make_unique<ProfileProviderAuthBroker>(
        base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
            &network_),
        account_.get());
    broker_->SetFlowTerminalCallback(base::BindLambdaForTesting(
        [this](uint64_t generation,
               service::ProviderAuthCallbackCommandPtr command,
               base::OnceCallback<void(bool)> submitted) {
          terminals_.push_back({generation, std::move(command)});
          std::move(submitted).Run(terminal_accepted_);
        }));
    network_.SetInterceptor(base::BindLambdaForTesting(
        [this](const network::ResourceRequest &request) {
          requested_urls_.push_back(request.url.spec());
        }));
  }

  void RespondJson(const std::string &url, const std::string &body) {
    // The two-argument overload takes string_view, not GURL.
    network_.AddResponse(url, body);
  }

  // Flushes mojo hops without advancing the mock clock.
  void Settle() { base::RunLoop().RunUntilIdle(); }

  size_t RequestCount(const std::string &url) const {
    size_t count = 0;
    for (const std::string &requested : requested_urls_) {
      if (requested == url) {
        ++count;
      }
    }
    return count;
  }

  std::vector<account_mojom::ProviderFlowEventKind> EventKinds() const {
    std::vector<account_mojom::ProviderFlowEventKind> kinds;
    for (const account_mojom::ProviderFlowEventPtr &event : adapter_.events()) {
      kinds.push_back(event->kind);
    }
    return kinds;
  }

  content::BrowserTaskEnvironment task_environment_;
  content::TestBrowserContext browser_context_;
  FakeProfilePlatformAdapter adapter_;
  std::unique_ptr<ProfileAccountBroker> account_;
  network::TestURLLoaderFactory network_;
  std::unique_ptr<ProfileProviderAuthBroker> broker_;
  bool terminal_accepted_ = true;
  std::vector<RecordedTerminal> terminals_;
  std::vector<std::string> requested_urls_;
};

TEST_F(ProfileProviderAuthBrokerRefreshTest, ARefreshThatRotatesNothingKeepsItsToken) {
  // RFC 6749 section 6 makes the new refresh token optional, and a vendor
  // that omits it means "keep the one you have". Sealed without it, the next
  // expiry finds a stale access token and no way to renew it, and the person
  // is signed out about one token lifetime after signing in — every time,
  // with nothing reporting a failure.
  ScopedProviderAuthVendorForTesting scoped(&kDeviceTestVendor);
  RespondJson(kTokenUrl,
              R"({"access_token":"at-2","expires_in":3600})");
  account_mojom::ProviderOauthRecordPtr rotated;
  bool answered = false;
  broker_->RefreshCredential(
      "test-device", "rt-1",
      base::BindLambdaForTesting(
          [&](account_mojom::ProviderOauthRecordPtr record, bool definitive) {
            rotated = std::move(record);
            answered = true;
            EXPECT_FALSE(definitive);
          }));
  Settle();

  ASSERT_TRUE(answered);
  ASSERT_TRUE(rotated);
  EXPECT_EQ("at-2", std::string(rotated->access_token.begin(),
                                rotated->access_token.end()));
  ASSERT_TRUE(rotated->refresh_token);
  EXPECT_EQ("rt-1", std::string(rotated->refresh_token->begin(),
                                rotated->refresh_token->end()));
}

TEST_F(ProfileProviderAuthBrokerRefreshTest, ARefreshThatRotatesTakesTheNewToken) {
  // The other half of the same rule: a vendor that does rotate replaces the
  // credential, and the spent one is not carried forward over it.
  ScopedProviderAuthVendorForTesting scoped(&kDeviceTestVendor);
  RespondJson(kTokenUrl,
              R"({"access_token":"at-2","refresh_token":"rt-2",)"
              R"("expires_in":3600})");
  account_mojom::ProviderOauthRecordPtr rotated;
  broker_->RefreshCredential(
      "test-device", "rt-1",
      base::BindLambdaForTesting(
          [&](account_mojom::ProviderOauthRecordPtr record, bool) {
            rotated = std::move(record);
          }));
  Settle();

  ASSERT_TRUE(rotated);
  ASSERT_TRUE(rotated->refresh_token);
  EXPECT_EQ("rt-2", std::string(rotated->refresh_token->begin(),
                                rotated->refresh_token->end()));
}

}  // namespace
}  // namespace taffy
