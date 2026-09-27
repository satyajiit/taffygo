// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import java.security.MessageDigest
import java.util.Base64
import org.chromium.chrome.browser.profiles.Profile
import org.chromium.chrome.browser.profiles.ProfileResolver

/** Derives one opaque, profile-kind-partitioned identity without exposing Chromium's token. */
internal fun opaqueProfileToken(profile: Profile, expectPrivate: Boolean): String {
    check(profile.isOffTheRecord == expectPrivate) {
        if (expectPrivate) {
            "A private graph requires an off-the-record Chromium profile"
        } else {
            "A regular graph cannot be created for an off-the-record Chromium profile"
        }
    }
    val raw = ProfileResolver().tokenize(profile)
    require(raw.isNotEmpty()) { "Chromium returned an empty profile token" }
    val partitionedToken = buildString {
        append(raw)
        append(if (expectPrivate) ":private" else ":regular")
    }
    val bytes = partitionedToken.encodeToByteArray()
    val digest = try {
        MessageDigest.getInstance("SHA-256").digest(bytes)
    } finally {
        bytes.fill(0)
    }
    return Base64.getUrlEncoder().withoutPadding().encodeToString(digest)
        .also { digest.fill(0) }
}
