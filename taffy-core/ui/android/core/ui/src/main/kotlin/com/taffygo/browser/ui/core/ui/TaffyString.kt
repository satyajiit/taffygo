// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.annotation.StringRes
import androidx.compose.runtime.Composable
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.res.stringResource

/**
 * Read a string, the only way the UI layer reads one.
 *
 * Nothing in a Taffy-owned surface writes a literal: `checkStringResources`
 * fails the build over one. Going through here rather than through
 * `stringResource` directly is what makes the pseudo-localization variant
 * possible, and it formats the arguments after the transformation so a
 * positional argument survives it.
 */
@Composable
@ReadOnlyComposable
fun taffyString(@StringRes id: Int, vararg formatArgs: Any): String {
    val template = LocalStringTransformer.current.transform(stringResource(id))
    if (formatArgs.isEmpty()) return template
    // The locale is read from the composition rather than from the platform, so
    // a locale change recomposes the text instead of leaving it stale.
    val locale = LocalConfiguration.current.locales[0]
    return String.format(locale, template, *formatArgs)
}

/** The transformation in scope. Identity unless the UI layer turns one on. */
val LocalStringTransformer = staticCompositionLocalOf { StringTransformer.Identity }
