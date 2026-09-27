// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.annotation.PluralsRes
import androidx.compose.runtime.Composable
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.res.pluralStringResource

/**
 * Read a counted string.
 *
 * A count followed by a noun is a plural, not a format argument: English has
 * two forms and other languages have up to six, and a string that hard-codes
 * one of them cannot be translated. Every counted phrase in the UI layer goes
 * through here, and the string check refuses a plain string that ends up
 * looking like one.
 */
@Composable
@ReadOnlyComposable
fun taffyPlural(@PluralsRes id: Int, count: Int, vararg formatArgs: Any): String {
    val template = LocalStringTransformer.current.transform(pluralStringResource(id, count))
    if (formatArgs.isEmpty()) return template
    val locale = LocalConfiguration.current.locales[0]
    return String.format(locale, template, *formatArgs)
}
