// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_BROWSER_EFFECT_SOURCE_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_BROWSER_EFFECT_SOURCE_H_

#include "base/observer_list_types.h"
#include "taffy/components/intelligence/content/postcondition_evidence.h"

// Browser-owned effects that //taffy cannot observe for itself.
//
// A download starting, a file chooser opening, a permission prompt appearing
// and an external intent being offered are all browser-process facts, and all
// of them live above //components in the layering: the download system, the
// permission system and the Android intent surface are the browser layer's.
// //taffy must not reach up to them, so the direction is inverted —
// the layer that owns those surfaces reports into this interface, and the
// verifier observes it.
//
// The alternative would have been to let the renderer say a download started.
// That is precisely the class of claim protocol section 11.6 refuses: a
// renderer acknowledgement is DISPATCHED and nothing more, so a browser flow
// with no browser-owned observer is a postcondition that cannot be verified at
// all — and the honest handling of one is to deny the action at authorization
// time. The production download witness is installed; the other browser-flow
// kinds stay closed until they gain an equally attributable witness.
//
// UI thread only.

namespace taffy {

class BrowserEffectObserver : public base::CheckedObserver {
 public:
  // A browser flow the assistant's action was expected to start has started.
  virtual void OnBrowserFlowStarted(const BrowserFlowEvidence& evidence) {}
};

class BrowserEffectSource {
 public:
  virtual ~BrowserEffectSource() = default;

  // Named AddObserver and RemoveObserver rather than something more specific
  // so that base::ScopedObservation's default traits apply: an observation
  // that unregisters itself in its destructor is one nobody has to remember to
  // unregister.
  virtual void AddObserver(BrowserEffectObserver* observer) = 0;
  virtual void RemoveObserver(BrowserEffectObserver* observer) = 0;

  // Opens the attribution window for one action about to be dispatched, and
  // returns the watermark that names it.
  //
  // Called immediately before the dispatch, from the same place the dispatch
  // revision is stamped, and for the same reason: a flow that had already
  // begun is not this action's effect. Everything the source can bind to this
  // dispatch is stamped with the returned watermark; everything else is
  // reported with none, which is what the check refuses to settle on.
  //
  // The source mints it rather than receiving one, because the source is the
  // only object that knows what its window contains — see DispatchWatermark.
  virtual DispatchWatermark NoteDispatch() = 0;

  // Closes exactly the window `NoteDispatch()` returned. A stale close is a
  // no-op: a later dispatch may already have replaced the caller's window,
  // and finishing the earlier action must not withdraw the later action's
  // attribution. The dispatcher calls this on every terminal path so a page
  // cannot remain attributed to an action after that action has settled.
  virtual void CloseDispatch(DispatchWatermark watermark) = 0;
};

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_BROWSER_EFFECT_SOURCE_H_
