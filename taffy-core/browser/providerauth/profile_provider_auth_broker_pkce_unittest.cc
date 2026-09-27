// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The authorization-code half of the broker, mirroring the split the
// implementation already makes between profile_provider_auth_broker_pkce.cc
// and _device.cc. A redirect is claimed by its state and by nothing else, and
// the tests for that live apart from the device flow's polling because the two
// share no leg beyond the fixture.

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
#include "services/network/public/cpp/data_element.h"
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

constexpr char kTokenUrl[] = "https://vendor.example/token";

constexpr ProviderAuthVendor kPkceTestVendor = {
    "test-vendor",
    ProviderAuthFlowKind::kPkce,
    ProviderAuthRedirectKind::kCustomScheme,
    ProviderAuthExchangeKind::kOauthTokenPair,
    "https://vendor.example/authorize",
    "",
    kTokenUrl,
    "",
    "",
    "",
    "https://vendor.example/revoke",
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

class ProfileProviderAuthBrokerPkceTest : public testing::Test {
 protected:
  ProfileProviderAuthBrokerPkceTest()
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
          // The body, so a test can assert what a vendor is actually sent
          // rather than only which address it went to. A form body is one
          // in-memory element; anything else is recorded as empty, which is
          // what every request in this suite that is not a form exchange is.
          std::string body;
          if (request.request_body &&
              request.request_body->elements()->size() == 1u) {
            const network::DataElement &element =
                request.request_body->elements()->at(0);
            if (element.type() == network::DataElement::Tag::kBytes) {
              body = std::string(
                  element.As<network::DataElementBytes>().AsStringView());
            }
          }
          requested_bodies_.push_back(std::move(body));
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
  std::vector<std::string> requested_bodies_;

  // The body of the one request that went to this address.
  std::string BodySentTo(const std::string &url) const {
    for (size_t index = 0; index < requested_urls_.size(); ++index) {
      if (requested_urls_[index] == url) {
        return requested_bodies_[index];
      }
    }
    return {};
  }
};

TEST_F(ProfileProviderAuthBrokerPkceTest, ThePkceRedirectIsClaimedByItsStateAlone) {
  ScopedProviderAuthVendorForTesting scoped(&kPkceTestVendor);
  RespondJson(kTokenUrl,
              R"({"access_token":"at-2","refresh_token":"rt-2",)"
              R"("expires_in":600})");
  broker_->StartFlow("test-vendor", "flow-2", "binding-2", 3u);
  Settle();

  // The Custom Tab opened on the authorization URL carrying the minted state
  // and an S256 challenge; the redirect below must echo that exact state.
  ASSERT_EQ(1u, adapter_.opened_urls().size());
  const GURL authorize(adapter_.opened_urls()[0]);
  EXPECT_TRUE(authorize.SchemeIs("https"));
  EXPECT_EQ("vendor.example", authorize.host());
  EXPECT_EQ("/authorize", authorize.path());
  std::string state;
  ASSERT_TRUE(net::GetValueForKeyInQuery(authorize, "state", &state));
  std::string challenge_method;
  ASSERT_TRUE(net::GetValueForKeyInQuery(authorize, "code_challenge_method",
                                         &challenge_method));
  EXPECT_EQ("S256", challenge_method);

  // A redirect naming no live state is nobody's; it must not end the flow.
  EXPECT_FALSE(broker_->DeliverProviderCallback(
      "com.taffygo.browser://provider-auth?state=wrong&code=stolen"));
  EXPECT_FALSE(broker_->DeliverProviderCallback(
      "com.taffygo.browser://auth?state=" + state + "&code=misrouted"));
  EXPECT_TRUE(terminals_.empty());

  EXPECT_TRUE(broker_->DeliverProviderCallback(
      "com.taffygo.browser://provider-auth?state=" + state +
      "&code=auth-code-1"));
  Settle();

  ASSERT_EQ(1u, adapter_.sealed_codes().size());
  EXPECT_EQ("auth-code-1", adapter_.sealed_codes()[0].material);
  ASSERT_EQ(1u, terminals_.size());
  EXPECT_EQ(service::AuthCallbackStatus::kAuthorizationCode,
            terminals_[0].command->status);
  EXPECT_EQ(state, terminals_[0].command->returned_state);
  // The exchange spent the code at the compiled token endpoint and the
  // rotated triple landed under the provider id.
  EXPECT_EQ(1u, RequestCount(kTokenUrl));
  ASSERT_EQ(1u, adapter_.stored_records().size());
  EXPECT_EQ("test-vendor", adapter_.stored_records()[0].provider_id);
  EXPECT_EQ(account_mojom::ProviderFlowEventKind::kCompleted,
            EventKinds().back());
}

// Two vendor facts that decide whether a flow reaches the vendor at all, and
// neither is visible in any address: what the authorization page calls the
// address it answers to, and what the token endpoint requires beside the
// client id.
//
// Both fail in the same silent way when they are wrong. A vendor that never
// received a callback under the name it reads shows the person a consent
// screen that works and then answers nowhere; an exchange missing the field
// its vendor requires is refused as if the person had done something wrong.
TEST_F(ProfileProviderAuthBrokerPkceTest, ARowDecidesItsRedirectNameAndSecret) {
  constexpr char kAddress[] = "http://127.0.0.1:51121/oauth-callback";
  ProviderAuthVendor vendor = kPkceTestVendor;
  vendor.redirect = ProviderAuthRedirectKind::kTabInterception;
  vendor.registered_redirect_uri = kAddress;
  vendor.authorization_redirect_param = "callback_url";
  vendor.client_secret = "test-secret";
  ScopedProviderAuthVendorForTesting scoped(&vendor);
  RespondJson(kTokenUrl, R"({"access_token":"at-5","expires_in":600})");

  broker_->StartFlow("test-vendor", "flow-5", "binding-5", 9u);
  Settle();

  ASSERT_EQ(1u, adapter_.opened_urls().size());
  const GURL authorize(adapter_.opened_urls()[0]);
  std::string named;
  ASSERT_TRUE(net::GetValueForKeyInQuery(authorize, "callback_url", &named));
  EXPECT_EQ(kAddress, named);
  // The name is a rename and not an addition: the OAuth one is not also sent,
  // because a vendor honouring both would be answering two addresses.
  std::string standard;
  EXPECT_FALSE(
      net::GetValueForKeyInQuery(authorize, "redirect_uri", &standard));
  // A secret is a field of the exchange and never of an authorization page,
  // which is a URL this browser hands to a Custom Tab and a person can read.
  std::string leaked;
  EXPECT_FALSE(
      net::GetValueForKeyInQuery(authorize, "client_secret", &leaked));

  std::string state;
  ASSERT_TRUE(net::GetValueForKeyInQuery(authorize, "state", &state));
  EXPECT_TRUE(broker_->ClaimInterceptedRedirect(
      GURL(std::string(kAddress) + "?code=code-5&state=" + state)));
  Settle();

  ASSERT_EQ(1u, RequestCount(kTokenUrl));
  const std::string body = BodySentTo(kTokenUrl);
  EXPECT_NE(std::string::npos, body.find("client_secret=test-secret"));
  // The exchange names its redirect the way RFC 6749 does. The rename above
  // is a property of that vendor's authorization page and of nothing else.
  EXPECT_NE(std::string::npos,
            body.find("redirect_uri=http://127.0.0.1:51121/oauth-callback"));
  EXPECT_EQ(std::string::npos, body.find("callback_url="));
}

// A row with no secret sends no empty field. An empty `client_secret` is not
// the same request as no `client_secret`, and at least one authorization
// server refuses the first while accepting the second.
TEST_F(ProfileProviderAuthBrokerPkceTest, ARowWithNoSecretSendsNoEmptyField) {
  ScopedProviderAuthVendorForTesting scoped(&kPkceTestVendor);
  RespondJson(kTokenUrl, R"({"access_token":"at-6","expires_in":600})");
  broker_->StartFlow("test-vendor", "flow-6", "binding-6", 10u);
  Settle();
  const GURL authorize(adapter_.opened_urls()[0]);
  std::string state;
  ASSERT_TRUE(net::GetValueForKeyInQuery(authorize, "state", &state));

  EXPECT_TRUE(broker_->DeliverProviderCallback(
      "com.taffygo.browser://provider-auth?state=" + state + "&code=code-6"));
  Settle();

  ASSERT_EQ(1u, RequestCount(kTokenUrl));
  EXPECT_EQ(std::string::npos, BodySentTo(kTokenUrl).find("client_secret"));
}

TEST_F(ProfileProviderAuthBrokerPkceTest, ADeniedRedirectEndsThePkceFlow) {
  ScopedProviderAuthVendorForTesting scoped(&kPkceTestVendor);
  broker_->StartFlow("test-vendor", "flow-2", "binding-2", 3u);
  Settle();
  const GURL authorize(adapter_.opened_urls()[0]);
  std::string state;
  ASSERT_TRUE(net::GetValueForKeyInQuery(authorize, "state", &state));

  EXPECT_TRUE(broker_->DeliverProviderCallback(
      "com.taffygo.browser://provider-auth?state=" + state +
      "&error=access_denied"));
  Settle();

  ASSERT_EQ(1u, terminals_.size());
  EXPECT_EQ(service::AuthCallbackStatus::kDenied, terminals_[0].command->status);
  EXPECT_EQ(0u, RequestCount(kTokenUrl));
  EXPECT_EQ(account_mojom::ProviderFlowEventKind::kFailedDenied,
            EventKinds().back());
}

TEST_F(ProfileProviderAuthBrokerPkceTest,
       ARefusedTerminalEndsTheFlowWithoutStoring) {
  terminal_accepted_ = false;
  ScopedProviderAuthVendorForTesting scoped(&kPkceTestVendor);
  broker_->StartFlow("test-vendor", "flow-2", "binding-2", 3u);
  Settle();
  const GURL authorize(adapter_.opened_urls()[0]);
  std::string state;
  ASSERT_TRUE(net::GetValueForKeyInQuery(authorize, "state", &state));

  EXPECT_TRUE(broker_->DeliverProviderCallback(
      "com.taffygo.browser://provider-auth?state=" + state +
      "&code=auth-code-1"));
  Settle();

  // The core refused the terminal: no exchange may spend the code, nothing
  // may be stored, and the flow ends unavailable.
  EXPECT_EQ(0u, RequestCount(kTokenUrl));
  EXPECT_TRUE(adapter_.stored_records().empty());
  EXPECT_EQ(account_mojom::ProviderFlowEventKind::kFailedUnavailable,
            EventKinds().back());
}

}  // namespace
}  // namespace taffy
