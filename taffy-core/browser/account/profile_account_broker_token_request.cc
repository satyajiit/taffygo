// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/account/profile_account_broker.h"

#include <algorithm>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/time/time.h"
#include "base/values.h"
#include "net/base/net_errors.h"
#include "taffy/browser/account_plane_configuration.h"

namespace taffy {
namespace {

namespace account_mojom = browser::account::mojom;
namespace service = core_service::mojom;

void ClearString(std::string *value) {
  if (!value) {
    return;
  }
  std::fill(value->begin(), value->end(), '\0');
  value->clear();
}

// Names the grant for the refusal log line. Content-free: the kind of
// exchange, never what was exchanged.
std::string_view TokenOperationName(
    service::AccountNetworkOperation operation_kind) {
  switch (operation_kind) {
  case service::AccountNetworkOperation::kExchangeAuthorizationCode:
    return "authorization code grant";
  case service::AccountNetworkOperation::kExchangeNativeCredential:
    return "native credential grant";
  case service::AccountNetworkOperation::kRefreshSession:
    return "refresh grant";
  default:
    return "token grant";
  }
}

void ClearBytes(std::vector<uint8_t> *value) {
  if (!value) {
    return;
  }
  std::ranges::fill(*value, 0u);
  value->clear();
}

std::optional<std::string> WriteJson(base::DictValue body) {
  std::string encoded;
  if (!base::JSONWriter::Write(body, &encoded)) {
    return std::nullopt;
  }
  return encoded;
}

uint64_t NowMonotonicMillisForAccount() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

} // namespace

void ProfileAccountBroker::OnRefreshSessionRead(
    service::EffectEnvelopePtr effect, EffectCallback callback,
    service::EffectStatus status,
    account_mojom::SecretMaterialReadResultPtr result) {
  if (status != service::EffectStatus::kCompleted || !result) {
    if (result) {
      ClearBytes(&result->material);
    }
    std::move(callback).Run(MakeResult(*effect, status));
    return;
  }
  std::optional<SessionPayload> session =
      DecodeSessionPayload(result->material);
  ClearBytes(&result->material);
  const auto &request = effect->network_request->refresh_session;
  if (!session || !request ||
      session->account_subject != request->expected_account_subject ||
      session->auth_method != request->expected_auth_method) {
    DeleteSessionAndFinish(std::move(effect), std::move(callback),
                           service::EffectStatus::kInvalidResult);
    return;
  }
  base::DictValue body;
  body.Set("refresh_token", session->refresh_token);
  std::optional<std::string> encoded = WriteJson(std::move(body));
  if (!encoded) {
    DeleteSessionAndFinish(std::move(effect), std::move(callback),
                           service::EffectStatus::kInvalidResult);
    return;
  }
  StartTokenRequest(std::move(effect), std::move(callback), std::move(*encoded),
                    request->expected_auth_method, request->session_handle,
                    request->expected_rotation,
                    request->expected_account_subject);
}

void ProfileAccountBroker::StartTokenRequest(
    service::EffectEnvelopePtr effect, EffectCallback callback,
    std::string request_body, service::AccountAuthMethod method,
    std::optional<std::string> previous_handle, uint64_t target_rotation,
    std::optional<std::string> expected_subject) {
  AccountEndpointKind endpoint_kind;
  switch (effect->network_request->operation_kind) {
  case service::AccountNetworkOperation::kExchangeAuthorizationCode:
    endpoint_kind = AccountEndpointKind::kPkceToken;
    break;
  case service::AccountNetworkOperation::kExchangeNativeCredential:
    endpoint_kind = AccountEndpointKind::kNativeToken;
    break;
  case service::AccountNetworkOperation::kRefreshSession:
    endpoint_kind = AccountEndpointKind::kRefreshToken;
    break;
  default:
    std::move(callback).Run(
        MakeResult(*effect, service::EffectStatus::kInvalidResult));
    return;
  }
  const std::optional<GURL> endpoint = AccountEndpoint(endpoint_kind);
  if (!endpoint || effect->operation->deadline_monotonic_ms <=
                       NowMonotonicMillisForAccount()) {
    ClearString(&request_body);
    std::move(callback).Run(
        MakeResult(*effect, endpoint ? service::EffectStatus::kDeadlineExceeded
                                     : service::EffectStatus::kUnavailable));
    return;
  }
  StartJsonRequest(*effect, *endpoint, std::move(request_body), std::nullopt,
                   base::BindOnce(&ProfileAccountBroker::OnTokenResponse,
                                  weak_factory_.GetWeakPtr(), std::move(effect),
                                  std::move(callback), method,
                                  std::move(previous_handle), target_rotation,
                                  std::move(expected_subject)));
}

void ProfileAccountBroker::OnTokenResponse(
    service::EffectEnvelopePtr effect, EffectCallback callback,
    service::AccountAuthMethod method,
    std::optional<std::string> previous_handle, uint64_t target_rotation,
    std::optional<std::string> expected_subject, NetworkResponse response) {
  if (!response.dispatched) {
    if (response.body) {
      ClearString(&*response.body);
    }
    const service::EffectStatus status =
        response.net_error == net::ERR_TIMED_OUT
            ? service::EffectStatus::kDeadlineExceeded
            : service::EffectStatus::kInvalidResult;
    std::move(callback).Run(MakeResult(*effect, status));
    return;
  }
  const AccountNetworkResponseDisposition disposition =
      ClassifyAccountNetworkResponse(response.net_error, response.http_status,
                                     response.body.has_value());
  if (disposition != AccountNetworkResponseDisposition::kCompleted) {
    if (disposition == AccountNetworkResponseDisposition::kRejected &&
        response.body) {
      // The one place the account plane's answer is read: reduced to a closed
      // tag for the log, then cleared with everything else.
      LogAccountRefusal(
          TokenOperationName(effect->network_request->operation_kind),
          response.http_status, *response.body);
    }
    if (response.body) {
      ClearString(&*response.body);
    }
    const service::EffectStatus status =
        disposition == AccountNetworkResponseDisposition::kRejected
            ? service::EffectStatus::kDenied
            : service::EffectStatus::kOutcomeUnknown;
    if (previous_handle && status == service::EffectStatus::kOutcomeUnknown) {
      DeleteSessionAndFinish(std::move(effect), std::move(callback), status);
    } else {
      std::move(callback).Run(MakeResult(*effect, status));
    }
    return;
  }
  if (!token_validator_ ||
      response.body->size() > service::kMaxAccountResponseBytes) {
    ClearString(&*response.body);
    if (previous_handle) {
      DeleteSessionAndFinish(std::move(effect), std::move(callback),
                             service::EffectStatus::kOutcomeUnknown);
    } else {
      std::move(callback).Run(
          MakeResult(*effect, service::EffectStatus::kOutcomeUnknown));
    }
    return;
  }
  auto request = service::AccountTokenValidationRequest::New();
  request->operation = effect->operation.Clone();
  request->operation_kind = effect->network_request->operation_kind;
  request->expected_auth_method = method;
  request->expected_account_subject = expected_subject;
  request->target_rotation = target_rotation;
  request->response_body.assign(response.body->begin(), response.body->end());
  ClearString(&*response.body);
  token_validator_.Run(
      std::move(request),
      base::BindOnce(&ProfileAccountBroker::OnTokenValidated,
                     weak_factory_.GetWeakPtr(), std::move(effect),
                     std::move(callback), method, std::move(previous_handle),
                     target_rotation, std::move(expected_subject)));
}

} // namespace taffy
