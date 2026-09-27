// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

/**
 * Person-started playback of already-visible assistant text.
 *
 * The interface accepts text and nothing else: an adapter cannot fetch a task,
 * page, model answer, or credential behind the caller's back. Implementations
 * must stop playback and suppress later callbacks when the returned session is
 * closed. Events are serialized onto the UI thread that called [speak]. Text is
 * process-resident and must not be written to a speech cache by the product.
 * [ReadAloudEvent.Finished] and [ReadAloudEvent.Failed] are terminal.
 */
interface ReadAloud {
    fun speak(text: String, onEvent: (ReadAloudEvent) -> Unit): ReadAloudSession
}
