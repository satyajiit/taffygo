// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_RENDERER_ACTION_EXECUTOR_H_
#define TAFFY_RENDERER_RENDERER_ACTION_EXECUTOR_H_

#include <optional>
#include <string>

#include "taffy/renderer/action_accessibility_mapping.h"
#include "taffy/renderer/node_precondition_checker.h"
#include "taffy/renderer/semantic_graph.h"
#include "taffy/renderer/semantic_graph_store.h"

namespace blink {
class WebLocalFrame;
}  // namespace blink

namespace taffy {

// The renderer-local part of an already authorized operation.
//
// Spec section 11.5 opens with "prefer Chromium's normal input/event path
// over direct DOM mutation", and decision 0059 turns that preference into the
// only path there is. Every operation goes through Blink's accessibility
// action path - the same path assistive technology uses - and none of them
// touches the DOM. The mapping from operation to accessibility action lives
// in action_accessibility_mapping.h, alone, so that the claim is a table a
// host test enumerates rather than a habit spread across a switch:
//
//   * kScrollIntoView and kFocus move the viewport and the focus ring. Both
//     are ordinary, non-mutating, and already exposed to TalkBack.
//
//   * kActivate performs the element's default action. This dispatches
//     through Blink's event pipeline, so a site's own handlers see the events
//     they expect, and it does not synthesize or extend user activation to
//     get past a web platform requirement - which protocol section 11.5
//     forbids separately. Setting properties on the element directly was
//     rejected: it produces state changes without the events sites depend on.
//
//     The alternative considered was returning a hit point and letting the
//     browser process dispatch synthetic input. It is stricter - the renderer
//     would hold no primitive that can activate anything - and Outcome below
//     keeps a value for it so the switch is already written. It was not
//     chosen because the wire contract in //taffy/contracts/bip/mojom has the
//     renderer report kDispatched for an activation it performed, and because
//     browser-dispatched input needs a hit test that a compromised renderer
//     could aim. If SP-04 finds that the default-action path extends user
//     activation or bypasses a platform requirement, activation moves to the
//     browser and every mutating operation moves with it.
//
//   * kSubmitForm is kActivate on the control the page offered. It is not a
//     separate primitive and it is never a programmatic form submission: the
//     target of a submission in this contract is a button, so the page's own
//     validation and submit handler run and the act is one the page put there
//     to be performed.
//
//   * kSetText and kSelectOption set a value the BROWSER resolved. This class
//     never sees the reference the proposal named, so nothing here can be
//     made to type something a person did not supply.
//
//   * kToggle flips a control, so the state it must start in is required as
//     a precondition: a request to end up checked is performable only from a
//     control that asserts unchecked. A plan that is wrong about the page
//     fails rather than converging on what it wanted, and a control whose
//     state nobody could read is refused rather than flipped on a guess.
//
// Nothing here is authorization. The browser's lease, capability, canonical
// intent, live classification and durable-intent gates decide whether a write
// may be asked for at all. The narrowest renderer gate is still the exact
// node's advertised actions[]: only an eligible form control offers one of
// these writes, and the precondition checker re-reads it immediately before
// this executor reaches Blink.
class RendererActionExecutor {
 public:
  enum class Outcome {
    // The renderer-local operation completed. Not success of the action: spec
    // section 11.6 is explicit that renderer acknowledgement is DISPATCHED,
    // never VERIFIED. Only the browser process, watching its own lifecycle
    // and postcondition observers, may call something verified.
    kPerformed,
    // The preflight passed and the browser must dispatch real input. Carries
    // the point to dispatch at.
    kRequiresBrowserInputDispatch,
    // The request does not describe an operation this class can perform - an
    // operation handed no value, most of all. It is a refusal, never a
    // smaller version of what was asked.
    kUnsupported,
    // A precondition failed; `precondition` says which.
    kPreconditionFailed,
    // Blink refused or the element vanished between the check and the call.
    kDispatchFailed,
  };

  // The endpoint translates the command's tagged preconditions into
  // `precondition` and hands it over intact. This class adds nothing to it:
  // an executor that could relax a precondition on the way past would make
  // the checker's guarantees advisory.
  struct Request {
    Request();
    Request(const Request&);
    ~Request();

    ActionKind action = ActionKind::kScrollIntoView;
    // What the operation writes, as the browser resolved it. Empty for every
    // read-oriented operation and for a submission.
    ActionValue value;
    PreconditionRequest precondition;
  };

  struct Result {
    Outcome outcome = Outcome::kUnsupported;
    PreconditionCode precondition = PreconditionCode::kUnsupported;
    GraphRevision revision_at_dispatch{0};
    // Viewport-relative point for the browser to dispatch input at, set only
    // with kRequiresBrowserInputDispatch. Diagnostic elsewhere; never
    // identity.
    std::optional<NodeBounds> dispatch_bounds;
    // The node as re-read at dispatch time: the verifier's baseline.
    std::optional<SemanticGraphStore::LiveNode> observed_state;
  };

  RendererActionExecutor();
  RendererActionExecutor(const RendererActionExecutor&) = delete;
  RendererActionExecutor& operator=(const RendererActionExecutor&) = delete;
  ~RendererActionExecutor();

  // Holds an action barrier for the whole preflight and dispatch, so no
  // adapter can coalesce a change away underneath it.
  Result Execute(SemanticGraphStore& store,
                 blink::WebLocalFrame* frame,
                 const Request& request);

 private:
  const NodePreconditionChecker checker_;
};

}  // namespace taffy

#endif  // TAFFY_RENDERER_RENDERER_ACTION_EXECUTOR_H_
