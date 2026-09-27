// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/task_navigation_refusal.h"

namespace taffy {

TaskNavigationRefusal::TaskNavigationRefusal(
    content::NavigationHandle& /*handle*/,
    ActionResultCode code)
    : code_(code) {}

TaskNavigationRefusal::~TaskNavigationRefusal() = default;

NAVIGATION_HANDLE_USER_DATA_KEY_IMPL(TaskNavigationRefusal);

}  // namespace taffy
