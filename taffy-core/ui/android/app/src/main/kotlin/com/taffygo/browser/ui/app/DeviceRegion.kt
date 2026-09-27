// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import android.content.Context
import android.content.res.Resources
import android.telephony.TelephonyManager
import com.taffygo.browser.ui.core.model.LanguageRegionPolicy
import com.taffygo.browser.ui.core.preferences.DEFAULT_REGION_CODE
import java.util.Locale

/**
 * Which country this device is in, without asking for a single permission.
 *
 * A first-run profile has no stored region, and until this existed the answer
 * was the literal `"IN"` — correct for the first market and wrong everywhere
 * else, including for the people most likely to notice, who are the ones the
 * default is furthest from. Nothing here needs `ACCESS_COARSE_LOCATION` or
 * `READ_PHONE_STATE`: the network and SIM country codes are readable by any
 * application, and the two locales below need nothing at all.
 *
 * The order is by how much each signal knows. The network the handset is
 * attached to is where the person is right now; the SIM is where their account
 * was opened; the format locale is what they told the operating system they
 * want numbers and dates to look like; the system locale is the last resort
 * before the fixed default. Each is consulted only if the one before it gave
 * nothing usable — and "usable" means present in TaffyGo's own catalog, so a
 * device reporting a country the product does not carry falls through rather
 * than pinning the profile to a region with no content.
 *
 * `TelephonyManager` can throw `SecurityException` on a device whose vendor has
 * locked a getter down further than the platform does, so each read is guarded
 * separately: a device that refuses the network code may still answer for the
 * SIM, and losing both should cost the locale signals nothing.
 */
internal fun detectedRegionCode(context: Context): String {
    val telephony = context.getSystemService(TelephonyManager::class.java)
    val network = try {
        telephony?.networkCountryIso
    } catch (_: SecurityException) {
        null
    }
    val sim = try {
        telephony?.simCountryIso
    } catch (_: SecurityException) {
        null
    }
    return firstSupportedRegion(
        network,
        sim,
        Locale.getDefault(Locale.Category.FORMAT).country,
        Resources.getSystem().configuration.locales.get(0).country,
    )
}

/**
 * The first candidate TaffyGo actually supports, or the fixed default.
 *
 * Separate from [detectedRegionCode] so the ordering rule — the part with the
 * judgement in it — can be tested on a host, without a device, a SIM or a
 * `TelephonyManager`.
 */
internal fun firstSupportedRegion(vararg candidates: String?): String =
    candidates.firstNotNullOfOrNull { candidate ->
        candidate?.let(LanguageRegionPolicy::normalizedRegionCode)
    } ?: DEFAULT_REGION_CODE
