// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_TASK_POLICY_H_
#define TAFFY_BROWSER_CORE_TASK_POLICY_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <vector>

#include "taffy/common/public/page_intelligence_service.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

// Validates the disjoint literal-address and fresh-link download intents.
bool TaskDownloadCanonicalMatchesEffect(
    const core_service::mojom::TaskPolicyEffect& effect);
// Both download intent families freeze the browser session, in different
// canonical fields. A non-download effect has no download session to check.
bool TaskDownloadCanonicalMatchesSession(
    const core_service::mojom::TaskPolicyEffect& effect,
    const std::string& browser_session_id);

// Browser-owned live facts that replace no reducer fact: the proposal fixes
// its task/action/tab and the browser supplies only the current document tuple.
struct TaskPolicyDocumentBinding {
  std::string tab_id;
  std::string frame_id;
  std::string page_epoch;
  std::string origin;
  // Present only for the browser-created opaque blank document used to find
  // the first source of a zero-source Web errand. It is never serialized as a
  // tuple origin and grants no ordinary page authority.
  std::optional<std::string> opaque_origin_id;
  uint64_t graph_revision = 0;
  // The browser-resolved address a destination-bearing proposal leads to: a
  // configured search, an observed link, a task tab, a download, or the https
  // address a navigate typed. Absent for back, forward and reload, whose
  // target is the tab's own history and is bound at execution.
  std::optional<std::string> destination_address;
  // The tuple origin derived from destination_address. A search, a task tab,
  // a link and a typed navigate may deliberately cross origins while
  // retaining the source document above: where they land is admitted and
  // counted when the outcome comes back. When the binder is given none it
  // falls back to `origin`, which admits only a same-origin destination.
  std::optional<std::string> destination_origin;
};

// Browser-private metadata retained after the exact-value confirmation. It
// deliberately contains no field bytes or value-reference material: those
// remain in ValueReferenceVault until the action dispatcher spends them.
struct BrowserFormActionPreapproval {
  std::string task_id;
  std::string action_id;
  std::string proposal_digest;
  std::string tool_name;
  std::string tab_id;
  std::string node_id;
  std::string request_id;
  std::vector<uint8_t> canonical_intent;
  uint32_t supplied_value_index = 0u;
  std::string normalized_origin;
  std::string frame_id;
  std::string page_epoch;
  uint64_t graph_revision = 0u;
  uint64_t expires_at_monotonic_ms = 0u;
  uint64_t expires_at_utc_ms = 0u;
};

// Validates immutable reducer facts before any actor lease is issued. This
// migration's executable task slice is page-scoped: observation, click, and
// scroll. Other action classes still lack a typed dispatch body here and stay
// refused rather than rebound.
bool IsValidReadOnlyTaskPolicyEffect(
    const core_service::mojom::TaskPolicyEffect& effect,
    uint64_t service_generation,
    uint64_t task_revision,
    uint64_t now_monotonic_ms);
bool IsValidReadOnlyTaskPolicyEffect(
    const core_service::mojom::TaskPolicyEffect& effect,
    uint64_t service_generation,
    uint64_t task_revision,
    const std::string& browser_session_id,
    uint64_t now_monotonic_ms,
    uint64_t now_utc_ms);

// Binds a validated task proposal to one browser-resolved document and the
// exact lease just issued for it. All reducer policy facts are copied without
// reinterpretation; the browser never creates an approval or authority fact.
// A null answer is a malformed or inconsistent binding, except when
// `denial_code` is set: then the binding was understood and refused — a
// destination outside the origin the document may reach — and the code is
// the closed result the proposal is denied with.
core_service::mojom::PolicyEvaluationRequestPtr BindReadOnlyTaskPolicyRequest(
    const core_service::mojom::TaskPolicyEffect& effect,
    const TaskPolicyDocumentBinding& document,
    const std::string& browser_profile_id,
    const ActorLeaseResult& lease,
    uint64_t service_generation,
    uint64_t now_monotonic_ms,
    uint64_t now_utc_ms,
    std::optional<core_service::mojom::TaskActionResultCode>* denial_code =
        nullptr);

// Whether this ask is a fill that has not been approved yet. Its only
// admissible answer is a question for the person: the policy engine answers
// it with one (decision 0089 section 3), the person's answer on the sheet
// approves it (decision 0063), and the approved ask is what may be granted. A
// grant for an ask this names is refused by the caller whatever the decider
// said, so the browser holds that rule independently (decision 0239).
bool TaskPolicyAskIsAnsweredByAsking(
    const core_service::mojom::TaskPolicyEffect& effect);

// Matches the later executable policy ask against the browser-private,
// one-use confirmation tuple. The caller must erase the record before calling
// this function so a mismatch cannot be used as an oracle for retrying a
// substitution.
bool TaskPolicyEffectMatchesFormPreapproval(
    const core_service::mojom::TaskPolicyEffect& effect,
    const TaskPolicyDocumentBinding& document,
    const BrowserFormActionPreapproval& approved,
    const std::string& browser_session_id,
    uint64_t now_monotonic_ms,
    uint64_t now_utc_ms);

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_TASK_POLICY_H_
