// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/action_value_resolution.h"

#include <string>
#include <utility>

namespace taffy {

ResolvedActionInput::ResolvedActionInput() = default;
ResolvedActionInput::ResolvedActionInput(ResolvedActionInput&&) = default;
ResolvedActionInput& ResolvedActionInput::operator=(ResolvedActionInput&&) =
    default;
ResolvedActionInput::~ResolvedActionInput() = default;

namespace {

// Sensitivity, member for member. Written out rather than static_cast so that
// a member appended to one enumeration and not the other fails to compile.
mojom::Sensitivity ToMojomSensitivity(Sensitivity sensitivity) {
  switch (sensitivity) {
    case Sensitivity::kNotSensitive:
      return mojom::Sensitivity::kNotSensitive;
    case Sensitivity::kPersonal:
      return mojom::Sensitivity::kPersonal;
    case Sensitivity::kAccount:
      return mojom::Sensitivity::kAccount;
    case Sensitivity::kPayment:
      return mojom::Sensitivity::kPayment;
    case Sensitivity::kIdentity:
      return mojom::Sensitivity::kIdentity;
    case Sensitivity::kHealth:
      return mojom::Sensitivity::kHealth;
    case Sensitivity::kFinancial:
      return mojom::Sensitivity::kFinancial;
    case Sensitivity::kLegal:
      return mojom::Sensitivity::kLegal;
    case Sensitivity::kPrivateCommunication:
      return mojom::Sensitivity::kPrivateCommunication;
    case Sensitivity::kAdministration:
      return mojom::Sensitivity::kAdministration;
    case Sensitivity::kCredential:
      return mojom::Sensitivity::kCredential;
    case Sensitivity::kUnknownSensitive:
      return mojom::Sensitivity::kUnknownSensitive;
    case Sensitivity::kOneTimeCode:
      return mojom::Sensitivity::kOneTimeCode;
    case Sensitivity::kChallengeResponse:
      return mojom::Sensitivity::kChallengeResponse;
  }
  // Fail closed: an unrecognized classification is the strictest one this
  // enumeration can name that is not a credential, and every consumer treats
  // it as unreadable.
  return mojom::Sensitivity::kUnknownSensitive;
}

mojom::ActionInputKind ToMojomInputKind(ActionInputKind kind) {
  switch (kind) {
    case ActionInputKind::kNone:
      return mojom::ActionInputKind::kNone;
    case ActionInputKind::kText:
      return mojom::ActionInputKind::kText;
    case ActionInputKind::kOption:
      return mojom::ActionInputKind::kOption;
    case ActionInputKind::kToggleState:
      return mojom::ActionInputKind::kToggleState;
  }
  // An input whose kind this build does not recognize carries nothing the
  // renderer will accept, and the translator refuses a kNone input with an
  // operand.
  return mojom::ActionInputKind::kNone;
}

}  // namespace

mojom::ActionInputPtr ToMojom(const ActionInput& input) {
  auto out = mojom::ActionInput::New();
  out->kind = ToMojomInputKind(input.kind);
  out->sensitivity = ToMojomSensitivity(input.sensitivity);
  // value_reference is deliberately not forwarded, and this is the only place
  // in the browser process where the field is in scope beside a message bound
  // for a renderer. The browser resolved the reference and spent it before
  // this ran; a renderer that received the name as well as the bytes could
  // present the name again, and a reference that outlives its use is a
  // capability wearing a different name.
  out->text = input.text;
  out->option_value = input.option_value;
  out->checked = input.checked;
  return out;
}

ActionResultCode ResolutionToResultCode(ValueResolution resolution) {
  switch (resolution) {
    case ValueResolution::kResolved:
      // Not a terminal code. Callers check for kResolved before mapping.
      return ActionResultCode::kInternalError;
    case ValueResolution::kFieldMayNotBeFilled:
      // The field is the reason, and it is the reason at every milestone. A
      // credential field is a sensitive field and reads as one.
      return ActionResultCode::kSensitiveField;
    case ValueResolution::kUnknownReference:
    case ValueResolution::kNotThisTask:
    case ValueResolution::kExpired:
    case ValueResolution::kFieldClassMismatch:
      return ActionResultCode::kValueReferenceUnknown;
  }
  // Fail closed on a value this build does not recognize.
  return ActionResultCode::kDeniedByPolicy;
}

ResolvedActionInput ResolveActionInput(const AuthorizedActionEnvelope& envelope,
                                       const ResolvedNodeFacts& facts,
                                       ValueReferenceVault* vault,
                                       base::TimeTicks now) {
  ResolvedActionInput result;
  if (!envelope.input.has_value()) {
    // Nothing to resolve. The read-oriented operations are dispatched without
    // an input at all, which is the same statement as an input of kind kNone.
    return result;
  }

  const ActionInput& named = *envelope.input;

  // The envelope has to be shaped the way an envelope is allowed to be shaped
  // before anything is looked up. An envelope that already carries bytes has
  // had them written by somebody, and the only author upstream of here is the
  // assistant runtime - which is exactly the authorship this whole path exists
  // to make impossible. It is refused rather than used.
  if (!IsNamedValueInputShape(named)) {
    result.refusal = ActionResultCode::kDeniedByPolicy;
    return result;
  }

  ActionInput resolved;
  resolved.kind = named.kind;
  // The classification the browser re-read, never the one the envelope
  // asserted about the field it is aiming at.
  resolved.sensitivity = facts.sensitivity;

  switch (named.kind) {
    case ActionInputKind::kNone:
      // Nothing is named and nothing is carried. The input is passed through
      // rather than dropped, so that "this operation takes no input" stays a
      // statement the renderer's translator can check rather than an absence.
      break;

    case ActionInputKind::kToggleState:
      // A toggle state is not content authored for the page: there are two
      // possible values and the page already names both, so it is carried the
      // whole way rather than referenced. IsNamedValueInputShape has already
      // established that it is present and that no reference sits beside it.
      resolved.checked = named.checked;
      break;

    case ActionInputKind::kText:
    case ActionInputKind::kOption: {
      if (!vault) {
        // No vault holds anything, so no reference resolves. Dispatching
        // without the value would perform a different action than the one that
        // was authorized, and dispatching with an empty one would type nothing
        // into a field and call it done.
        result.refusal = ActionResultCode::kValueReferenceUnknown;
        return result;
      }
      std::string value;
      const ValueResolution verdict = vault->Resolve(
          *named.value_reference, envelope.task_id, facts.sensitivity, now,
          value);
      if (verdict != ValueResolution::kResolved) {
        result.refusal = ResolutionToResultCode(verdict);
        return result;
      }
      if (named.kind == ActionInputKind::kText) {
        resolved.text = std::move(value);
      } else {
        resolved.option_value = std::move(value);
      }
      break;
    }
  }

  // The last statement before these bytes are handed to a sandboxed process.
  // The shape rule is checked here rather than assumed from the branches
  // above, because "the browser resolved the reference and spent it" is only
  // true of a message that carries the value and names nothing, and this is
  // the one place in the browser that can still say so.
  if (!IsResolvedValueInputShape(resolved)) {
    result.refusal = ActionResultCode::kInternalError;
    return result;
  }
  result.input = std::move(resolved);
  return result;
}

}  // namespace taffy
