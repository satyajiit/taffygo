// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import com.taffygo.buildlogic.StringResourceTask

plugins {
    id("taffygo.android.application")
    id("taffygo.android.compose")
    id("taffygo.dagger")
}

android {
    namespace = "com.taffygo.browser.ui.app"

    sourceSets.named("main") {
        res.directories.add("src/backup/res")
        res.directories.add("src/taskContinuation/res")
    }

    defaultConfig {
        applicationId = "com.taffygo.browser"
    }

    // No signing identity is declared here, and that is the rule rather than
    // an omission (decision 0203). This module compiles the Compose projection
    // and assembles no browser (see AndroidManifest.xml), so what it produces
    // is a fast-loop build a person installs by hand; AGP's own debug key
    // signs it. The APK a person installs is GN's, and ./tools/chromium/build
    // resolves that identity from outside the repository.

}

tasks.named<StringResourceTask>("checkStringResources") {
    stringResources.from(
        fileTree("src/backup/res") { include("values/strings*.xml") },
        fileTree("src/taskContinuation/res") { include("values/strings*.xml") },
    )
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.core.splashscreen)
    implementation(libs.androidx.appcompat)
    implementation(libs.androidx.browser)
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.lifecycle.runtime.ktx)
    implementation(libs.androidx.credentials)
    implementation(libs.androidx.credentials.play.services)
    implementation(libs.googleid)
}
