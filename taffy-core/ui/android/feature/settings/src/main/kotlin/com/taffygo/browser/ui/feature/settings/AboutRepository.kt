// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/**
 * The two build facts screen SCR-408 states.
 *
 * It carries no credits reader. The third-party notices are published on the
 * website rather than indexed out of the package at run time (decision 0127),
 * so this seam reads two version strings and holds nothing open.
 */
interface AboutRepository {
    val facts: Facts

    data class Facts(
        val versionName: String?,
        val chromiumVersion: String?,
    )
}

internal open class UnavailableAboutRepository : AboutRepository {
    override val facts = AboutRepository.Facts(
        versionName = null,
        chromiumVersion = null,
    )
}

internal class EmptyAboutRepository : UnavailableAboutRepository()
