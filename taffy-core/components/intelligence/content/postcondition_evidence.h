// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_POSTCONDITION_EVIDENCE_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_POSTCONDITION_EVIDENCE_H_

#include <stdint.h>

#include <optional>
#include <vector>

#include "taffy/common/public/bip_action.h"
#include "taffy/common/public/bip_identity.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/common/public/taffy_download_facts.h"

// What the verifier is allowed to reason from (protocol section 11.6).
//
// Every field here is labelled by where it came from, and the labelling is the
// point rather than documentation. "The verifier uses browser-owned navigation
// events and a fresh/delta observation. Renderer acknowledgement alone is
// DISPATCHED, not VERIFIED." A structure that mixed the two would let a
// reviewer lose track of which was which; this one cannot, because renderer
// acknowledgement has no field at all.
//
// That absence is deliberate and worth stating plainly: there is nowhere in
// this struct to put "the renderer said it worked". The verifier physically
// cannot read it, so no check can be written that depends on it, so no code
// path can turn it into kVerified.

namespace taffy {

struct NavigationHopEvidence {
  Origin origin;
  UrlMetadata url;
};

// From Chromium's own navigation record, read on the browser's UI thread.
// Nothing a renderer sent contributes to any of it.
struct NavigationEvidence {
  bool committed = false;
  // Set only by WebContentsObserver::DidStopLoading after the verifier starts.
  // Reading WebContents::IsLoading() would confuse state that predated the
  // dispatch with evidence caused by this command.
  bool loading_stopped = false;
  bool is_same_document = false;
  bool is_in_primary_main_frame = false;
  bool is_error_page = false;
  Origin committed_origin;
  UrlMetadata committed_url;
  // Initial request, every server redirect, and the final URL, in Chromium's
  // order. An exact destination grant is satisfied only when every hop stays
  // within the same declared destination policy; returning to the approved
  // address after an undeclared redirect is not success.
  std::vector<NavigationHopEvidence> redirect_chain;
  // The frame the commit happened in, resolved through the broker's own frame
  // identity rather than through anything the navigation carried.
  FrameId frame_id;
};

// From the browser's own tab model, delivered when a new WebContents is
// created for a request that came from this tab.
struct TabEvidence {
  bool tab_created = false;
  TabId opener_tab_id;
  Origin destination_origin;
  UrlMetadata destination_url;
  // True when the new tab can reach its opener. A research task tab is opened
  // without one, and the postcondition says which it expected.
  bool has_opener_reference = true;
};

// From a fresh observation or a delta, taken after dispatch and re-resolved
// through the broker.
//
// `observed_at_revision` is what makes it evidence at all: state that was
// already true before the action would otherwise read as the action's effect,
// so a check believes this only when the revision is strictly newer than the
// revision at dispatch.
struct ObservationEvidence {
  bool attempted = false;
  bool node_resolved = false;
  bool node_gone = false;
  bool handle_still_live = false;
  GraphRevision observed_at_revision = 0;
  GraphRevision dispatch_revision = 0;
  std::optional<ResolvedNodeFacts> facts;
  // Which observation path produced it. A delta and a fresh snapshot are both
  // acceptable (protocol section 11.6); which one it was is recorded because
  // the two have different latency and a reviewer comparing verified rates
  // needs to know.
  VerifierKind source = VerifierKind::kFreshSnapshot;

  bool IsNewerThanDispatch() const {
    return observed_at_revision > dispatch_revision;
  }
};

// Names one dispatched action, so that a browser-owned flow can say which
// action it belongs to — or say that it belongs to none.
//
// The observation path has `GraphRevision` for this. An observation counts
// only when it was read at a revision strictly newer than the revision at
// dispatch, so state that was already true before the action cannot be
// mistaken for its effect. The browser-flow path had no equivalent, and its
// scope rules do not supply one: a witness is scoped to a tab and a verifier
// runs for a few seconds, so "a download appeared in this tab while the
// verifier was waiting" is satisfied just as well by the person clicking a
// link themselves. Crediting the assistant with that is a false entry in an
// audit record, which is worse than no entry.
//
// So the browser-flow path carries a marker of its own. The dispatcher takes
// one immediately before it dispatches; the source stamps it onto the flows it
// can bind to that dispatch and onto no others; the check believes evidence
// only when the two are equal. A flow the source cannot bind carries no
// watermark at all and is recorded that way — never quietly credited to
// whichever dispatch happened to be outstanding.
//
// Minted by the source, because the source is the only object that can decide
// what falls inside the window it opens. Zero is a value `NoteDispatch` never
// returns, so a default-constructed watermark matches nothing.
struct DispatchWatermark {
  uint64_t value = 0;
  friend bool operator==(const DispatchWatermark&,
                         const DispatchWatermark&) = default;
};

// What a download witness may say about the transfer it watched.
//
// Every field is a **class, a count or a binding**, and the two facts a
// reviewer will reach for first are deliberately absent:
//
//   * **the file name.** It is page-authored. `Content-Disposition` is a
//     header the server writes and `download="..."` is an attribute the page
//     writes, so a file name is attacker-controlled text wearing the costume
//     of metadata. Carrying it here would put that text into the verifier's
//     evidence, and from there into whatever an audit record and a model
//     context are built from — the one direction protocol section 16 and the
//     redaction invariant exist to close. Chromium keeps the name, shows it to
//     the person, and writes the file under it; none of that needs this
//     struct's help.
//   * **the URL.** Same argument with a shorter path to harm: a query string
//     is where tokens live, and a witness that proves "a download started" has
//     no use for the address it came from.
//
// **There is no string here at all**, and that is the settled answer to a
// question this struct used to leave open. It carried the reported media type,
// filtered so that `application/octet-stream; name="statement.pdf"` could not
// walk a file name through the one field that held text. That filter is a
// grammar rather than an allowlist, and `type/subtype` where both halves are
// RFC 9110 tokens admits `application/TaffyCanaryQ3-statement.pdf` — a file
// name with no space in it is a token, so the field that existed to refuse a
// name refused only one spelling of it. Decision 0062 section 3 settles it the
// other way: `media_type` is the **top-level type**, a closed enumeration, and
// the subtype never enters this process's evidence. A field that cannot hold a
// string cannot be pressed into service as one.
//
// `directory_class` is the whole reason the path is not here. A verifier
// asking "did the bytes land somewhere the person chose" is answered by a
// class; asking it with a path would mean the profile's directory layout
// travelled with every answer.
struct DownloadFlowFacts {
  // Chromium's own identifier for the item. Identity belongs to the download
  // system; nothing here allocates one.
  //
  // Unique within one download manager and not across two: the identifiers are
  // handed out per profile, so a witness that changes the manager it watches
  // must forget what it knew rather than compare a new manager's item against
  // a remembered one.
  uint32_t download_id = 0;

  // What kind of thing arrived, to the precision that is a class rather than
  // an instance. kUnknown when the server said nothing this process is willing
  // to call a media type.
  MediaTopLevelType media_type = MediaTopLevelType::kUnknown;

  // Bytes written so far. A count, and counts are not content.
  int64_t received_bytes = 0;

  DownloadDestinationKind directory_class = DownloadDestinationKind::kUndecided;

  // Where the transfer is in its lifecycle. `DownloadStateIsTerminal` is the
  // one definition of "it has stopped".
  DownloadState state = DownloadState::kCreated;

  // The dispatch this transfer is bound to, or absent when nothing binds it to
  // one.
  //
  // Absent is the ordinary answer, not the error case: a person's own download
  // is unattributed, and so is one that started before the action did, and so
  // is one the witness saw after the person touched the tab. The check reads
  // this before it reads anything else, so unattributed evidence leaves a
  // postcondition pending rather than settling it — see
  // `CheckBrowserFlowStarted`. It is carried into the record either way,
  // because "a download happened and it was not ours" is a fact worth having
  // in an audit trail; what it must never be is silence that reads as
  // agreement.
  std::optional<DispatchWatermark> attributed_to;
};

// From a browser-owned flow the page can start and only the browser can
// confirm: a download, a file chooser, a permission prompt, an external
// intent. Supplied by the layer above //taffy, which owns those
// surfaces.
struct BrowserFlowEvidence {
  bool flow_started = false;
  BrowserFlowKind kind = BrowserFlowKind::kDownload;

  // Present only when `kind` is kDownload and a witness is installed.
  //
  // The check reads exactly one field of it — `attributed_to`, which is what
  // ties the transfer to the action that is claiming it. The rest are for the
  // readers that come after the check: the task that has to know whether the
  // file actually arrived, and the journal that has to record what happened
  // without recording what it was called.
  //
  // Its absence is why the other three browser flows are still refused at
  // admission. A file chooser, a permission prompt and an external intent have
  // no witness, so they have nothing to bind an action to, so nothing about
  // them can be believed — which is what `CheckBrowserFlowStarted` says by
  // leaving them pending rather than by trusting a `flow_started` nobody can
  // attribute.
  std::optional<DownloadFlowFacts> download;
};

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_POSTCONDITION_EVIDENCE_H_
