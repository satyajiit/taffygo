// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/wire_conversions.h"

#include "base/notreached.h"

// The action half of the enumeration translation: which action a proposal
// names, and which result code a renderer-side precondition failure carries.
// These two are apart from the rest because they are the ones where a silent
// fall-through would authorize something, rather than describe something.
//
// Every function here is a switch with no default case, and that is the only
// reason this file is worth reading: a member added to the mojom, or to the
// internal mirror in semantic_graph.h, becomes a compile error rather than a
// silent fall-through. For an action type or a sensitivity class, a silent
// fall-through is a security bug rather than a defect.
//
// The struct conversions live in wire_struct_conversions.cc. They are a
// different job - they apply the redaction gate and the URL disclosure rule -
// and mixing them in here buried the exhaustiveness property under several
// hundred lines of field copying.

namespace taffy::wire {

mojom::ActionType ToMojom(ActionKind action) {
  switch (action) {
    case ActionKind::kActivate:
      return mojom::ActionType::kActivate;
    case ActionKind::kFocus:
      return mojom::ActionType::kFocus;
    case ActionKind::kScrollIntoView:
      return mojom::ActionType::kScrollIntoView;
    case ActionKind::kSetText:
      return mojom::ActionType::kSetText;
    case ActionKind::kSelectOption:
      return mojom::ActionType::kSelectOption;
    case ActionKind::kToggle:
      return mojom::ActionType::kToggle;
    case ActionKind::kSubmitForm:
      return mojom::ActionType::kSubmitForm;
  }
}

// The one place an incoming action type is interpreted. Total since protocol
// 0.8; see the declaration in wire_conversions.h for what changed and for
// what refuses a write now that this function does not.
ActionKind FromMojom(mojom::ActionType action) {
  switch (action) {
    case mojom::ActionType::kActivate:
      return ActionKind::kActivate;
    case mojom::ActionType::kFocus:
      return ActionKind::kFocus;
    case mojom::ActionType::kScrollIntoView:
      return ActionKind::kScrollIntoView;
    case mojom::ActionType::kSetText:
      return ActionKind::kSetText;
    case mojom::ActionType::kSelectOption:
      return ActionKind::kSelectOption;
    case mojom::ActionType::kToggle:
      return ActionKind::kToggle;
    case mojom::ActionType::kSubmitForm:
      return ActionKind::kSubmitForm;
  }
}

mojom::RendererActionOutcome ToMojom(PreconditionCode code) {
  switch (code) {
    case PreconditionCode::kOk:
      return mojom::RendererActionOutcome::kDispatched;
    case PreconditionCode::kStalePageEpoch:
      return mojom::RendererActionOutcome::kStalePageEpoch;
    case PreconditionCode::kStaleGraph:
    case PreconditionCode::kGraphMovedDuringPreflight:
      return mojom::RendererActionOutcome::kStaleGraph;
    case PreconditionCode::kNodeGone:
      return mojom::RendererActionOutcome::kNodeGone;
    case PreconditionCode::kRoleOrActionChanged:
      return mojom::RendererActionOutcome::kRoleOrActionChanged;
    case PreconditionCode::kNotVisible:
      return mojom::RendererActionOutcome::kNotVisible;
    case PreconditionCode::kOccluded:
      return mojom::RendererActionOutcome::kOccluded;
    case PreconditionCode::kNotEnabled:
      return mojom::RendererActionOutcome::kNotEnabled;
    case PreconditionCode::kNotEditable:
      return mojom::RendererActionOutcome::kNotEditable;
    case PreconditionCode::kSensitiveField:
      return mojom::RendererActionOutcome::kSensitiveField;
    case PreconditionCode::kDestinationChanged:
      return mojom::RendererActionOutcome::kDestinationChanged;
    case PreconditionCode::kOriginChanged:
      return mojom::RendererActionOutcome::kOriginChanged;
    case PreconditionCode::kUnsupported:
      return mojom::RendererActionOutcome::kUnsupported;
  }
}

}  // namespace taffy::wire
