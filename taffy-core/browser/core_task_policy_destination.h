// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_TASK_POLICY_DESTINATION_H_
#define TAFFY_BROWSER_CORE_TASK_POLICY_DESTINATION_H_

#include <optional>
#include <string>

#include "base/functional/function_ref.h"
#include "base/types/expected.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace taffy {

struct TaskPolicyDocumentContext;

// The address a proposal's policy scope is judged against, resolved by the
// browser before policy is asked, with the tuple origin derived from it.
struct TaskPolicyDestination {
  std::string address;
  std::string origin;
};

// Whether a proposal of this operation kind carries such a destination. The
// reducer's proposal names a target; the browser alone resolves where that
// target actually leads (a configured search, an observed link, a typed
// address). Back, forward and reload carry none: their target is the tab's
// own history, bound at execution.
bool TaskPolicyOperationCarriesDestination(
    core_service::mojom::TaskActionOperationKind operation);

// Whether this operation leaves the current document rather than reading or
// acting on it: a typed navigate, a search, opening a tab. Each names its own
// destination and nothing about the page it was proposed from, so the origin
// it is leaving authorizes nothing about it. A link open is deliberately not
// one of these — its target is a node in the live document, so that document
// must be a source the task holds.
bool TaskPolicyOperationLeavesDocument(
    core_service::mojom::TaskActionOperationKind operation);

// Whether a task's own tab, now showing `live_origin`, is still on the site
// the source `source_origin` was issued for.
//
// Exact equality first, then the rule decision 0155 already settled for a
// navigation's destination: over https, on the same port, a host sharing the
// registrable domain is the same site answering. A redirect a task never
// proposed — a portal moving `myaadhaar` to `myaadhaarbeta` on load — used to
// make the tab's source stop naming the document, which took the whole task
// consent record with it on the next published snapshot and left every later
// move refused as EGRESS_NOT_AUTHORIZED, the navigate that could have left
// included. Nothing about a different site changes: another registrable
// domain, another port and an http downgrade are each still a different
// source.
bool TaskSourceOriginStillNamesTheSite(const std::string& source_origin,
                                       const std::string& live_origin);

// Resolves the destination for one proposal. `live` is the tab's live
// document, or null on a discovery tab. `resolve_search` answers the
// configured search address for a tab and query. A destination the browser
// cannot resolve is a refusal, answered with the closed result code the
// action is settled under: a search with no configured engine is
// UNSUPPORTED, an address outside http(s) — or, for a navigate or a download,
// outside https — is EGRESS_NOT_AUTHORIZED, and a link handle that no longer
// names the live document is STALE_GRAPH (or NODE_GONE when the document is
// current but the link is not).
base::expected<TaskPolicyDestination, core_service::mojom::TaskActionResultCode>
ResolveTaskPolicyDestination(
    content::BrowserContext* browser_context,
    const core_service::mojom::TaskPolicyEffect& effect,
    const TaskPolicyDocumentContext* live,
    base::FunctionRef<std::optional<std::string>(const std::string& tab_id,
                                                 const std::string& query)>
        resolve_search);

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_TASK_POLICY_DESTINATION_H_
