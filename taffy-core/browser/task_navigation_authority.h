// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_TASK_NAVIGATION_AUTHORITY_H_
#define TAFFY_BROWSER_TASK_NAVIGATION_AUTHORITY_H_

#include <memory>
#include <optional>

#include "url/gurl.h"

namespace content {
class NavigationHandle;
class NavigationUIData;
}  // namespace content

namespace taffy {

// Browser-only authority carried into the one NavigationRequest created for an
// already-authorized task navigation. Page content, the isolated core and the
// model cannot create this marker. It is cloned into the request before
// NavigationThrottles run, so the first network request is inside the boundary.
class TaskNavigationAuthority final {
 public:
  TaskNavigationAuthority(const TaskNavigationAuthority&) = default;
  TaskNavigationAuthority& operator=(const TaskNavigationAuthority&) = default;
  TaskNavigationAuthority(TaskNavigationAuthority&&) = default;
  TaskNavigationAuthority& operator=(TaskNavigationAuthority&&) = default;
  ~TaskNavigationAuthority();

  // Creates the embedder's pre-start navigation carrier with one exact
  // normalized HTTP(S) destination, or null when the address is not
  // admissible or no product carrier has been installed.
  static std::unique_ptr<content::NavigationUIData> CreateNavigationData(
      const GURL& exact_destination);

  // Reads authority from the request's pre-start carrier. The portable layer
  // never casts that carrier to an embedder type.
  static std::optional<TaskNavigationAuthority> FromNavigation(
      content::NavigationHandle& navigation_handle);

  // Why one request was admitted or refused. The verdict exists because the
  // bool below cannot be debugged: a phone cancelled a followed link on
  // `myaadhaarbeta.uidai.gov.in` at `at=not-the-authorized-destination
  // redirect=1` and that line is true of five different clauses, one of which
  // is a deliberate class refusal and four of which are shapes of address. It
  // names a clause and never an address: where the hop was going is the
  // page's business and stays out of the log (decision 0226).
  enum class RequestVerdict {
    kAllowedExact,
    kAllowedSameSiteHop,
    kAllowedCrossSiteHop,
    // The address itself is not one this product will act on: invalid, not
    // HTTP(S), or carrying credentials in its authority.
    kRefusedNotNormalized,
    // Not a redirect, and not the address that was authorized. A first
    // request is bound exactly.
    kRefusedFirstRequestNotExact,
    // A redirect that leaves HTTPS. The class boundary, not a detail.
    kRefusedInsecureHop,
    // A cross-site hop whose landing origin is opaque.
    kRefusedOpaqueLanding,
    // A cross-site hop into a compiled restricted destination class.
    kRefusedRestrictedClass,
    // A cross-site hop the classifier could not read as a tuple origin.
    kRefusedUnclassifiableLanding,
  };

  // Whether `address` is one this task's authority reaches. The first request
  // must be the exact address policy authorized; a redirect may be answered by
  // the same site, or by another site that is not in a restricted class
  // (decision 0177).
  bool AllowsRequest(const GURL& address, bool is_redirect) const;

  // The same question, answered with the clause that decided it.
  RequestVerdict VerdictForRequest(const GURL& address, bool is_redirect) const;

  // Whether the clause is one of the three that allow the request.
  static bool IsAllowed(RequestVerdict verdict);

  // The clause's own name, for a log line.
  static const char* NameOfVerdict(RequestVerdict verdict);

  const GURL& exact_destination_for_testing() const {
    return exact_destination_;
  }

 private:
  explicit TaskNavigationAuthority(GURL exact_destination);

  GURL exact_destination_;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_TASK_NAVIGATION_AUTHORITY_H_
