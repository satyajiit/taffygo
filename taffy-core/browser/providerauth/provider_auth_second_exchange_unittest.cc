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

// The second exchange: the vendors whose first token is not the credential
// but buys one, what that request has to say about itself, and the two places
// the answer may name the host this particular credential belongs to.
//
// Split from `provider_auth_credential_unittest.cc`, which keeps the refresh
// leg, revocation and the minted key. The seam is the subject rather than the
// size: everything here is about a request this browser composes from one
// row's own facts and an address it then has to bound, and none of it shares a
// rule with the grant machinery beside it.
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
constexpr char kSecondaryUrl[] = "https://vendor.example/second";
// The domain the second-exchange row licenses, and one address under it.
constexpr char kCredentialHostSuffix[] = ".api.vendor.example";
// A row's second-exchange headers, at namespace scope for the same reason the
// shipping table's are: a span has to point at something that outlives it.
constexpr char kTestEditor[] = "TestEditor/9.9";
constexpr ProviderAuthAuthorizationParam kTestEditorHeaders[] = {
    {"editor-version", kTestEditor},
};
constexpr char kCredentialHost[] = "https://one.api.vendor.example";



constexpr ProviderAuthVendor kSecondExchangeVendor = {
    "test-second-exchange",
    ProviderAuthFlowKind::kDeviceCode,
    ProviderAuthRedirectKind::kManualCode,
    ProviderAuthExchangeKind::kOauthTokenPair,
    "",
    kDeviceCodeUrl,
    kTokenUrl,
    kSecondaryUrl,
    kCredentialHostSuffix,
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
    "read:user",
};
class ProviderAuthSecondExchangeTest : public testing::Test {
 protected:
  ProviderAuthSecondExchangeTest()
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
// One vendor's first token is not the API credential; it buys one. The
// credential is what the record carries, and the token that bought it is kept
// as the record's refresh material, because re-running that exchange is the
// only way this credential is ever renewed.
TEST_F(ProviderAuthSecondExchangeTest, TheSecondExchangeBuysTheRealCredential) {
  ScopedProviderAuthVendorForTesting scoped(&kSecondExchangeVendor);
  network_.AddResponse(kDeviceCodeUrl,
                       R"({"device_code":"dc-1","user_code":"ABCD",)"
                       R"("verification_uri":"https://vendor.example/d",)"
                       R"("interval":1})");
  network_.AddResponse(kTokenUrl, R"({"access_token":"primary-1"})");
  network_.AddResponse(
      kSecondaryUrl,
      R"({"token":"credential-1","endpoints":{"api":")" +
          std::string(kCredentialHost) + R"("}})");
  broker_->StartFlow("test-second-exchange", "flow-4", "binding-4", 8u);
  Settle();
  task_environment_.FastForwardBy(base::Seconds(1));
  Settle();

  EXPECT_EQ(1u, RequestCount(kSecondaryUrl));
  ASSERT_EQ(1u, adapter_.stored_records().size());
  const account_mojom::ProviderOauthRecordPtr &record =
      adapter_.stored_records()[0].record;
  EXPECT_EQ(std::vector<uint8_t>({'c', 'r', 'e', 'd', 'e', 'n', 't', 'i', 'a',
                                  'l', '-', '1'}),
            record->access_token);
  ASSERT_TRUE(record->refresh_token.has_value());
  EXPECT_EQ(std::vector<uint8_t>({'p', 'r', 'i', 'm', 'a', 'r', 'y', '-', '1'}),
            *record->refresh_token);
  // The address the exchange named travels with the credential it named it
  // for, because which address applies depends on the account behind it.
  EXPECT_EQ(std::optional<std::string>(kCredentialHost),
            record->credential_host);
  EXPECT_EQ(account_mojom::ProviderFlowEventKind::kCompleted, LastEventKind());
}

// What the second exchange presents, and what it says it is.
//
// This endpoint is not a published OAuth resource server: it is an internal
// one of that vendor's, and it wants the token under the legacy `token`
// keyword and a name for the client asking. Both are row facts, and both fail
// the same way when they are missing — a 401 that reads like an expired
// sign-in on a credential that was minted two seconds ago.
TEST_F(ProviderAuthSecondExchangeTest, TheSecondExchangeSaysWhoIsAsking) {
  ProviderAuthVendor vendor = kSecondExchangeVendor;
  vendor.secondary_authorization_scheme = "token";
  vendor.secondary_request_headers = kTestEditorHeaders;
  ScopedProviderAuthVendorForTesting scoped(&vendor);
  network_.AddResponse(kSecondaryUrl, R"({"token":"credential-4"})");

  broker_->RefreshCredential(
      "test-second-exchange", "primary-4",
      base::BindLambdaForTesting(
          [](account_mojom::ProviderOauthRecordPtr record, bool definitive) {
            EXPECT_TRUE(record);
          }));
  Settle();

  ASSERT_EQ(1u, RequestCount(kSecondaryUrl));
  EXPECT_EQ(std::optional<std::string>("token primary-4"),
            HeaderSentTo(kSecondaryUrl, "Authorization"));
  EXPECT_EQ(std::optional<std::string>(kTestEditor),
            HeaderSentTo(kSecondaryUrl, "editor-version"));
}

// A row that names no scheme presents the token the way RFC 6750 does, which
// is what every other exchange in this table does.
TEST_F(ProviderAuthSecondExchangeTest, ARowWithNoSchemePresentsABearer) {
  ScopedProviderAuthVendorForTesting scoped(&kSecondExchangeVendor);
  network_.AddResponse(kSecondaryUrl, R"({"token":"credential-5"})");
  broker_->RefreshCredential(
      "test-second-exchange", "primary-5",
      base::BindLambdaForTesting(
          [](account_mojom::ProviderOauthRecordPtr record, bool definitive) {}));
  Settle();

  EXPECT_EQ(std::optional<std::string>("Bearer primary-5"),
            HeaderSentTo(kSecondaryUrl, "Authorization"));
}

// The same address, said the other way the same vendor says it. Its minted
// credential is a list of its own fields, one of which names the proxy the
// account was routed to; the API host is that name with its first label
// changed. It is a fallback for the document above and is bounded by the same
// licensed domain, so a vendor cannot reach further through it than through
// the field it prefers.
TEST_F(ProviderAuthSecondExchangeTest, TheCredentialCanNameItsOwnHost) {
  ScopedProviderAuthVendorForTesting scoped(&kSecondExchangeVendor);
  network_.AddResponse(
      kSecondaryUrl,
      R"({"token":"tid=1;proxy-ep=proxy.api.vendor.example;exp=2"})");
  account_mojom::ProviderOauthRecordPtr rotated;
  broker_->RefreshCredential(
      "test-second-exchange", "primary-6",
      base::BindLambdaForTesting(
          [&](account_mojom::ProviderOauthRecordPtr record, bool definitive) {
            rotated = std::move(record);
          }));
  Settle();

  ASSERT_TRUE(rotated);
  EXPECT_EQ(std::optional<std::string>("https://api.api.vendor.example"),
            rotated->credential_host);
}

// The same field, naming somewhere the row does not license. It is refused
// exactly as the document is: the fallback reads a second place, never a
// second rule.
TEST_F(ProviderAuthSecondExchangeTest, ADerivedAddressIsBoundedTheSameWay) {
  ScopedProviderAuthVendorForTesting scoped(&kSecondExchangeVendor);
  network_.AddResponse(kSecondaryUrl,
                       R"({"token":"tid=1;proxy-ep=proxy.attacker.example"})");
  account_mojom::ProviderOauthRecordPtr rotated;
  broker_->RefreshCredential(
      "test-second-exchange", "primary-7",
      base::BindLambdaForTesting(
          [&](account_mojom::ProviderOauthRecordPtr record, bool definitive) {
            rotated = std::move(record);
          }));
  Settle();

  ASSERT_TRUE(rotated);
  EXPECT_FALSE(rotated->credential_host.has_value());
}

// An address outside the domain the row licenses is dropped, and the
// credential is kept: the exchange still answered, and a request addressed to
// the catalog's own host is the same request every other vendor's credential
// makes. A token response is a remote party's document, and one that could
// name any host would be one that could point a person's credential anywhere.
TEST_F(ProviderAuthSecondExchangeTest, AnUnlicensedAddressIsDroppedNotFollowed) {
  ScopedProviderAuthVendorForTesting scoped(&kSecondExchangeVendor);
  network_.AddResponse(
      kSecondaryUrl,
      R"({"token":"credential-3","endpoints":{"api":"https://attacker.example"}})");
  account_mojom::ProviderOauthRecordPtr rotated;
  broker_->RefreshCredential(
      "test-second-exchange", "primary-3",
      base::BindLambdaForTesting(
          [&](account_mojom::ProviderOauthRecordPtr record, bool definitive) {
            rotated = std::move(record);
            EXPECT_FALSE(definitive);
          }));
  Settle();

  ASSERT_TRUE(rotated);
  EXPECT_FALSE(rotated->credential_host.has_value());
}

TEST_F(ProviderAuthSecondExchangeTest, TheSecondExchangeIsAlsoTheRefreshLeg) {
  ScopedProviderAuthVendorForTesting scoped(&kSecondExchangeVendor);
  network_.AddResponse(kSecondaryUrl, R"({"token":"credential-2"})");
  account_mojom::ProviderOauthRecordPtr rotated;
  broker_->RefreshCredential(
      "test-second-exchange", "primary-1",
      base::BindLambdaForTesting(
          [&](account_mojom::ProviderOauthRecordPtr record, bool definitive) {
            rotated = std::move(record);
            EXPECT_FALSE(definitive);
          }));
  Settle();

  // The OAuth token endpoint is never asked: this shape has no refresh grant.
  EXPECT_EQ(0u, RequestCount(kTokenUrl));
  EXPECT_EQ(1u, RequestCount(kSecondaryUrl));
  ASSERT_TRUE(rotated);
  ASSERT_TRUE(rotated->refresh_token.has_value());
  EXPECT_EQ(std::vector<uint8_t>({'p', 'r', 'i', 'm', 'a', 'r', 'y', '-', '1'}),
            *rotated->refresh_token);
}

}  // namespace
}  // namespace taffy
