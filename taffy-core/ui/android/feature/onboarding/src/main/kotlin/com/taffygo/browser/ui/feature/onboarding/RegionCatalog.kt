// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import java.text.Collator
import java.util.Locale

/** One country supplied by the platform's ISO region catalog. */
internal data class RegionOption(
    val code: String,
    val name: String,
)

/** The complete platform country list, localized and alphabetized for reading. */
internal fun regionOptions(locale: Locale): List<RegionOption> {
    val collator = Collator.getInstance(locale)
    return Locale.getISOCountries()
        .map { code -> RegionOption(code, regionDisplayName(code, locale)) }
        .filter { it.name.isNotBlank() }
        .sortedWith { left, right -> collator.compare(left.name, right.name) }
}

/** A localized country name with the stable ISO code as a safe fallback. */
internal fun regionDisplayName(code: String, locale: Locale): String {
    val country = Locale.Builder().setRegion(code).build().getDisplayCountry(locale)
    return country.ifBlank { code }
}
