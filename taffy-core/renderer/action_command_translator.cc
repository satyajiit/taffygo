// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/action_command_translator.h"

#include <utility>

#include "taffy/renderer/wire_conversions.h"

namespace taffy {

namespace {

// The input kind an operation takes, as a single fact rather than as a
// condition spelled out at each of the places that needs it.
mojom::ActionInputKind InputKindFor(ActionKind action) {
  switch (action) {
    case ActionKind::kScrollIntoView:
    case ActionKind::kFocus:
    case ActionKind::kActivate:
    case ActionKind::kSubmitForm:
      return mojom::ActionInputKind::kNone;
    case ActionKind::kSetText:
      return mojom::ActionInputKind::kText;
    case ActionKind::kSelectOption:
      return mojom::ActionInputKind::kOption;
    case ActionKind::kToggle:
      return mojom::ActionInputKind::kToggleState;
  }
}

// Reads the command's input into the executor's value, or refuses.
//
// Every branch that is not an exact match refuses. There is deliberately no
// path that ignores a field: a field a consumer ignores is a field nobody
// notices is being sent, which is the whole reason the schema refuses the
// same shapes rather than tolerating them.
bool ReadInput(const mojom::RendererActionCommand& command,
               ActionKind action,
               ActionValue& out) {
  const mojom::ActionInputKind expected = InputKindFor(action);
  if (!command.input) {
    // An absent input is the same statement as an input of kind kNone, and
    // the read-oriented operations are sent without one.
    return expected == mojom::ActionInputKind::kNone;
  }
  const mojom::ActionInput& input = *command.input;
  if (input.value_reference) {
    // The browser resolved and spent the reference before it built this
    // command. One that arrives here is either a browser defect or a forgery,
    // and neither is executable.
    return false;
  }
  if (input.kind != expected) {
    return false;
  }

  switch (input.kind) {
    case mojom::ActionInputKind::kNone:
      // Nothing may ride along. A kNone input carrying an operand is a
      // command whose operation and whose payload disagree.
      return !input.text && !input.option_value && !input.checked;
    case mojom::ActionInputKind::kText:
      if (!input.text || input.option_value || input.checked) {
        return false;
      }
      out.text = input.text.value();
      return true;
    case mojom::ActionInputKind::kOption:
      if (!input.option_value || input.text || input.checked) {
        return false;
      }
      out.text = input.option_value.value();
      return true;
    case mojom::ActionInputKind::kToggleState:
      if (!input.checked || input.text || input.option_value) {
        return false;
      }
      out.checked = input.checked.value();
      return true;
  }
}

std::optional<RendererContentTrust> RendererTrustFloor(
    mojom::ContentTrust trust) {
  switch (trust) {
    case mojom::ContentTrust::kFirstPartyDocument:
      return RendererContentTrust::kFirstPartyDocument;
    case mojom::ContentTrust::kUserGeneratedContent:
      return RendererContentTrust::kUserGeneratedContent;
    case mojom::ContentTrust::kThirdPartyEmbedded:
      return RendererContentTrust::kThirdPartyEmbedded;
    case mojom::ContentTrust::kUnknownUntrusted:
      return RendererContentTrust::kUnknownUntrusted;
    case mojom::ContentTrust::kUserAuthored:
    case mojom::ContentTrust::kTaffyAuthored:
    case mojom::ContentTrust::kModelAuthored:
      // Privileged authorship cannot be minted in a renderer. The checker
      // still requires a known page-authored label, but has no matching label
      // to forbid.
      return std::nullopt;
  }
}

}  // namespace

ActionCommandTranslator::Result::Result() = default;
ActionCommandTranslator::Result::Result(const Result&) = default;
ActionCommandTranslator::Result& ActionCommandTranslator::Result::operator=(
    const Result&) = default;
ActionCommandTranslator::Result::~Result() = default;

// static
ActionCommandTranslator::Result ActionCommandTranslator::Translate(
    const mojom::RendererActionCommand& command) {
  Result result;

  const ActionKind action = wire::FromMojom(command.operation);

  RendererActionExecutor::Request request;
  request.action = action;
  if (!ReadInput(command, action, request.value)) {
    result.refusal = mojom::RendererActionOutcome::kUnsupported;
    return result;
  }
  request.precondition.node_id = SemanticNodeId(command.node_id);
  request.precondition.expected_page_epoch = PageEpoch(command.page_epoch);
  request.precondition.minimum_graph_revision =
      GraphRevision(command.required_graph_revision);

  bool declared_role = false;

  for (const mojom::PreconditionPtr& precondition :
       command.renderer_preconditions) {
    switch (precondition->kind) {
      case mojom::PreconditionKind::kNodeRoleUnchanged:
        if (precondition->expected_role.has_value()) {
          request.precondition.expected_role =
              wire::FromMojom(precondition->expected_role.value());
          declared_role = true;
        }
        break;

      case mojom::PreconditionKind::kNodeActionAvailable:
        // The action must still be offered by the node. The executor checks
        // the requested action regardless; a command naming a different one
        // here is contradicting itself, and a self-contradictory command is
        // refused rather than reconciled.
        if (precondition->expected_action_type.has_value() &&
            wire::FromMojom(precondition->expected_action_type.value()) !=
                request.action) {
          result.refusal = mojom::RendererActionOutcome::kUnsupported;
          result.failed_precondition = precondition->kind;
          return result;
        }
        break;

      case mojom::PreconditionKind::kNodeStateAsserted:
        if (precondition->node_state.has_value()) {
          request.precondition.required_states.push_back(
              wire::FromMojom(precondition->node_state.value()));
        }
        break;

      case mojom::PreconditionKind::kNodeStateAbsent:
        if (precondition->node_state.has_value()) {
          request.precondition.forbidden_states.push_back(
              wire::FromMojom(precondition->node_state.value()));
        }
        break;

      case mojom::PreconditionKind::kExpectedDestination:
        if (precondition->expected_destination &&
            precondition->expected_destination->url_metadata->url
                .has_value()) {
          request.precondition.expected_destination =
              precondition->expected_destination->url_metadata->url.value();
        } else {
          // A destination precondition whose URL was minimized away cannot be
          // compared. Refusing is the only honest answer: comparing origins
          // and calling it a destination match is exactly the swap this
          // precondition exists to catch.
          result.refusal = mojom::RendererActionOutcome::kUnsupported;
          result.failed_precondition = precondition->kind;
          return result;
        }
        break;

      case mojom::PreconditionKind::kExactOrigin:
        if (precondition->origin &&
            precondition->origin->kind == mojom::OriginKind::kTuple &&
            precondition->origin->serialization.has_value()) {
          request.precondition.expected_origin_serialization =
              precondition->origin->serialization.value();
        }
        // An opaque origin is deliberately not compared here: every opaque
        // origin serializes alike, and the browser's nonce comparison is the
        // one that means anything.
        break;

      case mojom::PreconditionKind::kNotSensitiveField:
        request.precondition.max_sensitivity =
            precondition->max_sensitivity.has_value()
                ? wire::FromMojom(precondition->max_sensitivity.value())
                : std::optional<Sensitivity>(Sensitivity::kNotSensitive);
        break;

      case mojom::PreconditionKind::kContentTrustAtLeast:
        if (!precondition->min_content_trust) {
          result.refusal = mojom::RendererActionOutcome::kUnsupported;
          result.failed_precondition = precondition->kind;
          return result;
        }
        request.precondition.content_trust_check_declared = true;
        request.precondition.forbidden_content_trust =
            RendererTrustFloor(*precondition->min_content_trust);
        break;

      case mojom::PreconditionKind::kExactPageEpoch:
      case mojom::PreconditionKind::kAcceptableGraphRevision:
      case mojom::PreconditionKind::kNodeExists:
      case mojom::PreconditionKind::kDocumentActive:
        // Checked unconditionally for every action, so naming one adds
        // nothing and skipping one is impossible.
        break;

      case mojom::PreconditionKind::kAllowedRedirectSet:
      case mojom::PreconditionKind::kExpectedValueDigest:
      case mojom::PreconditionKind::kNoUserInteractionSinceLease:
      case mojom::PreconditionKind::kBudgetRemaining:
      case mojom::PreconditionKind::kDestinationClassAllowed:
      case mojom::PreconditionKind::kPreparedEffectUnchanged:
      case mojom::PreconditionKind::kNoUndeclaredEgress:
        // Not evaluable here. Redirect policy and budgets are browser-owned;
        // no adapter reads a value, so no digest exists to compare; and
        // whether the user has interacted since the lease was issued is
        // knowledge a renderer must not be believed about. Destination class,
        // prepared effect and declared egress compare against browser-only
        // state, so the browser never forwards them. Content trust is handled
        // above because repeating that live-node guard can only refuse. A
        // precondition this endpoint cannot check is a refusal, never an
        // assumption that it holds.
        //
        // There is no unknown-kind arm because there is no unknown kind to
        // reach it: PreconditionKind is closed - no [Extensible], no
        // [Default] - so a `kind` outside this list makes Mojo's validator
        // reject the whole message and close the pipe before Translate() is
        // ever called. That is a stricter refusal than anything this switch
        // could write, and the switch stays default-less so a new enumerator
        // still fails the build rather than falling through to "evaluable".
        result.refusal = mojom::RendererActionOutcome::kUnsupported;
        result.failed_precondition = precondition->kind;
        return result;
    }
  }

  // Every node action must say what it expects to be acting on. Scroll is
  // non-consequential, but acting on a replacement node would still violate
  // the exact target the browser admitted.
  if (!declared_role) {
    result.refusal = mojom::RendererActionOutcome::kPreconditionFailed;
    result.failed_precondition = mojom::PreconditionKind::kNodeRoleUnchanged;
    return result;
  }

  result.request = std::move(request);
  return result;
}

}  // namespace taffy
