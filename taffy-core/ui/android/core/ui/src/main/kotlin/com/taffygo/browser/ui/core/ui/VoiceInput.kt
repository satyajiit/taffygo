// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

/**
 * Person-started speech recognition for one visible editor.
 *
 * This is the seam the feature layer knows. It carries no Android type and it
 * deliberately has no page, task, provider, or model parameter: recognizing
 * speech cannot send page context or start work. The adapter may use the
 * device's speech service only after [listen] is called, and every callback is
 * scoped to the returned [VoiceInputSession]. Events are serialized onto the
 * UI thread that called [listen]. Closing that session must stop microphone
 * use and suppress later callbacks. [VoiceInputEvent.Ready] and
 * [VoiceInputEvent.Failed] are terminal; an implementation emits at most one.
 */
interface VoiceInput {
    fun listen(onEvent: (VoiceInputEvent) -> Unit): VoiceInputSession
}
