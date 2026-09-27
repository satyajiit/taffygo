// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import androidx.annotation.VisibleForTesting

/** One immutable deletion plan, shared by the platform adapter and its tests. */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
data class ClearDataPlan(
    val chromiumTypes: List<Int>,
    val clearTimeOnSites: Boolean,
)
