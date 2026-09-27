// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/ai_runtime_availability.h"

#include "content/public/browser/browser_thread.h"

namespace taffy {

AiRuntimeAvailability::AiRuntimeAvailability() = default;
AiRuntimeAvailability::~AiRuntimeAvailability() = default;

// static
AiRuntimeAvailability& AiRuntimeAvailability::Get() {
  static base::NoDestructor<AiRuntimeAvailability> instance;
  return *instance;
}

void AiRuntimeAvailability::SetState(AiRuntimeState state) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  // Deliberately no observers, no notifications and no callbacks. Anything
  // that reacted to this value would be a thing manual browsing could end up
  // waiting on, and the parity row says manual browsing waits for nothing.
  state_ = state;
}

}  // namespace taffy
