// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/time/time.h"
#include "base/values.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "taffy/browser/account/profile_account_broker.h"
#include "taffy/browser/providerauth/profile_provider_auth_broker.h"
#include "taffy/browser/providerauth/provider_credential_host.h"

// What a vendor is allowed to answer with, and where the answer lands. Token
// responses are vendor-authored input: every field is bounded before it
// crosses a seam, and nothing here reaches a log.

namespace taffy {
namespace {

namespace account_mojom = browser::account::mojom;

constexpr size_t kMaxTokenBytes = 4096u;
// A vendor that names no lifetime gets one hour: shorter than any vendor's
// real lifetime, so the only cost of the guess is an early refresh.
constexpr int64_t kAssumedLifetimeSeconds = 3600;
// A second exchange's answer is short-lived by design, so its guess is too.
constexpr int64_t kAssumedSecondaryLifetimeSeconds = 1800;

uint64_t NowEpochMillis() {
  const int64_t value = base::Time::Now().InMillisecondsSinceUnixEpoch();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

// The API host one vendor's minted credential names inside itself.
//
// That credential is not opaque: it is a semicolon-separated list of the
// fields its own service reads, and `proxy-ep` is the proxy the account was
// routed to. The API host is the same name with its first label changed from
// `proxy` to `api`. Nothing here trusts the result — the caller still puts it
// through the row's licensed domain — so this only has to be careful about
// what it reads, not about what it concludes.
//
// It reads nothing that is not a plain host name: a value carrying a scheme,
// a port, a path or anything outside a host's alphabet is refused rather than
// repaired, because a repaired address is one nobody wrote.
std::optional<std::string> CopilotHostFromToken(const std::string &token) {
  constexpr std::string_view kField = "proxy-ep=";
  constexpr std::string_view kProxyLabel = "proxy.";
  std::string_view rest(token);
  while (!rest.empty()) {
    const size_t separator = rest.find(';');
    const std::string_view part = rest.substr(0, separator);
    rest = separator == std::string_view::npos ? std::string_view()
                                               : rest.substr(separator + 1);
    if (!part.starts_with(kField)) {
      continue;
    }
    const std::string_view host = part.substr(kField.size());
    if (!host.starts_with(kProxyLabel) || host.size() > 253u) {
      return std::nullopt;
    }
    for (const char character : host) {
      const bool allowed = (character >= 'a' && character <= 'z') ||
                           (character >= 'A' && character <= 'Z') ||
                           (character >= '0' && character <= '9') ||
                           character == '.' || character == '-';
      if (!allowed) {
        return std::nullopt;
      }
    }
    return "https://api." + std::string(host.substr(kProxyLabel.size()));
  }
  return std::nullopt;
}

}  // namespace

// One vendor token response, into the typed record Android seals. Returns
// nullopt for anything that is not a usable grant.
std::optional<account_mojom::ProviderOauthRecordPtr>
ProfileProviderAuthBroker::ParseTokenResponse(const std::string &body) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(body, base::JSON_PARSE_RFC);
  const base::DictValue *dict = parsed ? parsed->GetIfDict() : nullptr;
  if (!dict) {
    return std::nullopt;
  }
  const std::string *access = dict->FindString("access_token");
  if (!access || access->empty() || access->size() > kMaxTokenBytes) {
    return std::nullopt;
  }
  const std::string *refresh = dict->FindString("refresh_token");
  if (refresh && (refresh->empty() || refresh->size() > kMaxTokenBytes)) {
    return std::nullopt;
  }
  std::string token_type = "Bearer";
  if (const std::string *value = dict->FindString("token_type");
      value && !value->empty() && value->size() <= 32u) {
    token_type = *value;
  }
  int64_t expires_in_seconds = kAssumedLifetimeSeconds;
  if (std::optional<int> value = dict->FindInt("expires_in");
      value && *value > 0) {
    expires_in_seconds = *value;
  }
  std::string scopes;
  if (const std::string *value = dict->FindString("scope");
      value && value->size() <= 1024u) {
    scopes = *value;
  }
  auto record = account_mojom::ProviderOauthRecord::New();
  record->token_type = token_type;
  record->expires_at_epoch_ms =
      NowEpochMillis() + static_cast<uint64_t>(expires_in_seconds) * 1000u;
  record->scopes = scopes;
  record->access_token.assign(access->begin(), access->end());
  if (refresh) {
    record->refresh_token.emplace(refresh->begin(), refresh->end());
  }
  return record;
}

// The second exchange's answer, for the one vendor whose first token is not
// the API credential. What comes back is the credential and a lifetime, and
// the token that bought it is kept as the record's refresh material — because
// re-running this exchange is how the credential is renewed. There is no
// OAuth refresh grant in this shape and asking for one would be refused.
//
// The answer also names the host this particular credential is good for, and
// the record carries it: which host that is depends on the plan behind the
// account, so it is a fact about the credential rather than about the vendor
// and it is sealed with the credential. It is bounded before it is kept —
// this is a document a remote party served, and the address in it decides
// where a person's requests go — so the row's licensed domain and the
// canonical-origin rule both have to accept it. An answer that names
// something else keeps its token and loses its address, and losing the address
// is not the same as never having had one: the row that licenses this domain
// names that domain's licensing *parent* as its catalog origin, because the
// issued host has to sit at or beneath it, and a licensing parent is not an
// API. So there is nothing here to fall back to, and there is deliberately no
// attempt to make one — `ProfileModelBroker::ResolveCredentialOrigin` refuses
// a call for this vendor that reaches it with no address rather than sending
// it to a host that answers nothing. The token is still sealed, because it is
// real and because renewing it re-runs this same exchange, which is how a
// record that lost its address gets another one.
std::optional<account_mojom::ProviderOauthRecordPtr>
ProfileProviderAuthBroker::ParseSecondaryTokenResponse(
    const std::string &provider_id, const std::string &body,
    const std::string &primary_token) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(body, base::JSON_PARSE_RFC);
  const base::DictValue *dict = parsed ? parsed->GetIfDict() : nullptr;
  if (!dict || primary_token.empty() ||
      primary_token.size() > kMaxTokenBytes) {
    return std::nullopt;
  }
  const std::string *token = dict->FindString("token");
  if (!token || token->empty() || token->size() > kMaxTokenBytes) {
    return std::nullopt;
  }
  uint64_t expires_at_epoch_ms =
      NowEpochMillis() +
      static_cast<uint64_t>(kAssumedSecondaryLifetimeSeconds) * 1000u;
  if (std::optional<double> value = dict->FindDouble("expires_at");
      value && *value > 0.0) {
    expires_at_epoch_ms = static_cast<uint64_t>(*value) * 1000u;
  }
  auto record = account_mojom::ProviderOauthRecord::New();
  record->token_type = "Bearer";
  record->expires_at_epoch_ms = expires_at_epoch_ms;
  record->access_token.assign(token->begin(), token->end());
  record->refresh_token.emplace(primary_token.begin(), primary_token.end());
  if (const base::DictValue *endpoints = dict->FindDict("endpoints")) {
    if (const std::string *api = endpoints->FindString("api");
        api && ProviderCredentialHostLicensed(provider_id, *api)) {
      record->credential_host = *api;
    }
  }
  if (!record->credential_host.has_value()) {
    // The same fact, said a second way by the same vendor. This credential is
    // a semicolon-separated list of its own fields, one of which names the
    // proxy the account belongs to; the API host is that name with its first
    // label changed. The document above is the vendor's preferred answer and
    // this is its fallback, not a guess: both come out of the same response,
    // and both go through the row's licensed domain before anything keeps
    // them, so a vendor that answered with a host of somebody else's choosing
    // is refused twice rather than once.
    if (std::optional<std::string> derived = CopilotHostFromToken(*token);
        derived && ProviderCredentialHostLicensed(provider_id, *derived)) {
      record->credential_host = std::move(*derived);
    }
  }
  return record;
}

void ProfileProviderAuthBroker::StoreTokens(const std::string &flow_id,
                                            const std::string &body) {
  auto found = flows_.find(flow_id);
  if (found == flows_.end()) {
    return;
  }
  PendingFlow &flow = found->second;
  std::optional<account_mojom::ProviderOauthRecordPtr> record =
      ParseTokenResponse(body);
  if (!record) {
    NotifyEvent(flow.provider_id, flow_id,
                account_mojom::ProviderFlowEventKind::kFailedProvider,
                std::nullopt, std::nullopt);
    flows_.erase(found);
    return;
  }
  const ProviderAuthVendor *vendor = ProviderAuthVendorFor(flow.provider_id);
  if (vendor && vendor->secondary_token_url[0] != '\0') {
    // The token this vendor just returned is not the API credential; it is
    // what buys one. Nothing is stored until the second exchange answers.
    std::string primary((*record)->access_token.begin(),
                        (*record)->access_token.end());
    ExchangeSecondaryToken(flow_id, *vendor, std::move(primary));
    return;
  }
  StoreRecord(flow_id, std::move(*record));
}

void ProfileProviderAuthBroker::ExchangeSecondaryToken(
    const std::string &flow_id, const ProviderAuthVendor &vendor,
    std::string primary_token) {
  auto found = flows_.find(flow_id);
  if (found == flows_.end()) {
    std::fill(primary_token.begin(), primary_token.end(), '\0');
    return;
  }
  // The token buys the credential and is also what will buy the next one, so
  // it goes both to this request and to the answer's handler; the copy this
  // function holds is zeroed once both have theirs.
  PostSecondary(found->second, vendor, primary_token,
                base::BindOnce(
                    &ProfileProviderAuthBroker::OnSecondaryExchangeResponse,
                    weak_factory_.GetWeakPtr(), flow_id, primary_token));
  std::fill(primary_token.begin(), primary_token.end(), '\0');
}

void ProfileProviderAuthBroker::OnSecondaryExchangeResponse(
    const std::string &flow_id, std::string primary_token,
    std::optional<std::string> body) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = flows_.find(flow_id);
  if (found == flows_.end()) {
    std::fill(primary_token.begin(), primary_token.end(), '\0');
    return;
  }
  found->second.loader.reset();
  std::optional<account_mojom::ProviderOauthRecordPtr> record =
      body ? ParseSecondaryTokenResponse(found->second.provider_id, *body,
                                         primary_token)
           : std::nullopt;
  std::fill(primary_token.begin(), primary_token.end(), '\0');
  if (body) {
    std::fill(body->begin(), body->end(), '\0');
  }
  if (!record) {
    NotifyEvent(found->second.provider_id, flow_id,
                account_mojom::ProviderFlowEventKind::kFailedProvider,
                std::nullopt, std::nullopt);
    flows_.erase(found);
    return;
  }
  StoreRecord(flow_id, std::move(*record));
}

// The exchange minted a permanent API key rather than a grant. A key is not a
// subscription token and must not be filed as one: a record in the OAuth
// store is behind the refresh machinery, and the first rotation attempt would
// replace a key that nothing can reissue with nothing at all.
//
// So it goes where a key the person pasted goes, and by the same method. The
// platform seals it under the provider id and announces
// SAVE_PROVIDER_CREDENTIAL with API_KEY, inside the one writer's critical
// section (decision 0078) — the same store write, the same announcement, the
// same registry state. That equivalence is the point rather than a
// convenience: a browser-minted key that took a path of its own would be a
// second kind of stored key, and every question afterwards — what the roster
// draws, what decision 0083's probe proves, what forget removes — would have
// two answers where the product only ever wanted one. What the person did to
// obtain the key differs; what the key is does not.
//
// The key is read only far enough to bound it, is handed straight across, and
// never enters a log or a terminal. The terminal for this flow was already
// submitted when the code was sealed, so both exits here report an event and
// drop the flow rather than filing a second one.
void ProfileProviderAuthBroker::StoreMintedKey(const std::string &flow_id,
                                               const std::string &body) {
  auto found = flows_.find(flow_id);
  if (found == flows_.end()) {
    return;
  }
  const std::string provider_id = found->second.provider_id;
  std::optional<base::Value> parsed =
      base::JSONReader::Read(body, base::JSON_PARSE_RFC);
  const base::DictValue *dict = parsed ? parsed->GetIfDict() : nullptr;
  const std::string *key = dict ? dict->FindString("key") : nullptr;
  if (!key || key->empty() || key->size() > kMaxTokenBytes) {
    NotifyEvent(provider_id, flow_id,
                account_mojom::ProviderFlowEventKind::kFailedProvider,
                std::nullopt, std::nullopt);
    flows_.erase(found);
    return;
  }
  account_broker_->StoreProviderApiKey(
      provider_id, *key,
      base::BindOnce(&ProfileProviderAuthBroker::OnMintedKeyStored,
                     weak_factory_.GetWeakPtr(), provider_id, flow_id));
}

void ProfileProviderAuthBroker::OnMintedKeyStored(
    const std::string &provider_id, const std::string &flow_id, bool stored) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Unavailable rather than a provider error when the write did not land: the
  // vendor did its part and the key exists at the vendor, so this is the
  // device failing to keep it and the person can ask again.
  NotifyEvent(provider_id, flow_id,
              stored ? account_mojom::ProviderFlowEventKind::kCompleted
                     : account_mojom::ProviderFlowEventKind::kFailedUnavailable,
              std::nullopt, std::nullopt);
  flows_.erase(flow_id);
}

void ProfileProviderAuthBroker::StoreRecord(
    const std::string &flow_id,
    account_mojom::ProviderOauthRecordPtr record) {
  auto found = flows_.find(flow_id);
  if (found == flows_.end() || !record) {
    return;
  }
  const std::string provider_id = found->second.provider_id;
  account_broker_->StoreProviderOauthRecord(
      provider_id, std::move(record), /*rotation=*/false,
      base::BindOnce(
          [](base::WeakPtr<ProfileProviderAuthBroker> broker,
             std::string provider_id, std::string flow_id, bool stored) {
            if (!broker) {
              return;
            }
            broker->NotifyEvent(
                provider_id, flow_id,
                stored
                    ? account_mojom::ProviderFlowEventKind::kCompleted
                    : account_mojom::ProviderFlowEventKind::kFailedUnavailable,
                std::nullopt, std::nullopt);
            broker->flows_.erase(flow_id);
          },
          weak_factory_.GetWeakPtr(), provider_id, flow_id));
}

}  // namespace taffy
