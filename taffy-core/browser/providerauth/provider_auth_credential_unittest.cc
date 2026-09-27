// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <optional>
#include <string>
#include <string_view>
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
#include "net/http/http_request_headers.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "taffy/browser/account/fake_profile_platform_adapter.h"
#include "taffy/browser/account/profile_account_broker.h"
#include "taffy/browser/providerauth/profile_provider_auth_broker.h"
#include "taffy/browser/providerauth/provider_auth_configuration.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

// What becomes of a credential after the grant: the one refresh leg and its
// single flight, the second exchange that some vendors need before a token is
// a credential at all, revocation on sign-out, and the exchange that mints a
// permanent key instead of a grant — which has no refresh leg by shape rather
// than by branch.
//
// Every vendor here rides the scoped override, because no shipping row
// carries a dated terms review and so no shipping row can run a flow
// (decision 0095 section 1).

namespace taffy {
namespace {

namespace account_mojom = browser::account::mojom;
namespace service = core_service::mojom;

constexpr char kDeviceCodeUrl[] = "https://vendor.example/device";
constexpr char kTokenUrl[] = "https://vendor.example/token";
constexpr char kRevocationUrl[] = "https://vendor.example/revoke";
constexpr char kKeyMintUrl[] = "https://vendor.example/keys";

constexpr ProviderAuthVendor kTokenPairVendor = {
    "test-token-pair",
    ProviderAuthFlowKind::kDeviceCode,
    ProviderAuthRedirectKind::kManualCode,
    ProviderAuthExchangeKind::kOauthTokenPair,
    "",
    kDeviceCodeUrl,
    kTokenUrl,
    "",
    "",
    "",
    kRevocationUrl,
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

constexpr ProviderAuthVendor kMintedKeyVendor = {
    "test-minted-key",
    ProviderAuthFlowKind::kPkce,
    ProviderAuthRedirectKind::kManualCode,
    ProviderAuthExchangeKind::kMintedApiKey,
    "https://vendor.example/authorize",
    "",
    kKeyMintUrl,
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
    "profile",
};


class ProviderAuthCredentialTest : public testing::Test {
 protected:
  ProviderAuthCredentialTest()
      : task_environment_(base::test::TaskEnvironment::TimeSource::MOCK_TIME) {
    account_ = std::make_unique<ProfileAccountBroker>(&browser_context_);
    account_->BindPlatformAdapter(adapter_.BindNewPipeAndPassRemote());
    broker_ = std::make_unique<ProfileProviderAuthBroker>(
        base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
            &network_),
        account_.get());
    broker_->SetFlowTerminalCallback(base::BindLambdaForTesting(
        [](uint64_t generation,
           service::ProviderAuthCallbackCommandPtr command,
           base::OnceCallback<void(bool)> submitted) {
          std::move(submitted).Run(true);
        }));
    network_.SetInterceptor(base::BindLambdaForTesting(
        [this](const network::ResourceRequest &request) {
          requested_urls_.push_back(request.url.spec());
          requested_headers_.push_back(request.headers);
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
  std::vector<std::string> requested_urls_;
  std::vector<net::HttpRequestHeaders> requested_headers_;

  // The headers of the one request that went to this address.
  std::optional<std::string> HeaderSentTo(const std::string &url,
                                          std::string_view name) const {
    for (size_t index = 0; index < requested_urls_.size(); ++index) {
      if (requested_urls_[index] == url) {
        return requested_headers_[index].GetHeader(name);
      }
    }
    return std::nullopt;
  }
};

TEST_F(ProviderAuthCredentialTest, RefreshIsSingleFlightPerProvider) {
  ScopedProviderAuthVendorForTesting scoped(&kTokenPairVendor);
  account_mojom::ProviderOauthRecordPtr first_record;
  bool first_definitive = true;
  bool second_ran = false;
  account_mojom::ProviderOauthRecordPtr second_record;
  broker_->RefreshCredential(
      "test-token-pair", "rt-old",
      base::BindLambdaForTesting(
          [&](account_mojom::ProviderOauthRecordPtr record, bool definitive) {
            first_record = std::move(record);
            first_definitive = definitive;
          }));
  broker_->RefreshCredential(
      "test-token-pair", "rt-old",
      base::BindLambdaForTesting(
          [&](account_mojom::ProviderOauthRecordPtr record, bool definitive) {
            second_ran = true;
            second_record = std::move(record);
          }));
  Settle();
  // Two callers, one flight: the rotation credential is spent once.
  EXPECT_EQ(1u, RequestCount(kTokenUrl));

  network_.AddResponse(kTokenUrl,
                       R"({"access_token":"at-3","refresh_token":"rt-3",)"
                       R"("expires_in":3600})");
  Settle();

  // Every joined caller is answered; only the first owns the record.
  ASSERT_TRUE(first_record);
  EXPECT_FALSE(first_definitive);
  EXPECT_EQ(std::vector<uint8_t>({'a', 't', '-', '3'}),
            first_record->access_token);
  EXPECT_TRUE(second_ran);
  EXPECT_FALSE(second_record);
}

TEST_F(ProviderAuthCredentialTest, InvalidGrantIsTheOneDefinitiveRefusal) {
  ScopedProviderAuthVendorForTesting scoped(&kTokenPairVendor);
  network_.AddResponse(kTokenUrl, R"({"error":"invalid_grant"})");
  std::optional<bool> definitive;
  broker_->RefreshCredential(
      "test-token-pair", "rt-dead",
      base::BindLambdaForTesting(
          [&](account_mojom::ProviderOauthRecordPtr record,
              bool was_definitive) {
            EXPECT_FALSE(record);
            definitive = was_definitive;
          }));
  Settle();
  EXPECT_EQ(true, definitive);

  // Anything else about the failure is the network's fault, not the grant's.
  network_.AddResponse(kTokenUrl, "not json");
  definitive.reset();
  broker_->RefreshCredential(
      "test-token-pair", "rt-maybe",
      base::BindLambdaForTesting(
          [&](account_mojom::ProviderOauthRecordPtr record,
              bool was_definitive) {
            EXPECT_FALSE(record);
            definitive = was_definitive;
          }));
  Settle();
  EXPECT_EQ(false, definitive);
}

TEST_F(ProviderAuthCredentialTest, RevocationIsFireAndForgetWhereOneExists) {
  ScopedProviderAuthVendorForTesting scoped(&kTokenPairVendor);
  network_.AddResponse(kRevocationUrl, "{}");
  broker_->RevokeBestEffort("test-token-pair", "at-1");
  Settle();
  EXPECT_EQ(1u, RequestCount(kRevocationUrl));

  // A vendor that publishes no revocation endpoint gets no post at all.
  const size_t before = requested_urls_.size();
  broker_->RevokeBestEffort("xai", "at-2");
  Settle();
  EXPECT_EQ(before, requested_urls_.size());
}

// A minted key is not a subscription token and must not be filed as one: the
// OAuth store is behind the refresh machinery, and there is no refresh leg
// for a key nothing reissues. It is filed where a pasted key is filed, by the
// method a pasted key is filed with.
TEST_F(ProviderAuthCredentialTest, AMintedKeyIsFiledWhereAPastedKeyIs) {
  ScopedProviderAuthVendorForTesting scoped(&kMintedKeyVendor);
  network_.AddResponse(kKeyMintUrl, R"({"key":"sk-minted-1"})");
  broker_->StartFlow("test-minted-key", "flow-3", "binding-3", 2u);
  Settle();

  EXPECT_TRUE(broker_->SubmitManualCode("flow-3", "code-1#" + OpenedState()));
  Settle();

  EXPECT_EQ(1u, RequestCount(kKeyMintUrl));
  EXPECT_TRUE(adapter_.stored_records().empty());
  ASSERT_EQ(1u, adapter_.stored_keys().size());
  EXPECT_EQ("test-minted-key", adapter_.stored_keys()[0].provider_id);
  EXPECT_EQ("sk-minted-1", adapter_.stored_keys()[0].key);
  EXPECT_EQ(account_mojom::ProviderFlowEventKind::kCompleted, LastEventKind());

  // And there is no refresh leg to run, so asking for one costs no request.
  const size_t before = requested_urls_.size();
  bool answered = false;
  broker_->RefreshCredential(
      "test-minted-key", "anything",
      base::BindLambdaForTesting(
          [&](account_mojom::ProviderOauthRecordPtr record, bool definitive) {
            answered = true;
            EXPECT_FALSE(record);
            EXPECT_FALSE(definitive);
          }));
  Settle();
  EXPECT_TRUE(answered);
  EXPECT_EQ(before, requested_urls_.size());
}

}  // namespace
}  // namespace taffy
