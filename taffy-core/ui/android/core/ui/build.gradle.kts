// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

plugins {
    id("taffygo.android.library")
    id("taffygo.android.compose")
    id("taffygo.dagger")
}

android {
    namespace = "com.taffygo.browser.ui.core.ui"
}

dependencies {
    implementation(libs.androidx.core.ktx)
    // `TaffyScreen` routes the system back gesture to the same `onBack` it draws
    // the up control from, so one screen has one meaning of back. That needs the
    // activity's back dispatcher, and this is the module that owns the frame.
    implementation(libs.androidx.activity.compose)
}
