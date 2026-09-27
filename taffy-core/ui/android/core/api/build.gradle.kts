// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import com.taffygo.buildlogic.mountGeneratedKotlin

plugins {
    id("taffygo.android.library")
}

android {
    namespace = "com.taffygo.browser.ui.core.api"
}

mountGeneratedKotlin("taffy-core/contracts/core-api/generated/kotlin")

dependencies {
    api(libs.kotlinx.coroutines.core)
}
