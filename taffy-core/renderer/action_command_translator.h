// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_ACTION_COMMAND_TRANSLATOR_H_
#define TAFFY_RENDERER_ACTION_COMMAND_TRANSLATOR_H_

#include <optional>

#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"
#include "taffy/renderer/renderer_action_executor.h"

namespace taffy {

// Turns a browser-issued RendererActionCommand into the executor's request,
// or into a refusal.
//
// It is a module of its own because it is the single point at which an
// attacker-shaped message becomes an internal instruction, and because every
// branch in it fails closed. Reviewing that property is much easier when the
// file contains nothing else.
//
// The rules it encodes:
//
//   * A precondition this endpoint cannot evaluate is a REFUSAL, never an
//     assumption that it holds. Redirect policy and budgets are browser-
//     owned; no adapter reads a value, so no digest exists to compare;
//     whether the user has interacted since the lease was issued is knowledge
//     a renderer must not be believed about. Content trust is the exception:
//     after the browser checks it, this endpoint repeats the same live-node
//     guard so a mutation in the final resolve-to-execute window can only
//     subtract authority.
//
//   * A command's input must be the one its operation takes, and must be the
//     shape the browser is allowed to have sent. Three refusals live here and
//     each is a different attack:
//
//       - an input carrying a value_reference is refused outright. The
//         browser resolves a reference and spends it before it builds a
//         command, so a reference arriving in a sandboxed process is a name
//         that outlived its use, and a reference that outlives its use is a
//         capability wearing a different name (protocol 0.8, ActionInput).
//       - an input whose kind does not match the operation is refused rather
//         than reconciled. A command that says SET_TEXT and carries a toggle
//         state is contradicting itself, and a self-contradictory command is
//         not a command to execute the agreeable half of.
//       - an operation that needs a value and was sent none is refused. Only
//         the browser builds these, so a missing value is a browser defect,
//         and the honest answer to a defect is a refusal rather than a
//         smaller version of what was asked for.
//
//   * A consequential action must declare what it believes it is acting on. A
//     command with no role precondition is refused rather than executed
//     against whatever is there now.
class ActionCommandTranslator {
 public:
  struct Result {
    Result();
    Result(const Result&);
    Result& operator=(const Result&);
    ~Result();

    // Present only when the command is executable as stated.
    std::optional<RendererActionExecutor::Request> request;
    // Set when the command is refused. Carries the outcome to report and,
    // when the refusal is about one precondition, which one.
    mojom::RendererActionOutcome refusal =
        mojom::RendererActionOutcome::kUnsupported;
    // Absent when the refusal names no single precondition - a malformed
    // input, say, which fails before any precondition is looked at. Optional
    // rather than a kUnknown enumerator because PreconditionKind cannot have
    // one: every enum in page_intelligence.mojom is closed, with no
    // [Extensible] and no [Default], precisely so that Mojo's own validator
    // rejects an out-of-range value rather than coercing it to a member. This
    // mirrors the wire field, which is `PreconditionKind? failed_precondition`,
    // and matches how the same datum is already spelled in public/bip_action.h
    // and browser/action_dispatcher.h.
    std::optional<mojom::PreconditionKind> failed_precondition;
  };

  static Result Translate(const mojom::RendererActionCommand& command);
};

}  // namespace taffy

#endif  // TAFFY_RENDERER_ACTION_COMMAND_TRANSLATOR_H_
