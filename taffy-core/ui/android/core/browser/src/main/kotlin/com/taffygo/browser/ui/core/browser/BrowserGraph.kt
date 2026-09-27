// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

import com.taffygo.browser.ui.core.browser.internal.NoPageSurface

/** Stateless browser UI defaults; profile ports are supplied by Chromium. */
object BrowserGraph {
    /**
     * The page surface that is not live.
     *
     * `NoPageSurface` is `internal`, which is what stops a screen reaching past
     * [PageSurface] into it. But `:core:ui` has to name *some* instance as the default
     * of its composition local, and a `staticCompositionLocalOf` default cannot
     * throw here: a surface has no page host before the browser attaches one.
     * So the instance is published through
     * this one function while its type stays hidden, which gives the default a
     * value without widening the seam.
     */
    fun pageSurface(): PageSurface = NoPageSurface
}
