// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/semantic_graph_store.h"

#include <optional>
#include <string_view>

#include "base/check.h"
#include "base/check_op.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"

namespace taffy {

namespace {

// Bounds the identity bookkeeping one store keeps for one page epoch.
//
// Two of the store's registers only ever grow, and the document decides how
// fast: `identity_generation_` gains an entry the first time a source node is
// removed or recycled, and `retired_` gains one for every id ever retired. A
// page that removes nodes forever grows both forever, and this is a sandboxed
// renderer - the page is the adversary, so anything it can grow needs a cap.
// //taffy/components/intelligence/content/origin_codec.cc bounds its opaque
// table for the same reason; the difference here is what happens at the
// ceiling, and ResetIdentityIfExhausted() below says why.
//
// The number is several complete turnovers of the largest graph one
// extraction is allowed to describe. (//taffy/renderer/observation_limits.json
// owns that ceiling and forbids any other file in this component from
// restating it, so this comment names the relationship and not the value.) A
// document that behaves never comes near it. A feed that replaces its whole
// content over and over reaches it after a long session and pays one
// re-observation. Lower, and the reset would be a behaviour of ordinary
// browsing rather than a backstop against a hostile one; higher, and the
// registers are megabytes of small strings held in a renderer for a page that
// is still open.
constexpr size_t kMaxTrackedIdentities = 16384;

// The one prefix every issued id carries. AllocateOrLookup() formats it and
// OrdinalOf() reads it back; they are the only two places that know the shape,
// and they are adjacent on purpose.
constexpr std::string_view kNodeIdPrefix = "n";

// An issued id back to the counter value it was formatted from, or nothing
// when the string did not come out of AllocateOrLookup(). The ordinal, not the
// string, is what carries the never-reuse property, so the retirement floor
// has to be able to read it back. An id this store never issued does not
// parse, and the caller treats that as "not retired" - which is correct, and
// harmless, because such an id is not live either and the lookup that follows
// refuses it as unknown.
std::optional<uint64_t> OrdinalOf(const SemanticNodeId& id) {
  const std::string_view text = id.value();
  uint64_t ordinal = 0;
  if (!text.starts_with(kNodeIdPrefix) ||
      !base::StringToUint64(text.substr(kNodeIdPrefix.size()), &ordinal)) {
    return std::nullopt;
  }
  return ordinal;
}

// Whether a change class can invalidate a precondition that an in-flight
// action already checked. Every class currently does; the function exists so
// that adding a cheap, action-irrelevant class later is a visible, reviewed
// change rather than a silent one.
bool AdvancesRevision(SemanticGraphStore::ChangeClass change) {
  switch (change) {
    case SemanticGraphStore::ChangeClass::kRouteTransition:
    case SemanticGraphStore::ChangeClass::kNodeAdded:
    case SemanticGraphStore::ChangeClass::kNodeRemoved:
    case SemanticGraphStore::ChangeClass::kNodeReparented:
    case SemanticGraphStore::ChangeClass::kNodeReplaced:
    case SemanticGraphStore::ChangeClass::kRoleChanged:
    case SemanticGraphStore::ChangeClass::kAccessibleStateChanged:
    case SemanticGraphStore::ChangeClass::kFormStateChanged:
    case SemanticGraphStore::ChangeClass::kVisibilityChanged:
    case SemanticGraphStore::ChangeClass::kEnabledStateChanged:
    case SemanticGraphStore::ChangeClass::kBoundsChanged:
    case SemanticGraphStore::ChangeClass::kDestinationChanged:
    case SemanticGraphStore::ChangeClass::kVirtualizedRecycled:
    case SemanticGraphStore::ChangeClass::kShadowTreeChanged:
    case SemanticGraphStore::ChangeClass::kChildFrameChanged:
    case SemanticGraphStore::ChangeClass::kAdapterInvalidated:
      return true;
  }
}

}  // namespace

SemanticGraphStore::LiveNode::LiveNode() = default;
SemanticGraphStore::LiveNode::LiveNode(const LiveNode&) = default;
SemanticGraphStore::LiveNode::LiveNode(LiveNode&&) = default;
SemanticGraphStore::LiveNode& SemanticGraphStore::LiveNode::operator=(
    const LiveNode&) = default;
SemanticGraphStore::LiveNode& SemanticGraphStore::LiveNode::operator=(
    LiveNode&&) = default;
SemanticGraphStore::LiveNode::~LiveNode() = default;

SemanticGraphStore::ResolveResult::ResolveResult() = default;
SemanticGraphStore::ResolveResult::ResolveResult(const ResolveResult&) =
    default;
SemanticGraphStore::ResolveResult::~ResolveResult() = default;

SemanticGraphStore::ScopedActionBarrier::ScopedActionBarrier(
    SemanticGraphStore* store)
    : store_(store), revision_at_entry_(store->current_revision_) {
  if (store_->barrier_depth_++ == 0) {
    store_->revision_at_barrier_entry_ = store_->current_revision_;
  }
}

SemanticGraphStore::ScopedActionBarrier::~ScopedActionBarrier() {
  CHECK_GT(store_->barrier_depth_, 0);
  --store_->barrier_depth_;
}

bool SemanticGraphStore::ScopedActionBarrier::graph_moved() const {
  return store_->current_revision_ != revision_at_entry_;
}

SemanticGraphStore::SemanticGraphStore(FrameId frame_id, PageEpoch page_epoch)
    : frame_id_(frame_id), page_epoch_(page_epoch) {}

SemanticGraphStore::~SemanticGraphStore() = default;

SemanticGraphStore::DomNodeKey SemanticGraphStore::MakeKey(
    IdentitySpace space,
    int64_t node_id) const {
  DomNodeKey key;
  key.space = space;
  key.dom_node_id = node_id;
  auto it = identity_generation_.find({space, node_id});
  key.identity_generation = it == identity_generation_.end() ? 0u : it->second;
  return key;
}

SemanticNodeId SemanticGraphStore::AllocateOrLookup(const DomNodeKey& key) {
  auto it = id_by_dom_key_.find(key);
  if (it != id_by_dom_key_.end()) {
    // A retired id is never reachable from a DOM key: Retire() removes the
    // mapping. If this ever fires, the never-reuse invariant is broken and
    // the process must not continue serving observations.
    CHECK(!IsRetired(it->second));
    return it->second;
  }

  // Monotonic. There is no branch here that can produce a previously issued
  // value, including on wraparound: a 64-bit counter incremented once per
  // observed node cannot wrap inside a page epoch, and the CHECK states that
  // rather than assuming it.
  CHECK_LT(next_node_ordinal_, UINT64_MAX);
  const SemanticNodeId id{base::StrCat(
      {kNodeIdPrefix, base::NumberToString(next_node_ordinal_++)})};
  CHECK(!IsRetired(id));
  id_by_dom_key_.emplace(key, id);
  dom_key_by_id_.emplace(id, key);
  return id;
}

std::optional<SemanticNodeId> SemanticGraphStore::Lookup(
    const DomNodeKey& key) const {
  auto it = id_by_dom_key_.find(key);
  if (it == id_by_dom_key_.end()) {
    return std::nullopt;
  }
  // A retired id is unreachable from a key by construction - Retire() erases
  // the mapping - and the check states that rather than assuming it.
  return IsRetired(it->second) ? std::nullopt
                               : std::optional<SemanticNodeId>(it->second);
}

void SemanticGraphStore::Retire(SemanticNodeId id) {
  if (IsRetired(id)) {
    return;
  }

  // Drop the reverse mapping first. After this line the DOM node that used to
  // carry this id cannot lead back to it by any code path. The key comes from
  // `dom_key_by_id_` rather than from the live record, which covers the two
  // cases that used to be written separately: an id that was described, and
  // an id that was allocated and never described - the second of which cost a
  // walk of every key in the epoch to find one entry.
  auto key_entry = dom_key_by_id_.find(id);
  if (key_entry != dom_key_by_id_.end()) {
    id_by_dom_key_.erase(key_entry->second);
    dom_key_by_id_.erase(key_entry);
  }
  live_by_id_.erase(id);

  retired_.insert(id);
  NoteChange(ChangeClass::kNodeRemoved);
  ResetIdentityIfExhausted();
}

std::vector<SemanticNodeId> SemanticGraphStore::RetireSourceNode(
    IdentitySpace space,
    int64_t node_id) {
  // `id_by_dom_key_` is ordered by DomNodeKey's defaulted comparison, which
  // compares members in declaration order, so every key ever issued for one
  // source node - whatever generations it has been through - occupies one
  // contiguous run. Finding that run is a lower_bound; finding it by reading
  // the whole map, which is what this did before, made one removal cost the
  // size of the page and n removals cost the square of it.
  //
  // The three assertions state the property the walk rests on rather than
  // leaving it to be rediscovered. Reordering the members of DomNodeKey would
  // silently narrow the run - the loop below would stop at the first key that
  // no longer matched and leave the rest live - and a removal that retired
  // only part of a source node's ids leaves a replaced row resolvable under
  // the id of its predecessor.
  static_assert(DomNodeKey{IdentitySpace::kDom, 7, 0} <
                    DomNodeKey{IdentitySpace::kDom, 7, 1},
                "keys for one source node must order by identity generation");
  static_assert(DomNodeKey{IdentitySpace::kDom, 7, UINT32_MAX} <
                    DomNodeKey{IdentitySpace::kDom, 8, 0},
                "every key for one source node must precede the next node's");
  static_assert(DomNodeKey{IdentitySpace::kDom, INT64_MAX, UINT32_MAX} <
                    DomNodeKey{IdentitySpace::kAccessibility, 0, 0},
                "the identity space must order ahead of the source node id");

  std::vector<SemanticNodeId> ids;
  for (auto it = id_by_dom_key_.lower_bound(
           DomNodeKey{.space = space, .dom_node_id = node_id});
       it != id_by_dom_key_.end() && it->first.space == space &&
       it->first.dom_node_id == node_id;
       ++it) {
    ids.push_back(it->second);
  }
  // Collected before retiring rather than retired in the walk, because Retire()
  // erases from the map being walked - and because it may reach the ceiling
  // partway through, after which every remaining id is already below the
  // retirement floor and Retire() answers that it has nothing to do.
  for (const SemanticNodeId& id : ids) {
    Retire(id);
  }
  return ids;
}

std::vector<SemanticNodeId> SemanticGraphStore::RetireByDomNode(
    IdentitySpace space,
    int64_t node_id) {
  return AdvanceIdentityGeneration(space, node_id);
}

GraphRevision SemanticGraphStore::NoteChange(ChangeClass change) {
  if (!AdvancesRevision(change)) {
    return current_revision_;
  }
  current_revision_ = GraphRevision(current_revision_.value() + 1);
  return current_revision_;
}

GraphRevision SemanticGraphStore::NoteNodeChange(ChangeClass change,
                                                 SemanticNodeId node_id) {
  const GraphRevision revision = NoteChange(change);
  if (change != ChangeClass::kFormStateChanged || node_id.value().empty() ||
      IsRetired(node_id)) {
    return revision;
  }
  auto it = live_by_id_.find(node_id);
  if (it != live_by_id_.end()) {
    it->second.value_changed_at_revision = revision;
  }
  return revision;
}

SemanticGraphStore::ResolveResult SemanticGraphStore::Resolve(
    SemanticNodeId id,
    PageEpoch expected_epoch,
    std::optional<GraphRevision> minimum_revision) const {
  ResolveResult result;
  result.current_revision = current_revision_;

  // Epoch first. A handle from another document is not a stale node; it is a
  // handle for a page that no longer exists, and saying so precisely is what
  // stops a caller from "refreshing" its way into acting on the wrong
  // document.
  if (expected_epoch != page_epoch_) {
    result.status = ResolveStatus::kStalePageEpoch;
    return result;
  }

  if (IsRetired(id)) {
    result.status = ResolveStatus::kNodeGone;
    return result;
  }

  auto it = live_by_id_.find(id);
  if (it == live_by_id_.end()) {
    result.status = ResolveStatus::kNodeUnknown;
    return result;
  }

  // Freshness, in two parts.
  if (minimum_revision.has_value()) {
    // The renderer is behind the revision the browser demanded. This should
    // not happen - the browser learns of revisions from here - so it means
    // something is confused, and confused fails closed.
    if (current_revision_ < minimum_revision.value()) {
      result.status = ResolveStatus::kStaleGraph;
      return result;
    }
    // The node changed since the caller observed it. The graph moving is
    // fine; this node moving is not, because the caller's decision was about
    // this node as it was.
    if (it->second.last_changed > minimum_revision.value()) {
      result.status = ResolveStatus::kStaleGraph;
      result.node_changed_at = it->second.last_changed;
      return result;
    }
  }

  result.status = ResolveStatus::kOk;
  result.node = it->second;
  return result;
}

bool SemanticGraphStore::IsRetired(SemanticNodeId id) const {
  if (retired_.contains(id)) {
    return true;
  }
  // Free until the store has actually reset. Before the first reset the floor
  // is the first ordinal ever issued, nothing can be below it, and no id is
  // parsed at all - so a page that never exhausts the store pays nothing for
  // the mechanism that catches one that does.
  if (retired_floor_ordinal_ <= 1) {
    return false;
  }
  // Everything issued before the reset is gone and stays gone, with no set
  // entry left to prove it.
  const std::optional<uint64_t> ordinal = OrdinalOf(id);
  return ordinal.has_value() && ordinal.value() < retired_floor_ordinal_;
}

// static
size_t SemanticGraphStore::MaxTrackedIdentities() {
  return kMaxTrackedIdentities;
}

// The answer to exhaustion is to forget every identity at once, not to evict
// the oldest one.
//
// Eviction is what a cache does, and this is not a cache. Dropping one entry
// from `identity_generation_` puts that source node's generation back to zero,
// so the store re-issues the same sequence of keys it issued before; a later
// recycle walks the counter back up to a generation whose key is still mapped
// to an id minted for the row that used to live in that element, and
// AllocateOrLookup hands that id to the row that replaced it. That is exactly
// the confusion the generation exists to prevent, it is silent, and it lands
// on the action path - `required_graph_revision` would be satisfied and the
// dispatcher would act on a row nobody looked at. An LRU is cheap here and
// wrong here.
//
// So the store starts again instead. Everything the epoch learned is dropped,
// while `next_node_ordinal_` is not - it is the only thing that carries the
// never-reuse property, and leaving it alone means no id issued after a reset
// can equal one issued before it. Every handle outstanding at the moment of
// the reset stops resolving: its ordinal is below the new floor, so IsRetired()
// still answers yes and Resolve() still answers kNodeGone, which is the same
// refusal the caller would get for a node the page genuinely removed. Its only
// correct response to that is to observe again. The cost is one re-observation
// of a page that churned this much; the alternative was one wrong action on
// the wrong row, and this store exists to make that impossible rather than
// unlikely.
//
// The reset leaves on a change class of its own, so an action barrier held
// open across it sees the graph move and refuses rather than completing a
// preflight whose node no longer exists.
//
// One sequencing assumption is worth writing down: this is only ever reached
// from a removal signal, never from inside an extraction pass. That is what
// lets UpsertLiveNode go on CHECKing that the id handed to it is not retired -
// an id allocated during a pass is described in the same pass, with no removal
// delivered in between. A future caller that retired from inside an extraction
// would break that assumption, and would break it loudly.
void SemanticGraphStore::ResetIdentityIfExhausted() {
  if (tracked_identity_count() < kMaxTrackedIdentities) {
    return;
  }

  retired_floor_ordinal_ = next_node_ordinal_;
  id_by_dom_key_.clear();
  dom_key_by_id_.clear();
  live_by_id_.clear();
  retired_.clear();
  identity_generation_.clear();
  ++identity_reset_count_;
  NoteChange(ChangeClass::kAdapterInvalidated);
}

std::vector<SemanticNodeId> SemanticGraphStore::AdvanceIdentityGeneration(
    IdentitySpace space,
    int64_t node_id) {
  // Retire first. Leaving the previous id live would let a caller resolve the
  // logical row that USED to be in this element, which is exactly the
  // offscreen-continuity assumption protocol section 8.3 forbids.
  std::vector<SemanticNodeId> retired = RetireSourceNode(space, node_id);

  // The next observation of this node is a different logical thing until
  // proven otherwise, so it allocates a new id (protocol section 8.3).
  ++identity_generation_[{space, node_id}];
  NoteChange(ChangeClass::kVirtualizedRecycled);
  // Checked here as well as inside Retire(), because this is the line that
  // grows `identity_generation_` and it grows it even when the removal retired
  // nothing at all - a source node the store had never issued an id for still
  // leaves an entry behind.
  ResetIdentityIfExhausted();
  return retired;
}

uint32_t SemanticGraphStore::identity_generation_for_testing(
    IdentitySpace space,
    int64_t node_id) const {
  auto it = identity_generation_.find({space, node_id});
  return it == identity_generation_.end() ? 0u : it->second;
}

}  // namespace taffy
