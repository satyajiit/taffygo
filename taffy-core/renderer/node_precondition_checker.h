// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_NODE_PRECONDITION_CHECKER_H_
#define TAFFY_RENDERER_NODE_PRECONDITION_CHECKER_H_

#include <optional>
#include <string>
#include <vector>

#include "taffy/renderer/semantic_graph.h"
#include "taffy/renderer/semantic_graph_store.h"

namespace taffy {

// The renderer-local half of the stale-node algorithm, protocol section 12
// steps 5 and 6.
//
// Steps 1 to 4 - profile, tab, frame, document activity, origin, actor lease,
// capability - happen in the browser process and cannot happen here. A
// compromised renderer would answer all four with whatever the attacker
// wanted. So this class does not attempt them, does not accept an assertion
// about them, and its output is never sufficient to authorize anything. The
// browser process re-checks everything it can observe itself, and the
// renderer's answer can only ever *subtract* from what the browser allows.
//
// Every path through Check() that is not a positive confirmation returns a
// refusal. There is no "unknown" that means "proceed": a check that cannot be
// performed returns kUnsupported, and kUnsupported is a refusal.

// The subset of the protocol section 11.7 taxonomy that a renderer is
// entitled to produce. The browser-only codes - policy denial, approval,
// actor leases, capability expiry, tab lifetime, postcondition outcomes -
// are deliberately
// absent: a renderer that could name them could also claim them, and a
// renderer must not be able to say "approved" in any vocabulary.
enum class PreconditionCode {
  kOk,
  kStalePageEpoch,
  kStaleGraph,
  kNodeGone,
  kRoleOrActionChanged,
  kNotVisible,
  kOccluded,
  kNotEnabled,
  kNotEditable,
  kSensitiveField,
  kDestinationChanged,
  kOriginChanged,
  kUnsupported,
  // The graph advanced between the start of the preflight and this check.
  // Distinct from kStaleGraph so the diagnostics can tell "the caller was
  // behind" apart from "the page moved while we were checking", which are
  // different bugs on a page and different fixes for us.
  kGraphMovedDuringPreflight,
};

// What the caller asks us to confirm. Every field is an expectation recorded
// at observation time by the browser broker; none of it is derived here.
struct PreconditionRequest {
  PreconditionRequest();
  PreconditionRequest(const PreconditionRequest&);
  PreconditionRequest(PreconditionRequest&&);
  PreconditionRequest& operator=(const PreconditionRequest&);
  PreconditionRequest& operator=(PreconditionRequest&&);
  ~PreconditionRequest();

  SemanticNodeId node_id;
  PageEpoch expected_page_epoch;

  // Absent means "any current revision", which is only ever acceptable for a
  // read. Every action path fills this in.
  std::optional<GraphRevision> minimum_graph_revision;

  // Absent means the caller declared no role expectation. Only a read-only
  // viewport move may leave it absent; the endpoint enforces that.
  std::optional<SemanticRole> expected_role;

  // The action the node must still support. Always set for an action.
  ActionKind requested_action = ActionKind::kScrollIntoView;

  // States the node must assert, and states it must not. Both lists exist
  // because both mojom::NodeState members exist: an unasserted state is
  // "could not tell", and a requirement is not satisfied by silence.
  //
  // Note what an absent-state assertion is NOT. `kObscured` absent means "we
  // recorded no obscuring", not "the target is visible to the user". A
  // caller that forbids kObscured therefore also gets a refusal when NOTHING
  // determined occlusion at all, which is what LiveNode::occlusion_determined
  // records. Which occlusion test is reliable enough to gate an action on
  // Android is `[Open (OD-054)]`, and the browser must not treat this
  // passing as proof either way.
  std::vector<NodeState> required_states;
  std::vector<NodeState> forbidden_states;

  // The destination the caller believes this node leads to, already reduced
  // to the disclosure level the browser chose to state. Compared literally: a
  // page that swapped an href after the plan was made is the single most
  // valuable attack against an assistant that follows links, and "close
  // enough" has no meaning here.
  std::optional<std::string> expected_destination;

  // The strictest sensitivity the caller will act on. Absent means the
  // default, which is that only a demonstrably not-sensitive node may be
  // acted on at all.
  std::optional<Sensitivity> max_sensitivity;

  // A browser-checked content-authorship guard repeated here only to close the
  // resolve-to-execute race. Unknown authorship always refuses. A privileged
  // browser-only floor has no renderer representation, but still requires the
  // live node to carry a known page-authored label.
  bool content_trust_check_declared = false;
  std::optional<RendererContentTrust> forbidden_content_trust;

  // Where the caller believes the target is. Compared, never resolved from:
  // protocol section 12 forbids finding a node by nearest coordinates, and
  // this asks the opposite question - whether the node the id already
  // resolved to is still where it was when the plan was made. A target that
  // moved is a changed precondition even when nothing else about it changed.
  std::optional<NodeBounds> expected_bounds;

  // The origin the caller believes this document has, serialized. A cheap
  // early rejection; the comparison that matters is the browser's, against
  // its own committed record.
  std::optional<std::string> expected_origin_serialization;
};

// The document facts the check needs, read out of Blink by the caller.
//
// Taking facts rather than a frame is what makes this class host-testable,
// and the properties it enforces - fail closed on every path, refuse a check
// it cannot perform - are exactly the ones worth proving on every host rather
// than only on the Chromium builder.
// Where a node is now, as the executor measured it just before the check.
struct MeasuredVisibility {
  MeasuredVisibility();
  MeasuredVisibility(const MeasuredVisibility&);
  MeasuredVisibility& operator=(const MeasuredVisibility&);
  ~MeasuredVisibility();

  // The visibility states the measurement asserts (IsVisibilityState).
  std::vector<NodeState> states;
  // Whether an occlusion hit test decided them.
  bool occlusion_determined = false;
};

// Whether `state` is one a visibility determination owns, and so one a
// measurement of where a node is now replaces.
bool IsVisibilityState(NodeState state);

struct DocumentFacts {
  DocumentFacts();
  DocumentFacts(const DocumentFacts&);
  DocumentFacts& operator=(const DocumentFacts&);
  ~DocumentFacts();

  // False when there is no document at all, which is a refusal.
  bool has_document = false;
  // Every opaque origin serializes to the same token, so an opaque document
  // must never satisfy an origin precondition by string equality. The browser
  // compares nonces and that comparison means something; this one is a cheap
  // early rejection.
  bool origin_is_opaque = true;
  std::string origin_serialization;
  // The target's visibility now, when the executor could measure it. A
  // reading records where a node was, and no reading follows a scroll, so a
  // field read below the fold went on being refused as not visible after it
  // was scrolled into plain sight (decision 0250). When present it replaces
  // the reading's visibility states; when absent the reading's stand.
  std::optional<MeasuredVisibility> visibility_now;
};

struct PreconditionResult {
  PreconditionResult();
  PreconditionResult(const PreconditionResult&);
  ~PreconditionResult();

  PreconditionCode code = PreconditionCode::kUnsupported;
  GraphRevision observed_revision{0};
  // Present only when code is kOk.
  std::optional<SemanticGraphStore::LiveNode> node;
};

class NodePreconditionChecker {
 public:
  NodePreconditionChecker();
  NodePreconditionChecker(const NodePreconditionChecker&) = delete;
  NodePreconditionChecker& operator=(const NodePreconditionChecker&) = delete;
  ~NodePreconditionChecker();

  // Re-resolves the node at the required revision and re-checks role,
  // actions, sensitivity, visibility, enabled and editable state, and
  // destination. `barrier` must be held by the caller for the whole preflight
  // and the dispatch that follows it.
  PreconditionResult Check(const SemanticGraphStore& store,
                           const SemanticGraphStore::ScopedActionBarrier&
                               barrier,
                           const DocumentFacts& document,
                           const PreconditionRequest& request) const;
};

}  // namespace taffy

#endif  // TAFFY_RENDERER_NODE_PRECONDITION_CHECKER_H_
