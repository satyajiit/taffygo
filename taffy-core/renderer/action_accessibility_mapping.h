// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_ACTION_ACCESSIBILITY_MAPPING_H_
#define TAFFY_RENDERER_ACTION_ACCESSIBILITY_MAPPING_H_

#include <optional>
#include <string>

#include "taffy/renderer/semantic_graph.h"
#include "ui/accessibility/ax_enums.mojom-shared.h"

namespace taffy {

// Which accessibility object receives the mapped platform action. Most
// operations act on the exact node the browser named. SELECT_OPTION is the
// one exception: Blink does not implement kSetValue on a <select>, while the
// accessibility default action on its exact <option> performs the page's
// normal selection behavior. The resolver treats the option value only as a
// bounded selector and still returns an accessibility object; this enum has no
// spelling for a DOM write.
enum class AccessibilityTarget {
  kExactNode,
  kOptionWithValue,
};

// The one table that decides how an authorized operation reaches the page,
// and the reason decision 0059 is enforceable rather than aspirational.
//
// Every BIP operation - the three that read and the four that write - is
// performed by asking the platform accessibility layer to do something. The
// mapping is here, alone, in a file with no Blink type in it, so that the
// claim "a write never becomes a DOM call" is a property of a total function
// a host test can enumerate rather than a property of code somebody read
// once.
//
// The output type is what carries the guarantee. It is an ax::mojom::Action
// and a value, and there is no member for anything else: no element setter,
// no event to synthesize, no form to submit. A caller wanting one of those
// would have to change this header, which is a very different review from
// adding a branch inside an executor.
//
// The two entries worth arguing about:
//
//   * kSubmitForm maps to kDoDefault, the same action kActivate maps to,
//     because that is exactly what it is: pressing the control the page
//     offered. It is never HTMLFormElement::submit(). That call bypasses
//     constraint validation, bypasses the page's own submit handler, and
//     submits a form no button on the page would have submitted - so the
//     postcondition verifier could not say what the page agreed to do, and
//     the page never got the chance to decline. Going through the control
//     means the page's machinery runs and the act is one the page offered.
//     It also means the endpoint cannot submit a form that has no submit
//     control, which is correct: neither could a person.
//
//   * kToggle maps to kDoDefault as well, not to kSetValue with a boolean.
//     A toggle's default action flips it, so the requested state is a
//     precondition rather than an argument, and the executor refuses when the
//     control is already in it. Expressing "make sure this is checked" as a
//     set would have quietly changed the control when its state was not what
//     the plan believed - a plan that is wrong about the page should fail,
//     not converge.
struct AccessibilityAction {
  AccessibilityAction();
  AccessibilityAction(const AccessibilityAction&);
  AccessibilityAction& operator=(const AccessibilityAction&);
  ~AccessibilityAction();

  // The platform action to perform on the target.
  ax::mojom::Action action = ax::mojom::Action::kNone;
  AccessibilityTarget target = AccessibilityTarget::kExactNode;
  // The authorized value carried by SET_TEXT or used as the bounded exact
  // selector for SELECT_OPTION. UTF-8, because ui::AXActionData::value is:
  // keeping the same encoding here means the executor hands it straight over
  // rather than converting at the one place a conversion could lose
  // something. Empty for every other operation, and empty is not a value - an
  // operation that needs one and is handed nothing is refused by the caller
  // rather than performed with the empty string.
  std::string value;
};

// The value an operation carries, as the browser resolved it. Held apart from
// the operation because the operation comes from a closed wire enumeration and
// the value comes from the browser's own store, and mixing the two would let a
// command name one operation and supply another's argument.
struct ActionValue {
  ActionValue();
  ActionValue(const ActionValue&);
  ActionValue(ActionValue&&);
  ActionValue& operator=(const ActionValue&);
  ActionValue& operator=(ActionValue&&);
  ~ActionValue();

  // Set for kSetText and kSelectOption. Already resolved from a value
  // reference by the browser process; a renderer never resolves one and never
  // sees one (protocol 0.8, ActionInput).
  std::optional<std::string> text;
  // Set for kToggle: the state the control must end in.
  std::optional<bool> checked;
};

// Whether `action` needs a value to be performed at all.
bool ActionRequiresValue(ActionKind action);

// Whether `action` changes something a person would notice. Both writes and
// kActivate do; the two viewport-and-focus operations do not.
bool ActionMutatesPage(ActionKind action);

// The accessibility action for `action`, or nullopt when `value` does not
// carry what the operation needs.
//
// A refusal here is a malformed command rather than a page problem: the
// browser builds the command and is the only thing that could have left the
// value out.
std::optional<AccessibilityAction> AccessibilityActionFor(
    ActionKind action,
    const ActionValue& value);

}  // namespace taffy

#endif  // TAFFY_RENDERER_ACTION_ACCESSIBILITY_MAPPING_H_
