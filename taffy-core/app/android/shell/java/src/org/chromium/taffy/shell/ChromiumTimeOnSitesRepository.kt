// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import android.app.Activity
import android.app.Application
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import androidx.annotation.VisibleForTesting
import com.taffygo.browser.ui.app.ProfilePreferenceNames
import com.taffygo.browser.ui.app.ProfilePreferenceStore
import com.taffygo.browser.ui.feature.settings.ClearBrowsingDataUiState
import com.taffygo.browser.ui.feature.settings.TimeOnSitesRepository
import com.taffygo.browser.ui.feature.settings.YouSurfaceAvailability
import java.io.Closeable
import java.time.Instant
import java.time.ZoneId
import java.util.Locale
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import org.chromium.base.ActivityState
import org.chromium.base.ApplicationStatus
import org.chromium.base.ThreadUtils
import org.chromium.chrome.browser.profiles.Profile
import org.chromium.chrome.browser.tab.Tab
import org.chromium.chrome.browser.tabmodel.TabModelSelector
import org.chromium.chrome.browser.tabmodel.TabModelSelectorTabModelObserver
import org.chromium.chrome.browser.tabmodel.TabModelSelectorTabObserver
import org.chromium.components.embedder_support.util.UrlUtilities
import org.chromium.taffy.browser.TaffyTaskSourceSelectionBridge
import org.chromium.url.GURL

/**
 * Profile-owned foreground dwell-time engine.
 *
 * Only a resumed product window contributes time, and every change re-checks
 * the exact selected regular tab and its browser-owned task provenance. The
 * persisted ledger contains registrable domains and time intervals only.
 */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
class ChromiumTimeOnSitesRepository(
    profile: Profile,
    private val preferences: ProfilePreferenceStore,
    private val wallClockMillis: () -> Long = System::currentTimeMillis,
    private val elapsedClockMillis: () -> Long = SystemClock::elapsedRealtime,
    private val zoneId: ZoneId = ZoneId.systemDefault(),
    private val handler: Handler = Handler(Looper.getMainLooper()),
) : TimeOnSitesRepository,
    Closeable {
    private val regularProfile = profile.also {
        require(!it.isOffTheRecord) {
            "Time on sites cannot persist against an off-the-record profile"
        }
    }
    private val ledger = TimeOnSitesLedger.decode(
        preferences.getString(ProfilePreferenceNames.TIME_ON_SITES),
        wallClockMillis(),
        zoneId,
    )
    private val state = MutableStateFlow(snapshotFromLedger())
    private val sessions = ForegroundTimeSessions(ledger, wallClockMillis, elapsedClockMillis)
    private val writer = CoalescedTimeOnSitesWriter(
        schedule = handler::postDelayed,
        cancel = handler::removeCallbacks,
        write = {
            preferences.putString(ProfilePreferenceNames.TIME_ON_SITES, ledger.encode())
        },
    )
    private var closed = false
    private val checkpoint = object : Runnable {
        override fun run() {
            if (closed) return
            finishAllActive(restart = true)
            persistAndPublish()
            scheduleCheckpoint()
        }
    }

    override val snapshot: StateFlow<TimeOnSitesRepository.Snapshot> = state.asStateFlow()

    init {
        if (ledger.wasRecoveredFromCorruption()) writer.request()
    }

    /** Attaches one browser window; the returned registration is window-owned. */
    fun attach(
        activity: Activity,
        selector: TabModelSelector,
        taskSources: TaffyTaskSourceSelectionBridge,
    ): Closeable {
        ThreadUtils.assertOnUiThread()
        check(!closed) { "A closed dwell-time engine cannot attach a window" }
        return WindowAttachment(activity, selector, taskSources)
    }

    /** Clears exactly the measured overlap with the selected Chromium range. */
    fun clear(range: ClearBrowsingDataUiState.Range): Boolean {
        ThreadUtils.assertOnUiThread()
        if (closed) return false
        finishAllActive(restart = true)
        val now = wallClockMillis()
        val cutoff = when (range) {
            ClearBrowsingDataUiState.Range.LAST_HOUR -> now - HOUR_MILLIS
            ClearBrowsingDataUiState.Range.LAST_DAY -> now - DAY_MILLIS
            ClearBrowsingDataUiState.Range.LAST_WEEK -> now - WEEK_MILLIS
            ClearBrowsingDataUiState.Range.ALL_TIME -> Long.MIN_VALUE
        }
        ledger.clearFrom(cutoff)
        persistAndPublish()
        return true
    }

    override fun close() {
        ThreadUtils.assertOnUiThread()
        if (closed) return
        if (finishAllActive(restart = false) == ForegroundTimeSessions.Transition.RECORDED) {
            writer.request()
        }
        closed = true
        handler.removeCallbacks(checkpoint)
        writer.close()
        state.value = TimeOnSitesRepository.Snapshot(
            availability = YouSurfaceAvailability.UNAVAILABLE,
            clearedWithHistory = true,
        )
    }

    private fun activate(token: Any, site: String?) {
        ThreadUtils.assertOnUiThread()
        if (closed) return
        val transition = sessions.activate(token, site)
        if (transition == ForegroundTimeSessions.Transition.UNCHANGED) return
        scheduleCheckpoint()
        if (transition == ForegroundTimeSessions.Transition.RECORDED) {
            publishAndRequestPersistence()
        }
    }

    private fun deactivate(token: Any) {
        ThreadUtils.assertOnUiThread()
        val transition = sessions.finish(token, restart = false)
        if (transition == ForegroundTimeSessions.Transition.UNCHANGED) return
        if (!sessions.hasActiveWindows) handler.removeCallbacks(checkpoint)
        if (transition == ForegroundTimeSessions.Transition.RECORDED) {
            publishLedger()
            writer.request()
            if (!sessions.hasActiveWindows) writer.flush()
        }
    }

    private fun finishAllActive(restart: Boolean): ForegroundTimeSessions.Transition {
        val transition = sessions.finishAll(restart)
        if (!sessions.hasActiveWindows) handler.removeCallbacks(checkpoint)
        return transition
    }

    private fun persistAndPublish() {
        publishLedger()
        writer.request()
        writer.flush()
    }

    private fun publishAndRequestPersistence() {
        publishLedger()
        writer.request()
    }

    private fun publishLedger() {
        val now = wallClockMillis()
        ledger.prune(now)
        state.value = snapshotFromLedger(now)
    }

    private fun snapshotFromLedger(now: Long = wallClockMillis()): TimeOnSitesRepository.Snapshot {
        val todayStart = Instant.ofEpochMilli(now)
            .atZone(zoneId)
            .toLocalDate()
            .atStartOfDay(zoneId)
            .toInstant()
            .toEpochMilli()
        val weekStart = Instant.ofEpochMilli(now)
            .atZone(zoneId)
            .toLocalDate()
            .minusDays(6)
            .atStartOfDay(zoneId)
            .toInstant()
            .toEpochMilli()
        val recent = ledger.aggregateRecent(todayStart, weekStart, now)
        return TimeOnSitesRepository.Snapshot(
            availability = YouSurfaceAvailability.READY,
            today = recent.today,
            week = recent.week,
            clearedWithHistory = true,
            hasGroupedSites = recent.today.any { it.grouped } || recent.week.any { it.grouped },
            recoveredFromCorruption = ledger.wasRecoveredFromCorruption(),
        )
    }

    private fun scheduleCheckpoint() {
        handler.removeCallbacks(checkpoint)
        if (sessions.hasActiveWindows && !closed) {
            handler.postDelayed(checkpoint, CHECKPOINT_MILLIS)
        }
    }

    private inner class WindowAttachment(
        private val owner: Activity,
        private val selector: TabModelSelector,
        private val taskSources: TaffyTaskSourceSelectionBridge,
    ) : Application.ActivityLifecycleCallbacks,
        ApplicationStatus.WindowFocusChangedListener,
        Closeable {
        private val token = Any()
        private var resumed = false
        private var focused = false
        private var attachmentClosed = false
        private val modelObserver = object : TabModelSelectorTabModelObserver(selector) {
            override fun didSelectTab(tab: Tab, type: Int, lastId: Int) = refresh()
            override fun tabRemoved(tab: Tab) = refresh()
            override fun didRemoveTabForClosure(tab: Tab) = refresh()
        }
        private val tabObserver = object : TabModelSelectorTabObserver(selector) {
            override fun onUrlUpdated(tab: Tab) {
                if (tab === selector.currentTab) refresh()
            }

            override fun onContentChanged(tab: Tab) {
                if (tab === selector.currentTab) refresh()
            }

            override fun onPageLoadFinished(tab: Tab, url: GURL) {
                if (tab === selector.currentTab) refresh()
            }

            override fun onCrash(tab: Tab) {
                if (tab === selector.currentTab) refresh()
            }
        }

        init {
            owner.application.registerActivityLifecycleCallbacks(this)
            ApplicationStatus.registerWindowFocusChangedListener(this)
            try {
                resumed = ApplicationStatus.getStateForActivity(owner) == ActivityState.RESUMED
                focused = owner.hasWindowFocus()
                refresh()
            } catch (failure: Throwable) {
                owner.application.unregisterActivityLifecycleCallbacks(this)
                ApplicationStatus.unregisterWindowFocusChangedListener(this)
                modelObserver.destroy()
                tabObserver.destroy()
                throw failure
            }
        }

        override fun onActivityResumed(activity: Activity) {
            if (activity !== owner || attachmentClosed) return
            resumed = true
            refresh()
        }

        override fun onActivityPaused(activity: Activity) {
            if (activity !== owner || attachmentClosed) return
            resumed = false
            deactivate(token)
        }

        override fun onWindowFocusChanged(activity: Activity, hasFocus: Boolean) {
            if (activity !== owner || attachmentClosed) return
            focused = hasFocus
            refresh()
        }

        override fun onActivityDestroyed(activity: Activity) {
            if (activity === owner) close()
        }

        override fun close() {
            if (attachmentClosed) return
            attachmentClosed = true
            resumed = false
            focused = false
            owner.application.unregisterActivityLifecycleCallbacks(this)
            ApplicationStatus.unregisterWindowFocusChangedListener(this)
            modelObserver.destroy()
            tabObserver.destroy()
            deactivate(token)
        }

        private fun refresh() {
            if (attachmentClosed || !resumed || !focused) {
                deactivate(token)
                return
            }
            activate(token, selectedSite())
        }

        private fun selectedSite(): String? {
            val tab = selector.currentTab ?: return null
            if (
                tab.isDestroyed || tab.isClosing || tab.isOffTheRecord ||
                tab.profile !== regularProfile || taskSources.isAssistantCreatedTaskTab(tab)
            ) {
                return null
            }
            val contents = tab.webContents ?: return null
            if (contents.isDestroyed) return null
            val address = contents.lastCommittedUrl
            if (!address.isTrackableWebAddress()) return null
            return UrlUtilities.getDomainAndRegistry(address.spec, false)
                .trim()
                .lowercase(Locale.ROOT)
                .takeIf { it.isSafeSite() }
        }

        override fun onActivityCreated(activity: Activity, savedInstanceState: Bundle?) = Unit
        override fun onActivityStarted(activity: Activity) = Unit
        override fun onActivityStopped(activity: Activity) = Unit
        override fun onActivitySaveInstanceState(activity: Activity, outState: Bundle) = Unit
    }

    private companion object {
        const val CHECKPOINT_MILLIS = 60_000L
        const val HOUR_MILLIS = 60L * 60L * 1_000L
        const val DAY_MILLIS = 24L * HOUR_MILLIS
        const val WEEK_MILLIS = 7L * DAY_MILLIS
    }
}

private fun GURL.isTrackableWebAddress(): Boolean =
    isValid &&
        (scheme == "http" || scheme == "https") &&
        host.isNotBlank() && username.isEmpty() && password.isEmpty()
