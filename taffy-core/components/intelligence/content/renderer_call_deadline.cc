// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/renderer_call_deadline.h"

#include "base/check.h"

namespace taffy {

RendererCallDeadline::RendererCallDeadline() = default;

RendererCallDeadline::~RendererCallDeadline() = default;

// static
scoped_refptr<RendererCallDeadline> RendererCallDeadline::Arm(
    base::TimeDelta deadline,
    base::OnceClosure on_timeout) {
  CHECK(deadline.is_positive());
  auto guard = base::WrapRefCounted(new RendererCallDeadline());
  guard->on_timeout_ = std::move(on_timeout);
  // The timer holds a raw `this`, and that is safe precisely because the timer
  // is a member: it is stopped by ~RendererCallDeadline before the object goes
  // away. A reference here would keep the guard alive forever.
  guard->timer_.Start(FROM_HERE, deadline,
                      base::BindOnce(&RendererCallDeadline::OnDeadline,
                                     base::Unretained(guard.get())));
  return guard;
}

bool RendererCallDeadline::Claim() {
  if (claimed_) {
    return false;
  }
  claimed_ = true;
  timer_.Stop();
  on_timeout_.Reset();
  return true;
}

void RendererCallDeadline::OnDeadline() {
  if (claimed_) {
    return;
  }
  claimed_ = true;
  base::OnceClosure on_timeout = std::move(on_timeout_);
  if (on_timeout) {
    std::move(on_timeout).Run();
  }
}

}  // namespace taffy
