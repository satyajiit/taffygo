// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_RENDERER_CALL_DEADLINE_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_RENDERER_CALL_DEADLINE_H_

#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/ref_counted.h"
#include "base/time/time.h"
#include "base/timer/timer.h"

// A bounded wait for one renderer reply (protocol section 15).
//
// "A renderer timeout produces a bounded failure; it does not stall UI or
// policy cancellation." A mojo reply callback on its own gives no such bound:
// a renderer that is wedged, spinning, or simply hostile never calls it, the
// request never settles, and the public API's promise of exactly one terminal
// result per request quietly becomes zero.
//
// This class is the bound. Arming it starts a timer; whichever of the reply
// and the timer arrives first claims the call, and the other is dropped and
// counted. There is no window in which both run and none in which neither
// does, which is what makes "exactly one terminal result" hold against a
// renderer that does not cooperate.
//
// Deliberately not a general-purpose timeout helper. It is ref-counted so the
// reply callback can keep it alive without the caller having to reason about
// which of the two outlives the other, and it holds nothing but a flag and a
// timer so that keeping it alive costs nothing worth avoiding.
//
// **The caller must hold a reference for as long as the request is pending.**
// The timer is a member and holds no reference back, so if the only remaining
// reference were the one inside the reply callback, a pipe disconnect that
// destroyed that callback would take the timer with it and the request would
// never settle. Storing the guard beside the pending request — which is where
// the terminal result is produced anyway — is what closes that hole.
//
// UI thread only.

namespace taffy {

class RendererCallDeadline : public base::RefCounted<RendererCallDeadline> {
 public:
  // Starts the timer. `on_timeout` runs if and only if nothing claimed the
  // call first.
  static scoped_refptr<RendererCallDeadline> Arm(base::TimeDelta deadline,
                                                 base::OnceClosure on_timeout);

  RendererCallDeadline(const RendererCallDeadline&) = delete;
  RendererCallDeadline& operator=(const RendererCallDeadline&) = delete;

  // True exactly once, for the first caller. Every later call returns false,
  // including the timer's.
  bool Claim();

  bool claimed() const { return claimed_; }

 private:
  friend class base::RefCounted<RendererCallDeadline>;

  RendererCallDeadline();
  ~RendererCallDeadline();

  void OnDeadline();

  bool claimed_ = false;
  base::OnceClosure on_timeout_;
  base::OneShotTimer timer_;
};

// Wraps a reply callback so it runs only if it claims the call first. The
// wrapper holds a reference to the guard, so the guard lives exactly as long
// as either outcome is still possible.
//
// Use this rather than calling Claim() by hand wherever the reply handler has
// nothing else to do about the race: forgetting the Claim() call is a silent
// double-settle, and there is no way to forget this one.
template <typename... Args>
base::OnceCallback<void(Args...)> BindReplyWithDeadline(
    scoped_refptr<RendererCallDeadline> guard,
    base::OnceCallback<void(Args...)> on_reply) {
  return base::BindOnce(
      [](scoped_refptr<RendererCallDeadline> guard,
         base::OnceCallback<void(Args...)> on_reply, Args... args) {
        if (!guard->Claim()) {
          // The deadline already settled this request. A late reply is dropped
          // and counted, never delivered (protocol section 6.3).
          return;
        }
        std::move(on_reply).Run(std::forward<Args>(args)...);
      },
      std::move(guard), std::move(on_reply));
}

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_RENDERER_CALL_DEADLINE_H_
