// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

import android.graphics.Bitmap

/**
 * What the engine has drawn for one tab: a favicon and a page thumbnail.
 *
 * Lives here rather than on [com.taffygo.browser.ui.core.model.Tab] because
 * `:core:model` is the pure-JVM module and must not name `Bitmap`. A separate
 * flow also keeps a favicon byte changing from rebuilding every tab's
 * metadata. Chrome reads these from the local engine cache, never from the
 * network.
 */
data class TabArtwork(
    /** The site's own mark, from the engine's favicon cache. */
    val favicon: Bitmap? = null,
    /**
     * A snapshot of the page, or null when none has been captured yet.
     *
     * A missing thumbnail is drawn as a silent slab. There is no "preview
     * unavailable" caption: that sentence claimed a source that was absent,
     * and a slab that says nothing is the honest placeholder.
     */
    val thumbnail: Bitmap? = null,
)
