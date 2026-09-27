// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/dom_mutation_signal_source.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_local_frame.h"

namespace taffy {

SemanticGraphStore::ChangeClass ChangeClassForAttribute(
    std::string_view name) {
  // Protocol section 5.3 names the classes; this table decides which one an
  // attribute belongs to. It is deliberately conservative: an attribute that
  // is not listed is reported as an accessible-state change, which advances
  // the revision and is never dropped before a layout signal. Guessing
  // narrower - "this one is only cosmetic" - is how a precondition change
  // gets coalesced away.
  if (name == "href" || name == "src" || name == "action" ||
      name == "formaction") {
    return SemanticGraphStore::ChangeClass::kDestinationChanged;
  }
  if (name == "disabled" || name == "aria-disabled" || name == "readonly") {
    return SemanticGraphStore::ChangeClass::kEnabledStateChanged;
  }
  if (name == "hidden" || name == "inert" || name == "style" ||
      name == "class" || name == "aria-hidden") {
    return SemanticGraphStore::ChangeClass::kVisibilityChanged;
  }
  if (name == "role") {
    return SemanticGraphStore::ChangeClass::kRoleChanged;
  }
  if (name == "value" || name == "checked" || name == "selected" ||
      name == "aria-checked" || name == "aria-selected") {
    return SemanticGraphStore::ChangeClass::kFormStateChanged;
  }
  return SemanticGraphStore::ChangeClass::kAccessibleStateChanged;
}

DomMutationSignals::DomMutationSignals() = default;
DomMutationSignals::DomMutationSignals(const DomMutationSignals&) = default;
DomMutationSignals::DomMutationSignals(DomMutationSignals&&) = default;
DomMutationSignals& DomMutationSignals::operator=(const DomMutationSignals&) =
    default;
DomMutationSignals& DomMutationSignals::operator=(DomMutationSignals&&) =
    default;
DomMutationSignals::~DomMutationSignals() = default;

DomMutationSignals TranslateDomMutations(
    const std::vector<blink::WebDomMutation>& mutations) {
  DomMutationSignals signals;
  for (const blink::WebDomMutation& mutation : mutations) {
    switch (mutation.kind) {
      case blink::WebDomMutation::Kind::kChildList:
        for (int removed : mutation.removed_dom_node_ids) {
          if (removed != 0) {
            signals.removed_dom_node_ids.push_back(removed);
          }
        }
        // An insertion changes the parent's relationships, so the parent is
        // what the change is about. The inserted node itself has no identity
        // in this graph yet and must not be given one here - a producing
        // adapter names nodes, a mutation signal does not.
        if (!mutation.added_dom_node_ids.empty()) {
          signals.changes.push_back(
              {SemanticGraphStore::ChangeClass::kNodeAdded,
               mutation.target_dom_node_id});
        }
        break;

      case blink::WebDomMutation::Kind::kAttributes:
        signals.changes.push_back(
            {ChangeClassForAttribute(mutation.attribute_name.Utf8()),
             mutation.target_dom_node_id});
        break;

      case blink::WebDomMutation::Kind::kCharacterData:
        // The target is a text node, which no adapter ever names. The element
        // it lives in is the thing whose accessible name just changed, so
        // that is the node reported - and when Blink named no parent, the
        // change is still reported with no node rather than dropped.
        signals.changes.push_back(
            {SemanticGraphStore::ChangeClass::kAccessibleStateChanged,
             mutation.parent_dom_node_id != 0 ? mutation.parent_dom_node_id
                                              : mutation.target_dom_node_id});
        break;
    }
  }
  return signals;
}

DomMutationSignalSource::DomMutationSignalSource(
    blink::WebLocalFrame* frame,
    const SemanticGraphStore* store,
    MutatedCallback on_mutated,
    NodesRemovedCallback on_nodes_removed)
    : store_(store),
      on_mutated_(std::move(on_mutated)),
      on_nodes_removed_(std::move(on_nodes_removed)) {
  if (!frame) {
    return;
  }
  observer_ = blink::WebDomMutationObserver::Create(
      frame->GetDocument(),
      base::BindRepeating(&DomMutationSignalSource::OnMutations,
                          weak_factory_.GetWeakPtr()));
  // Create returns null when the frame has no Document, or the Document has
  // no execution context yet — a popup's initial empty document. Observing
  // nothing is correct: there is no page to describe. A CHECK here would
  // crash the renderer the moment a window.open produced such a document.
}

void DomMutationSignalSource::SetStreaming(bool streaming) {
  if (!observer_) {
    return;
  }
  if (streaming) {
    if (!drain_timer_.IsRunning()) {
      drain_timer_.Start(FROM_HERE, base::Milliseconds(50), this,
                         &DomMutationSignalSource::OnDrainTimer);
    }
    return;
  }
  drain_timer_.Stop();
}

void DomMutationSignalSource::OnDrainTimer() {
  if (observer_) {
    observer_->FlushPending();
  }
}

DomMutationSignalSource::~DomMutationSignalSource() {
  drain_timer_.Stop();
  if (observer_) {
    // Blink holds the observer alive until this call, so skipping it would
    // leak an observer that keeps delivering into a dead callback.
    observer_->Disconnect();
    observer_ = nullptr;
  }
}

void DomMutationSignalSource::OnMutations(
    const std::vector<blink::WebDomMutation>& mutations) {
  const DomMutationSignals signals = TranslateDomMutations(mutations);

  // Removals first, and in this order on purpose: retiring an id is what
  // makes a handle to it fail closed, and a subscriber that learned of the
  // sibling change first could re-resolve the doomed handle in between.
  for (int removed : signals.removed_dom_node_ids) {
    on_nodes_removed_.Run(SemanticGraphStore::IdentitySpace::kDom, removed);
  }
  for (const DomMutationChange& change : signals.changes) {
    on_mutated_.Run(change.change, LookUp(change.dom_node_id));
  }
}

SemanticNodeId DomMutationSignalSource::LookUp(int dom_node_id) const {
  if (!store_ || dom_node_id == 0) {
    return SemanticNodeId();
  }
  return store_
      ->Lookup(store_->MakeKey(SemanticGraphStore::IdentitySpace::kDom,
                               dom_node_id))
      .value_or(SemanticNodeId());
}

}  // namespace taffy
