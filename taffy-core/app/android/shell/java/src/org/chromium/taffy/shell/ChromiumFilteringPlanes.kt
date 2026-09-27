// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import java.io.Closeable
import org.chromium.chrome.browser.tab.Tab
import org.chromium.chrome.browser.tabmodel.IncognitoTabModel
import org.chromium.chrome.browser.tabmodel.IncognitoTabModelObserver
import org.chromium.chrome.browser.tabmodel.TabModelSelector
import org.chromium.taffy.browser.TaffyFilteringBridge

/**
 * The layer that binds Chromium's filtering bridge to the projection above it.
 *
 * One plane per profile, opened on first use rather than in the constructor,
 * because neither model's profile is promised to exist when the mediator is
 * built. `getModel(true).profile` is safe to ask for eagerly: it delegates
 * without an `ensure` and answers null on an empty model, so asking never
 * brings a private profile into existence.
 *
 * This is the class that is allowed to name [TaffyFilteringBridge] and a [Tab];
 * [ChromiumFilteringProjection] holds no Chromium type at all, and that split
 * is what lets a plain JUnit test drive its routing. It lives beside the
 * mediator rather than inside it because a mediator that also knows how to open
 * a profile's bridge is two responsibilities in one file.
 *
 * The private plane is closed when the last private tab goes.
 * `didBecomeEmpty` fires before the model destroys its profile, which is the
 * window in which the bridge can still be shut cleanly. Everything the plane
 * held — every site a person allowed while private — goes with it, which is the
 * promise the mode makes (decision 0128).
 */
internal class ChromiumFilteringPlanes(
    private val selector: TabModelSelector,
    private val onChanged: () -> Unit,
    private val onPrivateTabsEmptied: () -> Unit,
) : Closeable {

    /** The projected planes, which is all anything above this layer sees. */
    val projection = ChromiumFilteringProjection<Tab>(
        isPrivate = { tab -> tab.isOffTheRecord },
    ) { plane -> openSeam(plane) }

    private val privateTabsObserver = object : IncognitoTabModelObserver {
        override fun didBecomeEmpty() {
            projection.closePrivatePlane()
            onPrivateTabsEmptied()
        }
    }

    private var closed = false

    init {
        privateTabModel()?.addIncognitoObserver(privateTabsObserver)
    }

    override fun close() {
        if (closed) return
        closed = true
        privateTabModel()?.removeIncognitoObserver(privateTabsObserver)
        projection.close()
    }

    private fun privateTabModel(): IncognitoTabModel? =
        selector.getModel(/* incognito= */ true) as? IncognitoTabModel

    private fun openSeam(
        plane: ChromiumFilteringProjection.Plane,
    ): ChromiumFilteringProjection.Seam<Tab>? {
        val wantsPrivate = plane == ChromiumFilteringProjection.Plane.PRIVATE
        val profile = selector.getModel(wantsPrivate).profile ?: return null
        val bridge = TaffyFilteringBridge.open(profile) { onChanged() }
        return ChromiumFilteringProjection.Seam(
            isEnabled = { bridge.isEnabled },
            setEnabled = bridge::setEnabled,
            setSiteException = bridge::setSiteException,
            siteExceptions = bridge::siteExceptions,
            postureRevision = bridge::postureRevision,
            blockedTotal = bridge::blockedTotal,
            blockedThisWeek = bridge::blockedThisWeek,
            minimumSitesThisWeek = bridge::minimumSitesThisWeek,
            isActiveFor = bridge::isActiveFor,
            isExceptedFor = bridge::isExceptedFor,
            blockedCountFor = bridge::blockedCountFor,
            flushCountFor = bridge::flushCountFor,
            close = bridge::close,
        )
    }
}
