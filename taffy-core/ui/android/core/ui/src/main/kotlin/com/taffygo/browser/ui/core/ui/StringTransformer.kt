// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import com.taffygo.browser.ui.core.designsystem.PseudoLocalizer

/**
 * What happens to a string between the resource file and the screen.
 *
 * There is normally nothing to do. The pseudo-localization variant of parity
 * row PAR-L10N-001 is the reason the seam exists: it turns every screen into a
 * check for text that never left the source and for layouts that only fit
 * English, without rebuilding or changing the device language.
 */
fun interface StringTransformer {

    /** The string as it will be shown. */
    fun transform(source: String): String

    companion object {
        /** The normal case: the resource, unchanged. */
        val Identity: StringTransformer = StringTransformer { it }

        /** The pseudo-localizing case, which the Appearance screen turns on. */
        val PseudoLocalizing: StringTransformer = StringTransformer(PseudoLocalizer::transform)
    }
}
