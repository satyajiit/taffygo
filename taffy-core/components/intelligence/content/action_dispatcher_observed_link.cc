// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The one browser-owned command that still has a renderer node to re-read.
// Keeping it separate makes the distinction explicit: generic navigation has
// no node authority, while browser.link.open is valid only while the exact
// observed link and destination are both still live.

#include <optional>
#include <string>

#include "base/functional/bind.h"
#include "taffy/components/intelligence/content/action_dispatcher.h"
#include "url/gurl.h"

namespace taffy {
namespace {

bool ResolvedObservedLinkMatches(const NodeHandle& handle,
                                 const std::string& destination,
                                 const ResolvedNodeFacts& facts) {
  // The live read may be newer than the handle — an ordinary page mutates
  // between the snapshot a model read and the tap it asks for. It may never be
  // older: that would mean the handle names a revision this document has not
  // reached, which is not a stale read but a claim about a future. The href
  // this compares below is re-read from the live node either way, and it is
  // the third of four independent comparisons of that same address.
  if (facts.node_id != handle.node_id ||
      facts.observed_at_revision < handle.graph_revision ||
      facts.role != static_cast<uint16_t>(mojom::SemanticRole::kLink) ||
      !facts.SupportsAction(ActionType::kActivate) || !facts.destination ||
      // Not `opens_new_tab`: this dispatch navigates the tab it is given and
      // never honours `target`, so the attribute describes nothing it does
      // (decision 0184). `is_download` stays — that link is not a navigation
      // and `browser.download.from_link` is the move for it.
      facts.destination->is_download ||
      facts.destination->url_metadata.disclosure != UrlDisclosure::kFullUrl ||
      !facts.destination->url_metadata.url ||
      *facts.destination->url_metadata.url != destination) {
    return false;
  }
  const GURL parsed(destination);
  return parsed.is_valid() && parsed.SchemeIsHTTPOrHTTPS() &&
         !parsed.has_username() && !parsed.has_password() &&
         parsed.spec() == destination;
}

bool RegistryStillResolves(const ObservedLinkResolver* resolver,
                           const NodeHandle& handle,
                           const std::string& destination) {
  const std::optional<std::string> current =
      resolver ? resolver->ResolveObservedLink(handle) : std::nullopt;
  return current && *current == destination;
}

}  // namespace

void ActionDispatcher::ResolveAndDispatchObservedLink(
    const RequestId& request_id) {
  PendingAction* pending = Find(request_id);
  if (!pending || !pending->command || !pending->command->source_handle) {
    return;
  }
  const NodeHandle handle = *pending->command->source_handle;
  const std::string destination = pending->command->argument;
  ResolveNodeFacts(
      handle,
      base::BindOnce(
          [](base::WeakPtr<ActionDispatcher> self, RequestId id,
             NodeHandle expected_handle, std::string expected_destination,
             std::optional<ResolvedNodeFacts> facts) {
            if (!self || !self->Find(id)) {
              return;
            }
            if (!facts) {
              self->FinishAction(id, ActionResultCode::kNodeGone, std::nullopt,
                                 std::nullopt, PreconditionKind::kNodeExists,
                                 VerifierOutcome::kNotAttempted);
              return;
            }
            if (!ResolvedObservedLinkMatches(expected_handle,
                                             expected_destination, *facts) ||
                !RegistryStillResolves(self->observed_link_resolver_,
                                       expected_handle, expected_destination)) {
              self->FinishAction(id, ActionResultCode::kStaleGraph,
                                 std::nullopt, std::nullopt,
                                 PreconditionKind::kExpectedDestination,
                                 VerifierOutcome::kNotAttempted);
              return;
            }
            self->JournalAndDispatch(id, *facts);
          },
          weak_factory_.GetWeakPtr(), request_id, handle, destination));
}

}  // namespace taffy
