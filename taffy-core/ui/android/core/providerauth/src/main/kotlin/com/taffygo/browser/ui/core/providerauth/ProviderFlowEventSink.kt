// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.providerauth

/** Where the platform shell delivers browser-reported sign-in events. */
interface ProviderFlowEventSink {
    /** Folds one event into the visible sign-in state. Never blocks. */
    fun onFlowEvent(event: ProviderFlowEvent)
}
