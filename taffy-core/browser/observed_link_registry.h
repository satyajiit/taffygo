// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_OBSERVED_LINK_REGISTRY_H_
#define TAFFY_BROWSER_OBSERVED_LINK_REGISTRY_H_

#include <cstddef>
#include <map>
#include <set>
#include <optional>
#include <string>

#include "taffy/browser/core_task_canonical_intent.h"
#include "taffy/common/public/bip_delta.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/components/intelligence/content/observed_link_resolver.h"

namespace taffy {

// Per-WebContents, memory-only authority for links present in the latest
// complete BIP snapshot. It stores destinations outside the graph payload so
// an opaque node handle can be resolved by the trusted browser without ever
// disclosing the address to Rust or a model.
class ObservedLinkRegistry : public ObservedLinkResolver {
 public:
  ObservedLinkRegistry();
  ObservedLinkRegistry(const ObservedLinkRegistry&) = delete;
  ObservedLinkRegistry& operator=(const ObservedLinkRegistry&) = delete;
  ~ObservedLinkRegistry() override;

  // Atomically replaces the current document binding. Failure leaves the
  // registry empty; an incomplete or malformed snapshot never preserves
  // authority inherited from an older page.
  bool Replace(const ObservationEnvelope& observation);

  // Carries the current binding across one graph advance, or clears it.
  //
  // A delta used to empty the table outright, which is why the phone recorded
  // `NodeGone` on the single `browser.link.open` it ever proposed: an
  // ordinary page mutates between the snapshot the model read and the tap it
  // asks for, and an empty registry resolves nothing. `removed_node_ids` is
  // carried in full for exactly this reason — a subscriber is told which
  // handles died — so the links a delta did not touch keep resolving at the
  // revision it advances to. Anything this cannot reason about still clears:
  // the graph payload is opaque here by construction, so a delta that changed
  // a node or an edge takes the whole table with it.
  //
  // Returns whether the binding survived.
  bool ApplyDelta(const DeltaEnvelope& delta);
  void Clear();

  // Clears the binding when `notice` is about the document it was built from,
  // and keeps it when the notice is about another frame; either way it says
  // which. Returns whether the binding survived.
  //
  // Every link here is the root frame's — `FindExactLink` refuses any other
  // frame — so a child frame navigating inside the page (an advertisement, an
  // embedded video, a CAPTCHA widget) says nothing about any of them. Clearing
  // on every notice emptied the table between the reading a model was shown
  // and the link it then opened from it: on a phone, `at=no-table links=0`
  // right after a reading whose collector kept 87 links.
  bool Invalidate(const InvalidationNotice& notice);

  // Navigation resolves only an exact tab/frame/epoch/revision/origin/node
  // handle for an ordinary link: download and new-tab flags are refused.
  std::optional<std::string> Resolve(
      const CanonicalLinkOpenHandle& handle) const;
  std::optional<std::string> ResolveObservedLink(
      const NodeHandle& handle) const override;
  // Explicit download proposals may use either flag, but require HTTPS.
  std::optional<std::string> ResolveDownload(
      const CanonicalObservedNodeHandle& handle) const;

  size_t size_for_testing() const { return destinations_.size(); }

 private:
  const TransientObservedLink* FindExactLink(
      const CanonicalObservedNodeHandle& handle) const;
  TabId tab_id_;
  FrameId frame_id_;
  PageEpoch page_epoch_;

  // The reading this table was built from. It is a diagnostic rather than a
  // gate (decision 0185): a handle minted before it is admitted, and the log
  // line says so, because the floor refused the ordinary case and protected
  // nothing that the four gates below do not.
  //
  // What it looked like it protected was "the href the model saw". It never
  // did. An address is resolved from this table at the moment a capability is
  // minted and re-read from the live node at the moment it is dispatched, so
  // an href that changed since the reading a handle came from reaches the
  // model's move either way — the floor only fired when a *replacement*
  // happened to land in between, which is a fact about when this tab was last
  // observed and not about the link. What does carry the property is the page
  // epoch together with `SemanticGraphStore`'s first invariant: an id is
  // never reissued inside an epoch, so a handle from any revision of this
  // epoch names the same node it always named, or names nothing.
  GraphRevision built_at_revision_ = 0;
  GraphRevision graph_revision_ = 0;
  Origin origin_;
  std::map<std::string, TransientObservedLink> destinations_;

  // Links a delta removed since the table was built, by id, so a refusal can
  // say whether the model named a link the page took away after the reading
  // or a node that was never a link. Diagnostic only: nothing here resolves,
  // because an address is re-read from the live node at dispatch and a
  // removed node has none. Bounded, and emptied with the table.
  std::set<std::string> retired_links_;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_OBSERVED_LINK_REGISTRY_H_
