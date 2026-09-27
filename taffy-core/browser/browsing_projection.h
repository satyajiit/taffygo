// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_BROWSING_PROJECTION_H_
#define TAFFY_BROWSER_BROWSING_PROJECTION_H_

#include <string>
#include <string_view>

#include "taffy/browser/download_record.h"
#include "taffy/browser/download_state_machine.h"
#include "taffy/browser/navigation_error_classifier.h"
#include "taffy/browser/tab_record.h"
#include "taffy/browser/tab_session_registry.h"
#include "taffy/contracts/browsing/generated/mojom/browsing.mojom.h"

// The one translation between what this process knows about tabs, navigation
// and downloads and what the browsing contract says (decision
// `docs/decisions/0043-the-browsing-seam-is-a-contract.md`).
//
// The M1 seams beside this file already hold every fact the contract
// describes: `NavigationErrorClassifier` decides what a failed navigation was,
// `DownloadRecord` mirrors the download system's own lifecycle,
// `DownloadStateMachine` decides which command a person may ask for, and
// `TabSessionRegistry` answers whether a tab may be opened, selected or
// closed. What none of them had was a way to say any of it to a surface that
// is not written in Kotlin.
//
// Every function here is pure and every switch is exhaustive with no
// `default`. That is the whole maintenance argument: adding an enumerator to
// one of this directory's enumerations is a compile error here rather than a
// value that silently becomes something else, and the compiler is a better
// register than a comment asking the next person to remember.
//
// Two directions, and they are not symmetric. Outbound — a browser fact
// becoming a contract value — is total, because the browser is the authority
// and every value it holds has a meaning to state. Inbound — a contract value
// becoming a browser command — is not, because a surface is a less trusted
// speaker than this process: it may name a command this build does not have,
// and the answer to that is a refusal, not a nearest neighbour.

namespace taffy {

namespace browsing_projection {

// Outbound. Total, exhaustive, and pure.
browsing::mojom::NavigationFailure Project(NavigationErrorClass value);
browsing::mojom::InterstitialKind Project(InterstitialKind value);
browsing::mojom::DownloadState Project(DownloadState value);
browsing::mojom::DownloadFailure Project(DownloadFailureClass value);
browsing::mojom::DownloadDestination Project(DownloadDestinationKind value);
browsing::mojom::TabOwner Project(TabOwnership value);

// Outbound verdicts. Each of the registry's refusals has one contract status,
// and the mapping is many-to-one on purpose: a surface is told that its
// request was refused and which category of refusal it was, never which
// internal predicate fired.
browsing::mojom::BrowsingStatus Project(TabOpenResult value);
browsing::mojom::BrowsingStatus Project(TabActivateResult value);
browsing::mojom::BrowsingStatus Project(TabCloseResult value);
browsing::mojom::BrowsingStatus Project(DownloadCommandLegality value);

// One download, as a surface sees it. `host` is supplied by the caller because
// a `DownloadRecord` names the tab a download started in and not its host, and
// a projection that resolved a tab would be a projection that needs a browser
// object to run — which is exactly what keeps this file testable on any host.
browsing::mojom::DownloadViewPtr ProjectDownload(const DownloadRecord& record,
                                                 std::string_view host);

// Inbound. Refuses rather than approximates: `std::nullopt` for a command this
// build does not have.
std::optional<DownloadCommand> AcceptCommand(
    browsing::mojom::DownloadCommand value);

// Whether a snapshot describes one coherent browser.
//
// The contract's shape rules describe records one at a time, so this is the
// one rule about a snapshot that they cannot express: exactly one tab carries
// `selected`, and the navigation beside the list is that tab's. A snapshot
// that broke it would draw an address bar for a tab that is not in its own
// switcher — which is not a malformed record, it is two well-formed records
// that disagree. The browser side is where it has to hold, because the
// browser is the only party that has both halves at once.
bool StateIsCoherent(const browsing::mojom::BrowsingStateView& state);

}  // namespace browsing_projection

}  // namespace taffy

#endif  // TAFFY_BROWSER_BROWSING_PROJECTION_H_
