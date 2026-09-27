// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/providerauth/provider_access_resolver.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "taffy/browser/account/profile_account_broker.h"
#include "taffy/browser/providerauth/profile_provider_auth_broker.h"

namespace taffy {

namespace account_mojom = browser::account::mojom;

ProviderAccessResolver::ProviderAccessResolver(
    ProfileAccountBroker *account_broker,
    ProfileProviderAuthBroker *provider_auth_broker)
    : account_broker_(account_broker),
      provider_auth_broker_(provider_auth_broker) {
  CHECK(account_broker_);
  CHECK(provider_auth_broker_);
}

ProviderAccessResolver::~ProviderAccessResolver() = default;

void ProviderAccessResolver::Resolve(const std::string &provider_id,
                                     const std::string &credential_handle,
                                     MaterialCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  account_broker_->ResolveProviderAccess(
      provider_id, credential_handle,
      base::BindOnce(&ProviderAccessResolver::OnAccess,
                     weak_factory_.GetWeakPtr(), provider_id,
                     credential_handle, std::move(callback),
                     /*already_refreshed=*/false));
}

void ProviderAccessResolver::OnAccess(
    const std::string &provider_id, const std::string &credential_handle,
    MaterialCallback callback, bool already_refreshed,
    account_mojom::ProviderAccessResultPtr result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!result) {
    std::move(callback).Run(std::nullopt, std::nullopt);
    return;
  }
  switch (result->kind) {
    case account_mojom::ProviderAccessKind::kRawKey:
    case account_mojom::ProviderAccessKind::kAccessToken: {
      std::string material(result->material.begin(), result->material.end());
      std::fill(result->material.begin(), result->material.end(), 0);
      // The address travels with the material and only with it. A record that
      // could not answer with a credential has nowhere to send anything, and
      // an address carried past a refusal would be a place a later call could
      // be sent on a credential this one could not produce.
      std::move(callback).Run(std::move(material),
                              std::move(result->credential_host));
      return;
    }
    case account_mojom::ProviderAccessKind::kSignInRequired: {
      std::fill(result->material.begin(), result->material.end(), 0);
      // Android decided the record needs the person; this files the fact so
      // the roster row flips instead of reading USABLE over denied calls.
      NotifyOutcome(provider_id,
                    account_mojom::ProviderFlowEventKind::kRefreshSignInRequired);
      std::move(callback).Run(std::nullopt, std::nullopt);
      return;
    }
    case account_mojom::ProviderAccessKind::kRefreshRequired: {
      std::string refresh_token(result->material.begin(),
                                result->material.end());
      std::fill(result->material.begin(), result->material.end(), 0);
      // One refresh per resolve: a record still stale after a stored
      // rotation cannot be made fresh by refreshing harder, and a second leg
      // here would loop on a vendor that keeps answering short expiries.
      if (already_refreshed || refresh_token.empty()) {
        std::fill(refresh_token.begin(), refresh_token.end(), '\0');
        NotifyOutcome(provider_id,
                      account_mojom::ProviderFlowEventKind::kRefreshFailed);
        std::move(callback).Run(std::nullopt, std::nullopt);
        return;
      }
      provider_auth_broker_->RefreshCredential(
          provider_id, std::move(refresh_token),
          base::BindOnce(&ProviderAccessResolver::OnRefreshed,
                         weak_factory_.GetWeakPtr(), provider_id,
                         credential_handle, std::move(callback)));
      return;
    }
  }
}

void ProviderAccessResolver::OnRefreshed(
    const std::string &provider_id, const std::string &credential_handle,
    MaterialCallback callback, account_mojom::ProviderOauthRecordPtr record,
    bool definitive) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!record) {
    NotifyOutcome(
        provider_id,
        definitive
            ? account_mojom::ProviderFlowEventKind::kRefreshSignInRequired
            : account_mojom::ProviderFlowEventKind::kRefreshFailed);
    std::move(callback).Run(std::nullopt, std::nullopt);
    return;
  }
  // Store first, resolve second: the rotated tokens must be sealed before
  // anything answers with them, so a process death between the two costs a
  // resolve, never the rotation the vendor already performed.
  account_broker_->StoreProviderOauthRecord(
      provider_id, std::move(record), /*rotation=*/true,
      base::BindOnce(&ProviderAccessResolver::OnRotationStored,
                     weak_factory_.GetWeakPtr(), provider_id,
                     credential_handle, std::move(callback)));
}

void ProviderAccessResolver::OnRotationStored(
    const std::string &provider_id, const std::string &credential_handle,
    MaterialCallback callback, bool stored) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!stored) {
    // The rotation raced a forget and the person won; no event, because a
    // provider the person just removed has no state worth filing.
    std::move(callback).Run(std::nullopt, std::nullopt);
    return;
  }
  account_broker_->ResolveProviderAccess(
      provider_id, credential_handle,
      base::BindOnce(&ProviderAccessResolver::OnAccess,
                     weak_factory_.GetWeakPtr(), provider_id,
                     credential_handle, std::move(callback),
                     /*already_refreshed=*/true));
}

void ProviderAccessResolver::NotifyOutcome(
    const std::string &provider_id,
    account_mojom::ProviderFlowEventKind kind) {
  auto event = account_mojom::ProviderFlowEvent::New();
  event->provider_id = provider_id;
  // No flow exists for a resolve-time outcome; the empty id says so.
  event->flow_id = std::string();
  event->kind = kind;
  account_broker_->NotifyProviderFlowEvent(std::move(event));
}

}  // namespace taffy
