// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_ACTION_VALUE_RESOLUTION_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_ACTION_VALUE_RESOLUTION_H_

#include <optional>

#include "base/time/time.h"
#include "taffy/components/security/browser/value_reference_vault.h"
#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"
#include "taffy/common/public/bip_action.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/common/public/bip_result.h"

// The single point at which a named value becomes bytes.
//
// It is a module of its own for the same reason
// //taffy/renderer/action_command_translator.h is: it is where one kind of
// thing turns into another kind of thing, every branch in it fails closed, and
// reviewing that is much easier when the file contains nothing else.
//
// What crosses it, in one direction only. An authorized envelope may **name**
// a value and may not carry one. The renderer command the browser builds from
// that envelope may **carry** the resolved bytes and may not name anything.
// This function is the step between the two, and it is the only step: the
// reference is spent here, in the browser process, and what the renderer
// receives has no reference on it (protocol 0.8 ActionInput; decision 0059
// section 4).
//
// The bytes never travel back. There is no field on a renderer reply, a
// postcondition outcome, an action result or a journal record that a resolved
// value could be written into, and this function returns the value to exactly
// one caller: the code that fills in the one-use command about to be sent. It
// does not log it, digest it, measure it, or hand it to the verifier - the
// verifier corroborates a NODE_VALUE_CHANGED postcondition from the page's own
// state, and asking it to compare against the value would put the value back
// on the path this whole design takes it off.

namespace taffy {

// What resolution produced. Exactly one of the two members is set.
struct ResolvedActionInput {
  ResolvedActionInput();
  ResolvedActionInput(const ResolvedActionInput&) = delete;
  ResolvedActionInput& operator=(const ResolvedActionInput&) = delete;
  ResolvedActionInput(ResolvedActionInput&&);
  ResolvedActionInput& operator=(ResolvedActionInput&&);
  ~ResolvedActionInput();

  // The input to put on the renderer command. Absent both when the envelope
  // had no input to resolve and when resolution refused; `refusal` tells the
  // two apart.
  std::optional<ActionInput> input;

  // Set when the dispatch must stop. The action has been journalled by the
  // time this runs, so a refusal here is a terminal result rather than a
  // silent downgrade to an action with no value.
  std::optional<ActionResultCode> refusal;
};

// Resolves the envelope's named value against the browser's vault.
//
// `facts` is the browser's own re-read of the target node at dispatch time.
// The field classification used to decide whether this field may be written
// comes from there and from nowhere else: a proposal's own claim about the
// field it is aiming at is the claim of the least trusted author in the
// system.
//
// `vault` may be null, and a null vault holds nothing - an envelope naming a
// value is then refused rather than dispatched without one.
ResolvedActionInput ResolveActionInput(const AuthorizedActionEnvelope& envelope,
                                       const ResolvedNodeFacts& facts,
                                       ValueReferenceVault* vault,
                                       base::TimeTicks now);

// The terminal code one resolution verdict is reported under.
//
// A permanently non-fillable field reports kSensitiveField ahead of every
// temporary reference problem. Every invalid, spent, expired, wrong-task or
// wrong-class reference reports kValueReferenceUnknown, whose matching
// persisted Core Service member was added in the same 2.65 change.
ActionResultCode ResolutionToResultCode(ValueResolution resolution);

// The wire form of an input the browser is about to put on a one-use command.
//
// It lives beside the resolution rather than with the other conversions
// because it is the same job: this is where the resolved bytes are written
// into the only message that may carry them, and `value_reference` is left
// unset here in the one function that could have set it.
mojom::ActionInputPtr ToMojom(const ActionInput& input);

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_ACTION_VALUE_RESOLUTION_H_
