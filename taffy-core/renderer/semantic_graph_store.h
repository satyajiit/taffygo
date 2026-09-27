// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_SEMANTIC_GRAPH_STORE_H_
#define TAFFY_RENDERER_SEMANTIC_GRAPH_STORE_H_

#include <stdint.h>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "taffy/renderer/semantic_graph.h"

namespace taffy {

// Identity and lifetime for one (FrameId, PageEpoch), per protocol sections 5.3
// and 5.4.
//
// The three properties this class exists to hold - two invariants, and the
// bound that lets a renderer hold them for a page that never stops changing:
//
//   1. NEVER REUSE. A SemanticNodeId is allocated from a counter that only
//      ever increases, and a retired id stays retired for the whole epoch -
//      named in `retired_`, or sitting below the ordinal floor that an
//      identity reset leaves behind. There is no code path that assigns an
//      id to a second node, and there is no code path that resurrects a
//      retired id - Retire() erases the reverse mapping, so the underlying
//      DOM node cannot lead back to it either. A recycled virtualized row
//      keeps its DOM node but gets a new identity generation, which forces a
//      new id (protocol section 8.3).
//
//   2. FAIL CLOSED. Resolve() distinguishes "retired" from "never existed"
//      and from "stale revision", and every one of those is a refusal. There
//      is no "probably the same node" answer, because the only way to make
//      that answer would be to match on selector, text, role, or bounds -
//      exactly what protocol section 12 forbids.
//
//   3. BOUNDED. The bookkeeping the first two invariants need only ever
//      grows, and the document decides how fast: every node a page removes
//      costs an entry that nothing prunes. A page that churns nodes forever -
//      an infinite scroll, a live feed, a chat transcript, an ad rotator - is
//      therefore a page that grows renderer memory for as long as it is open,
//      and a sandboxed renderer does not let a document do that. So the
//      bookkeeping has a ceiling, and reaching it starts identity over
//      wholesale rather than evicting anything. The comment on
//      kMaxTrackedIdentities in the .cc says why eviction would be the one
//      response that breaks invariant 1.
//
// The store holds no Blink object. It is keyed by an opaque DOM identity that
// the adapters hand in, which is what makes the invariants testable on a host
// with no renderer.
class SemanticGraphStore {
 public:
  // Which numbering an identity came from. DOM node ids and accessibility
  // object ids are both small integers from unrelated sequences, so keying on
  // the number alone would silently merge a DOM node with whichever
  // accessibility object happened to share its value. The space is part of
  // the key so that cannot happen.
  enum class IdentitySpace {
    kDom,
    kAccessibility,
    // Nodes with no element of their own: a statement inside a JSON-LD block,
    // for example. Keyed by the adapter's own ordinal. Nothing in this space
    // is ever an action target, because there is no element to act on.
    kDerived,
  };

  // Opaque per-frame node identity plus an identity generation. The
  // generation is what separates "this element changed" from "a different
  // logical thing is now living in this element", which is the
  // virtualized-list case.
  struct DomNodeKey {
    IdentitySpace space = IdentitySpace::kDom;
    int64_t dom_node_id = 0;
    uint32_t identity_generation = 0;

    friend auto operator<=>(const DomNodeKey&, const DomNodeKey&) = default;
    friend bool operator==(const DomNodeKey&, const DomNodeKey&) = default;
  };

  // Spec section 5.3: the change classes that advance a graph revision. The
  // switch over this enum in the .cc has no default case on purpose, so
  // adding a class here forces a decision about how it advances.
  enum class ChangeClass {
    kRouteTransition,
    // A node appeared under a parent this graph already describes. It is not
    // the inserted node's own identity - that is minted by a producing
    // adapter, never by a mutation signal - but the parent's relationships,
    // which a caller may have observed.
    kNodeAdded,
    kNodeRemoved,
    kNodeReparented,
    kNodeReplaced,
    kRoleChanged,
    kAccessibleStateChanged,
    kFormStateChanged,
    kVisibilityChanged,
    kEnabledStateChanged,
    kBoundsChanged,
    kDestinationChanged,
    kVirtualizedRecycled,
    kShadowTreeChanged,
    kChildFrameChanged,
    kAdapterInvalidated,
  };

  enum class ResolveStatus {
    kOk,
    // The id was allocated in this epoch and has been permanently retired.
    kNodeGone,
    // The id was never allocated in this epoch. Treated exactly as harshly as
    // kNodeGone; the distinction is for diagnostics, not for policy.
    kNodeUnknown,
    // The caller's required revision is not satisfied by the current graph.
    kStaleGraph,
    kStalePageEpoch,
  };

  // What the store remembers about a live node. This is the state that the
  // precondition re-check compares against; it is a snapshot of facts, never
  // a pointer into Blink.
  struct LiveNode {
    LiveNode();
    LiveNode(const LiveNode&);
    LiveNode(LiveNode&&);
    LiveNode& operator=(const LiveNode&);
    LiveNode& operator=(LiveNode&&);
    ~LiveNode();

    // Default-constructed, not `{0}`: SemanticNodeId is a StrongAlias over
    // std::string, and its only single-argument constructors take the
    // underlying type, so `{0}` builds a std::string from a null pointer
    // constant - which libc++ deletes (`basic_string(nullptr_t) = delete`).
    // The default gives the empty string, and empty is a safe "no identity
    // yet" sentinel because the allocation counter starts at 1 and every
    // issued id is that counter formatted, so no live node can ever be empty.
    SemanticNodeId node_id;
    DomNodeKey dom_key;
    // Bounded accessible label from the form adapter. Kept only so a fresh
    // ResolveNode can name the exact field to the trusted local surface.
    std::string display_label;
    SemanticRole role = SemanticRole::kUnknownContent;
    std::vector<ActionKind> actions;
    Sensitivity sensitivity = Sensitivity::kUnknownSensitive;
    // Authorship as the producing adapter observed it. Unknown is the
    // fail-closed default, and repeated descriptions are joined so a later
    // adapter can never raise the retained trust of this node.
    RendererContentTrust content_trust =
        RendererContentTrust::kUnknownUntrusted;
    ChallengeKind challenge_kind = ChallengeKind::kNone;
    // Renderer-only identity of the image/widget that justified a challenge
    // hint. It is resolved to fresh viewport geometry on demand and never
    // crosses the process boundary as an identity.
    std::optional<int64_t> challenge_dom_node_id;
    // Exact browser-issued children when this node is a form region. Nullopt
    // means this adapter does not know; an engaged empty vector means the form
    // was inspected and currently has no emitted controls.
    std::optional<std::vector<SemanticNodeId>> form_field_node_ids;
    // The form adapter is the exact owner of a control's role, writable
    // actions, sensitivity, challenge facts and form-state assertions. This
    // bit distinguishes a legitimate fail-safe value such as
    // kUnknownSensitive/kNone/an empty action list from an unrelated adapter
    // that simply had no form facts. Without the distinction, carry-forward
    // can preserve an old fill clearance after the page reclassifies a field.
    bool form_semantics_authoritative = false;
    std::vector<NodeState> states;
    // The destination as observed. Compared at dispatch time; a changed
    // destination is a refusal, not a redirect to follow.
    std::optional<Destination> destination;

    // Viewport-relative bounds as the layout adapter last measured them.
    // Diagnostic, never identity (protocol section 7.3) - but a target that
    // moved between observation and dispatch is a precondition change, so
    // the record has to carry them.
    std::optional<NodeBounds> bounds;

    // True when the layout adapter actually probed for occlusion on this
    // node, whatever the answer was. Renderer-internal on purpose: an action
    // precondition that forbids occlusion is refused when this is false,
    // because "we did not look" is not "not obscured". Whether any occlusion
    // test is reliable enough to gate an action on is
    // `[Open (OD-054)]`; this flag is what keeps the answer honest until it
    // is settled.
    bool occlusion_determined = false;

    // The revision at which this record was last written.
    GraphRevision last_changed{0};

    // The latest exact form-state mutation Blink attributed to this node.
    // This is evidence that a value changed, not a value, length, or digest.
    GraphRevision value_changed_at_revision{0};
  };

  struct ResolveResult {
    ResolveResult();
    ResolveResult(const ResolveResult&);
    ~ResolveResult();

    ResolveStatus status = ResolveStatus::kNodeUnknown;
    GraphRevision current_revision{0};
    // When the node itself changed after the caller's floor: the revision it
    // last changed at. A counter, so the action path can say how far apart
    // the two were without saying anything about the node.
    GraphRevision node_changed_at{0};
    std::optional<LiveNode> node;
  };

  // Holds the coalescing window open in "no coalescing" mode for the duration
  // of an action preflight and dispatch (protocol section 5.3: adapters must
  // not
  // coalesce across an action preflight/dispatch boundary in a way that hides
  // a changed precondition).
  //
  // While a barrier is alive:
  //   * every change class advances the revision immediately;
  //   * DeltaCoalescer flushes rather than batching;
  //   * any advance is recorded, so the dispatcher can see that the page
  //     moved underneath it even if the node's own fields look unchanged.
  class ScopedActionBarrier {
   public:
    explicit ScopedActionBarrier(SemanticGraphStore* store);
    ScopedActionBarrier(const ScopedActionBarrier&) = delete;
    ScopedActionBarrier& operator=(const ScopedActionBarrier&) = delete;
    ~ScopedActionBarrier();

    GraphRevision revision_at_entry() const { return revision_at_entry_; }

    // True when any change class was observed between construction and now.
    // A dispatcher that sees this must refuse and ask for a fresh
    // observation: protocol section 12 forbids retrying the old handle.
    bool graph_moved() const;

   private:
    const raw_ptr<SemanticGraphStore> store_;
    const GraphRevision revision_at_entry_;
  };

  SemanticGraphStore(FrameId frame_id, PageEpoch page_epoch);
  SemanticGraphStore(const SemanticGraphStore&) = delete;
  SemanticGraphStore& operator=(const SemanticGraphStore&) = delete;
  ~SemanticGraphStore();

  FrameId frame_id() const { return frame_id_; }
  PageEpoch page_epoch() const { return page_epoch_; }
  GraphRevision current_revision() const { return current_revision_; }

  // The key an adapter must use for `node_id` right now: the identity space,
  // the source's own node identifier, and the current identity generation for
  // that pair.
  DomNodeKey MakeKey(IdentitySpace space, int64_t node_id) const;

  // Returns the live id for `key`, allocating one if this key has never been
  // seen. A key whose id was retired never resolves again: Retire() removed
  // the mapping, so this allocates a fresh id. That is the virtualized-list
  // rule (protocol section 8.3) falling out of the data structure rather than
  // out
  // of adapter discipline.
  SemanticNodeId AllocateOrLookup(const DomNodeKey& key);

  // Lookup without allocation. Returns the id already issued for `key`, or
  // nothing. The annotating adapters need this and must NOT use
  // AllocateOrLookup(): allocating from an annotation pass would mint an
  // identity for a node no producing adapter described, and a node with an
  // identity and no description is exactly the shape a stale handle has.
  std::optional<SemanticNodeId> Lookup(const DomNodeKey& key) const;

  // Records or updates the facts about a live node. Advances the revision
  // when a fact that an action precondition depends on changed.
  void UpsertLiveNode(LiveNode node);

  // Merges an annotation from a later adapter into a live record. States are
  // appended rather than replaced - two adapters that disagree about a state
  // both get to say so, and the precondition check reads the strictest -
  // and the revision advances when the merge changed something an action
  // precondition can see.
  //
  // Returns false when `id` is unknown or retired, which an annotating
  // adapter reports rather than retries.
  bool AnnotateLiveNode(SemanticNodeId id,
                        const std::vector<NodeState>& states,
                        const std::optional<NodeBounds>& bounds,
                        bool occlusion_determined);

  // Read-only lookup of a live record. Used by the annotating adapters to get
  // back to the DOM or accessibility identity a node was allocated against.
  // Returns null for an unknown or retired id; there is no "probably this
  // one" answer, because producing one would mean matching on selector, text,
  // role, or bounds - exactly what protocol section 12 forbids.
  const LiveNode* FindLive(SemanticNodeId id) const;

  // True only when a currently live, authoritative form description names
  // this renderer DOM node as the presentation target for `kind`. Media
  // inspection uses this as a final renderer-owned refusal, so a challenge
  // image can never be copied into the model attachment store by a later
  // page.images tool. The lookup is bounded by this store's live-node ceiling
  // and runs only for an already-authorized media capture.
  bool IsChallengePresentationTarget(int64_t dom_node_id,
                                     ChallengeKind kind) const;

  // Returns every DOM node whose fresh viewport geometry must be painted over
  // before a whole-viewport capture may leave the browser. The endpoint
  // resolves each identity against live Blink immediately before and after
  // capture; the store's diagnostic bounds are deliberately not trusted.
  // Nullopt refuses a person-facing challenge, an unlocatable prohibited
  // secret, or more targets than the caller can hold.
  std::optional<std::vector<int64_t>> VisualRedactionDomNodeIds(
      size_t maximum_targets) const;

  // Permanent retirement (protocol section 5.4). Idempotent.
  void Retire(SemanticNodeId id);

  // Retires every live id whose source node identifier matches, which is what
  // a subtree removal or a virtualized recycle needs. Returns the retired ids
  // so the delta stream can report them; removal signals are the last thing
  // that may ever be dropped (protocol section 10).
  //
  // Identical to AdvanceIdentityGeneration() and kept as a name of its own
  // because the two callers mean different things by it: one is describing a
  // removal, the other a recycle, and the code that reads better at the call
  // site is the one that gets used correctly.
  std::vector<SemanticNodeId> RetireByDomNode(IdentitySpace space,
                                              int64_t node_id);

  // Spec section 5.3. Returns the revision after the advance.
  GraphRevision NoteChange(ChangeClass change);

  // Advances the graph and, for an exact form-state signal, records the
  // revision on the named live node. Unknown nodes still advance the graph
  // but mint no identity and produce no node-local evidence.
  GraphRevision NoteNodeChange(ChangeClass change, SemanticNodeId node_id);

  // Resolves `id` in `expected_epoch`, refusing unless the graph is at least
  // at `minimum_revision` AND the node itself has not changed since that
  // revision. Both halves are needed: the first says the renderer is not
  // behind the caller, the second says the thing the caller looked at is
  // still the thing it looked at. Passing no revision is only ever
  // acceptable for a read.
  ResolveResult Resolve(SemanticNodeId id,
                        PageEpoch expected_epoch,
                        std::optional<GraphRevision> minimum_revision) const;

  bool IsRetired(SemanticNodeId id) const;
  size_t live_node_count() const { return live_by_id_.size(); }
  size_t retired_node_count() const { return retired_.size(); }
  bool action_barrier_active() const { return barrier_depth_ > 0; }

  // The identity bookkeeping a page can grow, as one number: the source nodes
  // whose identity generation has been advanced, plus the ids still named in
  // `retired_`. This is the quantity MaxTrackedIdentities() bounds, and it is
  // public so a test can assert the bound itself rather than the mechanism
  // that keeps it.
  size_t tracked_identity_count() const {
    return identity_generation_.size() + retired_.size();
  }

  // The ceiling. A function rather than a public constant because the number
  // belongs beside the reasoning for it, which is in the .cc; a test that
  // restated the value would be a second copy to drift.
  static size_t MaxTrackedIdentities();

  // How many times this store has reached that ceiling and begun its identity
  // bookkeeping again. Diagnostic: a page that drives this above zero is a
  // page that churns nodes, and every handle outstanding at each reset was
  // refused rather than answered.
  uint32_t identity_reset_count() const { return identity_reset_count_; }

  // Bumps the identity generation used for subsequent allocations against
  // `node_id`, retiring every id currently issued against it first, and
  // returns those retired ids.
  //
  // Retiring is part of the operation rather than the caller's job on
  // purpose. Advancing the generation MEANS "the logical thing living in this
  // element is different now" (protocol section 8.3), and a caller that
  // advanced without retiring would leave the previous logical row resolvable
  // under its old id - which is precisely the offscreen-continuity assumption
  // the rule forbids. Making it one operation removes the ordering mistake.
  std::vector<SemanticNodeId> AdvanceIdentityGeneration(IdentitySpace space,
                                                        int64_t node_id);
  uint32_t identity_generation_for_testing(IdentitySpace space,
                                           int64_t node_id) const;

 private:
  friend class ScopedActionBarrier;

  // Retires every id currently issued against one source node and returns
  // them. The walk is over the contiguous run those keys occupy in
  // `id_by_dom_key_`, never over the whole map.
  std::vector<SemanticNodeId> RetireSourceNode(IdentitySpace space,
                                               int64_t node_id);

  // Begins identity bookkeeping again when it has reached its ceiling. Called
  // from the two operations that grow it, and a no-op until then.
  void ResetIdentityIfExhausted();

  const FrameId frame_id_;
  const PageEpoch page_epoch_;

  // Only ever incremented, and the sole source of node identity. Never reset,
  // never reused, never derived from a container size or an index. Node ids
  // are strings on the wire, so this counter is formatted into one at
  // allocation - the counter, not the string, is what carries the never-reuse
  // property.
  uint64_t next_node_ordinal_ = 1;
  GraphRevision current_revision_{1};

  std::map<DomNodeKey, SemanticNodeId> id_by_dom_key_;

  // The reverse of `id_by_dom_key_`, and the authority on that mapping.
  // Retirement has to reach the key from the id, and this is what makes that
  // a lookup rather than a walk of every key the epoch ever issued - which,
  // once per removed node on a page that removes nodes forever, was quadratic
  // in the page's own terms. It is the authority rather than a convenience
  // because LiveNode::dom_key is whatever a describing adapter put there,
  // while this is what AllocateOrLookup actually keyed on; retiring against
  // the record's copy would leave the real mapping in place whenever the two
  // disagreed, and a DOM node that still leads back to a retired id is the
  // one thing retirement exists to prevent. It holds an entry per issued id
  // and loses it at retirement, so it is bounded by the live graph rather
  // than by the page's history.
  std::map<SemanticNodeId, DomNodeKey> dom_key_by_id_;

  std::map<SemanticNodeId, LiveNode> live_by_id_;
  std::set<SemanticNodeId> retired_;
  std::map<std::pair<IdentitySpace, int64_t>, uint32_t> identity_generation_;

  // Every id whose ordinal is below this is retired, whether or not
  // `retired_` still names it. An identity reset raises it to the allocation
  // counter and empties the set, which is how retirement outlives the entries
  // that recorded it: the guarantee costs one integer instead of one string
  // per node the page ever removed.
  uint64_t retired_floor_ordinal_ = 1;
  uint32_t identity_reset_count_ = 0;

  int barrier_depth_ = 0;
  GraphRevision revision_at_barrier_entry_{0};
};

}  // namespace taffy

#endif  // TAFFY_RENDERER_SEMANTIC_GRAPH_STORE_H_
