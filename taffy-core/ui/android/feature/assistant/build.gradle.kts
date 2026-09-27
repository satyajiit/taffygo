// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

plugins {
    id("taffygo.android.feature")
}

dependencies {
    implementation(libs.commonmark)
    implementation(libs.commonmark.tables)
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.core.ktx)
}

android {
    namespace = "com.taffygo.browser.ui.feature.assistant"
}
