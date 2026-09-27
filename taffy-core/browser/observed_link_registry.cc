// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/observed_link_registry.h"

#include <utility>

#include "base/logging.h"
#include "url/gurl.h"

namespace taffy {
namespace {

// Enough for every link a page's reading could hold at the transient-link
// limit's order of magnitude, and a bound on what a page that churns without
// end can make this remember.
constexpr size_t kMaxRetiredLinks = 256u;

bool IsSafeNormalizedDestination(const std::string& destination) {
  if (destination.empty() ||
      destination.size() > kMaxTransientObservedLinkDestinationBytes) {
    return false;
  }
  const GURL parsed(destination);
  return parsed.is_valid() && parsed.SchemeIsHTTPOrHTTPS() &&
         !parsed.has_username() && !parsed.has_password() &&
         parsed.spec() == destination;
}

}  // namespace

ObservedLinkRegistry::ObservedLinkRegistry() = default;
ObservedLinkRegistry::~ObservedLinkRegistry() = default;

bool ObservedLinkRegistry::Replace(const ObservationEnvelope& observation) {
  // A replacement that fails revokes what was here, which is deliberate and
  // checked by `FailedReplacementRevokesOlderAuthority`: a snapshot this build
  // could not read says nothing about whether the page it names still matches
  // the table, and keeping the table under it would be inheriting authority
  // from an older reading. `ApplyDelta` is the narrower thing — a delta says
  // exactly which handles died, so those and only those go.
  Clear();
  // Three codes are admitted and the truncation flag is not read at all. A
  // reading that stopped early is still a reading: the links it carries were
  // resolved from nodes it actually saw, by the same collector either way.
  // Refusing them left this table empty on every page big enough to hit a
  // budget, which is every page a person would send an errand to, so
  // `browser.link.open` could not open anything. Every code above these
  // three is still refused, because those say the observation did not happen
  // or does not describe this document rather than that it stopped early.
  // `ObservationResultBuilder` states the same rule where it decides which
  // links cross at all; both copies are load-bearing and the components may
  // not include each other.
  // Every way this table becomes empty was silent, and the one message that
  // does print reads as the wrong one of them: `FindExactLink` answers
  // `at=empty-table` for `!tab_id_.is_valid()`, which is the state `Clear()`
  // leaves, so a table that was built from ninety-seven links and then
  // revoked is indistinguishable from a page nothing ever read. A phone
  // printed exactly that — `at=empty-table links=0 window=0..0 asked=2014`
  // on a run whose collector had reported `kept=97` thirteen times — and
  // there was no way to tell which of the six clauses below had fired, or
  // whether any had. Same rule as decision 0185's admission line: a gate
  // that empties this table has to leave evidence that it fired.
  const char* refused_at = nullptr;
  if (observation.code != ObservationResultCode::kOk &&
      observation.code != ObservationResultCode::kConflicted &&
      observation.code != ObservationResultCode::kIncomplete) {
    refused_at = "code";
  } else if (observation.encoding != GraphPayloadEncoding::kBipContract) {
    refused_at = "encoding";
  } else if (observation.lifecycle_state != DocumentLifecycleState::kActive) {
    refused_at = "lifecycle";
  } else if (!observation.tab_id.is_valid() ||
             !observation.root_frame_id.is_valid() ||
             !observation.page_epoch.is_valid()) {
    refused_at = "identity";
  } else if (!observation.origin.is_valid() || observation.origin.is_opaque()) {
    refused_at = "origin";
  } else if (observation.transient_observed_links.size() >
             observation.node_count) {
    refused_at = "more-links-than-nodes";
  }
  if (refused_at) {
    // The code travels with the clause. Reading it off a neighbouring line
    // worked once and only because one was there; a wire enumeration is a
    // number this build chose, so printing it says nothing about the page.
    LOG(WARNING) << "[taffy_observed_link_not_replaced] at=" << refused_at
                 << " code=" << static_cast<int>(observation.code)
                 << " nodes=" << observation.node_count
                 << " offered=" << observation.transient_observed_links.size();
    return false;
  }

  std::map<std::string, TransientObservedLink> replacement;
  for (const TransientObservedLink& link :
       observation.transient_observed_links) {
    if (!link.node_id.is_valid() ||
        !IsSafeNormalizedDestination(link.normalized_destination) ||
        !replacement.emplace(link.node_id.value, link).second) {
      // No node id and no destination: one names a node on the page and the
      // other is the address itself. The clause is the whole finding.
      LOG(WARNING) << "[taffy_observed_link_not_replaced] at=link"
                   << " offered=" << observation.transient_observed_links.size()
                   << " accepted=" << replacement.size();
      return false;
    }
  }

  tab_id_ = observation.tab_id;
  frame_id_ = observation.root_frame_id;
  page_epoch_ = observation.page_epoch;
  built_at_revision_ = observation.graph_revision;
  graph_revision_ = observation.graph_revision;
  origin_ = observation.origin;
  destinations_ = std::move(replacement);
  retired_links_.clear();
  return true;
}

bool ObservedLinkRegistry::ApplyDelta(const DeltaEnvelope& delta) {
  if (!tab_id_.is_valid()) {
    return false;
  }
  // The delta has to be this document's, and has to start exactly where this
  // table stands. A gap means changes were lost, so `from_revision` is not
  // evidence of anything — the same reasoning `DeltaApplicability` states.
  if (delta.tab_id != tab_id_ || delta.frame_id != frame_id_ ||
      delta.page_epoch != page_epoch_ ||
      delta.from_revision != graph_revision_ ||
      delta.to_revision <= delta.from_revision || delta.truncation.truncated ||
      delta.removed_node_ids.size() != delta.removed_node_count) {
    LOG(WARNING) << "[taffy_observed_link_cleared] at=delta-does-not-apply"
                 << " links=" << destinations_.size()
                 << " at_revision=" << graph_revision_
                 << " from=" << delta.from_revision;
    Clear();
    return false;
  }
  // Added nodes cannot repurpose a handle this table already holds: an
  // identifier is never reissued inside an epoch. A changed node or edge can,
  // and the payload that would say how is opaque to the browser process, so
  // the honest answer to one is the empty table a delta always used to leave.
  if (delta.changed_node_count != 0 || delta.changed_edge_count != 0) {
    // The suspected cause of the phone's empty table, and the reason this
    // line exists rather than a guess: a results page changes under a
    // reading all the time, so a delta carrying one changed node revokes
    // every link the last full reading found, and only the next full
    // observation rebuilds them. Whether that is what happened is a question
    // this log answers and the previous one could not.
    LOG(WARNING) << "[taffy_observed_link_cleared] at=delta-changed-something"
                 << " links=" << destinations_.size()
                 << " changed_nodes=" << delta.changed_node_count
                 << " changed_edges=" << delta.changed_edge_count;
    Clear();
    return false;
  }
  size_t retired = 0u;
  for (const SemanticNodeId& removed : delta.removed_node_ids) {
    if (!removed.is_valid()) {
      LOG(WARNING) << "[taffy_observed_link_cleared] at=removed-id"
                   << " links=" << destinations_.size();
      Clear();
      return false;
    }
    if (destinations_.erase(removed.value) != 0u) {
      ++retired;
      if (retired_links_.size() < kMaxRetiredLinks) {
        retired_links_.insert(removed.value);
      }
    }
  }
  graph_revision_ = delta.to_revision;
  if (retired != 0u) {
    // A page that re-renders takes links out of this table between the
    // reading a model was shown and the move it asks for, and until this line
    // that happened in silence: a phone recorded `node-not-in-table links=2`
    // twice on a page whose every reading had kept six, with no line saying
    // where the other four went. Counts only.
    LOG(WARNING) << "[taffy_observed_link_retired] retired=" << retired
                 << " links=" << destinations_.size()
                 << " at_revision=" << graph_revision_;
  }
  return true;
}

bool ObservedLinkRegistry::Invalidate(const InvalidationNotice& notice) {
  if (!tab_id_.is_valid()) {
    return false;
  }
  // The reason is a compiled-in enumeration member and the count is this
  // table's own; neither says anything about the page.
  if (notice.frame_id != frame_id_) {
    LOG(WARNING) << "[taffy_observed_link_kept] at=other-frame"
                 << " reason=" << static_cast<int>(notice.reason)
                 << " links=" << destinations_.size();
    return true;
  }
  LOG(WARNING) << "[taffy_observed_link_cleared] at=page-invalidated"
               << " reason=" << static_cast<int>(notice.reason)
               << " links=" << destinations_.size();
  Clear();
  return false;
}

void ObservedLinkRegistry::Clear() {
  tab_id_ = TabId{};
  frame_id_ = FrameId{};
  page_epoch_ = PageEpoch{};
  built_at_revision_ = 0;
  graph_revision_ = 0;
  origin_ = Origin{};
  destinations_.clear();
  retired_links_.clear();
}

std::optional<std::string> ObservedLinkRegistry::Resolve(
    const CanonicalLinkOpenHandle& handle) const {
  const auto* link = FindExactLink(handle);
  if (!link) {
    return std::nullopt;
  }
  // A link this table holds but will not open in place still reaches the
  // model as `kNodeGone`, which reads as "that link is gone" for a link that
  // is right there. Naming the branch is what tells the two apart.
  //
  // `opens_new_tab` is not one of those branches any more (decision 0184).
  // `browser.link.open` never clicks: it resolves the href and navigates the
  // task's own tab, so `target` is a rendering hint nothing here consults, and
  // refusing on it made a `target="_blank"` result unreachable by any move the
  // model has — `browser.tabs.open` takes a typed address and the model is
  // never shown a URL. A phone measured the cost: an errand reached the
  // eAadhaar download page, was refused this link at `at=opens-new-tab`, and
  // went back to a search engine.
  if (link->is_download) {
    LOG(WARNING) << "[taffy_observed_link_refused] at=is-download";
    return std::nullopt;
  }
  return link->normalized_destination;
}

std::optional<std::string> ObservedLinkRegistry::ResolveDownload(
    const CanonicalObservedNodeHandle& handle) const {
  const auto* link = FindExactLink(handle);
  if (!link || !GURL(link->normalized_destination).SchemeIs("https")) {
    return std::nullopt;
  }
  return link->normalized_destination;
}

const TransientObservedLink* ObservedLinkRegistry::FindExactLink(
    const CanonicalObservedNodeHandle& handle) const {
  // Which gate refused, as a compiled-in name. Seven of them reach the model
  // as one `kNodeGone`, and that word cannot tell an empty table from a page
  // that moved from a node the model named that is not a link. The same rule
  // decisions 0162 and 0165 state for the seams above.
  const char* at = nullptr;
  if (!tab_id_.is_valid()) {
    // `Clear()` and "never built" leave the same state, so this name cannot
    // be read as "nothing was ever read here" — it means only that nothing
    // is here now. Which of the two it is comes from the
    // `[taffy_observed_link_cleared]` and `[taffy_observed_link_not_replaced]`
    // lines above, or from their absence.
    at = "no-table";
  } else if (handle.expected_origin_is_opaque) {
    at = "opaque-origin";
  } else if (handle.tab_id != tab_id_.value) {
    at = "tab";
  } else if (handle.frame_id != frame_id_.value) {
    at = "frame";
  } else if (handle.page_epoch != page_epoch_.value) {
    at = "page-epoch";
  } else if (handle.graph_revision > graph_revision_) {
    // Past the newest delta absorbed. Nothing has told the browser about
    // anything beyond it.
    at = "after-the-newest-delta";
  } else if (handle.expected_origin != origin_.serialization) {
    at = "origin";
  }
  if (at) {
    LOG(WARNING) << "[taffy_observed_link_refused] at=" << at
                 << " links=" << destinations_.size()
                 << " window=" << built_at_revision_ << ".." << graph_revision_
                 << " asked=" << handle.graph_revision;
    return nullptr;
  }
  if (handle.graph_revision < built_at_revision_) {
    // Older than the reading this table was built from, and admitted
    // (decision 0185). This used to refuse, and it refused the ordinary case:
    // the walk re-reads a page before each paid turn, so the table is
    // routinely replaced between the reading a model was shown and the move it
    // asks for from that reading. A phone measured `window=2833..2833
    // asked=1` on a results page — the whole of a search errand's one route to
    // the site it found.
    //
    // It is logged rather than silent for the same reason decision 0183's
    // repair is: a gate that stops refusing has to leave evidence that it
    // fired, or the next device run cannot tell it apart from a gate nothing
    // reached.
    LOG(WARNING) << "[taffy_observed_link_before_this_reading]"
                 << " links=" << destinations_.size()
                 << " window=" << built_at_revision_ << ".." << graph_revision_
                 << " asked=" << handle.graph_revision;
  }
  const auto found = destinations_.find(handle.node_id);
  if (found == destinations_.end()) {
    // The identifier is browser-minted and synthetic; printing it says
    // nothing about the page and is the only way to tell "the model named a
    // node that is not a link" apart from "the table is stale".
    LOG(WARNING) << "[taffy_observed_link_refused] at=node-not-in-table"
                 << " links=" << destinations_.size()
                 << " retired=" << (retired_links_.contains(handle.node_id) ? 1 : 0)
                 << " node=" << handle.node_id;
    return nullptr;
  }
  if (!IsSafeNormalizedDestination(found->second.normalized_destination)) {
    LOG(WARNING) << "[taffy_observed_link_refused] at=unsafe-destination";
    return nullptr;
  }
  return &found->second;
}

std::optional<std::string> ObservedLinkRegistry::ResolveObservedLink(
    const NodeHandle& handle) const {
  if (!handle.is_well_formed() || handle.expected_origin != origin_) {
    return std::nullopt;
  }
  return Resolve(CanonicalLinkOpenHandle{
      .tab_id = handle.tab_id.value,
      .frame_id = handle.frame_id.value,
      .page_epoch = handle.page_epoch.value,
      .graph_revision = handle.graph_revision,
      .node_id = handle.node_id.value,
      .expected_origin_is_opaque = handle.expected_origin.is_opaque(),
      .expected_origin = handle.expected_origin.serialization,
  });
}

}  // namespace taffy
