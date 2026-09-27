// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_MODEL_PROFILE_ENTITLEMENT_CACHE_H_
#define TAFFY_BROWSER_MODEL_PROFILE_ENTITLEMENT_CACHE_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

namespace taffy {

// The browser-process custody of the managed route's entitlement (decision
// 0082).
//
// One class owns the whole of what the mint produces, because the two things
// it produces have opposite lifetimes and neither may leave this process.
// The TAFFY-ENT-1 **token** is a single-use bearer credential — the worker
// replays its `jti`, so every dispatch needs a fresh one — held here in
// memory only, never persisted, never in an effect result, never in a
// contract struct: the summary type it travels beside has no field for it by
// construction. The **summary** is counts and identifiers, and it is what
// crosses to the sandboxed core.
//
// The cache holds at most one unspent pre-minted token. A planned entitlement
// fetch (the core's FETCH_ENTITLEMENT effect) is a mint, and the token that
// mint produces is kept for the next managed dispatch rather than discarded;
// a dispatch that finds none held mints its own. Handing a token out spends
// it here — the worker would refuse its second use anyway, and a copy that
// stayed behind would be a copy something could read.
//
// **Nothing here logs.** The token is a bearer credential and the GoTrue
// access token that buys it is another; neither appears in any log statement,
// and there are none in this subsystem.
class ProfileEntitlementCache final {
 public:
  // Asks the account plane for the signed-in session's GoTrue access token.
  // nullopt is "no usable session", which fails the mint without a request.
  using AccessTokenProvider = base::RepeatingCallback<void(
      base::OnceCallback<void(std::optional<std::string>)>)>;

  // The parsed summary, or null when the worker was not definitively heard.
  // A definitive absence (the worker's signed 403 "no entitlement") is a
  // summary with `definitive_absent` set, not a null.
  using SummaryCallback =
      base::OnceCallback<void(core_service::mojom::EntitlementSummaryResultPtr)>;

  // One single-use bearer token, or nullopt when no mint could produce one.
  using TokenCallback = base::OnceCallback<void(std::optional<std::string>)>;

  // `worker_origin` is the compiled managed-worker origin at the production
  // call site; empty or invalid disables every mint, fail-closed.
  ProfileEntitlementCache(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      std::string worker_origin, AccessTokenProvider access_token_provider);
  ProfileEntitlementCache(const ProfileEntitlementCache&) = delete;
  ProfileEntitlementCache& operator=(const ProfileEntitlementCache&) = delete;
  ~ProfileEntitlementCache();

  // Performs one mint for the core's planned fetch and answers the parsed
  // summary. The token the mint produced is kept for the next dispatch. The
  // callback runs exactly once.
  void FetchSummary(SummaryCallback callback);

  // Hands out one single-use token for a managed dispatch, minting when none
  // is held or the held one is within a minute of expiry. `evict` drops the
  // held token first and mints fresh — the 402 path, where the held state is
  // known stale. The callback runs exactly once.
  void AcquireToken(bool evict, TokenCallback callback);

  // Cancels only the mint in progress when its Core generation goes away.
  // Its callbacks are dropped: their outer effect owners settle the lost
  // generation. A still-unspent token and last summary remain valid.
  void CancelPendingMint();

  // Forgets everything held, for sign-out. A mint still in flight answers
  // its waiters with failure: whatever it returns was the departed account's.
  void Clear();

  bool has_unspent_token_for_testing() const;

 private:
  // One waiter on the single in-flight mint. Exactly one of the two fields
  // is set; both kinds are answered from the same response.
  struct MintWaiter {
    MintWaiter();
    MintWaiter(MintWaiter&&);
    MintWaiter& operator=(MintWaiter&&);
    ~MintWaiter();

    SummaryCallback summary_callback;
    TokenCallback token_callback;
  };

  void StartMint();
  void OnAccessToken(std::optional<std::string> access_token);
  void OnMintResponse(std::optional<std::string> response_body);

  // Parses one 2xx mint body into the summary and the token. Returns false
  // on any malformation; nothing is installed from a body that does not
  // parse whole.
  bool InstallMintedResponse(const std::string& body);

  // Answers and drops every waiter. `summary` is cloned per summary waiter;
  // the held token is spent on the first token waiter and later token
  // waiters mint again — in practice there is at most one, the dispatch that
  // asked.
  void AnswerWaiters(core_service::mojom::EntitlementSummaryResultPtr summary);
  void FailWaiters();

  void DropToken();

  const scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  const std::string worker_origin_;
  const AccessTokenProvider access_token_provider_;

  // The one unspent pre-minted token and when it lapses, epoch seconds.
  std::string unspent_token_;
  uint64_t token_expires_at_seconds_ = 0;

  // The last parsed summary, refreshed by every mint including dispatch
  // mints. Held so a token acquisition does not discard the counts that
  // arrived beside the token; the core's copy refreshes only through planned
  // fetches.
  core_service::mojom::EntitlementSummaryResultPtr last_summary_;

  std::vector<MintWaiter> waiters_;
  std::unique_ptr<network::SimpleURLLoader> loader_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<ProfileEntitlementCache> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_MODEL_PROFILE_ENTITLEMENT_CACHE_H_
