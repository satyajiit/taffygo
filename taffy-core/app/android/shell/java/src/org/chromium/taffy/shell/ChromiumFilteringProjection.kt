// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import androidx.annotation.VisibleForTesting
import com.taffygo.browser.ui.core.browser.FilteringSettings
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * The filtering half of the browser seam: one plane per profile, projected.
 *
 * A plane is not there when the mediator is built — a profile arrives with
 * native initialization — so every command and every read has a "not open yet"
 * answer, and each of those answers is a decision rather than an accident. A
 * command before a plane exists is refused and reaches nothing; a fact before
 * it exists is the contract's own default, never a zero invented here.
 *
 * **There are two planes, and which one answers is decided by the tab.** A
 * private tab's allowances belong to the private profile's plane: held in its
 * preference overlay, never written to disk, and gone when the last private
 * tab closes (decision 0128). Every per-tab call routes through [planeOf], and
 * the profile-wide reads and the settings screen's own command go to
 * [Plane.REGULAR] explicitly, so a private tab selected behind a settings
 * screen cannot capture a write meant for the profile.
 *
 * Nothing Chromium-shaped is named in this file. [Seam] is the whole platform
 * surface, handed over as function references by the one layer allowed to name
 * `TaffyFilteringBridge` and a `Tab`; the tab is a type parameter because this
 * class never does anything with one but pass it back, and [Plane] is this
 * file's own type rather than a profile. That is what lets a plain JUnit test
 * drive every branch below, including the ones that only happen on a device:
 * an unopened plane, a posture edit, a count-only publication, a private tab
 * writing to its own plane, and a close followed by a reopen.
 *
 * Facts are pulled rather than pushed, which is the same shape
 * [ChromiumBrowserMediator] has for tabs and navigation: a plane's change
 * signal says only that *something* changed, and [refreshSettings] re-reads
 * whatever is projected.
 */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
class ChromiumFilteringProjection<T : Any>(
    private val isPrivate: (T) -> Boolean,
    private val openSeam: (Plane) -> Seam<T>?,
) {

    private val filteringState = MutableStateFlow(FilteringSettings())

    /**
     * The exception list is cached against the regular plane's revision only.
     *
     * Both services start at revision 1, so one cache shared between planes
     * would hand the private plane the regular plane's hosts. Giving the
     * private plane no cache is the stronger half of that: no code path in
     * this class ever reads a private plane's host list at all.
     */
    private val filteringPostureCache = FilteringPostureCache()
    private val planes = mutableMapOf<Plane, Seam<T>>()

    /** The projected posture, counts and exception hosts the surfaces read. */
    val settings: StateFlow<FilteringSettings> = filteringState.asStateFlow()

    /**
     * Opens the regular plane once there is something to open it for, and says
     * whether this call is the one that did.
     *
     * True means the caller now has facts it did not have a moment ago and
     * must republish; false covers both "already open" and "still nothing to
     * open", because neither changes what is projected. The private plane is
     * not opened here — it is opened by the first private tab that asks
     * something of it, and closed when the last one goes.
     */
    fun ensureFilteringBridge(): Boolean {
        if (planes.containsKey(Plane.REGULAR)) return false
        planes[Plane.REGULAR] = openSeam(Plane.REGULAR) ?: return false
        return true
    }

    /**
     * Rebuilds the projection from the regular plane, or publishes the
     * contract's defaults when there is none.
     *
     * Only the regular plane is projected. Screen SCR-206 is the regular
     * profile's list and says so; a private tab's allowances are never
     * published here, never counted here and never named here.
     *
     * The exception hosts go through [FilteringPostureCache] because they are
     * the expensive half: a coalesced count publication and a posture edit
     * arrive through the same signal, and only the second can have changed the
     * list.
     */
    fun refreshSettings() {
        filteringState.value = planes[Plane.REGULAR]?.let { seam ->
            FilteringSettings(
                enabled = seam.isEnabled(),
                exceptionHosts = filteringPostureCache.hostsFor(
                    seam.postureRevision(),
                    seam.siteExceptions,
                ),
                blockedTotal = seam.blockedTotal(),
                blockedThisWeek = seam.blockedThisWeek(),
                minimumSitesThisWeek = seam.minimumSitesThisWeek(),
            )
        } ?: FilteringSettings()
    }

    /**
     * Sets the master toggle.
     *
     * The regular plane holds the preference, and the private profile's
     * overlay reads through to it until something is written into that
     * overlay, so this one command reaches both while neither has forked.
     * Does nothing while the plane is shut.
     */
    fun setEnabled(enabled: Boolean) {
        planes[Plane.REGULAR]?.setEnabled?.invoke(enabled)
    }

    /**
     * Records or removes one site exception on the **regular** profile's list.
     *
     * This is screen SCR-206's command. It names its plane rather than
     * following the selected tab, because a private tab selected behind the
     * settings screen must not capture a write meant for the profile.
     *
     * False is the seam's own refusal — a host that is empty, over the
     * contract's bound, or carrying a scheme or a path — and it is also the
     * answer while no plane is open, because nothing was recorded either way.
     */
    fun setProfileSiteException(host: String, allow: Boolean): Boolean =
        planes[Plane.REGULAR]?.setSiteException?.invoke(host, allow) ?: false

    /**
     * Records or removes one site exception on the plane this tab belongs to.
     *
     * A private tab writes to the private plane, whose values live in that
     * profile's preference overlay: never on disk, and discarded with the
     * profile (decision 0128). One consequence to know rather than discover —
     * once the overlay holds the key, Chromium stops forwarding later
     * regular-profile changes to it, so a private session forks its list at
     * its first private edit. The fork dies with the profile.
     */
    fun setSiteException(tab: T, host: String, allow: Boolean): Boolean =
        planeOf(tab)?.setSiteException?.invoke(host, allow) ?: false

    /** Publishes whatever this tab's coalescer holds, before it is looked at. */
    fun flushCountFor(tab: T) {
        planeOf(tab)?.flushCountFor?.invoke(tab)
    }

    /** Whether blocking is acting on this tab's committed page right now. */
    fun isActiveFor(tab: T): Boolean = planeOf(tab)?.isActiveFor?.invoke(tab) ?: false

    /**
     * Whether a person has allowed this tab's committed site on this tab's own
     * plane, whatever the master toggle says. The host is never asked for or
     * returned; the browser reads the tab's own committed host and answers.
     */
    fun isExceptedFor(tab: T): Boolean = planeOf(tab)?.isExceptedFor?.invoke(tab) ?: false

    /** The blocked-request count of this tab's committed page. */
    fun blockedCountFor(tab: T): Int = planeOf(tab)?.blockedCountFor?.invoke(tab) ?: 0

    /**
     * Shuts the private plane and forgets it, without touching what is
     * published.
     *
     * Called when the last private tab closes — before the model destroys its
     * profile — because that is the moment the allowances are meant to be
     * gone. Nothing published describes the private plane, so nothing needs
     * rewriting on the way out.
     */
    fun closePrivatePlane() {
        planes.remove(Plane.PRIVATE)?.close?.invoke()
    }

    /**
     * Shuts every plane and forgets the posture it read, in that order.
     *
     * What was last published stays published. A teardown is not a filtering
     * posture, and rewriting the flow on the way out would hand every surface
     * still collecting it one final snapshot describing a plane that no longer
     * exists.
     */
    fun close() {
        val open = planes.values.toList()
        planes.clear()
        filteringPostureCache.clear()
        open.forEach { it.close() }
    }

    /**
     * The plane this tab belongs to, opened on first use.
     *
     * Null means there is nothing to open it for yet — a profile that native
     * initialization has not produced — which every caller above treats as the
     * contract's default rather than as a zero of its own.
     */
    private fun planeOf(tab: T): Seam<T>? {
        val plane = if (isPrivate(tab)) Plane.PRIVATE else Plane.REGULAR
        planes[plane]?.let { return it }
        val opened = openSeam(plane) ?: return null
        planes[plane] = opened
        return opened
    }

    /** Which profile's filtering plane. Never a profile, and never a tab. */
    enum class Plane {
        REGULAR,
        PRIVATE,
    }

    /**
     * Every platform call one opened filtering plane answers.
     *
     * A record of function references rather than an interface over the
     * browser's bridge: the references are bound to one already-opened plane,
     * so nothing here can be asked for a plane that is shut, and a test binds
     * them to counters in three lines.
     */
    @VisibleForTesting
    class Seam<T : Any>(
        val isEnabled: () -> Boolean,
        val setEnabled: (Boolean) -> Unit,
        val setSiteException: (String, Boolean) -> Boolean,
        val siteExceptions: () -> Array<String>,
        val postureRevision: () -> Long,
        val blockedTotal: () -> Long,
        val blockedThisWeek: () -> Long?,
        val minimumSitesThisWeek: () -> Int?,
        val isActiveFor: (T) -> Boolean,
        val isExceptedFor: (T) -> Boolean,
        val blockedCountFor: (T) -> Int,
        val flushCountFor: (T) -> Unit,
        val close: () -> Unit,
    )
}
