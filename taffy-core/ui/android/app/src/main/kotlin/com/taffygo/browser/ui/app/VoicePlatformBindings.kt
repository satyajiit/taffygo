// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.ui.ReadAloud
import com.taffygo.browser.ui.core.ui.VoiceInput
import dagger.Binds
import dagger.Module

/** Trusted Android implementations behind feature-owned speech interfaces. */
@Module
interface VoicePlatformBindings {
    @Binds
    fun voiceInput(adapter: AndroidVoiceInputAdapter): VoiceInput

    @Binds
    fun readAloud(adapter: AndroidReadAloudAdapter): ReadAloud
}
