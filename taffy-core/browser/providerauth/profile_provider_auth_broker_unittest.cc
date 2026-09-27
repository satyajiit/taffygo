// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

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

class ProfileProviderAuthBrokerTest : public testing::Test {
 protected:
  ProfileProviderAuthBrokerTest()
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

TEST_F(ProfileProviderAuthBrokerTest,
       AVendorWithNoDatedReviewGetsItsOneTerminal) {
  // No shipping row is in this state any more — every one of the seven is
  // dated — so the row under test is a dictated one. That is not a weaker
  // test than naming a shipping vendor was: what is being checked is the
  // refusal, and the refusal is a property of an undated row rather than of
  // any particular vendor. A row that becomes undated tomorrow, because a
  // review was withdrawn, has to behave exactly like this one.
  ProviderAuthVendor undated = kDeviceTestVendor;
  undated.terms_reviewed_on = "";
  ScopedProviderAuthVendorForTesting scoped(&undated);

  broker_->StartFlow("test-device", "flow-1", "binding-1", 5u);
  Settle();

  ASSERT_EQ(1u, terminals_.size());
  EXPECT_EQ(5u, terminals_[0].generation);
  EXPECT_EQ("flow-1", terminals_[0].command->flow_id);
  EXPECT_EQ("binding-1", terminals_[0].command->redirect_binding_id);
  EXPECT_EQ(service::AuthCallbackStatus::kPlatformUnavailable,
            terminals_[0].command->status);
  EXPECT_FALSE(terminals_[0].command->authorization_code_handle.has_value());
  ASSERT_EQ(1u, adapter_.events().size());
  EXPECT_EQ(account_mojom::ProviderFlowEventKind::kFailedUnavailable,
            adapter_.events()[0]->kind);
  EXPECT_TRUE(requested_urls_.empty());
}

TEST_F(ProfileProviderAuthBrokerTest, AnUnknownProviderGetsItsOneTerminal) {
  broker_->StartFlow("moonshot", "flow-1", "binding-1", 1u);
  Settle();

  ASSERT_EQ(1u, terminals_.size());
  EXPECT_EQ(service::AuthCallbackStatus::kPlatformUnavailable,
            terminals_[0].command->status);
}

TEST_F(ProfileProviderAuthBrokerTest, TheDeviceFlowSignsInEndToEnd) {
  ScopedProviderAuthVendorForTesting scoped(&kDeviceTestVendor);
  RespondJson(kDeviceCodeUrl,
              R"({"device_code":"dc-1","user_code":"ABCD-EFGH",)"
              R"("verification_uri":"https://vendor.example/device","interval":1})");
  RespondJson(kTokenUrl,
              R"({"access_token":"at-1","refresh_token":"rt-1",)"
              R"("token_type":"Bearer","expires_in":3600,"scope":"api"})");

  broker_->StartFlow("test-device", "flow-1", "binding-1", 7u);
  Settle();

  // The person has their code; nothing has been polled yet.
  ASSERT_EQ(1u, adapter_.events().size());
  EXPECT_EQ(account_mojom::ProviderFlowEventKind::kUserCodeReady,
            adapter_.events()[0]->kind);
  EXPECT_EQ("https://vendor.example/device",
            adapter_.events()[0]->verification_url.value_or(""));
  EXPECT_EQ("ABCD-EFGH", adapter_.events()[0]->user_code.value_or(""));
  EXPECT_EQ(0u, RequestCount(kTokenUrl));

  task_environment_.FastForwardBy(base::Seconds(1));
  Settle();

  // One poll exchanged the grant. The RFC 8628 device_code is the grant, so
  // it is what was sealed for the terminal, and the handle was dropped right
  // after the terminal was submitted.
  EXPECT_EQ(1u, RequestCount(kTokenUrl));
  ASSERT_EQ(1u, adapter_.sealed_codes().size());
  EXPECT_EQ("flow-1", adapter_.sealed_codes()[0].flow_id);
  EXPECT_EQ("dc-1", adapter_.sealed_codes()[0].material);
  ASSERT_EQ(1u, terminals_.size());
  EXPECT_EQ(7u, terminals_[0].generation);
  EXPECT_EQ(service::AuthCallbackStatus::kAuthorizationCode,
            terminals_[0].command->status);
  EXPECT_EQ("sealed-1",
            terminals_[0].command->authorization_code_handle.value_or(""));
  EXPECT_FALSE(terminals_[0].command->returned_state.empty());
  ASSERT_EQ(1u, adapter_.deleted_handles().size());
  EXPECT_EQ("sealed-1", adapter_.deleted_handles()[0]);

  // The tokens landed under the provider id, and the flow announced itself
  // complete — the roster's Connected comes from the core, not from here.
  ASSERT_EQ(1u, adapter_.stored_records().size());
  EXPECT_EQ("test-device", adapter_.stored_records()[0].provider_id);
  EXPECT_FALSE(adapter_.stored_records()[0].rotation);
  const account_mojom::ProviderOauthRecordPtr &record =
      adapter_.stored_records()[0].record;
  EXPECT_EQ("Bearer", record->token_type);
  EXPECT_EQ(std::vector<uint8_t>({'a', 't', '-', '1'}), record->access_token);
  ASSERT_TRUE(record->refresh_token.has_value());
  EXPECT_EQ(std::vector<uint8_t>({'r', 't', '-', '1'}), *record->refresh_token);
  const std::vector<account_mojom::ProviderFlowEventKind> kinds = EventKinds();
  ASSERT_EQ(3u, kinds.size());
  EXPECT_EQ(account_mojom::ProviderFlowEventKind::kExchanging, kinds[1]);
  EXPECT_EQ(account_mojom::ProviderFlowEventKind::kCompleted, kinds[2]);

  // The flow is gone: its identity can never mint a second terminal.
  broker_->StartFlow("test-device", "flow-1", "binding-1", 7u);
  Settle();
  task_environment_.FastForwardBy(base::Seconds(1));
  Settle();
  EXPECT_EQ(2u, terminals_.size());
}

// A vendor that publishes the complete address has put the code inside it,
// so that is what a person is offered; the code is still carried beside it
// for the person reading it off the phone they are holding.
TEST_F(ProfileProviderAuthBrokerTest, TheCompleteVerificationAddressIsShown) {
  ScopedProviderAuthVendorForTesting scoped(&kDeviceTestVendor);
  RespondJson(kDeviceCodeUrl,
              R"({"device_code":"dc-1","user_code":"ABCD",)"
              R"("verification_uri":"https://vendor.example/device",)"
              R"("verification_uri_complete":"https://vendor.example/device?u=ABCD",)"
              R"("interval":1})");
  broker_->StartFlow("test-device", "flow-1", "binding-1", 1u);
  Settle();

  ASSERT_EQ(1u, adapter_.events().size());
  EXPECT_EQ("https://vendor.example/device?u=ABCD",
            adapter_.events()[0]->verification_url.value_or(""));
  EXPECT_EQ("ABCD", adapter_.events()[0]->user_code.value_or(""));
}

TEST_F(ProfileProviderAuthBrokerTest, SlowDownWidensThePollInterval) {
  ScopedProviderAuthVendorForTesting scoped(&kDeviceTestVendor);
  RespondJson(kDeviceCodeUrl,
              R"({"device_code":"dc-1","user_code":"ABCD",)"
              R"("verification_uri":"https://vendor.example/device","interval":1})");
  RespondJson(kTokenUrl, R"({"error":"slow_down"})");
  broker_->StartFlow("test-device", "flow-1", "binding-1", 1u);
  Settle();

  task_environment_.FastForwardBy(base::Seconds(1));
  Settle();
  EXPECT_EQ(1u, RequestCount(kTokenUrl));

  // The vendor asked for room: the next poll is one slow-down step later,
  // so the old cadence must not produce a request.
  RespondJson(kTokenUrl, R"({"error":"authorization_pending"})");
  task_environment_.FastForwardBy(base::Seconds(1));
  Settle();
  EXPECT_EQ(1u, RequestCount(kTokenUrl));
  task_environment_.FastForwardBy(base::Seconds(5));
  Settle();
  EXPECT_EQ(2u, RequestCount(kTokenUrl));
  EXPECT_TRUE(terminals_.empty());
}

TEST_F(ProfileProviderAuthBrokerTest, AccessDeniedEndsTheFlowDenied) {
  ScopedProviderAuthVendorForTesting scoped(&kDeviceTestVendor);
  RespondJson(kDeviceCodeUrl,
              R"({"device_code":"dc-1","user_code":"ABCD",)"
              R"("verification_uri":"https://vendor.example/device","interval":1})");
  RespondJson(kTokenUrl, R"({"error":"access_denied"})");
  broker_->StartFlow("test-device", "flow-1", "binding-1", 1u);
  Settle();
  task_environment_.FastForwardBy(base::Seconds(1));
  Settle();

  ASSERT_EQ(1u, terminals_.size());
  EXPECT_EQ(service::AuthCallbackStatus::kDenied, terminals_[0].command->status);
  EXPECT_EQ(account_mojom::ProviderFlowEventKind::kFailedDenied,
            EventKinds().back());
  EXPECT_TRUE(adapter_.stored_records().empty());
}

TEST_F(ProfileProviderAuthBrokerTest, AuthorizationDeniedEndsTheFlowDenied) {
  // The second spelling of the same answer. A vendor whose row this binary
  // carries says `authorization_denied`, and read as anything but a refusal a
  // person who declined is told the vendor failed — which invites them back
  // to a screen they have just said no to.
  ScopedProviderAuthVendorForTesting scoped(&kDeviceTestVendor);
  RespondJson(kDeviceCodeUrl,
              R"({"device_code":"dc-1","user_code":"ABCD",)"
              R"("verification_uri":"https://vendor.example/device","interval":1})");
  RespondJson(kTokenUrl, R"({"error":"authorization_denied"})");
  broker_->StartFlow("test-device", "flow-1", "binding-1", 1u);
  Settle();
  task_environment_.FastForwardBy(base::Seconds(1));
  Settle();

  ASSERT_EQ(1u, terminals_.size());
  EXPECT_EQ(service::AuthCallbackStatus::kDenied, terminals_[0].command->status);
  EXPECT_EQ(account_mojom::ProviderFlowEventKind::kFailedDenied,
            EventKinds().back());
  EXPECT_TRUE(adapter_.stored_records().empty());
}

TEST_F(ProfileProviderAuthBrokerTest, PendingIsReadFromTheBodyNotTheStatus) {
  // RFC 8628 does not say which status carries `authorization_pending`, and
  // at least one vendor whose row this binary carries answers 400 rather than
  // 200. Judged on the status first, every healthy flow would abort on its
  // very first poll. This passes today only because the loader is built with
  // SetAllowHttpErrorResults(true), which is a decision made elsewhere for
  // another reason — so it is pinned here, where the behaviour is the point.
  ScopedProviderAuthVendorForTesting scoped(&kDeviceTestVendor);
  RespondJson(kDeviceCodeUrl,
              R"({"device_code":"dc-1","user_code":"ABCD",)"
              R"("verification_uri":"https://vendor.example/device","interval":1})");
  network_.AddResponse(kTokenUrl, R"({"error":"authorization_pending"})",
                       net::HTTP_BAD_REQUEST);
  broker_->StartFlow("test-device", "flow-1", "binding-1", 1u);
  Settle();
  task_environment_.FastForwardBy(base::Seconds(1));
  Settle();

  // Still waiting: no terminal, and the poll was scheduled again rather than
  // the flow being ended by a status nobody read as an answer.
  EXPECT_TRUE(terminals_.empty());
  EXPECT_EQ(account_mojom::ProviderFlowEventKind::kUserCodeReady,
            EventKinds().back());

  network_.AddResponse(kTokenUrl,
                       R"({"access_token":"at-1","refresh_token":"rt-1",)"
                       R"("expires_in":3600})");
  task_environment_.FastForwardBy(base::Seconds(1));
  Settle();
  ASSERT_EQ(1u, terminals_.size());
  EXPECT_EQ(service::AuthCallbackStatus::kAuthorizationCode,
            terminals_[0].command->status);
}

TEST_F(ProfileProviderAuthBrokerTest, TheDeadlineEndsAFlowNobodyFinishes) {
  ScopedProviderAuthVendorForTesting scoped(&kDeviceTestVendor);
  RespondJson(kDeviceCodeUrl,
              R"({"device_code":"dc-1","user_code":"ABCD",)"
              R"("verification_uri":"https://vendor.example/device","interval":5})");
  RespondJson(kTokenUrl, R"({"error":"authorization_pending"})");
  broker_->StartFlow("test-device", "flow-1", "binding-1", 1u);
  Settle();

  task_environment_.FastForwardBy(
      base::Minutes(kProviderAuthFlowDeadlineMinutes));
  Settle();

  ASSERT_EQ(1u, terminals_.size());
  EXPECT_EQ(service::AuthCallbackStatus::kDeadlineExceeded,
            terminals_[0].command->status);
  EXPECT_EQ(account_mojom::ProviderFlowEventKind::kFailedDeadline,
            EventKinds().back());

  // The flow is gone, so its poll cadence died with it.
  const size_t polls = RequestCount(kTokenUrl);
  task_environment_.FastForwardBy(base::Minutes(1));
  Settle();
  EXPECT_EQ(polls, RequestCount(kTokenUrl));
}

TEST_F(ProfileProviderAuthBrokerTest,
       AGenerationLossAbortsOnlyItsExactFlowsWithoutATerminal) {
  ScopedProviderAuthVendorForTesting scoped(&kPkceTestVendor);
  broker_->StartFlow("test-vendor", "flow-old", "binding-old", 3u);
  broker_->StartFlow("test-vendor", "flow-new", "binding-new", 4u);
  Settle();
  const size_t events_before_abort = adapter_.events().size();

  broker_->AbortGeneration(3u);
  broker_->AbortGeneration(3u);
  Settle();

  EXPECT_TRUE(terminals_.empty());
  ASSERT_EQ(events_before_abort + 1u, adapter_.events().size());
  const account_mojom::ProviderFlowEvent& aborted =
      *adapter_.events().back();
  EXPECT_EQ("test-vendor", aborted.provider_id);
  EXPECT_EQ("flow-old", aborted.flow_id);
  EXPECT_EQ(account_mojom::ProviderFlowEventKind::kFailedUnavailable,
            aborted.kind);
  EXPECT_FALSE(broker_->CancelFlow("flow-old"));
  EXPECT_TRUE(broker_->CancelFlow("flow-new"));
}

}  // namespace
}  // namespace taffy
