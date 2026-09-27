// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_task_policy.h"

#include <algorithm>
#include <string>

#include "taffy/browser/core_task_action.h"
#include "url/gurl.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

bool IsIdentifier(const std::string& value) {
  return !value.empty() && value.size() <= mojom::kMaxIdentifierBytes &&
         std::none_of(value.begin(), value.end(), [](char character) {
           return static_cast<unsigned char>(character) < 0x20u;
         });
}

bool IsLowerHexDigest(const std::string& value) {
  return value.size() == 64u &&
         std::all_of(value.begin(), value.end(), [](char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

bool IsValidPrincipal(const mojom::PolicyPrincipal* principal) {
  if (!principal) {
    return false;
  }
  switch (principal->kind) {
    case mojom::PolicyPrincipalKind::kAssistant:
      return !principal->skill_version_id;
    case mojom::PolicyPrincipalKind::kSkill:
      return principal->skill_version_id &&
             IsIdentifier(*principal->skill_version_id);
  }
  return false;
}

bool IsValidDataClasses(const std::vector<mojom::BipSensitivity>& values) {
  if (values.empty() || values.size() > 12u) {
    return false;
  }
  for (size_t index = 0; index < values.size(); ++index) {
    if (std::find(values.begin(), values.begin() + index, values[index]) !=
        values.begin() + index) {
      return false;
    }
  }
  return values.size() == 1u ||
         std::find(values.begin(), values.end(),
                   mojom::BipSensitivity::kNotSensitive) == values.end();
}

bool IsValidApproval(const mojom::PolicyApprovalFact* approval,
                     const mojom::TaskPolicyEffect& effect,
                     uint64_t generation,
                     uint64_t now_monotonic_ms) {
  return !approval || (IsIdentifier(approval->receipt_reference) &&
                       approval->proposal_digest == effect.proposal_digest &&
                       approval->service_generation == generation &&
                       approval->expires_at_monotonic_ms > now_monotonic_ms &&
                       approval->expires_at_utc_ms != 0u &&
                       IsIdentifier(approval->browser_session_id));
}

bool ContextRiskMatchesClass(mojom::PolicyActionClass action_class,
                             mojom::PolicyRiskClass risk) {
  switch (action_class) {
    case mojom::PolicyActionClass::kObservePage:
    case mojom::PolicyActionClass::kScrollIntoView:
    // A tool job's floor is the read floor — it computes over material the
    // task already holds and sends nothing. Mirrors the reducer's projection
    // and policy-engine's baseline; a binding claiming more is refused here.
    case mojom::PolicyActionClass::kExecuteToolJob:
    case mojom::PolicyActionClass::kLibraryRead:
    case mojom::PolicyActionClass::kMemoryRead:
    case mojom::PolicyActionClass::kProfileStoreRead:
      return risk == mojom::PolicyRiskClass::kLocalRead;
    case mojom::PolicyActionClass::kSyntheticClick:
    case mojom::PolicyActionClass::kOpenLink:
    case mojom::PolicyActionClass::kCreateTaskTab:
    case mojom::PolicyActionClass::kMoveFocus:
    case mojom::PolicyActionClass::kControlTab:
      return risk == mojom::PolicyRiskClass::kReversibleDisclosure;
    case mojom::PolicyActionClass::kFillField:
    case mojom::PolicyActionClass::kSelectOption:
    case mojom::PolicyActionClass::kToggleControl:
    case mojom::PolicyActionClass::kSubmitForm:
    case mojom::PolicyActionClass::kStartDownload:
    case mojom::PolicyActionClass::kUploadFile:
    case mojom::PolicyActionClass::kLibraryWrite:
    case mojom::PolicyActionClass::kMemoryWrite:
      return risk == mojom::PolicyRiskClass::kSensitiveDisclosure;
    case mojom::PolicyActionClass::kSendMessage:
    case mojom::PolicyActionClass::kPurchase:
    case mojom::PolicyActionClass::kExtractCredential:
    case mojom::PolicyActionClass::kBypassAccessControl:
      return false;
  }
  return false;
}

bool NodeTargetIsWellFormed(const mojom::TaskPolicyEffect& effect) {
  if (effect.operation_kind == mojom::TaskActionOperationKind::kDownloadStart &&
      effect.tool_name == "browser.download.from_link") {
    return effect.action_class == mojom::PolicyActionClass::kStartDownload &&
           effect.node_id && IsIdentifier(*effect.node_id);
  }
  if (IsTaskLinkOpenOperation(effect.operation_kind)) {
    return effect.action_class == mojom::PolicyActionClass::kOpenLink &&
           effect.node_id && IsIdentifier(*effect.node_id);
  }
  if (effect.operation_kind == mojom::TaskActionOperationKind::kFormInspect ||
      effect.operation_kind == mojom::TaskActionOperationKind::kImageDescribe ||
      effect.operation_kind == mojom::TaskActionOperationKind::kImageReadText ||
      effect.operation_kind == mojom::TaskActionOperationKind::kVideoInspect) {
    return effect.action_class == mojom::PolicyActionClass::kObservePage &&
           effect.node_id && IsIdentifier(*effect.node_id);
  }
  switch (effect.action_class) {
    case mojom::PolicyActionClass::kObservePage:
      // DomRead and SelectionRead have no node target. FormInspect returned
      // above and therefore cannot fall through to this broader shape.
      return !effect.node_id;
    case mojom::PolicyActionClass::kScrollIntoView:
    case mojom::PolicyActionClass::kSyntheticClick:
    case mojom::PolicyActionClass::kFillField:
    case mojom::PolicyActionClass::kSelectOption:
    case mojom::PolicyActionClass::kToggleControl:
    case mojom::PolicyActionClass::kSubmitForm:
      return effect.node_id && IsIdentifier(*effect.node_id);
    case mojom::PolicyActionClass::kMoveFocus:
      if (effect.operation_kind ==
          mojom::TaskActionOperationKind::kTabsActivate) {
        return !effect.node_id;
      }
      return effect.operation_kind ==
                 mojom::TaskActionOperationKind::kDomFocus &&
             effect.node_id && IsIdentifier(*effect.node_id);
    case mojom::PolicyActionClass::kStartDownload:
    case mojom::PolicyActionClass::kLibraryRead:
    case mojom::PolicyActionClass::kLibraryWrite:
    case mojom::PolicyActionClass::kMemoryRead:
    case mojom::PolicyActionClass::kMemoryWrite:
    case mojom::PolicyActionClass::kProfileStoreRead:
      return !effect.node_id;
    case mojom::PolicyActionClass::kOpenLink:
    case mojom::PolicyActionClass::kCreateTaskTab:
    case mojom::PolicyActionClass::kControlTab:
    // A tool job has no page target at all, so a node identity on its ask is
    // a claim about a document it never touches.
    case mojom::PolicyActionClass::kExecuteToolJob:
      return !effect.node_id;
    case mojom::PolicyActionClass::kUploadFile:
    case mojom::PolicyActionClass::kSendMessage:
    case mojom::PolicyActionClass::kPurchase:
    case mojom::PolicyActionClass::kExtractCredential:
    case mojom::PolicyActionClass::kBypassAccessControl:
      return false;
  }
  return false;
}

bool TaskTabCanonicalMatchesEffect(const mojom::TaskPolicyEffect& effect) {
  if (!IsTaskTabOperation(effect.operation_kind)) {
    return true;
  }
  const std::optional<CanonicalTaskTabIntent> parsed =
      ReadCanonicalTaskTabIntent(effect.canonical_intent);
  if (!parsed || parsed->context_tab_id != effect.tab_id) {
    return false;
  }
  switch (effect.operation_kind) {
    case mojom::TaskActionOperationKind::kTabsList:
      return parsed->operation_tag == 5u;
    case mojom::TaskActionOperationKind::kTabsActivate:
      return parsed->operation_tag == 6u;
    case mojom::TaskActionOperationKind::kTabsClose:
      return parsed->operation_tag == 7u;
    default:
      return false;
  }
}

// A store read's ask carries no words: the policy decides on the class, the
// kind and the tab, and the digest of the words is checked at dispatch.
bool TaskStoreCanonicalMatchesEffect(const mojom::TaskPolicyEffect& effect) {
  if (!IsTaskStoreOperation(effect.operation_kind)) {
    return true;
  }
  const std::optional<CanonicalStoreIntent> parsed =
      ReadCanonicalStoreIntent(effect.canonical_intent);
  if (!parsed || parsed->context_tab_id != effect.tab_id ||
      effect.destination_address || effect.node_id ||
      effect.action_class != mojom::PolicyActionClass::kProfileStoreRead) {
    return false;
  }
  switch (effect.operation_kind) {
    case mojom::TaskActionOperationKind::kHistorySearch:
      return parsed->kind == 0u;
    case mojom::TaskActionOperationKind::kHistoryRecent:
      return parsed->kind == 1u;
    case mojom::TaskActionOperationKind::kBookmarksSearch:
      return parsed->kind == 2u;
    case mojom::TaskActionOperationKind::kBookmarksList:
      return parsed->kind == 3u;
    case mojom::TaskActionOperationKind::kOpenTabsList:
      return parsed->kind == 4u;
    default:
      return false;
  }
}

}  // namespace

bool IsValidReadOnlyTaskPolicyEffect(const mojom::TaskPolicyEffect& effect,
                                     uint64_t service_generation,
                                     uint64_t task_revision,
                                     uint64_t now_monotonic_ms) {
  // A download's first policy ask carries no receipt. The production decider
  // must return RequireApproval before the reducer can publish that question;
  // an existing receipt is still checked here and consumed by the ledger.
  if (!effect.operation || effect.operation->service_generation == 0u ||
      effect.operation->service_generation != service_generation ||
      effect.operation->task_revision != task_revision ||
      effect.operation->deadline_monotonic_ms <= now_monotonic_ms ||
      effect.operation->operation_id.empty() ||
      effect.operation->operation_id.size() > mojom::kMaxOperationIdBytes ||
      effect.effect_id.empty() ||
      effect.effect_id.size() > mojom::kMaxIdentifierBytes ||
      !IsIdentifier(effect.task_id) || !IsIdentifier(effect.action_id) ||
      !IsIdentifier(effect.tab_id) || !IsIdentifier(effect.idempotency_key) ||
      !IsIdentifier(effect.tool_name) || effect.canonical_intent.empty() ||
      effect.canonical_intent.size() > mojom::kMaxCanonicalActionIntentBytes ||
      !TaskOperationMatchesClassAndTool(
          effect.operation_kind, effect.action_class, effect.tool_name) ||
      !TaskActionInputMatchesOperationAndCanonical(
          effect.input.get(), effect.operation_kind, effect.canonical_intent,
          effect.tab_id, effect.node_id) ||
      ((effect.operation_kind == mojom::TaskActionOperationKind::kNavigate ||
        IsTaskTabOpenOperation(effect.operation_kind) ||
        (effect.operation_kind ==
             mojom::TaskActionOperationKind::kDownloadStart &&
         effect.tool_name != "browser.download.from_link")) !=
       effect.destination_address.has_value()) ||
      (IsTaskSearchOperation(effect.operation_kind) !=
       effect.transient_search_query.has_value()) ||
      (IsTaskLinkOpenOperation(effect.operation_kind) &&
       effect.destination_address.has_value()) ||
      (IsTaskTabControlOperation(effect.operation_kind) &&
       effect.destination_address.has_value()) ||
      (effect.destination_address && effect.destination_address->empty()) ||
      (effect.transient_search_query &&
       !CanonicalSearchQueryMatches(effect.canonical_intent, effect.tab_id,
                                    *effect.transient_search_query)) ||
      (effect.operation_kind == mojom::TaskActionOperationKind::kDomQuery &&
       (effect.node_id || effect.transient_search_query ||
        !CanonicalDomQueryIntentMatches(effect.canonical_intent,
                                        effect.tab_id))) ||
      (IsTaskTabOpenOperation(effect.operation_kind) &&
       !CanonicalTabsOpenIntentMatches(effect.canonical_intent, effect.tab_id,
                                       *effect.destination_address)) ||
      !TaskTabCanonicalMatchesEffect(effect) ||
      !TaskDownloadCanonicalMatchesEffect(effect) ||
      !TaskStoreCanonicalMatchesEffect(effect) ||
      (IsTaskLinkOpenOperation(effect.operation_kind) &&
       (!effect.node_id ||
        !CanonicalLinkOpenIntentMatchesProjections(
            effect.canonical_intent, effect.tab_id, *effect.node_id))) ||
      (effect.operation_kind == mojom::TaskActionOperationKind::kFormInspect &&
       (!effect.node_id ||
        !CanonicalFormInspectIntentMatches(effect.canonical_intent,
                                           effect.tab_id, *effect.node_id))) ||
      (effect.operation_kind ==
           mojom::TaskActionOperationKind::kSelectionRead &&
       !CanonicalSelectionReadIntentMatches(effect.canonical_intent,
                                            effect.tab_id)) ||
      ((effect.operation_kind ==
            mojom::TaskActionOperationKind::kImageDescribe ||
        effect.operation_kind ==
            mojom::TaskActionOperationKind::kImageReadText ||
        effect.operation_kind ==
            mojom::TaskActionOperationKind::kVideoInspect ||
        effect.operation_kind == mojom::TaskActionOperationKind::kPdfInspect ||
        effect.operation_kind ==
            mojom::TaskActionOperationKind::kPageScreenshotInspect) &&
       !CanonicalMediaReadIntentMatches(
           effect.canonical_intent,
           effect.operation_kind ==
                   mojom::TaskActionOperationKind::kImageDescribe
               ? 18u
           : effect.operation_kind ==
                   mojom::TaskActionOperationKind::kImageReadText
               ? 19u
           : effect.operation_kind ==
                   mojom::TaskActionOperationKind::kVideoInspect
               ? 20u
           : effect.operation_kind ==
                   mojom::TaskActionOperationKind::kPdfInspect
               ? 21u
               : 26u,
           effect.tab_id, effect.node_id)) ||
      effect.operation->idempotency_key != effect.idempotency_key ||
      !IsLowerHexDigest(effect.proposal_digest) ||
      !IsValidPrincipal(effect.principal.get()) ||
      !IsValidDataClasses(effect.data_classes) ||
      !IsValidApproval(effect.approval.get(), effect, service_generation,
                       now_monotonic_ms) ||
      // A fill is the one write class whose first ask may carry no approval:
      // the policy engine answers it with a question for the person (decision
      // 0089 section 3), and `TaskPolicyAskIsAnsweredByAsking` is what keeps
      // that the only answer it can have (decision 0239).
      ((effect.action_class == mojom::PolicyActionClass::kSelectOption ||
        effect.action_class == mojom::PolicyActionClass::kToggleControl ||
        effect.action_class == mojom::PolicyActionClass::kSubmitForm ||
        effect.action_class == mojom::PolicyActionClass::kLibraryWrite) &&
       !effect.approval) ||
      effect.policy_version == 0u) {
    return false;
  }

  // Whole-document and live-selection reads, exact-form inspection,
  // exact-node scroll, and same-origin in-tab navigation are the page-scoped
  // classes this binding will evaluate. Every operation-specific node shape
  // was checked above before a capability scope can be minted.
  if (!IsAdmittedPageActionClass(effect.action_class) ||
      !ContextRiskMatchesClass(effect.action_class, effect.context_risk) ||
      !NodeTargetIsWellFormed(effect)) {
    return false;
  }
  if (!effect.discovery) {
    return true;
  }
  const bool discovery_operation =
      effect.operation_kind == mojom::TaskActionOperationKind::kSearch ||
      (effect.operation_kind == mojom::TaskActionOperationKind::kNavigate &&
       effect.destination_address &&
       HttpAddressOrigin(*effect.destination_address).has_value() &&
       GURL(*effect.destination_address).SchemeIs("https") &&
       CanonicalInTabNavigateIntentMatches(effect.canonical_intent,
                                           effect.tab_id,
                                           *effect.destination_address));
  return discovery_operation &&
         effect.action_class == mojom::PolicyActionClass::kOpenLink &&
         effect.context_risk == mojom::PolicyRiskClass::kReversibleDisclosure &&
         effect.principal->kind == mojom::PolicyPrincipalKind::kAssistant &&
         !effect.principal->skill_version_id && !effect.approval &&
         effect.control_mode == mojom::TaskControlMode::kAssistant &&
         effect.data_classes.size() == 1u &&
         effect.data_classes.front() == mojom::BipSensitivity::kNotSensitive &&
         !effect.node_id &&
         effect.discovery->discovery_tab_id == effect.tab_id &&
         IsIdentifier(effect.discovery->browser_session_id) &&
         effect.discovery->remaining_new_source_cap != 0u &&
         effect.discovery->remaining_new_source_cap <= mojom::kMaxNewSourceCap;
}

bool IsValidReadOnlyTaskPolicyEffect(const mojom::TaskPolicyEffect& effect,
                                     uint64_t service_generation,
                                     uint64_t task_revision,
                                     const std::string& browser_session_id,
                                     uint64_t now_monotonic_ms,
                                     uint64_t now_utc_ms) {
  const std::optional<CanonicalTaskTabIntent> task_tab =
      IsTaskTabOperation(effect.operation_kind)
          ? ReadCanonicalTaskTabIntent(effect.canonical_intent)
          : std::nullopt;
  return IsValidReadOnlyTaskPolicyEffect(effect, service_generation,
                                         task_revision, now_monotonic_ms) &&
         (!IsTaskTabOperation(effect.operation_kind) ||
          (task_tab && task_tab->browser_session_id == browser_session_id)) &&
         TaskDownloadCanonicalMatchesSession(effect, browser_session_id) &&
         (!effect.discovery ||
          effect.discovery->browser_session_id == browser_session_id) &&
         (!effect.approval ||
          (effect.approval->expires_at_utc_ms > now_utc_ms &&
           effect.approval->browser_session_id == browser_session_id));
}

bool TaskPolicyAskIsAnsweredByAsking(const mojom::TaskPolicyEffect& effect) {
  return effect.action_class == mojom::PolicyActionClass::kFillField &&
         !effect.approval;
}

bool TaskPolicyEffectMatchesFormPreapproval(
    const mojom::TaskPolicyEffect& effect,
    const TaskPolicyDocumentBinding& document,
    const BrowserFormActionPreapproval& approved,
    const std::string& browser_session_id,
    uint64_t now_monotonic_ms,
    uint64_t now_utc_ms) {
  const bool exact_input =
      effect.input &&
      effect.input->kind == mojom::TaskActionInputKind::kSuppliedValue &&
      effect.input->supplied_value && !effect.input->toggle_state &&
      effect.input->supplied_value->request_id == approved.request_id &&
      effect.input->supplied_value->index == approved.supplied_value_index;
  return effect.action_class == mojom::PolicyActionClass::kFillField &&
         effect.operation_kind == mojom::TaskActionOperationKind::kFormFill &&
         effect.task_id == approved.task_id &&
         effect.action_id == approved.action_id &&
         effect.proposal_digest == approved.proposal_digest &&
         effect.tool_name == approved.tool_name &&
         effect.tab_id == approved.tab_id && effect.node_id &&
         *effect.node_id == approved.node_id &&
         effect.canonical_intent == approved.canonical_intent && exact_input &&
         !effect.destination_address && !effect.transient_search_query &&
         document.tab_id == approved.tab_id &&
         document.origin == approved.normalized_origin &&
         document.frame_id == approved.frame_id &&
         document.page_epoch == approved.page_epoch &&
         document.graph_revision == approved.graph_revision &&
         effect.approval &&
         effect.approval->expires_at_monotonic_ms ==
             approved.expires_at_monotonic_ms &&
         effect.approval->expires_at_utc_ms == approved.expires_at_utc_ms &&
         effect.approval->browser_session_id == browser_session_id &&
         now_monotonic_ms < approved.expires_at_monotonic_ms &&
         now_utc_ms < approved.expires_at_utc_ms;
}

}  // namespace taffy
