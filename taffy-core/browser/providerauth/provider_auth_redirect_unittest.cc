// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/memory/scoped_refptr.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "net/base/url_util.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "taffy/browser/account/fake_profile_platform_adapter.h"
#include "taffy/browser/account/profile_account_broker.h"
#include "taffy/browser/providerauth/profile_provider_auth_broker.h"
#include "taffy/browser/providerauth/provider_auth_configuration.h"
#include "taffy/browser/providerauth/provider_auth_wire.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

// How a code gets back into the browser after decision 0095: interception
// with the navigation cancelled before it connects, the manual fallback on
// the product's own chrome, a person's cancel, and the one vendor deviation
// that changes what the authorization request carries — a `state` that is
// the PKCE verifier.
//
// The throttle itself is not exercised here and holds nothing worth
// exercising: it reads a profile off a navigation and asks the broker. Every
// decision — which address belongs to a running flow, which flow a state
// claims, and what a claimed redirect does — is in this class and is dictated
// here.

namespace taffy {
namespace {

namespace account_mojom = browser::account::mojom;
namespace service = core_service::mojom;

constexpr char kRedirectUri[] = "https://vendor.example/callback";
constexpr char kTokenUrl[] = "https://vendor.example/token";

constexpr ProviderAuthVendor kInterceptVendor = {
    "test-intercept",
    ProviderAuthFlowKind::kPkce,
    ProviderAuthRedirectKind::kTabInterception,
    ProviderAuthExchangeKind::kOauthTokenPair,
    "https://vendor.example/authorize",
    "",
    kTokenUrl,
    "",
    "",
    "",
    "",
    kRedirectUri,
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
    "profile",
};

// The deviation one vendor requires: `state` is the verifier, and the
// authorization request asks for the code to be displayed rather than
// redirected, so the manual path is the only one this row has.
constexpr ProviderAuthVendor kVerifierStateVendor = {
    "test-verifier-state",
    ProviderAuthFlowKind::kPkce,
    ProviderAuthRedirectKind::kManualCode,
    ProviderAuthExchangeKind::kOauthTokenPair,
    "https://vendor.example/authorize",
    "",
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
    /*authorization_displays_code=*/true,
    /*state_is_pkce_verifier=*/true,
    /*presents_client_identity=*/true,
    "test-client",
    "",
    "profile",
};

class ProviderAuthRedirectTest : public testing::Test {
 protected:
  ProviderAuthRedirectTest()
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
          terminals_.push_back(std::move(command));
          std::move(submitted).Run(true);
        }));
    network_.SetInterceptor(base::BindLambdaForTesting(
        [this](const network::ResourceRequest &request) {
          requested_urls_.push_back(request.url.spec());
        }));
  }

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

  // The state the authorization request actually carried.
  std::string OpenedState() const {
    if (adapter_.opened_urls().empty()) {
      return std::string();
    }
    std::string state;
    net::GetValueForKeyInQuery(GURL(adapter_.opened_urls()[0]), "state",
                               &state);
    return state;
  }

  account_mojom::ProviderFlowEventKind LastEventKind() const {
    return adapter_.events().empty()
               ? account_mojom::ProviderFlowEventKind::kFailedProvider
               : adapter_.events().back()->kind;
  }

  content::BrowserTaskEnvironment task_environment_;
  content::TestBrowserContext browser_context_;
  FakeProfilePlatformAdapter adapter_;
  std::unique_ptr<ProfileAccountBroker> account_;
  network::TestURLLoaderFactory network_;
  std::unique_ptr<ProfileProviderAuthBroker> broker_;
  std::vector<service::ProviderAuthCallbackCommandPtr> terminals_;
  std::vector<std::string> requested_urls_;
};

TEST_F(ProviderAuthRedirectTest, TheRegisteredRedirectIsClaimedAndTakenApart) {
  ScopedProviderAuthVendorForTesting scoped(&kInterceptVendor);
  EXPECT_FALSE(broker_->HasInterceptableRedirect());
  network_.AddResponse(kTokenUrl,
                       R"({"access_token":"at-1","expires_in":600})");
  broker_->StartFlow("test-intercept", "flow-1", "binding-1", 4u);
  Settle();
  EXPECT_TRUE(broker_->HasInterceptableRedirect());
  const std::string state = OpenedState();
  ASSERT_FALSE(state.empty());

  // An address no running flow presented is somebody's ordinary navigation.
  EXPECT_FALSE(broker_->ClaimInterceptedRedirect(
      GURL("https://elsewhere.example/callback?code=c&state=" + state)));
  // The right address with a state nothing minted is not this flow's either.
  EXPECT_FALSE(broker_->ClaimInterceptedRedirect(
      GURL(std::string(kRedirectUri) + "?code=c&state=not-the-state")));
  EXPECT_TRUE(terminals_.empty());

  EXPECT_TRUE(broker_->ClaimInterceptedRedirect(
      GURL(std::string(kRedirectUri) + "?code=auth-code-1&state=" + state)));
  Settle();

  ASSERT_EQ(1u, adapter_.sealed_codes().size());
  EXPECT_EQ("auth-code-1", adapter_.sealed_codes()[0].material);
  ASSERT_EQ(1u, terminals_.size());
  EXPECT_EQ(service::AuthCallbackStatus::kAuthorizationCode,
            terminals_[0]->status);
  EXPECT_EQ(1u, RequestCount(kTokenUrl));
  EXPECT_EQ(account_mojom::ProviderFlowEventKind::kCompleted, LastEventKind());
  EXPECT_FALSE(broker_->HasInterceptableRedirect());
}

TEST_F(ProviderAuthRedirectTest, ADeniedRedirectIsClaimedTheSameWay) {
  ScopedProviderAuthVendorForTesting scoped(&kInterceptVendor);
  broker_->StartFlow("test-intercept", "flow-1", "binding-1", 4u);
  Settle();

  EXPECT_TRUE(broker_->ClaimInterceptedRedirect(GURL(
      std::string(kRedirectUri) + "?error=access_denied&state=" +
      OpenedState())));
  Settle();
  ASSERT_EQ(1u, terminals_.size());
  EXPECT_EQ(service::AuthCallbackStatus::kDenied, terminals_[0]->status);
  EXPECT_EQ(0u, RequestCount(kTokenUrl));
}

// The fallback ships with the interception, not after it: interception
// depends on a vendor keeping an address the product recognises, and a flow
// with no fallback breaks silently the day one is edited.
TEST_F(ProviderAuthRedirectTest, AHandEnteredCodeFinishesTheSameFlow) {
  ScopedProviderAuthVendorForTesting scoped(&kVerifierStateVendor);
  network_.AddResponse(kTokenUrl,
                       R"({"access_token":"at-2","expires_in":600})");
  broker_->StartFlow("test-verifier-state", "flow-2", "binding-2", 6u);
  Settle();
  EXPECT_FALSE(broker_->HasInterceptableRedirect());
  const std::string state = OpenedState();
  ASSERT_FALSE(state.empty());

  // A flow nobody is running, and a state that is not this flow's, both
  // change nothing at all — the person can simply try again.
  EXPECT_FALSE(broker_->SubmitManualCode("flow-absent", "code-1"));
  EXPECT_FALSE(broker_->SubmitManualCode("flow-2", "code-1#not-the-state"));
  EXPECT_FALSE(broker_->SubmitManualCode("flow-2", std::string()));
  EXPECT_TRUE(terminals_.empty());

  EXPECT_TRUE(broker_->SubmitManualCode("flow-2", "code-1#" + state));
  Settle();
  ASSERT_EQ(1u, adapter_.sealed_codes().size());
  EXPECT_EQ("code-1", adapter_.sealed_codes()[0].material);
  ASSERT_EQ(1u, terminals_.size());
  EXPECT_EQ(service::AuthCallbackStatus::kAuthorizationCode,
            terminals_[0]->status);
}

// A person whose interception did not fire has the whole address in front of
// them and nothing else, so the whole address is what the fallback takes.
//
// Without this the pasted address becomes the authorization code, the vendor
// refuses it, and the person is told the vendor said no — on the one path
// that exists because the vendor's own answer did not arrive.
TEST_F(ProviderAuthRedirectTest, APastedCallbackAddressFinishesTheFlow) {
  ScopedProviderAuthVendorForTesting scoped(&kInterceptVendor);
  network_.AddResponse(kTokenUrl,
                       R"({"access_token":"at-3","expires_in":600})");
  broker_->StartFlow("test-intercept", "flow-3", "binding-3", 7u);
  Settle();
  const std::string state = OpenedState();
  ASSERT_FALSE(state.empty());

  // The same address with somebody else's state, and one with no code at all,
  // change nothing: an address is read exactly as an intercepted one is.
  EXPECT_FALSE(broker_->SubmitManualCode(
      "flow-3", std::string(kRedirectUri) + "?code=c&state=not-the-state"));
  EXPECT_FALSE(broker_->SubmitManualCode(
      "flow-3", std::string(kRedirectUri) + "?state=" + state));
  EXPECT_TRUE(terminals_.empty());

  EXPECT_TRUE(broker_->SubmitManualCode(
      "flow-3", std::string(kRedirectUri) + "?code=code-3&state=" + state));
  Settle();
  ASSERT_EQ(1u, adapter_.sealed_codes().size());
  EXPECT_EQ("code-3", adapter_.sealed_codes()[0].material);
  ASSERT_EQ(1u, terminals_.size());
  EXPECT_EQ(service::AuthCallbackStatus::kAuthorizationCode,
            terminals_[0]->status);
}

// One refusal, spelled the way at least one of these vendors spells it. RFC
// 6749 names `access_denied` and this is the other name for the same act; a
// reader that knew only the first would turn a person declining a consent
// screen into a provider error, which is the outcome that reads as the
// product's fault and asks them to try again.
TEST_F(ProviderAuthRedirectTest, TheOtherSpellingOfARefusalIsStillARefusal) {
  ScopedProviderAuthVendorForTesting scoped(&kInterceptVendor);
  broker_->StartFlow("test-intercept", "flow-4", "binding-4", 8u);
  Settle();

  EXPECT_TRUE(broker_->ClaimInterceptedRedirect(
      GURL(std::string(kRedirectUri) + "?error=authorization_denied&state=" +
           OpenedState())));
  Settle();
  ASSERT_EQ(1u, terminals_.size());
  EXPECT_EQ(service::AuthCallbackStatus::kDenied, terminals_[0]->status);
  EXPECT_EQ(0u, RequestCount(kTokenUrl));
}

// The vendor is told to display the code, so the request asks for it, and the
// value in `state` is the PKCE verifier — which is what that vendor checks.
// What must not follow is the verifier crossing into the isolated core: the
// terminal echoes the flow's own nonce, which is a different value.
TEST_F(ProviderAuthRedirectTest, TheVerifierStateStaysInTheBrowser) {
  ScopedProviderAuthVendorForTesting scoped(&kVerifierStateVendor);
  network_.AddResponse(kTokenUrl,
                       R"({"access_token":"at-2","expires_in":600})");
  broker_->StartFlow("test-verifier-state", "flow-2", "binding-2", 6u);
  Settle();

  const GURL authorize(adapter_.opened_urls()[0]);
  std::string challenge;
  ASSERT_TRUE(net::GetValueForKeyInQuery(authorize, "code_challenge",
                                         &challenge));
  std::string displays_code;
  ASSERT_TRUE(net::GetValueForKeyInQuery(authorize, "code", &displays_code));
  EXPECT_EQ("true", displays_code);
  const std::string state = OpenedState();
  // The state is the verifier: its S256 challenge is the one that was sent.
  EXPECT_EQ(challenge, provider_auth::PkceChallenge(state));

  EXPECT_TRUE(broker_->SubmitManualCode("flow-2", "code-1#" + state));
  Settle();
  ASSERT_EQ(1u, terminals_.size());
  EXPECT_FALSE(terminals_[0]->returned_state.empty());
  EXPECT_NE(state, terminals_[0]->returned_state);
}

TEST_F(ProviderAuthRedirectTest, APersonCanStopAFlowTheyStarted) {
  ScopedProviderAuthVendorForTesting scoped(&kInterceptVendor);
  broker_->StartFlow("test-intercept", "flow-1", "binding-1", 4u);
  Settle();

  EXPECT_FALSE(broker_->CancelFlow("flow-absent"));
  EXPECT_TRUE(broker_->CancelFlow("flow-1"));
  Settle();
  ASSERT_EQ(1u, terminals_.size());
  EXPECT_EQ(service::AuthCallbackStatus::kDenied, terminals_[0]->status);
  EXPECT_EQ(account_mojom::ProviderFlowEventKind::kFailedDenied,
            LastEventKind());

  // The flow is gone, so nothing can claim it and cancelling twice is a
  // second tap on a screen that has already closed.
  EXPECT_FALSE(broker_->CancelFlow("flow-1"));
  EXPECT_FALSE(broker_->ClaimInterceptedRedirect(
      GURL(std::string(kRedirectUri) + "?code=c&state=" + OpenedState())));
  EXPECT_EQ(1u, terminals_.size());
}

}  // namespace
}  // namespace taffy
