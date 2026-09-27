// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_TAFFY_BROWSER_EFFECT_SOURCE_H_
#define TAFFY_BROWSER_TAFFY_BROWSER_EFFECT_SOURCE_H_

#include <stdint.h>

#include <map>
#include <optional>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/observer_list.h"
#include "base/scoped_multi_source_observation.h"
#include "base/scoped_observation.h"
#include "components/download/public/common/download_item.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/web_contents_observer.h"
#include "taffy/common/public/taffy_download_facts.h"
#include "taffy/components/intelligence/content/browser_effect_source.h"
#include "taffy/components/intelligence/content/postcondition_evidence.h"

namespace content {
class WebContents;
}  // namespace content

// The witness that a download happened — the browser-owned half of
// `BrowserEffectSource`, and the reason `kBrowserFlowStarted` can be settled
// by something other than a renderer saying so.
//
// browser_effect_source.h states the direction: the download system lives
// above //taffy in the layering, so //taffy does not reach up to it — the
// browser layer reports down into the interface and the verifier observes it.
// This class is that report for exactly one of the four browser flows. A file
// chooser, a permission prompt and an external intent still have no witness,
// so an action declaring one is still refused at admission rather than
// verified here.
//
// # What it emits, and the two things it refuses to emit
//
// The evidence is `DownloadFlowFacts`: an identifier, a top-level media type,
// a byte count, a directory **class**, a lifecycle state and the dispatch the
// transfer is bound to. That header carries the argument for each absence; the
// one worth repeating where a maintainer will be editing is the file name.
//
// **A file name is not metadata. It is page-authored text.** The server writes
// `Content-Disposition: attachment; filename="..."` and the page writes
// `<a download="...">`, so the name a download ends up with is chosen by the
// same party the whole protocol treats as hostile. It reads as harmless — it
// is short, it is displayed in the browser's own UI, it looks like a fact
// about a file rather than a claim by a stranger — and that is precisely why
// it is the field somebody will add. Emitting it would carry attacker-chosen
// text into the verifier's evidence, and from there into an audit record and
// into whatever the assistant is told about what it did. The person sees the
// name because Chromium shows it to them; nothing in the AI data plane needs
// it, and no postcondition is checked against it.
//
// The URL is refused for the same reason and with less argument required: a
// query string is where credentials and one-time tokens live.
//
// **The evidence carries no string at all**, which is the second half of that
// rule rather than a separate one. It used to carry the reported media type,
// filtered to `type/subtype` with both halves RFC 9110 tokens so that
// `application/octet-stream; name="statement.pdf"` contributed nothing. That
// filter is a grammar, and a file name with no space in it is a token, so
// `application/TaffyCanaryQ3-statement.pdf` walked straight through the field
// that existed to refuse exactly that. Decision 0062 section 3 closes it: what
// is carried is the **top-level type**, an enumeration over the IANA registry,
// and the subtype is read only to prove the header parsed and then discarded.
// The absence is structural — there is no member of any string type — so the
// next attempt to smuggle text has no field to smuggle it through.
//
// The target path exists in exactly one place in this class — the argument to
// the directory classifier — and it leaves as an enumeration. There is no
// member that can hold one.
//
// # Scope, and why scope is not attribution
//
// A `content::DownloadManager` is per profile and a `PostconditionVerifier` is
// per tab, so the witness is constructed against the tab whose actions it
// corroborates and reports nothing else. There is no way to build an unscoped
// one.
//
// That is necessary and it is nowhere near sufficient. A tab plus a few
// seconds is a coincidence a person produces by browsing: they click a link in
// the same tab shortly after the assistant clicked something, a download
// starts, and every scope rule this class has says yes. The verifier would
// then record a kVerified the action did not cause — a false entry in an audit
// trail, which is worse than no entry, because a missing record is read as
// missing and a wrong one is read as true.
//
// So evidence is **bound**, not merely scoped. `NoteDispatch` opens a window
// and returns the watermark naming it; a transfer is stamped with that
// watermark at the moment this witness first sees it, and with nothing at all
// otherwise. Three things close the window:
//
//   * **the person touching this tab.** `DidGetUserInteraction` is browser
//     owned: it fires from Chromium's input pipeline for a mouse-down, a
//     touch-start, a key-down or a scroll that actually reached this
//     WebContents. An assistant action does not go through that pipeline at
//     all — decision 0059 sends every write through the accessibility path,
//     which is a mojo call out of `AccessibilityPerformAction` straight into
//     the renderer — so this callback is the browser saying "a human did
//     that", and it is the one signal that separates the two causes rather
//     than merely timing them.
//   * **the next dispatch.** One window is open at a time and a new watermark
//     replaces the old one, so evidence can never be bound to a dispatch that
//     has already been superseded.
//   * **the action settling.** The dispatcher closes its exact watermark on
//     every terminal path. A tab cannot remain attributed to finished work,
//     and a late close for an older action cannot close a newer action's
//     window.
//
// Everything outside an open window is reported as unattributed and the check
// leaves it pending. The error this arrangement makes is refusing to credit an
// action that really did start a download — a person who scrolls while the
// transfer begins costs the action its corroboration. That direction is the
// one to be wrong in.
//
// This is weaker than proof and the difference is worth stating twice, once
// per way it fails.
//
// A page that starts a download by itself, inside the window, with nobody
// touching anything, is indistinguishable from one the action caused, and this
// class will bind it.
//
// And the person's own click is ruled out only for as long as the window it
// closed stays closed. A click does not produce a `DownloadItem`; the response
// head does, a network round trip later, and the item carries no memory of
// what started it —
// `download::DownloadResponseHandler::CreateDownloadCreateInfo` stamps its
// start time with `base::Time::Now()` when the response arrives. So a person's
// transfer that is still in flight when the next action is dispatched is first
// seen inside the *new* window and is bound to it, and the assistant is
// credited with a download the person started. Nothing available at this layer
// separates those two: an accessibility-path click and a human click produce
// the same navigation, with the same user-gesture bit, and a download begun by
// script produces no navigation at all. It is recorded in decision 0062
// section 6 and belongs to [Open (OD-056)] with the rest of the installation
// review, not to this class.
//
// So what "attributed" means here is exactly this and no more: **the browser
// delivered no input event to this tab between the dispatch and the moment
// this witness first saw the transfer**. Read as "the person did not cause
// it", it is wrong in the case above.
//
// Shipping download authority does not rely on that inference. A navigation is
// classified when its `NavigationHandle` is created and is cancelled if its
// response becomes a download during a task action. A direct content-initiated
// request overlapping an open dispatch is refused. The dedicated
// `StartDownload` route is separate: it reaches Chromium only after its exact
// lease, capability and durable effect have been accepted, and returns the
// download system's own identity. The open-window evidence remains available
// to the proposed generic postcondition, but it is not treated as exact
// causation by a shipping task action.
//
// # Installation
//
// TaffyPageIntelligenceHost owns one instance per eligible tab and attaches it
// before that host can dispatch an action. It supplies content-free download
// observation and the conservative open-window refusal signal. Typed task
// download completion is returned by the profile-owned download adapter
// instead; other consequential classes remain closed independently by their
// action and capability gates.
//
// UI thread only.

namespace taffy {

class TaffyBrowserEffectSource : public BrowserEffectSource,
                                 public content::WebContentsObserver,
                                 public content::DownloadManager::Observer,
                                 public download::DownloadItem::Observer {
 public:
  using DirectoryResolver = base::RepeatingCallback<base::FilePath()>;

  // `action_scope` is the tab whose actions this witness corroborates.
  // Required: see "Scope" above.
  explicit TaffyBrowserEffectSource(content::WebContents* action_scope);
  TaffyBrowserEffectSource(const TaffyBrowserEffectSource&) = delete;
  TaffyBrowserEffectSource& operator=(const TaffyBrowserEffectSource&) = delete;
  ~TaffyBrowserEffectSource() override;

  // Starts watching one profile's download system, and stops watching whatever
  // it was watching before.
  //
  // This name hides `content::WebContentsObserver::Observe`, which is
  // protected and means something else entirely. Nothing here calls it: the
  // tab is fixed at construction and never changes, so the only thing this
  // class ever re-points is the download manager.
  //
  // "Stops watching" is the whole of it and it is not only the manager
  // observation. The per-item observations are on the old manager's items and
  // the remembered facts are keyed by an identifier the old manager allocated
  // — download identifiers are unique within a manager and not across two — so
  // carrying either across would leave this object subscribed to a download
  // system it no longer serves, and comparing the new manager's first item
  // against a stranger's remembered facts. The second failure is the quiet
  // one: a recycled identifier whose facts happen to match is simply not
  // emitted, and a verifier waits out its deadline for an event that was
  // suppressed.
  void Observe(content::DownloadManager* manager);

  // Teaches the witness that everything under `directory` belongs to
  // `directory_class`. The embedder knows where the default downloads
  // directory and the application-private directory are; this class must not
  // guess, and must not report a class it was never told about.
  //
  // Registration is how a path enters this object and an enumeration is the
  // only thing that comes back out. A target under no registered directory
  // classifies as kUndecided, which is the honest answer for "the download
  // system put it somewhere nobody described".
  void RegisterDirectoryClass(DownloadDestinationKind directory_class,
                              const base::FilePath& directory);

  // Registers a live, cached directory source owned by the embedder. The
  // callback is read while projecting a download so an already-open tab does
  // not keep classifying against an old profile preference. It must not do
  // filesystem I/O: download progress can cause this projection to run often.
  void RegisterDirectoryClassResolver(DownloadDestinationKind directory_class,
                                      DirectoryResolver resolver);

  // BrowserEffectSource:
  void AddObserver(BrowserEffectObserver* observer) override;
  void RemoveObserver(BrowserEffectObserver* observer) override;
  DispatchWatermark NoteDispatch() override;
  void CloseDispatch(DispatchWatermark watermark) override;

  // Whether a task action's dispatch window is open in this exact tab. This
  // carries no task, action, destination, or page content; browser policy uses
  // it only to refuse a consequential flow that lacks StartDownload authority.
  bool HasOpenDispatch() const;

  // content::WebContentsObserver:
  //
  // The person did something to this tab. See "Scope, and why scope is not
  // attribution": this is the browser's own statement that a human acted, and
  // it closes the open dispatch window because everything after it may be
  // theirs.
  void DidGetUserInteraction(const blink::WebInputEvent& event) override;

  // content::DownloadManager::Observer:
  //
  // `manager` is unused: the observation this class holds already names the
  // one manager it watches, so a second, unchecked handle to it would be a
  // way for a caller to be told about a profile this witness does not serve.
  void OnDownloadCreated(content::DownloadManager* manager,
                         download::DownloadItem* item) override;
  void ManagerGoingDown(content::DownloadManager* manager) override;

  // download::DownloadItem::Observer:
  void OnDownloadUpdated(download::DownloadItem* item) override;
  void OnDownloadDestroyed(download::DownloadItem* item) override;

 private:
  struct ClassifiedDirectory {
    DownloadDestinationKind directory_class =
        DownloadDestinationKind::kUndecided;
    base::FilePath directory;
    DirectoryResolver resolver;
  };

  // One transfer this witness is following.
  struct WatchedDownload {
    // The dispatch window that was open the first time this witness saw the
    // item, and absent when none was. Decided once and never revisited: a
    // transfer that began outside a window does not enter one by continuing to
    // run into it, which is what makes this the browser-flow analogue of
    // "strictly newer than the revision at dispatch".
    std::optional<DispatchWatermark> attributed_to;
    DownloadFlowFacts last_emitted;
  };

  // True when this item belongs to the tab this witness was built for.
  bool InScope(const download::DownloadItem* item) const;

  // The whole projection, and the only place a download::DownloadItem is read.
  DownloadFlowFacts Project(const download::DownloadItem& item) const;

  // The only reader of a target path anywhere in this class.
  DownloadDestinationKind ClassifyDirectory(
      const base::FilePath& target_path) const;

  // Emits when the projection has changed in a way an observer can act on.
  //
  // A byte count alone never qualifies. Progress ticks arrive continuously and
  // say nothing a verifier can settle a postcondition with, so what is
  // compared is the state, the directory class and the media type; the current
  // byte count rides along on whatever emission those cause.
  void EmitIfChanged(const download::DownloadItem& item);

  // Drops every subscription and every remembered fact about the download
  // system this witness was watching. The one place that knowledge is
  // released, so that a manager change and a manager shutdown cannot release
  // different halves of it.
  //
  // The open attribution window is deliberately **not** part of it. A window
  // is about this tab's person and this tab's actions; which profile's
  // download system is being watched has nothing to do with either, and
  // closing the window here would make an action silently unverifiable
  // whenever a manager changed underneath it.
  void StopWatching();

  std::vector<ClassifiedDirectory> directory_classes_;

  // Keyed by Chromium's download identifier, which is what the evidence
  // carries and what an observer can correlate on. Valid only for the manager
  // currently observed — see StopWatching.
  std::map<uint32_t, WatchedDownload> watched_;

  // The dispatch a transfer first seen right now would be bound to. Absent
  // whenever nothing has been dispatched yet, the person has acted since, or
  // the current action has reached a terminal result.
  std::optional<DispatchWatermark> open_dispatch_;

  // Monotonic and never reused, so a stale watermark held by a finished
  // verifier can never match a later dispatch's evidence.
  uint64_t last_watermark_ = 0;

  base::ObserverList<BrowserEffectObserver> observers_;

  base::ScopedObservation<content::DownloadManager,
                          content::DownloadManager::Observer>
      manager_observation_{this};

  base::ScopedMultiSourceObservation<download::DownloadItem,
                                     download::DownloadItem::Observer>
      item_observations_{this};
};

// The top-level type of `reported`, or kUnknown when it is not a media type
// this witness will speak about.
//
// Exposed so the rule can be read and tested as a rule rather than inferred
// from a download. The whole value must parse — `type/subtype` where both
// halves are RFC 9110 tokens — and only the type half survives. Requiring the
// subtype to parse and then dropping it is deliberate: a value the filter does
// not recognise is dropped, never repaired, so
// `application/octet-stream; name="statement.pdf"` is kUnknown rather than
// this code deciding what a hostile server meant by it.
MediaTopLevelType MediaTopLevelTypeOf(std::string_view reported);

}  // namespace taffy

#endif  // TAFFY_BROWSER_TAFFY_BROWSER_EFFECT_SOURCE_H_
