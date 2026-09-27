// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PROVIDERAUTH_PROVIDER_ACCESS_RESOLVER_H_
#define TAFFY_BROWSER_PROVIDERAUTH_PROVIDER_ACCESS_RESOLVER_H_

#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "taffy/browser/account/profile_platform_adapter.mojom.h"

namespace taffy {

class ProfileAccountBroker;
class ProfileProviderAuthBroker;

// Answers the model broker's credential question for a provider that may be
// subscription-backed (decision 0081). A pasted key passes through untouched;
// a fresh access token is the material; a stale one buys exactly one
// browser-run refresh leg before the question is asked again. Which of those
// a sealed record is gets decided by Android's credential coordinator inside
// its own critical section (decision 0078) — this class only carries the
// dance, and it never looks inside the sealed envelope.
class ProviderAccessResolver final {
 public:
  // The material, and the origin the sealed record says this particular
  // credential's requests belong to. The second is nullopt for every
  // credential whose address is the one the catalog names, which is all of
  // them but the one vendor that issues an address with its token.
  using MaterialCallback = base::OnceCallback<void(
      std::optional<std::string> material, std::optional<std::string> host)>;

  // Both brokers outlive this class: the manager destroys its model broker —
  // the only holder of the resolver callback — before either of them.
  ProviderAccessResolver(ProfileAccountBroker *account_broker,
                         ProfileProviderAuthBroker *provider_auth_broker);
  ProviderAccessResolver(const ProviderAccessResolver &) = delete;
  ProviderAccessResolver &operator=(const ProviderAccessResolver &) = delete;
  ~ProviderAccessResolver();

  // The model broker's CredentialResolver target. Runs `callback` exactly
  // once: the usable material, or nullopt for anything only the person can
  // repair — which the model broker maps to a denied call. The state the
  // roster should show is filed as a side effect through the flow-event
  // seam, never inferred from the refusal.
  void Resolve(const std::string &provider_id,
               const std::string &credential_handle, MaterialCallback callback);

 private:
  void OnAccess(const std::string &provider_id,
                const std::string &credential_handle, MaterialCallback callback,
                bool already_refreshed,
                browser::account::mojom::ProviderAccessResultPtr result);
  void OnRefreshed(const std::string &provider_id,
                   const std::string &credential_handle,
                   MaterialCallback callback,
                   browser::account::mojom::ProviderOauthRecordPtr record,
                   bool definitive);
  void OnRotationStored(const std::string &provider_id,
                        const std::string &credential_handle,
                        MaterialCallback callback, bool stored);
  void NotifyOutcome(const std::string &provider_id,
                     browser::account::mojom::ProviderFlowEventKind kind);

  SEQUENCE_CHECKER(sequence_checker_);
  const raw_ptr<ProfileAccountBroker> account_broker_;
  const raw_ptr<ProfileProviderAuthBroker> provider_auth_broker_;
  base::WeakPtrFactory<ProviderAccessResolver> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_PROVIDERAUTH_PROVIDER_ACCESS_RESOLVER_H_
