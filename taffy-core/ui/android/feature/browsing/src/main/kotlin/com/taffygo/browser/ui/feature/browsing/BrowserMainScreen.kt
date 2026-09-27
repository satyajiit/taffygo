// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.taffygo.browser.ui.core.designsystem.TaffyEdges
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.LocalPageSurface
import com.taffygo.browser.ui.core.ui.LocalPageSurfaceSlot
import com.taffygo.browser.ui.core.ui.TaffyBackHandler
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyPaneSplit
import com.taffygo.browser.ui.core.ui.TaffyTwoPane
import com.taffygo.browser.ui.core.ui.rememberScreenViewModelStoreOwner
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * Screen SCR-101 — the browser's main surface.
 *
 * The Assistant bar is a slot rather than a dependency: it belongs to another
 * feature, and features never depend on features. The application shell fills
 * it, which is how the two meet without an edge in the module graph. The form
 * Taffy is holding open is a second slot, filled the same way for the same
 * reason. The Ask overlay is a third: the composer standing over this page,
 * composed last so that its back handler outranks this screen's own.
 *
 * What a task is doing in this window comes from a second view model rather
 * than from [BrowserMainViewModel]. Two subjects, two owners: the main one is
 * about the page, and a task started on another screen entirely is not.
 */
@Composable
fun BrowserMainScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    assistantBar: @Composable () -> Unit = {},
    taskWait: @Composable () -> Unit = {},
    askOverlay: @Composable () -> Unit = {},
    taskPanel: @Composable (
        started: StartedTask,
        onTryAgain: () -> Unit,
        onLeave: () -> Unit,
    ) -> Unit = { _, _, _ -> },
) {
    val viewModel: BrowserMainViewModel = screenViewModel(TaffyDestination.BrowserMain)
    val state by viewModel.state.collectAsStateWithLifecycle()
    // A key of its own: `viewModel` stores by key, so two view models on one
    // destination sharing the route would be one entry that keeps whichever
    // class asked last.
    val takeoverViewModel: BrowserTakeoverViewModel = viewModel(
        viewModelStoreOwner = rememberScreenViewModelStoreOwner(TaffyDestination.BrowserMain),
        key = "${TaffyDestination.BrowserMain.route}/takeover",
    )
    val takeover by takeoverViewModel.state.collectAsStateWithLifecycle()
    val offersViewModel: PageSkillOffersViewModel = viewModel(
        viewModelStoreOwner = rememberScreenViewModelStoreOwner(TaffyDestination.BrowserMain),
        key = "${TaffyDestination.BrowserMain.route}/saved-flows",
    )
    val offers by offersViewModel.state.collectAsStateWithLifecycle()
    val flowGoal = taffyString(R.string.taffy_page_flows_goal, state.host)
    val context = LocalContext.current
    val shareTitle = taffyString(R.string.taffy_browser_share_chooser)

    TaffyBackHandler(enabled = true) {
        viewModel.onIntent(BrowserMainIntent.SystemBack, navigator)
    }

    LaunchedEffect(Unit) { viewModel.onShown() }

    val onIntent: (BrowserMainIntent) -> Unit = { intent ->
        when (intent) {
            is BrowserMainIntent.SharePage ->
                sharePageUrl(context, state.canonicalUrl, shareTitle)
            is BrowserMainIntent.TakeOver -> takeoverViewModel.takeOver()
            is BrowserMainIntent.OpenSavedFlows -> offersViewModel.open(flowGoal)
            else -> Unit
        }
        viewModel.onIntent(intent, navigator)
    }

    // The box a tab that has been nowhere draws, on **this** screen's entry.
    // Screen SCR-102 builds one of its own the same way; neither borrows the
    // other's, and neither borrows the address bar's — see
    // [rememberStartPageComposer].
    //
    // No first-focus work: this screen already has the tab the words will
    // commit into, which is the one thing SCR-102 has to arrange.
    val composer = rememberStartPageComposer(
        host = TaffyDestination.BrowserMain,
        navigator = navigator,
        menuActions = StartPageMenuActions(openLibrary = { onIntent(BrowserMainIntent.OpenLibrary) }),
        showsStartBody = state.content == BrowserContent.START,
        taskPanel = taskPanel,
    )

    BrowserMainContent(
        state = state.copy(takeover = takeover),
        onIntent = onIntent,
        startComposer = composer,
        assistantBar = assistantBar,
        taskWait = taskWait,
        askOverlay = askOverlay,
        modifier = modifier,
    )
    if (offers.open) PageSkillOffersSheet(
        state = offers,
        onClose = offersViewModel::close,
        onRefresh = { offersViewModel.open(flowGoal) },
        onStart = { offersViewModel.start(it, navigator) },
        onLoadReviews = offersViewModel::loadReviews,
        onManage = {
            offersViewModel.close()
            navigator.goTo(TaffyDestination.SkillsList)
        },
        onSetup = {
            offersViewModel.close()
            navigator.goTo(TaffyDestination.AiAndProviders)
        },
    )
}

/** The stateless half, which is what a preview and a semantics test render. */
@Composable
fun BrowserMainContent(
    state: BrowserMainUiState,
    onIntent: (BrowserMainIntent) -> Unit,
    startComposer: StartPageComposerSlot,
    modifier: Modifier = Modifier,
    assistantBar: @Composable () -> Unit = {},
    taskWait: @Composable () -> Unit = {},
    askOverlay: @Composable () -> Unit = {},
) {
    val twoPane = TaffyTheme.windowWidth.showsTwoPane
    // The ground is painted here only when this screen composes the page
    // itself. When the window hosts it, the page sits *behind* this screen and
    // an opaque fill here would cover it, so the host paints the same colour
    // beneath the surface instead and the reporting is done in
    // [BrowserPageArea]. Composited result is identical: both are opaque
    // full-window fills of the same colour.
    val hosted = LocalPageSurfaceSlot.current != null && LocalPageSurface.current.isLive
    val ground = state.pageBackgroundArgb?.let(::Color) ?: TaffyTheme.colors.surface
    val onStartGround = state.content == BrowserContent.START ||
        state.content == BrowserContent.PREPARING ||
        state.content == BrowserContent.FAILED
    Box(
        modifier = modifier
            .fillMaxSize()
            .then(if (hosted) Modifier else Modifier.background(ground)),
    ) {
        if (onStartGround) {
            StartPageBackdrop(
                modifier = Modifier
                    .matchParentSize()
                    .testTag(START_BACKDROP_TEST_TAG),
            )
        }
        val frame = Modifier
            .fillMaxSize()
            .windowInsetsPadding(TaffyEdges.sides)
            .testTag(TaffyDestination.BrowserMain.screenId)
        if (twoPane) {
            TaffyTwoPane(
                primary = {
                    BrowserPageColumn(
                        state = state,
                        onIntent = onIntent,
                        assistantBar = null,
                        startComposer = startComposer,
                        taskWait = taskWait,
                    )
                },
                secondary = {
                    Box(
                        modifier = Modifier.fillMaxHeight().windowInsetsPadding(TaffyEdges.all),
                        contentAlignment = Alignment.BottomStart,
                    ) {
                        BrowserAssistantSlot(assistantBar = assistantBar)
                    }
                },
                split = TaffyPaneSplit.CONTENT_ASSISTANT,
                modifier = frame,
            )
        } else {
            BrowserPageColumn(
                state = state,
                onIntent = onIntent,
                assistantBar = assistantBar,
                startComposer = startComposer,
                modifier = frame,
                taskWait = taskWait,
            )
        }
        if (state.siteFilteringOpen) {
            SiteFilteringSheet(state = state.siteFiltering, onIntent = onIntent)
        }
        if (state.savePageOpen) {
            SavePageSheet(
                state = state.savePage,
                onIntent = { onIntent(it.toBrowserMainIntent()) },
            )
        }
        // Last on purpose, and over the whole window in both layouts: the
        // overlay's back handler registers after this screen's, so back closes
        // the overlay before it touches page history, and its scrim covers
        // the page it is about rather than docking beside it.
        askOverlay()
    }
}
