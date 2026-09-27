// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.assets

/**
 * The one member path a country-flag pack uses, and the identity of the pack.
 *
 * The prefix and suffix are the ones `flag-icons/tools/manifest.json` writes.
 * A code that is not two ASCII letters is not a path: the pack has no such
 * member, and inventing one would be a read that cannot succeed.
 */
object CountryFlagMember {
    const val ASSET_ID: String = "country-flags"

    fun pathFor(code: String): String? {
        val iso = code.trim().lowercase()
        if (iso.length != 2 || iso.any { it !in 'a'..'z' }) {
            return null
        }
        return "flags/$iso.webp"
    }
}
