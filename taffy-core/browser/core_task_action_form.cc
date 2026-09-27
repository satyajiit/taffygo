// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <utility>

#include "taffy/browser/core_task_action.h"
#include "taffy/browser/field_value_request_coordinator.h"

namespace taffy {

namespace mojom = core_service::mojom;

std::optional<ActionType> PageActionTypeForClass(
    mojom::PolicyActionClass action_class) {
  switch (action_class) {
    case mojom::PolicyActionClass::kScrollIntoView:
      return ActionType::kScrollIntoView;
    case mojom::PolicyActionClass::kSyntheticClick:
      return ActionType::kActivate;
    case mojom::PolicyActionClass::kMoveFocus:
      return ActionType::kFocus;
    case mojom::PolicyActionClass::kFillField:
      return ActionType::kSetText;
    case mojom::PolicyActionClass::kSelectOption:
      return ActionType::kSelectOption;
    case mojom::PolicyActionClass::kToggleControl:
      return ActionType::kToggle;
    case mojom::PolicyActionClass::kSubmitForm:
      return ActionType::kSubmitForm;
    case mojom::PolicyActionClass::kObservePage:
    case mojom::PolicyActionClass::kOpenLink:
    case mojom::PolicyActionClass::kCreateTaskTab:
    case mojom::PolicyActionClass::kStartDownload:
    case mojom::PolicyActionClass::kUploadFile:
    case mojom::PolicyActionClass::kSendMessage:
    case mojom::PolicyActionClass::kPurchase:
    case mojom::PolicyActionClass::kExtractCredential:
    case mojom::PolicyActionClass::kBypassAccessControl:
    case mojom::PolicyActionClass::kExecuteToolJob:
    case mojom::PolicyActionClass::kLibraryRead:
    case mojom::PolicyActionClass::kLibraryWrite:
    case mojom::PolicyActionClass::kMemoryRead:
    case mojom::PolicyActionClass::kMemoryWrite:
    case mojom::PolicyActionClass::kControlTab:
    case mojom::PolicyActionClass::kProfileStoreRead:
      return std::nullopt;
  }
  return std::nullopt;
}

bool IsValidNodeTargetedPageAction(const mojom::TaskActionEffect& action) {
  if (action.observation || !action.executable || !action.executable->node_id ||
      action.executable->destination_origin ||
      action.executable->destination_address ||
      action.executable->operand_handle ||
      action.executable->transient_search_query ||
      action.executable->node_id->empty() ||
      action.executable->node_id->size() > mojom::kMaxIdentifierBytes ||
      !TaskOperationMatchesClassAndTool(action.executable->operation_kind,
                                        action.executable->action_class,
                                        action.executable->tool_name) ||
      action.preconditions.size() != 3u ||
      action.preconditions[0] !=
          mojom::TaskActionPrecondition::kDocumentUnchanged ||
      action.preconditions[1] !=
          mojom::TaskActionPrecondition::kGraphRevisionAtLeast ||
      action.preconditions[2] != mojom::TaskActionPrecondition::kNodePresent ||
      action.postcondition !=
          mojom::TaskActionPostcondition::kNodeStateChanged ||
      !TaskActionInputMatchesOperationAndCanonical(
          action.executable->input.get(), action.executable->operation_kind,
          action.executable->canonical_intent, action.executable->tab_id,
          action.executable->node_id)) {
    return false;
  }
  switch (action.executable->action_class) {
    case mojom::PolicyActionClass::kScrollIntoView:
    case mojom::PolicyActionClass::kFillField:
    case mojom::PolicyActionClass::kSelectOption:
    case mojom::PolicyActionClass::kToggleControl:
    case mojom::PolicyActionClass::kSubmitForm:
      return true;
    case mojom::PolicyActionClass::kSyntheticClick: {
      return CanonicalDomActivationIntentMatchesDocument(
          action.executable->canonical_intent, action.executable->tab_id,
          action.document->frame_id, action.document->page_epoch,
          action.document->graph_revision, *action.executable->node_id,
          action.document->normalized_origin);
    }
    case mojom::PolicyActionClass::kMoveFocus:
      return CanonicalDomFocusIntentMatchesDocument(
          action.executable->canonical_intent, action.executable->tab_id,
          action.document->frame_id, action.document->page_epoch,
          action.document->graph_revision, *action.executable->node_id,
          action.document->normalized_origin);
    default:
      return false;
  }
}

std::optional<PostconditionKind> PagePostconditionForTaskAction(
    mojom::PolicyActionClass action_class,
    mojom::TaskActionPostcondition postcondition) {
  if (postcondition != mojom::TaskActionPostcondition::kNodeStateChanged) {
    return std::nullopt;
  }
  switch (action_class) {
    case mojom::PolicyActionClass::kScrollIntoView:
      return PostconditionKind::kSectionVisible;
    case mojom::PolicyActionClass::kSyntheticClick:
    case mojom::PolicyActionClass::kMoveFocus:
      return PostconditionKind::kNodeStateChanged;
    case mojom::PolicyActionClass::kFillField:
    case mojom::PolicyActionClass::kSelectOption:
      // The value never returns to the browser, not even as a digest. What is
      // corroborated is an exact form-state change on this exact node after
      // dispatch; the renderer transport carries only its revision.
      return PostconditionKind::kNodeValueChanged;
    case mojom::PolicyActionClass::kToggleControl:
      return PostconditionKind::kNodeStateChanged;
    case mojom::PolicyActionClass::kSubmitForm:
      // Submission is kDoDefault on the submit control. The browser installs
      // its navigation observer before that call and verifies the commit; a
      // renderer acknowledgement can never satisfy this claim.
      return PostconditionKind::kCommittedNavigation;
    default:
      return std::nullopt;
  }
}

std::optional<ActionInput> PageInputForTaskAction(
    const mojom::TaskActionEffect& action,
    ActionType action_type) {
  if (!action.executable || !action.executable->input) {
    return std::nullopt;
  }
  if (PageActionTypeForClass(action.executable->action_class) != action_type) {
    return std::nullopt;
  }
  const mojom::TaskActionInput& supplied = *action.executable->input;
  ActionInput input;
  switch (action_type) {
    case ActionType::kSetText:
    case ActionType::kSelectOption:
      if (supplied.kind != mojom::TaskActionInputKind::kSuppliedValue ||
          !supplied.supplied_value || supplied.toggle_state ||
          supplied.supplied_value->request_id.empty()) {
        return std::nullopt;
      }
      input.kind = action_type == ActionType::kSetText
                       ? ActionInputKind::kText
                       : ActionInputKind::kOption;
      input.value_reference = ValueReference{DerivedValueReference(
          supplied.supplied_value->request_id, supplied.supplied_value->index)};
      break;
    case ActionType::kToggle:
      if (supplied.kind != mojom::TaskActionInputKind::kToggleState ||
          supplied.supplied_value || !supplied.toggle_state) {
        return std::nullopt;
      }
      input.kind = ActionInputKind::kToggleState;
      input.checked = supplied.toggle_state->checked;
      break;
    case ActionType::kSubmitForm:
    case ActionType::kActivate:
    case ActionType::kFocus:
    case ActionType::kScrollIntoView:
      if (supplied.kind != mojom::TaskActionInputKind::kNone ||
          supplied.supplied_value || supplied.toggle_state) {
        return std::nullopt;
      }
      input.kind = ActionInputKind::kNone;
      break;
  }
  return NamedInputMatchesAction(action_type, input)
             ? std::optional<ActionInput>(std::move(input))
             : std::nullopt;
}

}  // namespace taffy
