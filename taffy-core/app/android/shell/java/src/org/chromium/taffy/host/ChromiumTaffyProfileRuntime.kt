// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import android.app.Activity
import android.app.ActivityManager
import androidx.activity.ComponentActivity
import com.taffygo.browser.ui.app.BackupDocumentPicker
import com.taffygo.browser.ui.app.TaffyProcessComponent
import com.taffygo.browser.ui.app.ProfileDataErasureCoordinator
import com.taffygo.browser.ui.app.ProfileDataExportEncoder
import com.taffygo.browser.ui.app.TaffyProfileComponent
import com.taffygo.browser.ui.app.startRequiredParts
import com.taffygo.browser.ui.app.startTaskContinuation
import com.taffygo.browser.ui.app.TaffyWindowComponent
import com.taffygo.browser.ui.core.browser.BrowserMediator
import com.taffygo.browser.ui.core.common.di.TaffyProfileIdentity
import com.taffygo.browser.ui.core.common.di.TaffyWindowIdentity
import com.taffygo.browser.ui.core.common.di.TaffyWindowLifetime
import java.io.Closeable
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.plus
import org.chromium.base.ContextUtils
import org.chromium.chrome.browser.profiles.Profile
import org.chromium.chrome.browser.tabmodel.TabModelSelector
import org.chromium.taffy.browser.TaffyBackupWorkflowBridge
import org.chromium.taffy.browser.TaffyTaskSourceSelectionBridge
import org.chromium.taffy.shell.ChromiumBookmarksRepository
import org.chromium.taffy.shell.ChromiumAboutRepository
import org.chromium.taffy.shell.ChromiumBrowserProfilesRepository
import org.chromium.taffy.shell.ChromiumBrowserMediator
import org.chromium.taffy.shell.ChromiumClearDataRepository
import org.chromium.taffy.shell.ChromiumFindInPagePort
import org.chromium.taffy.shell.ChromiumGeneralSettingsRepository
import org.chromium.taffy.shell.ChromiumHistoryRepository
import org.chromium.taffy.shell.ChromiumPageZoomRepository
import org.chromium.taffy.shell.ChromiumSiteInfoRepository
import org.chromium.taffy.shell.ChromiumSiteSettingsRepository
import org.chromium.taffy.shell.ChromiumTimeOnSitesRepository
import org.chromium.taffy.shell.TaffyDownloadNotificationController
import org.chromium.taffy.shell.openBrowserProfileWindowLease

/** One regular profile graph, its Window graphs, and its profile-owned Tab graphs. */
internal class ChromiumTaffyProfileRuntime(
    private val profile: Profile,
    processComponent: TaffyProcessComponent,
    private val provider: ChromiumTaffyProfileRuntimeProvider,
) : TaffyProfileRuntime,
    TaffyTabProfileRuntime {
    private val profileToken = opaqueProfileToken(profile, expectPrivate = false)
    private val endpoint = ChromiumCoreApiEndpoint(profile)
    private val taskInputEndpoint = ChromiumTaskInputEndpoint(profile)
    private val ports = ChromiumProfilePorts(profile, processComponent.dispatchers().io)
    private val platformSurfaces = ProfilePlatformSurfaceDispatcher()
    private val component: TaffyProfileComponent = processComponent.regularProfileBuilder()
        .identity(TaffyProfileIdentity(profileToken, private = false))
        .coreApiClient(endpoint)
        .taskInputEndpoint(taskInputEndpoint)
        .preferenceStore(ports.preferenceStore)
        .secureMaterial(ports.secureMaterial)
        .providerRevocation(ChromiumProviderRevocationPort(profile))
        .providerManualCode(
            ChromiumProviderManualCodePort(profile, processComponent.dispatchers().main),
        )
        .build()
    // The adapter answers Mojo calls, so it runs where its router was bound.
    //
    // `bindPlatformAdapter` below is called from here, on the thread that owns
    // the Profile, and a Mojo binding is affine to the sequence that created
    // it: a reply from another thread is a reply on the wrong sequence. The
    // objects the adapter reaches through are held to the same rule -- the
    // secure-material store reads this profile's `PrefService`, whose own
    // sequence checker is what fails first and loudest.
    //
    // Adding the dispatcher to the lifetime's scope rather than building a new
    // one keeps the job and the failure handler, so this is still cancelled
    // with the profile and still reports through the same sink.
    private val platformAdapter = ChromiumProfilePlatformAdapter(
        profile,
        ports.secureMaterial,
        platformSurfaces,
        component.providerCoordinator(),
        component.providerSignInEngine(),
        component.lifetime().scope + component.dispatchers().main,
    )
    private val tabs = TaffyTabComponentRegistry(profile, component, provider.pageFactory)
    private val timeOnSites = ChromiumTimeOnSitesRepository(profile, ports.preferenceStore)
    private val backupWorkflow = TaffyBackupWorkflowBridge.open(profile)
    private val windows = linkedSetOf<TaffyWindowLifetime>()
    private var acceptingAuthority = true
    private var closed = false

    init {
        component.lifetime().own(endpoint)
        component.lifetime().own(taskInputEndpoint)
        ports.ownWith(component.lifetime())
        component.lifetime().own(platformAdapter)
        component.lifetime().own(timeOnSites)
        backupWorkflow?.let(component.lifetime()::own)
        component.lifetime().own(
            TaffyDownloadNotificationController.open(
                ContextUtils.getApplicationContext(),
                profile,
                profileToken,
            ),
        )
        // The product's own downloads begin with the profile, not with the
        // screen that happens to need one first. The start page is held shut
        // until the Python library is installed, so an install that waited
        // for a window was a browser that waited for itself.
        component.startRequiredParts()
        component.startTaskContinuation()
    }

    override fun openWindow(
        activity: Activity,
        windowId: Int,
        selector: TabModelSelector,
        browserMediator: BrowserMediator,
    ): TaffyWindowComponent {
        check(acceptingAuthority && !closed) { "Cannot open a window in a closed profile graph" }
        check(!profile.isOffTheRecord) { "A mixed browser window must be rooted in its original profile" }
        val concreteMediator = browserMediator as? ChromiumBrowserMediator
            ?: error("The product window requires ChromiumBrowserMediator")

        val pending = mutableListOf<Closeable>()
        var lifetime: TaffyWindowLifetime? = null
        try {
            pending += openBrowserProfileWindowLease(profile)
            val pickerActivity = activity as? ComponentActivity
                ?: error("The product window requires ComponentActivity result custody")
            val backupDocumentPicker = BackupDocumentPicker(
                pickerActivity.activityResultRegistry,
                pickerActivity.savedStateRegistry,
            ).also { pending += it }
            val backupHost = ChromiumBackupWindowHost(
                activity.contentResolver,
                component.dispatchers().io,
                backupWorkflow?.openWindow()?.asBackupNativeWindow(),
            )
            pending += ResumedWindowRegistration.backup(activity, backupHost)
            pending += ResumedWindowRegistration.taskContinuation(
                activity,
                component.taskContinuation(),
                "$profileToken:window:$windowId",
            )
            val taskSources =
                TaffyTaskSourceSelectionBridge.open(profile).also { pending += it }
            concreteMediator.bindTaskTabAttribution(
                taskSources::isAssistantCreatedTaskTab,
                taskSources::getCreatingTaskId,
                taskSources::isAcceptedTaskSourceTab,
            )
                .also { pending += it }
            timeOnSites.attach(activity, selector, taskSources).also { pending += it }
            val selectorBinding =
                TaffySelectorTabBinding(selector, provider, taskSources).also { pending += it }
            val selectedPage =
                TaffySelectedTabRouter(selector, provider, taskSources).also { pending += it }
            val history = ChromiumHistoryRepository(profile, concreteMediator).also { pending += it }
            val bookmarks =
                ChromiumBookmarksRepository(profile, selector, concreteMediator).also { pending += it }
            val findInPage = ChromiumFindInPagePort(
                selector,
                component.dispatchers(),
            ).also { pending += it }
            val siteSettings = ChromiumSiteSettingsRepository(activity, profile).also {
                pending += it
            }
            val siteInfo = ChromiumSiteInfoRepository(
                selector,
                siteSettings,
                component.dispatchers(),
                component.lifetime().scope,
            ).also { pending += it }
            val pageZoom = ChromiumPageZoomRepository(
                profile,
                selector,
                component.dispatchers(),
            ).also { pending += it }
            val generalSettings = ChromiumGeneralSettingsRepository(profile).also { pending += it }
            val clearData = ChromiumClearDataRepository(
                profile,
                clearTimeOnSites = timeOnSites::clear,
            ).also { pending += it }
            val about = ChromiumAboutRepository()
            val activityManager = checkNotNull(
                activity.getSystemService(ActivityManager::class.java),
            ) { "Android application-data control is unavailable" }
            val dataEraser = ProfileDataErasureCoordinator(
                withdrawWindowAndTabAuthority = ::withdrawWindowAndTabAuthority,
                cancelTask = endpoint::cancelTask,
                closeRemainingProfileAuthority = ::closeRemainingProfileAuthority,
                destroyCredentialMaterial = ports::destroySecureMaterial,
                requestApplicationDataClear = activityManager::clearApplicationUserData,
            )
            val dataControlJob = SupervisorJob(
                component.lifetime().scope.coroutineContext[Job],
            )
            pending += Closeable { dataControlJob.cancel() }
            val dataControl = ChromiumProfileDataControl(
                core = endpoint,
                preferences = component.preferences(),
                history = history,
                bookmarks = bookmarks,
                browser = concreteMediator,
                siteSettings = siteSettings,
                timeOnSites = timeOnSites,
                encoder = ProfileDataExportEncoder(),
                eraser = dataEraser,
                dispatchers = component.dispatchers(),
                scope = CoroutineScope(
                    component.lifetime().scope.coroutineContext + dataControlJob,
                ),
            )
            val window = component.windowBuilder()
                .activity(activity)
                .identity(TaffyWindowIdentity("$profileToken:window:$windowId"))
                .browserMediator(browserMediator)
                .historyRepository(history)
                .bookmarksRepository(bookmarks)
                .bookmarksWriter(bookmarks)
                .findInPagePort(findInPage)
                .errandPagePort(concreteMediator.errandPage)
                .siteInfoRepository(siteInfo)
                .pageZoomRepository(pageZoom)
                .generalSettingsRepository(generalSettings)
                .browserProfilesRepository(ChromiumBrowserProfilesRepository(activity, profile))
                .siteSettingsRepository(siteSettings)
                .profileDataControl(dataControl)
                .clearDataRepository(clearData)
                .timeOnSitesRepository(timeOnSites)
                .aboutRepository(about)
                .backupDocumentPicker(backupDocumentPicker)
                .backupWindowHost(backupHost)
                .selectedPageIntelligence(selectedPage)
                .build()
            val windowLifetime = window.lifetime()
            lifetime = windowLifetime
            val permissionAdapter = window.permissionAdapter()
            val voiceInputAdapter = window.voiceInputAdapter()
            val readAloudAdapter = window.readAloudAdapter()
            // The endpoint registration is attached later and therefore closes first. It settles
            // every browser request unavailable before this adapter discards late Android results.
            windowLifetime.own(permissionAdapter)
            windowLifetime.own(voiceInputAdapter)
            windowLifetime.own(readAloudAdapter)
            check(
                taskSources.bindBrowserActions(
                    TaffyTaskBrowserActions(
                        taskSources,
                        window.browserRepository(),
                        concreteMediator.taskTabs,
                    ),
                ),
            ) { "The task-action window could not be bound" }
            pending +=
                ResumedWindowRegistration.permission(
                    activity,
                    endpoint.registerPermissionHandler(permissionAdapter::request),
                )
            pending +=
                ResumedWindowRegistration.platformSurface(
                    activity,
                    platformSurfaces.register(
                        window.authSurfaceAdapter(),
                        window.credentialAdapter(),
                    ),
                )
            val activeTaskSources = ResumedWindowRegistration.taskSource(activity, taskSources)
            check(pending.remove(taskSources)) { "The task-source owner was lost during construction" }
            // Lifetimes close in reverse registration order. Keep the native
            // window alive until its selector and selected-page routes have
            // withdrawn every tab and selection.
            pending.add(0, activeTaskSources)

            windows += windowLifetime
            windowLifetime.own(Closeable { windows.remove(windowLifetime) })
            pending.forEach(windowLifetime::own)
            pending.clear()
            return window
        } catch (failure: Throwable) {
            closeAfterFailure(failure, pending.asReversed(), lifetime)
        }
    }

    override fun requireTab(tab: org.chromium.chrome.browser.tab.Tab): TaffyTabComponentOwner {
        check(acceptingAuthority && !closed) { "Cannot open a tab in a closed profile graph" }
        check(tab.profile === profile) { "A Tab graph cannot cross its Chromium profile" }
        return tabs.require(tab)
    }

    override fun close() {
        if (closed) return
        var failure: Throwable? = null
        try {
            withdrawWindowAndTabAuthority()
        } catch (caught: Throwable) {
            failure = caught
        }
        try {
            closeRemainingProfileAuthority()
        } catch (caught: Throwable) {
            failure?.addSuppressed(caught) ?: run { failure = caught }
        }
        failure?.let { throw it }
    }

    /** Revokes every UI/window/tab entry point while leaving the endpoint alive for task stops. */
    private fun withdrawWindowAndTabAuthority() {
        if (!acceptingAuthority) return
        acceptingAuthority = false
        val owned = mutableListOf<Closeable>()
        owned += windows
        owned += tabs
        windows.clear()
        closeAllOwnedResources(owned)
    }

    /** Closes the profile facade and platform adapters after task stop requests were issued. */
    private fun closeRemainingProfileAuthority() {
        if (closed) return
        closed = true
        component.lifetime().close()
    }
}

/** Closes an uncommitted Window graph without hiding either the cause or a cleanup failure. */
private fun closeAfterFailure(
    failure: Throwable,
    pending: List<Closeable>,
    lifetime: TaffyWindowLifetime?,
): Nothing {
    for (resource in pending) {
        try {
            resource.close()
        } catch (closeFailure: Throwable) {
            failure.addSuppressed(closeFailure)
        }
    }
    try {
        lifetime?.close()
    } catch (closeFailure: Throwable) {
        failure.addSuppressed(closeFailure)
    }
    throw failure
}
