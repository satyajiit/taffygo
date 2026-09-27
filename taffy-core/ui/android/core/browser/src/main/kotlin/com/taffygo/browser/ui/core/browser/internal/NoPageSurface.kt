// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser.internal

import android.content.Context
import android.view.View
import com.taffygo.browser.ui.core.browser.PageSurface

/**
 * No web engine, and no pretence of one.
 *
 * This is the default every composition starts with, so a surface that forgot
 * to be given a page host draws the placeholder rather than an empty rectangle
 * that reads as a page which failed to paint. It is also what the fork uses
 * before native initialization finishes, which is a real window of time rather
 * than a hypothetical one.
 *
 * [attach] returns `null` and that is not a failure path: [isLive] already said
 * so, and the contract on [PageSurface.attach] makes `null` the answer a
 * non-live surface gives.
 */
internal object NoPageSurface : PageSurface {

    override val isLive: Boolean = false

    override fun attach(context: Context): View? = null

    override fun detach(view: View) = Unit
}
