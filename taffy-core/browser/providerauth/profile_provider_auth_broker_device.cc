// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/time/time.h"
#include "base/values.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "taffy/browser/account/profile_account_broker.h"
#include "taffy/browser/providerauth/profile_provider_auth_broker.h"
#include "taffy/browser/providerauth/provider_auth_wire.h"
#include "url/gurl.h"

// RFC 8628, the shape decision 0095 section 3 prefers wherever a vendor
// offers one: nothing redirects, nothing is intercepted, and the person
// authorizes on a device they are already signed in on. The two things a
// vendor asks to be shown — the address and the short code — are the only
// payload any event out of here carries.

namespace taffy {
namespace {

namespace account_mojom = browser::account::mojom;
namespace service = core_service::mojom;

constexpr base::TimeDelta kSlowDownStep = base::Seconds(5);

}  // namespace

void ProfileProviderAuthBroker::StartDeviceFlow(
    PendingFlow &flow, const ProviderAuthVendor &vendor) {
  std::string body = provider_auth::FormField("client_id", vendor.client_id);
  // One vendor's device-authorization request takes a client and nothing
  // else, and answers an empty `scope` with a 400. A field is sent where the
  // row has one and omitted where it does not, which is the same rule the
  // authorization composer follows.
  if (vendor.scope[0] != '\0') {
    body += "&" + provider_auth::FormField("scope", vendor.scope);
  }
  PostForm(flow, vendor.device_code_url, std::move(body),
           base::BindOnce(&ProfileProviderAuthBroker::OnDeviceCodeResponse,
                          weak_factory_.GetWeakPtr(), flow.flow_id));
}

void ProfileProviderAuthBroker::OnDeviceCodeResponse(
    const std::string &flow_id, std::optional<std::string> body) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = flows_.find(flow_id);
  if (found == flows_.end()) {
    return;
  }
  PendingFlow &flow = found->second;
  flow.loader.reset();
  std::optional<base::Value> parsed =
      body ? base::JSONReader::Read(*body, base::JSON_PARSE_RFC) : std::nullopt;
  const base::DictValue *dict = parsed ? parsed->GetIfDict() : nullptr;
  const std::string *device_code =
      dict ? dict->FindString("device_code") : nullptr;
  const std::string *user_code = dict ? dict->FindString("user_code") : nullptr;
  const std::string *verification_uri =
      dict ? dict->FindString("verification_uri") : nullptr;
  if (!verification_uri && dict) {
    verification_uri = dict->FindString("verification_url");
  }
  if (!device_code || device_code->empty() || !user_code ||
      user_code->empty() || !verification_uri ||
      !GURL(*verification_uri).SchemeIs("https")) {
    FinishFlow(flow_id, service::AuthCallbackStatus::kProviderError,
               std::nullopt,
               account_mojom::ProviderFlowEventKind::kFailedProvider);
    return;
  }
  // A vendor that publishes the complete address has put the code inside it,
  // which is the difference between a person typing eight characters on a
  // second device and a person tapping once. It is shown instead of the bare
  // address, and the code is still carried beside it, because a person who
  // follows the link on the phone they are holding still has to read it back.
  const std::string *complete = dict->FindString("verification_uri_complete");
  if (complete && !complete->empty() && GURL(*complete).SchemeIs("https")) {
    verification_uri = complete;
  }
  flow.device_code = *device_code;
  if (std::optional<int> interval = dict->FindInt("interval");
      interval && *interval > 0 && *interval <= 60) {
    flow.poll_interval = base::Seconds(*interval);
  }
  NotifyEvent(flow.provider_id, flow_id,
              account_mojom::ProviderFlowEventKind::kUserCodeReady,
              *verification_uri, *user_code);
  flow.poll_timer = std::make_unique<base::OneShotTimer>();
  flow.poll_timer->Start(
      FROM_HERE, flow.poll_interval,
      base::BindOnce(&ProfileProviderAuthBroker::PollDeviceToken,
                     weak_factory_.GetWeakPtr(), flow_id));
}

void ProfileProviderAuthBroker::PollDeviceToken(const std::string &flow_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = flows_.find(flow_id);
  if (found == flows_.end()) {
    return;
  }
  PendingFlow &flow = found->second;
  const ProviderAuthVendor *vendor = ProviderAuthVendorFor(flow.provider_id);
  if (!vendor) {
    FinishFlow(flow_id, service::AuthCallbackStatus::kPlatformUnavailable,
               std::nullopt,
               account_mojom::ProviderFlowEventKind::kFailedUnavailable);
    return;
  }
  std::string body =
      provider_auth::FormField(
          "grant_type", "urn:ietf:params:oauth:grant-type:device_code") +
      "&" + provider_auth::FormField("client_id", vendor->client_id) + "&" +
      provider_auth::FormField("device_code", flow.device_code);
  PostForm(flow, vendor->token_url, std::move(body),
           base::BindOnce(&ProfileProviderAuthBroker::OnDeviceTokenResponse,
                          weak_factory_.GetWeakPtr(), flow_id));
}

void ProfileProviderAuthBroker::OnDeviceTokenResponse(
    const std::string &flow_id, std::optional<std::string> body) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = flows_.find(flow_id);
  if (found == flows_.end()) {
    return;
  }
  PendingFlow &flow = found->second;
  flow.loader.reset();
  std::optional<base::Value> parsed =
      body ? base::JSONReader::Read(*body, base::JSON_PARSE_RFC) : std::nullopt;
  const base::DictValue *dict = parsed ? parsed->GetIfDict() : nullptr;
  const std::string *error = dict ? dict->FindString("error") : nullptr;
  if (error) {
    if (*error == "authorization_pending" || *error == "slow_down") {
      if (*error == "slow_down") {
        flow.poll_interval += kSlowDownStep;
      }
      flow.poll_timer = std::make_unique<base::OneShotTimer>();
      flow.poll_timer->Start(
          FROM_HERE, flow.poll_interval,
          base::BindOnce(&ProfileProviderAuthBroker::PollDeviceToken,
                         weak_factory_.GetWeakPtr(), flow_id));
      return;
    }
    // Two spellings, one meaning. RFC 8628 names `access_denied`, and at
    // least one vendor whose row this binary carries answers
    // `authorization_denied` instead. Read as anything but a refusal, a
    // person who declined on the other device is told the vendor failed —
    // which invites them to try again at a screen they have just said no to.
    if (*error == "access_denied" || *error == "authorization_denied") {
      FinishFlow(flow_id, service::AuthCallbackStatus::kDenied, std::nullopt,
                 account_mojom::ProviderFlowEventKind::kFailedDenied);
      return;
    }
    if (*error == "expired_token") {
      FinishFlow(flow_id, service::AuthCallbackStatus::kDeadlineExceeded,
                 std::nullopt,
                 account_mojom::ProviderFlowEventKind::kFailedDeadline);
      return;
    }
    FinishFlow(flow_id, service::AuthCallbackStatus::kProviderError,
               std::nullopt,
               account_mojom::ProviderFlowEventKind::kFailedProvider);
    return;
  }
  if (!dict) {
    FinishFlow(flow_id, service::AuthCallbackStatus::kProviderError,
               std::nullopt,
               account_mojom::ProviderFlowEventKind::kFailedProvider);
    return;
  }
  // The grant arrived and was already exchanged by the same poll: RFC 8628's
  // device_code is the authorization grant, so it is what the terminal's
  // sealed handle stands for. The tokens themselves are stored right after
  // the core clears the pending flow.
  NotifyEvent(flow.provider_id, flow_id,
              account_mojom::ProviderFlowEventKind::kExchanging, std::nullopt,
              std::nullopt);
  std::string grant = std::move(flow.device_code);
  flow.device_code.clear();
  account_broker_->SealProviderAuthorizationCode(
      flow_id, std::move(grant),
      base::BindOnce(&ProfileProviderAuthBroker::OnCodeSealed,
                     weak_factory_.GetWeakPtr(), flow_id, std::move(*body)));
}

}  // namespace taffy
