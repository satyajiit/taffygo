// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/providerauth/profile_provider_auth_broker.h"

#include <stddef.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "net/base/url_util.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "taffy/browser/providerauth/auth_callback_parser.h"
#include "taffy/browser/account/profile_account_broker.h"
#include "taffy/browser/providerauth/provider_auth_wire.h"
#include "url/gurl.h"
#include "url/url_constants.h"

// The flow's lifecycle and the one place a returned code claims it. Every
// way a code can arrive — an intercepted navigation, a custom-scheme
// redirect, a person typing what the vendor displayed — ends in
// `ClaimRedirect`, so the constant-time comparison that decides which flow a
// code belongs to exists exactly once.

namespace taffy {
namespace {

namespace account_mojom = browser::account::mojom;
namespace service = core_service::mojom;

constexpr size_t kStateEntropyBytes = 32u;
constexpr size_t kMaxAuthorizationCodeBytes = 4u * 1024u;
constexpr size_t kMaxManualEntryBytes = 8u * 1024u;
constexpr base::TimeDelta kDefaultPollInterval = base::Seconds(5);

bool HasControlByte(std::string_view value) {
  return std::ranges::any_of(value, [](const unsigned char character) {
    return character < 0x20u || character == 0x7fu;
  });
}

// The address a navigation is asking for, with the parts a redirect carries
// its answer in removed. Comparing this against what a flow presented is what
// makes interception exact rather than a prefix guess.
std::string AddressWithoutAnswer(const GURL &url) {
  GURL::Replacements strip;
  strip.ClearQuery();
  strip.ClearRef();
  return url.ReplaceComponents(strip).spec();
}

// What a vendor put in a redirect address, whether this browser cancelled the
// navigation to it or a person pasted it out of a tab that could not load.
struct RedirectAnswer {
  std::string code;
  std::string state;
  bool denied = false;
};

// Read one redirect's answer, or nothing if it is not one.
//
// Both ways an authorization code arrives at this browser read it here, so a
// bound checked on the intercepted path cannot be missing from the pasted one
// — which is the shape this kind of duplication fails in, silently and only
// on the path nobody exercises.
//
// A refusal is spelled two ways across these vendors. RFC 6749 names
// `access_denied` and most of them send it; at least one sends
// `authorization_denied` for the same act. Reading only the first turns a
// person declining a consent screen into a provider error, which is the one
// outcome that reads as the product's fault and asks them to try again.
std::optional<RedirectAnswer> ReadRedirectAnswer(const GURL &url) {
  RedirectAnswer answer;
  std::string error;
  for (net::QueryIterator it(url); !it.IsAtEnd(); it.Advance()) {
    const std::string_view key = it.GetKey();
    const std::string value = it.GetUnescapedValue();
    if (HasControlByte(value)) {
      return std::nullopt;
    }
    if (key == "state") {
      answer.state = value;
    } else if (key == "code") {
      answer.code = value;
    } else if (key == "error") {
      error = value;
    }
  }
  if (answer.code.size() > kMaxAuthorizationCodeBytes) {
    return std::nullopt;
  }
  answer.denied = error == "access_denied" || error == "authorization_denied";
  return answer;
}

}  // namespace

ProfileProviderAuthBroker::ProfileProviderAuthBroker(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    ProfileAccountBroker *account_broker)
    : account_broker_(account_broker),
      url_loader_factory_(std::move(url_loader_factory)) {}

ProfileProviderAuthBroker::~ProfileProviderAuthBroker() = default;

void ProfileProviderAuthBroker::SetFlowTerminalCallback(
    FlowTerminalCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  flow_terminal_ = std::move(callback);
}

void ProfileProviderAuthBroker::StartFlow(
    const std::string &provider_id, const std::string &flow_id,
    const std::string &redirect_binding_id, uint64_t service_generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (provider_id.empty() || flow_id.empty() || redirect_binding_id.empty() ||
      flows_.contains(flow_id)) {
    return;
  }
  PendingFlow &flow = flows_[flow_id];
  flow.provider_id = provider_id;
  flow.flow_id = flow_id;
  flow.redirect_binding_id = redirect_binding_id;
  flow.service_generation = service_generation;
  flow.state = provider_auth::RandomBase64Url(kStateEntropyBytes);
  flow.authorization_state = flow.state;
  flow.poll_interval = kDefaultPollInterval;
  flow.deadline_timer = std::make_unique<base::OneShotTimer>();
  flow.deadline_timer->Start(
      FROM_HERE, base::Minutes(kProviderAuthFlowDeadlineMinutes),
      base::BindOnce(&ProfileProviderAuthBroker::OnDeadline,
                     weak_factory_.GetWeakPtr(), flow_id));

  const ProviderAuthVendor *vendor = ProviderAuthVendorFor(provider_id);
  if (!vendor || !ProviderAuthVendorRegistered(provider_id) ||
      !url_loader_factory_ || !account_broker_) {
    // The flow machinery is complete; what is absent is this vendor's dated
    // terms review, or the identity that review licenses, or the profile's
    // network. The admitted flow still gets its one terminal so the core's
    // pending marker never stands.
    FinishFlow(flow_id, service::AuthCallbackStatus::kPlatformUnavailable,
               std::nullopt,
               account_mojom::ProviderFlowEventKind::kFailedUnavailable);
    return;
  }
  if (vendor->flow == ProviderAuthFlowKind::kDeviceCode) {
    StartDeviceFlow(flow, *vendor);
  } else {
    StartPkceFlow(flow, *vendor);
  }
}

bool ProfileProviderAuthBroker::DeliverProviderCallback(std::string raw_uri) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::optional<ParsedAccountAuthCallback> parsed =
      ParseProviderAuthCallback(raw_uri);
  std::fill(raw_uri.begin(), raw_uri.end(), '\0');
  if (!parsed) {
    return false;
  }
  return ClaimRedirect(
      parsed->state, std::move(parsed->authorization_code),
      parsed->outcome == AccountAuthCallbackOutcome::kDenied);
}

bool ProfileProviderAuthBroker::ClaimInterceptedRedirect(const GURL &url) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (flows_.empty() || !url.is_valid() || !url.has_query()) {
    return false;
  }
  // Only an address a running flow actually presented is a candidate. The
  // vendor table is not consulted: a row nobody is signing in to has no
  // business cancelling a person's navigation.
  const std::string address = AddressWithoutAnswer(url);
  bool addressed = false;
  for (const auto &[flow_id, flow] : flows_) {
    if (flow.redirect_uri.empty()) {
      continue;
    }
    // Both sides through the same canonicalisation, so a row that spells its
    // address one way and a vendor that redirects to the same address spelled
    // another are still the same address.
    const GURL presented(flow.redirect_uri);
    if (presented.is_valid() && presented.spec() == address) {
      addressed = true;
      break;
    }
  }
  if (!addressed) {
    return false;
  }

  std::optional<RedirectAnswer> answer = ReadRedirectAnswer(url);
  if (!answer || answer->state.empty() ||
      answer->state.size() > service::kMaxIdentifierBytes) {
    return false;
  }
  if (answer->code.empty()) {
    return ClaimRedirect(answer->state, std::nullopt, answer->denied);
  }
  return ClaimRedirect(answer->state, std::move(answer->code),
                       /*denied=*/false);
}

bool ProfileProviderAuthBroker::HasInterceptableRedirect() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (const auto &entry : flows_) {
    if (!entry.second.redirect_uri.empty()) {
      return true;
    }
  }
  return false;
}

bool ProfileProviderAuthBroker::has_active_flows() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return !flows_.empty();
}

bool ProfileProviderAuthBroker::SubmitManualCode(const std::string &flow_id,
                                                 std::string entered) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = flows_.find(flow_id);
  if (found == flows_.end() || entered.empty() ||
      entered.size() > kMaxManualEntryBytes || HasControlByte(entered) ||
      found->second.pkce_verifier.empty()) {
    std::fill(entered.begin(), entered.end(), '\0');
    return false;
  }
  // Three shapes reach this, and all three are what a person actually has in
  // front of them.
  //
  // A vendor that redirects leaves the answer in the address bar of a tab
  // that could not load, so the whole address is what there is to copy. That
  // is the standing fallback of every interception row — the row's strategy
  // depends on the vendor keeping an address this browser recognises, and
  // when it does not, this is the path that still finishes. Without it the
  // pasted address becomes the authorization code, the exchange refuses it,
  // and the person is told the vendor said no.
  //
  // A vendor that displays instead of redirecting shows the code and the
  // state joined by `#`, because the tool the row borrows from asks for both.
  // And a vendor that shows the code alone leaves the code alone.
  //
  // Where a state comes with the code it is checked exactly as an intercepted
  // one is; where it does not, the flow the person is looking at is the
  // correlation, and it came from the product's own chrome rather than from
  // the vendor.
  std::string code = entered;
  std::optional<std::string> claimed_state;
  const GURL pasted(entered);
  if (pasted.is_valid() && pasted.has_query() &&
      (pasted.SchemeIs(url::kHttpScheme) ||
       pasted.SchemeIs(url::kHttpsScheme))) {
    std::optional<RedirectAnswer> answer = ReadRedirectAnswer(pasted);
    if (!answer || answer->code.empty()) {
      std::fill(entered.begin(), entered.end(), '\0');
      return false;
    }
    code = std::move(answer->code);
    if (!answer->state.empty()) {
      claimed_state = std::move(answer->state);
    }
  } else {
    const size_t separator = entered.find('#');
    if (separator != std::string::npos) {
      code = entered.substr(0, separator);
      claimed_state = entered.substr(separator + 1);
    }
  }
  if (claimed_state &&
      !provider_auth::ConstantTimeEquals(found->second.authorization_state,
                                         *claimed_state)) {
    std::fill(entered.begin(), entered.end(), '\0');
    std::fill(code.begin(), code.end(), '\0');
    return false;
  }
  std::fill(entered.begin(), entered.end(), '\0');
  if (code.empty() || code.size() > kMaxAuthorizationCodeBytes) {
    std::fill(code.begin(), code.end(), '\0');
    return false;
  }
  OnRedirectCode(flow_id, std::move(code));
  return true;
}

bool ProfileProviderAuthBroker::CancelFlow(const std::string &flow_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!flows_.contains(flow_id)) {
    return false;
  }
  // The same terminal a dismissed authorization files. The contract has no
  // separate cancelled outcome, and inventing one would be worse than this
  // is: a person who stops a sign-in has denied it, and the core's pending
  // marker must clear either way.
  FinishFlow(flow_id, service::AuthCallbackStatus::kDenied, std::nullopt,
             account_mojom::ProviderFlowEventKind::kFailedDenied);
  return true;
}

void ProfileProviderAuthBroker::AbortGeneration(uint64_t service_generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<std::pair<std::string, std::string>> aborted;
  for (auto flow = flows_.begin(); flow != flows_.end();) {
    if (flow->second.service_generation != service_generation) {
      ++flow;
      continue;
    }
    aborted.emplace_back(flow->second.provider_id, flow->second.flow_id);
    flow = flows_.erase(flow);
  }
  for (const auto &[provider_id, flow_id] : aborted) {
    NotifyEvent(provider_id, flow_id,
                account_mojom::ProviderFlowEventKind::kFailedUnavailable,
                std::nullopt, std::nullopt);
  }
}

bool ProfileProviderAuthBroker::ClaimRedirect(const std::string &state,
                                              std::optional<std::string> code,
                                              bool denied) {
  // The redirect names its flow by the state the flow was started with; the
  // comparison is constant-time so a redirect cannot probe for a live value.
  // Only a PKCE flow is claimable: a device flow's grant never returns this
  // way, and matching one here would spend a device code as an auth code.
  PendingFlow *claimed = nullptr;
  for (auto &[flow_id, flow] : flows_) {
    if (!flow.pkce_verifier.empty() &&
        provider_auth::ConstantTimeEquals(flow.authorization_state, state)) {
      claimed = &flow;
      break;
    }
  }
  if (!claimed) {
    if (code) {
      std::fill(code->begin(), code->end(), '\0');
    }
    return false;
  }
  const std::string flow_id = claimed->flow_id;
  if (code) {
    OnRedirectCode(flow_id, std::move(*code));
    return true;
  }
  FinishFlow(flow_id,
             denied ? service::AuthCallbackStatus::kDenied
                    : service::AuthCallbackStatus::kProviderError,
             std::nullopt,
             denied ? account_mojom::ProviderFlowEventKind::kFailedDenied
                    : account_mojom::ProviderFlowEventKind::kFailedProvider);
  return true;
}

void ProfileProviderAuthBroker::OnRedirectCode(const std::string &flow_id,
                                               std::string code) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = flows_.find(flow_id);
  if (found == flows_.end()) {
    std::fill(code.begin(), code.end(), '\0');
    return;
  }
  NotifyEvent(found->second.provider_id, flow_id,
              account_mojom::ProviderFlowEventKind::kExchanging, std::nullopt,
              std::nullopt);
  // Sealed for the terminal command — the contract's callback carries an
  // opaque handle, never code material — while this class keeps the bytes it
  // is about to spend at the token endpoint and zeroes them after.
  account_broker_->SealProviderAuthorizationCode(
      flow_id, std::string(code),
      base::BindOnce(&ProfileProviderAuthBroker::OnCodeSealed,
                     weak_factory_.GetWeakPtr(), flow_id, std::move(code)));
}

void ProfileProviderAuthBroker::OnDeadline(const std::string &flow_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!flows_.contains(flow_id)) {
    return;
  }
  FinishFlow(flow_id, service::AuthCallbackStatus::kDeadlineExceeded,
             std::nullopt,
             account_mojom::ProviderFlowEventKind::kFailedDeadline);
}

void ProfileProviderAuthBroker::CancelAll() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  flows_.clear();
  for (auto &[provider_id, flight] : refreshes_) {
    for (RefreshCallback &callback : flight.callbacks) {
      std::move(callback).Run(nullptr, false);
    }
  }
  refreshes_.clear();
  revocations_.clear();
}

}  // namespace taffy
