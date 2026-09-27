// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_MODEL_PROFILE_PROVIDER_LISTING_FETCHER_H_
#define TAFFY_BROWSER_MODEL_PROFILE_PROVIDER_LISTING_FETCHER_H_

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "url/gurl.h"

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

namespace taffy {

// The authenticated transport for a provider-owned model listing.
//
// The sandbox chooses only a provider, wire family, canonical HTTPS origin,
// opaque credential handle and response allowance. This class maps the closed
// provider/family pair to a compiled path and credential placement, performs
// one bounded GET, and returns the bytes unread. The existing isolated catalog
// decoder is the only code that may interpret them (decision 0098).
class ProfileProviderListingFetcher final {
 public:
  using EffectCallback =
      base::OnceCallback<void(core_service::mojom::EffectResultPtr)>;
  using ProviderCredentialCallback =
      base::OnceCallback<void(std::optional<std::string> material,
                              std::optional<std::string> credential_origin)>;
  using CredentialResolver =
      base::RepeatingCallback<void(const std::string& provider_id,
                                   const std::string& credential_handle,
                                   ProviderCredentialCallback)>;

  explicit ProfileProviderListingFetcher(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);
  ProfileProviderListingFetcher(const ProfileProviderListingFetcher&) = delete;
  ProfileProviderListingFetcher& operator=(
      const ProfileProviderListingFetcher&) = delete;
  ~ProfileProviderListingFetcher();

  // Performs one listing fetch. A second in-flight request is answered
  // unavailable rather than queued. Invalid envelopes answer null; every
  // well-shaped listing effect gets one typed disposition.
  void Perform(core_service::mojom::EffectEnvelopePtr effect,
               const CredentialResolver& credential_resolver,
               EffectCallback callback);

  // Drops a request from a core generation that no longer exists. No callback
  // runs: CoreServiceManager rejects stale completions and the disconnected
  // core has nobody left to receive one.
  void CancelGeneration(uint64_t generation);

  // True from accepted envelope through credential resolution and transport.
  // The manager includes this live leg in its idle decision; tests read the
  // same fact rather than a second testing-only state.
  bool has_fetch() const;

 private:
  struct ListingRoute {
    std::string_view provider_id;
    core_service::mojom::ProviderWireApi wire_api;
    std::string_view path;
    std::string_view credential_header;
    std::string_view credential_prefix;
    // A future vendor may issue a host with its key. A row must opt into that
    // behavior explicitly; the launch row does not, so an unexpected origin
    // from credential storage refuses the request rather than repointing it.
    bool accepts_credential_origin;
  };

  struct PendingFetch {
    core_service::mojom::EffectEnvelopePtr effect;
    EffectCallback callback;
    GURL url;
    ListingRoute route;
  };

  static std::optional<ListingRoute> RouteFor(
      std::string_view provider_id,
      core_service::mojom::ProviderWireApi wire_api);
  static std::optional<GURL> ComposeUrl(const std::string& endpoint,
                                        const ListingRoute& route);

  void OnCredentialResolved(uint64_t generation,
                            std::string effect_id,
                            std::optional<std::string> credential,
                            std::optional<std::string> credential_origin);
  void OnFetched(uint64_t generation,
                 std::string effect_id,
                 std::optional<std::string> body);
  void Finish(core_service::mojom::CatalogFetchDisposition disposition,
              std::vector<uint8_t> body);

  const scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  std::unique_ptr<PendingFetch> pending_;
  std::unique_ptr<network::SimpleURLLoader> loader_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<ProfileProviderListingFetcher> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_MODEL_PROFILE_PROVIDER_LISTING_FETCHER_H_
