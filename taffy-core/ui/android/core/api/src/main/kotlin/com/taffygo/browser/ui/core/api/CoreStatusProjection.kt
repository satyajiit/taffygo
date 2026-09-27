// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.api

import taffy.core_api.CoreAvailability
import taffy.core_api.CoreStatus
import taffy.core_api.CoreStatusProjectionMode

/** Whether every bulk family in this ready status is present and actionable. */
fun CoreStatus.hasCompleteProjection(): Boolean =
    availability == CoreAvailability.READY &&
        projection_mode == CoreStatusProjectionMode.COMPLETE
