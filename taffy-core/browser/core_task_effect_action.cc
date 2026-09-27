// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_task_effect_action.h"

#include <algorithm>
#include <optional>
#include <string>

#include "taffy/browser/core_task_action.h"
#include "taffy/common/public/bip_identity.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

bool IsIdentifier(const std::string& value, size_t maximum) {
  return !value.empty() && value.size() <= maximum &&
         std::none_of(value.begin(), value.end(), [](char character) {
           return static_cast<unsigned char>(character) < 0x20u;
         });
}

bool IsOptionalIdentifier(const std::optional<std::string>& value) {
  return !value || IsIdentifier(*value, mojom::kMaxIdentifierBytes);
}

bool IsDigest(const std::string& value) {
  return value.size() == 64u &&
         std::all_of(value.begin(), value.end(), [](char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

bool IsNormalizedOrigin(const std::string& value) {
  if (value.empty() || value.size() > mojom::kMaxNormalizedOriginBytes) {
    return false;
  }
  const GURL parsed(value);
  const url::Origin origin = url::Origin::Create(parsed);
  return parsed.is_valid() && parsed.SchemeIsHTTPOrHTTPS() &&
         !origin.opaque() && origin.Serialize() == value;
}

bool FrozenDocumentOriginIsWellFormed(const mojom::TaskActionEffect& action) {
  if (!action.document || !action.executable) {
    return false;
  }
  if (action.document->opaque_origin_id) {
    const bool typed_navigate =
        action.executable->operation_kind ==
            mojom::TaskActionOperationKind::kNavigate &&
        action.executable->destination_address &&
        GURL(*action.executable->destination_address).SchemeIs("https") &&
        CanonicalInTabNavigateIntentMatches(
            action.executable->canonical_intent, action.executable->tab_id,
            *action.executable->destination_address);
    return action.document->normalized_origin.empty() &&
           IsIdentifier(*action.document->opaque_origin_id,
                        mojom::kMaxIdentifierBytes) &&
           action.document->graph_revision == 0u &&
           !action.executable->node_id &&
           (action.executable->operation_kind ==
                mojom::TaskActionOperationKind::kSearch ||
            typed_navigate) &&
           action.executable->action_class ==
               mojom::PolicyActionClass::kOpenLink;
  }
  return IsNormalizedOrigin(action.document->normalized_origin);
}

bool IsValidTaskTabAction(const mojom::TaskActionEffect& action) {
  if (!action.document || !action.executable || !action.executable->task_tab ||
      action.executable->task_download || action.executable->task_store ||
      action.observation || action.executable->node_id ||
      action.executable->destination_origin ||
      action.executable->destination_address ||
      action.executable->operand_handle ||
      action.executable->transient_search_query ||
      action.document->graph_revision == 0u ||
      action.preconditions.size() != 2u ||
      action.preconditions[0] !=
          mojom::TaskActionPrecondition::kDocumentUnchanged ||
      action.preconditions[1] !=
          mojom::TaskActionPrecondition::kGraphRevisionAtLeast) {
    return false;
  }
  const mojom::TaskTabActionBinding& tab = *action.executable->task_tab;
  if (!IsIdentifier(tab.browser_session_id, mojom::kMaxIdentifierBytes)) {
    return false;
  }
  switch (action.executable->operation_kind) {
    case mojom::TaskActionOperationKind::kTabsList:
      return !tab.target &&
             action.postcondition ==
                 mojom::TaskActionPostcondition::kTaskTabsListed &&
             CanonicalTaskTabListIntentMatches(
                 action.executable->canonical_intent, action.executable->tab_id,
                 tab.browser_session_id);
    case mojom::TaskActionOperationKind::kTabsActivate:
    case mojom::TaskActionOperationKind::kTabsClose: {
      if (!tab.target ||
          !IsIdentifier(tab.target->tab_id, mojom::kMaxIdentifierBytes) ||
          !IsIdentifier(tab.target->frame_id, mojom::kMaxIdentifierBytes) ||
          !IsIdentifier(tab.target->page_epoch, mojom::kMaxIdentifierBytes) ||
          tab.target->graph_revision == 0u) {
        return false;
      }
      const bool activates = action.executable->operation_kind ==
                             mojom::TaskActionOperationKind::kTabsActivate;
      return action.postcondition ==
                 (activates ? mojom::TaskActionPostcondition::kTaskTabActive
                            : mojom::TaskActionPostcondition::kTaskTabAbsent) &&
             CanonicalTaskTabTargetIntentMatches(
                 action.executable->canonical_intent, activates ? 6u : 7u,
                 action.executable->tab_id, tab.browser_session_id,
                 tab.target->tab_id, tab.target->frame_id,
                 tab.target->page_epoch, tab.target->graph_revision);
    }
    default:
      return false;
  }
}

bool IsValidObservationAction(const mojom::TaskActionEffect& action,
                              const mojom::TaskEffectBinding& binding,
                              uint64_t now_monotonic_ms) {
  const bool dom_query = action.executable->operation_kind ==
                         mojom::TaskActionOperationKind::kDomQuery;
  const bool form_inspection = action.executable->operation_kind ==
                               mojom::TaskActionOperationKind::kFormInspect;
  const bool selection_read = action.executable->operation_kind ==
                              mojom::TaskActionOperationKind::kSelectionRead;
  const bool media_node_read =
      action.executable->operation_kind ==
          mojom::TaskActionOperationKind::kImageDescribe ||
      action.executable->operation_kind ==
          mojom::TaskActionOperationKind::kImageReadText ||
      action.executable->operation_kind ==
          mojom::TaskActionOperationKind::kVideoInspect;
  const bool pdf_read = action.executable->operation_kind ==
                        mojom::TaskActionOperationKind::kPdfInspect;
  const bool page_screenshot_read =
      action.executable->operation_kind ==
      mojom::TaskActionOperationKind::kPageScreenshotInspect;
  const bool exact_node_read = form_inspection || media_node_read;
  const uint8_t media_operation_tag =
      action.executable->operation_kind ==
              mojom::TaskActionOperationKind::kImageDescribe
          ? 18u
      : action.executable->operation_kind ==
              mojom::TaskActionOperationKind::kImageReadText
          ? 19u
      : action.executable->operation_kind ==
              mojom::TaskActionOperationKind::kVideoInspect
          ? 20u
      : pdf_read ? 21u
                 : 26u;
  return action.observation &&
         IsTaskObservationOperation(action.executable->operation_kind) &&
         action.executable->node_id.has_value() == exact_node_read &&
         (!dom_query ||
          CanonicalDomQueryIntentMatches(action.executable->canonical_intent,
                                         action.executable->tab_id)) &&
         (!form_inspection ||
          CanonicalFormInspectIntentMatches(action.executable->canonical_intent,
                                            action.executable->tab_id,
                                            *action.executable->node_id)) &&
         (!selection_read || CanonicalSelectionReadIntentMatches(
                                 action.executable->canonical_intent,
                                 action.executable->tab_id)) &&
         (!(media_node_read || pdf_read || page_screenshot_read) ||
          CanonicalMediaReadIntentMatches(
              action.executable->canonical_intent, media_operation_tag,
              action.executable->tab_id, action.executable->node_id)) &&
         !action.executable->destination_origin &&
         !action.executable->destination_address &&
         !action.executable->operand_handle &&
         !action.executable->transient_search_query &&
         action.preconditions.size() == (exact_node_read ? 3u : 2u) &&
         action.preconditions[0] ==
             mojom::TaskActionPrecondition::kDocumentUnchanged &&
         action.preconditions[1] ==
             mojom::TaskActionPrecondition::kGraphRevisionAtLeast &&
         (!exact_node_read ||
          action.preconditions[2] ==
              mojom::TaskActionPrecondition::kNodePresent) &&
         action.postcondition ==
             mojom::TaskActionPostcondition::kObservationCaptured &&
         action.observation->scope ==
             mojom::ObservationScope::kCurrentDocument &&
         action.observation->max_bytes ==
             mojom::kMaxTaskObservationTotalBytes &&
         action.observation->max_nodes == mojom::kMaxTaskObservationNodes &&
         action.observation->max_text_bytes ==
             mojom::kMaxTaskObservationTextBytes &&
         action.observation->max_frames == mojom::kMaxTaskObservationFrames &&
         action.observation->deadline_ms ==
             mojom::kMaxTaskObservationDeadlineMs &&
         action.observation->deadline_ms <=
             binding.operation->deadline_monotonic_ms - now_monotonic_ms;
}

}  // namespace

bool IsValidTaskActionEffect(const mojom::TaskActionEffect& action,
                             const mojom::TaskEffectBinding& binding,
                             uint64_t now_monotonic_ms) {
  if (!binding.operation || !action.document || !action.executable ||
      !IsIdentifier(action.action_id, mojom::kMaxIdentifierBytes) ||
      !IsDigest(action.proposal_digest) ||
      !IsIdentifier(action.idempotency_key, mojom::kMaxIdempotencyKeyBytes) ||
      action.idempotency_key != binding.operation->idempotency_key ||
      !IsIdentifier(action.capability_id, mojom::kMaxIdentifierBytes) ||
      !IsIdentifier(action.dispatch_id, mojom::kMaxIdentifierBytes) ||
      !IsIdentifier(action.document->frame_id, mojom::kMaxIdentifierBytes) ||
      !IsIdentifier(action.document->page_epoch, mojom::kMaxIdentifierBytes) ||
      !GraphRevisionFloorIsWellFormed(action.executable->node_id.has_value(),
                                      action.document->graph_revision) ||
      !FrozenDocumentOriginIsWellFormed(action) ||
      !IsIdentifier(action.executable->tool_name, mojom::kMaxIdentifierBytes) ||
      !IsIdentifier(action.executable->tab_id, mojom::kMaxIdentifierBytes) ||
      action.executable->canonical_intent.empty() ||
      action.executable->canonical_intent.size() >
          mojom::kMaxCanonicalActionIntentBytes ||
      !TaskOperationMatchesClassAndTool(action.executable->operation_kind,
                                        action.executable->action_class,
                                        action.executable->tool_name) ||
      !TaskActionInputMatchesOperationAndCanonical(
          action.executable->input.get(), action.executable->operation_kind,
          action.executable->canonical_intent, action.executable->tab_id,
          action.executable->node_id) ||
      !IsOptionalIdentifier(action.executable->node_id) ||
      !IsOptionalIdentifier(action.executable->operand_handle) ||
      (action.executable->destination_origin &&
       !IsNormalizedOrigin(*action.executable->destination_origin)) ||
      (action.executable->destination_address &&
       (action.executable->destination_address->empty() ||
        action.executable->destination_address->size() >
            mojom::kMaxDestinationAddressBytes)) ||
      action.preconditions.empty() ||
      action.preconditions.size() > mojom::kMaxTaskActionPreconditions) {
    return false;
  }
  for (size_t index = 0; index < action.preconditions.size(); ++index) {
    if (std::find(action.preconditions.begin(),
                  action.preconditions.begin() + index,
                  action.preconditions[index]) !=
        action.preconditions.begin() + index) {
      return false;
    }
  }
  if (IsTaskDownloadOperation(action.executable->operation_kind)) {
    return IsValidTaskDownloadAction(action);
  }
  if (IsTaskTabOperation(action.executable->operation_kind)) {
    return IsValidTaskTabAction(action);
  }
  if (IsTaskStoreOperation(action.executable->operation_kind)) {
    return IsValidTaskStoreAction(action);
  }
  if (action.executable->task_tab || action.executable->task_download ||
      action.executable->task_store) {
    return false;
  }
  if (action.executable->action_class ==
      mojom::PolicyActionClass::kObservePage) {
    return IsValidObservationAction(action, binding, now_monotonic_ms);
  }
  // Five shapes, and `IsValidObservedLinkOpenAction` was not one of them.
  //
  // It was written, it is total over a link open's node, destination,
  // preconditions, postcondition and canonical intent, and
  // `CoreServiceManager::ExecuteTaskNavigate` calls it — but nothing ever
  // reached that call, because every one of the four shapes below refuses an
  // action carrying a node identifier and a `DOCUMENT_NAVIGATED`
  // postcondition. So every `browser.link.open` this product has ever
  // proposed was refused here, before an executor saw it, with no line in any
  // log: policy had granted it a moment earlier, and the refusal was one
  // boolean in `IsStructurallyValidTaskEffectBinding`.
  //
  // That is the whole of decision 0144's open acceptance condition — "a phone
  // shows a task following a result to the site it names" — and it is one
  // missing clause (decision 0175).
  return IsValidInTabNavigateAction(action) ||
         IsValidTaskTabControlAction(action) ||
         IsValidTaskOwnedBrowserAction(action) ||
         IsValidObservedLinkOpenAction(action) ||
         IsValidNodeTargetedPageAction(action);
}

}  // namespace taffy
