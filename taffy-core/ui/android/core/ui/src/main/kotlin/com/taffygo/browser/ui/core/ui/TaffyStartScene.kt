// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

/**
 * One painted plate: which part of the day it belongs to, and which of that
 * part's set it is.
 *
 * [StartSceneMember] is where one is chosen and where its pack member is named;
 * this is only the pair, so a preview or a test can state a plate outright
 * instead of arranging for a clock to produce it.
 */
data class TaffyStartScene(val part: TaffyDayPart, val variant: Int)
