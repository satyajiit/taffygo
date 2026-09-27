// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Staging one initial source consent, one clause at a time.
//
// It sits beside `StageResumeCommand` for the same reason that one does: the
// three commands this ledger stages have nothing in common past the operation
// envelope, and the start owns the most rules of the three. Every one of them
// used to answer a single unnamed verdict, which reached a person as "Taffy
// could not read this request" — see decision 0151.

#include <optional>
#include <string>
#include <utility>

#include "base/containers/flat_set.h"
#include "taffy/browser/accepted_approval_ledger.h"
#include "taffy/browser/accepted_approval_ledger_shapes.h"
#include "taffy/browser/core_api/task_consent_shape.h"
#include "taffy/browser/core_state_binding_registry.h"

namespace taffy {

namespace mojom = core_service::mojom;

using accepted_approval_ledger_shapes::IsIdentifier;
using accepted_approval_ledger_shapes::IsKnownTaskTemplate;
using accepted_approval_ledger_shapes::IsNormalizedTupleOrigin;
using accepted_approval_ledger_shapes::IsWellFormedProviderRoute;
using accepted_approval_ledger_shapes::IsWellFormedToolAllowlist;

AuthoritySubmissionStage AcceptedApprovalLedger::StageStartCommand(
    SubmissionRecord submission,
    const mojom::StartTaskCommand& start,
    const mojom::OperationEnvelope& operation,
    const std::string& browser_profile_id,
    const std::string& browser_session_id,
    const CoreStateBindingRegistry& bindings) {
  const char* refusal = nullptr;
  if (operation.task_revision != 0u) {
    refusal = "start/creation-revision";
  } else if (!IsIdentifier(start.task_id)) {
    refusal = "start/task-id";
  } else if (!start.consent_preview) {
    refusal = "start/consent-preview";
  } else if (!IsKnownTaskTemplate(start.template_id)) {
    refusal = "start/template";
  } else if (!IsWellFormedProviderRoute(start.provider_route_id)) {
    refusal = "start/provider-route";
  } else if (start.browser_profile_id != browser_profile_id) {
    refusal = "start/browser-profile-id";
  } else if (!IsIdentifier(start.browser_session_id)) {
    refusal = "start/browser-session-id";
  } else if (start.browser_session_id != browser_session_id) {
    refusal = "start/browser-session-mismatch";
  } else if (!IsIdentifier(start.initial_consent_receipt_id)) {
    refusal = "start/consent-receipt-id";
  } else if (!IsAdmittedInitialTaskConsentShape(
                 start.template_id, *start.consent_preview,
                 start.provider_route_id, start.skill_version_id)) {
    refusal = "start/consent-shape";
  } else if (!IsWellFormedToolAllowlist(start.tool_allowlist)) {
    refusal = "start/tool-allowlist";
  } else if (bindings.FindTaskRevision(start.task_id)) {
    refusal = "start/task-already-bound";
  } else if (staged_consents_.contains(start.task_id)) {
    refusal = "start/consent-already-staged";
  } else if (accepted_consents_.contains(start.task_id)) {
    refusal = "start/consent-already-accepted";
  } else if (generation_loss_consent_candidates_.contains(start.task_id)) {
    refusal = "start/consent-generation-loss";
  } else if (suspended_consents_.contains(start.task_id)) {
    refusal = "start/consent-suspended";
  } else if (staged_consents_.size() + accepted_consents_.size() +
                 generation_loss_consent_candidates_.size() +
                 suspended_consents_.size() >=
             mojom::kMaxTaskRevisionsPerProfile) {
    refusal = "start/consent-table-full";
  } else if (!ReceiptIsUnique(start.initial_consent_receipt_id)) {
    refusal = "start/consent-receipt-not-unique";
  }
  if (refusal) {
    last_stage_refusal_ = refusal;
    return AuthoritySubmissionStage::kInvalid;
  }

  ConsentRecord record;
  submission.task_id = start.task_id;
  record.submission = std::move(submission);
  record.receipt_reference = start.initial_consent_receipt_id;
  record.browser_session_id = start.browser_session_id;
  record.source_discovery_enabled =
      start.consent_preview->source_discovery_enabled;
  record.remaining_new_source_cap = start.consent_preview->new_source_cap;
  record.provider_route = start.consent_preview->provider_route;
  base::flat_set<std::string> source_ids;
  for (const auto& source : start.consent_preview->sources) {
    if (!source || !IsIdentifier(source->source_id) ||
        !IsIdentifier(source->tab_id) ||
        !IsNormalizedTupleOrigin(source->normalized_origin) ||
        !source_ids.insert(source->source_id).second ||
        !record.sources_by_tab
             .emplace(source->tab_id,
                      ConsentSourceRecord{source->source_id,
                                          source->normalized_origin,
                                          source->canonical_locator})
             .second) {
      last_stage_refusal_ = "start/source";
      return AuthoritySubmissionStage::kInvalid;
    }
    record.source_order.push_back(source->tab_id);
  }
  staged_consents_.emplace(start.task_id, std::move(record));
  return AuthoritySubmissionStage::kStaged;
}

}  // namespace taffy
