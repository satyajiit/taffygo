// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_DOM_MUTATION_SIGNAL_SOURCE_H_
#define TAFFY_RENDERER_DOM_MUTATION_SIGNAL_SOURCE_H_

#include <stdint.h>

#include <string_view>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "taffy/renderer/semantic_graph.h"
#include "taffy/renderer/semantic_graph_store.h"
#include "third_party/blink/public/web/web_dom_mutation_observer.h"

namespace blink {
class WebLocalFrame;
}  // namespace blink

namespace taffy {

// The document's own changes, as protocol section 5.3 change classes.
//
// Everything else the endpoint learns about a document arrives through a
// content::RenderFrameObserver callback, and for scrolling, focus and layout
// shift that is enough. For the DOM itself there is no such callback at the
// pinned milestone - RenderFrameObserver has none, and neither does any other
// //content or Blink public interface - so before this file a subscriber
// could open a delta stream on a document and never be told that the document
// changed. blink::WebDomMutationObserver is the signal, added to the fork for
// exactly this; everything here is the translation from what Blink reports to
// what the store and the delta publisher already understand.
//
// Two rules the translation follows, both of them about not inventing facts:
//
//   * A REMOVAL IS REPORTED BY IDENTITY, NEVER BY CLASS. A removed DOM node
//     is handed to the store's retirement path, which returns the ids it
//     actually retired. A node the graph never described retires nothing and
//     therefore invalidates nothing, and saying otherwise would put an id
//     that was never issued into a delta's removed list.
//
//   * A CHANGE NAMES A NODE ONLY IF THE STORE ALREADY ISSUED ONE. The lookup
//     never allocates. Allocating here would mint an identity for a node no
//     producing adapter described, which is the exact shape a stale handle
//     has (see SemanticGraphStore::Lookup).

// Which change class an attribute change belongs to. Split out and pure so
// the table is readable and testable on a host with no renderer: the mapping
// is policy, not plumbing, and a wrong entry here is a delta that describes
// the wrong kind of change rather than a crash.
SemanticGraphStore::ChangeClass ChangeClassForAttribute(std::string_view name);

// One mutation, translated. `dom_node_id` is a blink::WebNode::GetDomNodeId()
// value in the DOM identity space, or zero when Blink named no node.
struct DomMutationChange {
  SemanticGraphStore::ChangeClass change =
      SemanticGraphStore::ChangeClass::kAccessibleStateChanged;
  int dom_node_id = 0;
};

// What a batch of mutation records means, with no Blink object held and no
// store consulted. Pure, so the classification can be tested directly.
struct DomMutationSignals {
  DomMutationSignals();
  DomMutationSignals(const DomMutationSignals&);
  DomMutationSignals(DomMutationSignals&&);
  DomMutationSignals& operator=(const DomMutationSignals&);
  DomMutationSignals& operator=(DomMutationSignals&&);
  ~DomMutationSignals();

  std::vector<DomMutationChange> changes;
  std::vector<int> removed_dom_node_ids;
};

DomMutationSignals TranslateDomMutations(
    const std::vector<blink::WebDomMutation>& mutations);

// Holds the Blink observer for one document and forwards what it reports.
//
// Lifetime: the observer keeps itself alive inside the Blink heap until
// Disconnect(), so this object's destructor is what releases it. It is owned
// by the endpoint and therefore dies with the endpoint, which dies with its
// document - the same rule that retires the store.
class DomMutationSignalSource {
 public:
  using MutatedCallback =
      base::RepeatingCallback<void(SemanticGraphStore::ChangeClass,
                                   SemanticNodeId)>;
  using NodesRemovedCallback =
      base::RepeatingCallback<void(SemanticGraphStore::IdentitySpace,
                                   int64_t)>;

  DomMutationSignalSource(blink::WebLocalFrame* frame,
                          const SemanticGraphStore* store,
                          MutatedCallback on_mutated,
                          NodesRemovedCallback on_nodes_removed);
  DomMutationSignalSource(const DomMutationSignalSource&) = delete;
  DomMutationSignalSource& operator=(const DomMutationSignalSource&) = delete;
  ~DomMutationSignalSource();

  // A repeating drain of Blink's queued mutation records. On only while a
  // delta subscriber is listening: the same timer on every observed document
  // left the renderer never-idle and hung ExecJs on popup and cross-origin
  // frames. MutationObserver::Deliver remains the path when the document is
  // running; this catches the case where those microtasks never run.
  void SetStreaming(bool streaming);

 private:
  void OnMutations(const std::vector<blink::WebDomMutation>& mutations);
  void OnDrainTimer();

  // Never allocates: see the class comment.
  SemanticNodeId LookUp(int dom_node_id) const;

  const raw_ptr<const SemanticGraphStore> store_;
  const MutatedCallback on_mutated_;
  const NodesRemovedCallback on_nodes_removed_;
  raw_ptr<blink::WebDomMutationObserver> observer_ = nullptr;
  base::RepeatingTimer drain_timer_;

  base::WeakPtrFactory<DomMutationSignalSource> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_RENDERER_DOM_MUTATION_SIGNAL_SOURCE_H_
