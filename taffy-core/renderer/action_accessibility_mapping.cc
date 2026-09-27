// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/action_accessibility_mapping.h"

#include <string>

// VERIFY AT SP-04:
//   * ax::mojom::Action::kSetValue at the pin, and specifically whether
//     performing it on a text field dispatches the input and change events a
//     page's own validation listens for. If it does not, SET_TEXT is not
//     usable through this path and the operation goes back to being one the
//     endpoint refuses - it does NOT become a DOM setter, because a value
//     written with no events is a value the page never agreed to and cannot
//     be verified against NODE_VALUE_CHANGED either.
//   * kDoDefault on an option at the pin, and specifically that it reaches
//     HTMLSelectElement::SelectOptionByAccessKey and emits the select's normal
//     input/change events. The exact option is resolved separately by its
//     bounded, unique value attribute; a label is never treated as a value.
//   * kDoDefault on a submit button, and whether it runs constraint
//     validation. The whole reason this is the submission path is that it
//     should.

namespace taffy {

AccessibilityAction::AccessibilityAction() = default;
AccessibilityAction::AccessibilityAction(const AccessibilityAction&) = default;
AccessibilityAction& AccessibilityAction::operator=(
    const AccessibilityAction&) = default;
AccessibilityAction::~AccessibilityAction() = default;

ActionValue::ActionValue() = default;
ActionValue::ActionValue(const ActionValue&) = default;
ActionValue::ActionValue(ActionValue&&) = default;
ActionValue& ActionValue::operator=(const ActionValue&) = default;
ActionValue& ActionValue::operator=(ActionValue&&) = default;
ActionValue::~ActionValue() = default;

bool ActionRequiresValue(ActionKind action) {
  switch (action) {
    case ActionKind::kScrollIntoView:
    case ActionKind::kFocus:
    case ActionKind::kActivate:
    case ActionKind::kSubmitForm:
      return false;
    case ActionKind::kSetText:
    case ActionKind::kSelectOption:
    case ActionKind::kToggle:
      return true;
  }
}

bool ActionMutatesPage(ActionKind action) {
  switch (action) {
    case ActionKind::kScrollIntoView:
    case ActionKind::kFocus:
      // Moving the viewport and moving the focus ring. Both are already
      // exposed to assistive technology and neither changes what the page
      // holds.
      return false;
    case ActionKind::kActivate:
    case ActionKind::kSetText:
    case ActionKind::kSelectOption:
    case ActionKind::kToggle:
    case ActionKind::kSubmitForm:
      return true;
  }
}

std::optional<AccessibilityAction> AccessibilityActionFor(
    ActionKind action,
    const ActionValue& value) {
  AccessibilityAction mapped;
  switch (action) {
    case ActionKind::kScrollIntoView:
      mapped.action = ax::mojom::Action::kScrollToMakeVisible;
      return mapped;

    case ActionKind::kFocus:
      mapped.action = ax::mojom::Action::kFocus;
      return mapped;

    case ActionKind::kActivate:
    case ActionKind::kSubmitForm:
      // The element's default action, dispatched through Blink's event
      // pipeline. Submission is not a second primitive: it is this one, aimed
      // at the control the page put there for it. See the header for why the
      // programmatic alternative is refused rather than merely avoided.
      mapped.action = ax::mojom::Action::kDoDefault;
      return mapped;

    case ActionKind::kToggle:
      // The requested state is a precondition the caller checks, not an
      // argument. Being handed no state at all is still a refusal: it means
      // the command did not say what it wanted, and flipping a control on
      // that basis is guessing.
      if (!value.checked.has_value()) {
        return std::nullopt;
      }
      mapped.action = ax::mojom::Action::kDoDefault;
      return mapped;

    case ActionKind::kSetText:
      // Empty is refused rather than treated as "clear the field": clearing
      // is a different intent and a proposal that meant it would have to say
      // so, in a shape a person could be shown.
      if (!value.text.has_value() || value.text->empty()) {
        return std::nullopt;
      }
      mapped.action = ax::mojom::Action::kSetValue;
      mapped.value = value.text.value();
      return mapped;

    case ActionKind::kSelectOption:
      if (!value.text.has_value() || value.text->empty()) {
        return std::nullopt;
      }
      // Blink's AXNodeObject implements kSetValue for text controls, text
      // areas and contenteditable nodes, not for HTMLSelectElement. The exact
      // matching option's accessibility default action is the supported path
      // and invokes SelectOptionByAccessKey, including the page's events.
      mapped.action = ax::mojom::Action::kDoDefault;
      mapped.target = AccessibilityTarget::kOptionWithValue;
      mapped.value = value.text.value();
      return mapped;
  }
}

}  // namespace taffy
