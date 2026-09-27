// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import android.app.Activity
import androidx.compose.foundation.background
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalContext
import androidx.lifecycle.HasDefaultViewModelProviderFactory
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.LocalViewModelStoreOwner
import androidx.lifecycle.viewmodel.compose.viewModel
import com.taffygo.browser.ui.app.LocalBackupDocumentPicker
import com.taffygo.browser.ui.app.ShellViewModel
import com.taffygo.browser.ui.app.TaffyNavHost
import com.taffygo.browser.ui.app.TaffyWindowComponent
import com.taffygo.browser.ui.app.isDark
import com.taffygo.browser.ui.core.browser.PageSurface
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.LocalCountryFlagSource
import com.taffygo.browser.ui.core.ui.LocalStartSceneSource
import com.taffygo.browser.ui.core.ui.LocalAppDispatchers
import com.taffygo.browser.ui.core.ui.LocalPageSurface
import com.taffygo.browser.ui.core.ui.LocalStringTransformer
import com.taffygo.browser.ui.core.ui.StringTransformer
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyProductThemeGlyphs

/**
 * Every TaffyGo screen in the product activity.
 *
 * It owns the theme decision and the string transformation; it does not own
 * the navigation graph
 * ([TaffyNavHost] is mounted from `taffy-core/ui/android`), it does not own a
 * single screen, and it does not own the window — `TaffyBrowserActivity` and
 * Chromium's own startup do that.
 *
 *  - **No splash hold.** Browser preferences are read synchronously
 *    from the framework's own store, so `preferencesLoaded` is true on the
 *    first composition and there is nothing to wait for. The `if` remains a
 *    contract guard so a graph cannot render before its profile state exists.
 *  - **A per-app language, applied — but not through AppCompat.** This entry
 *    used to say the platform applied the choice in this build. It did not:
 *    choosing हिन्दी on SCR-006 or SCR-407 changed the stored value, the
 *    control marked it selected, the choice survived a force-stop, and every
 *    label stayed English, on a device, by both routes. What was missing was
 *    anything at all doing the applying. It has one now, and it is
 *    [TaffyAppLanguage] rather than
 *    AppCompat, because Chromium's own split at API 33 is the one that has to
 *    be honoured here: read that file for why, and for what applying a language
 *    costs the person on each side of that split (OD-098).
 *  - **No window setup, but the window is now edge-to-edge.** Those are still
 *    Chromium's decisions and `TaffyBrowserActivity` still makes them: it
 *    declines upstream's `EdgeToEdgeLayoutCoordinator` wrapper, which used to
 *    pad this composition below the status strip and then hand it insets of
 *    zero, and it asks for `LAYOUT_IN_DISPLAY_CUTOUT_MODE`. So `TaffyEdges`
 *    resolves to real amounts here. **What this file owns of that is one
 *    thing:** the colour under the bars, sent out through [TaffyWindowBars] so
 *    the icon polarity follows the Appearance setting (SCR-407) rather than the
 *    night qualifier Chrome's theme would answer with.
 */
@Composable
fun TaffyShellContent(
    windowComponent: TaffyWindowComponent,
    defaults: HasDefaultViewModelProviderFactory,
    modifier: Modifier = Modifier,
    bars: TaffyWindowBars? = null,
    pageSurface: PageSurface? = null,
    navigation: TaffyShellNavigation? = null,
) {
    // The factory and the owner are remembered rather than rebuilt, so a
    // recomposition cannot hand a screen a second copy of the graph's view of
    // itself. The graph behind them is owned by this browser window, and so is
    // the store: the window lifetime clears it, which is what keeps a view model
    // from outliving the mediator it reaches (see TaffyShellStoreOwner).
    val owner = remember(windowComponent, defaults) {
        TaffyShellStoreOwner(defaults, windowComponent.viewModelFactory())
            .also(windowComponent.lifetime()::own)
    }

    // The page surface is provided at the root rather than inside the browsing
    // screen, because it is a property of the host and not of a destination:
    // the composition local's default is the not-live surface, and a host that
    // has no engine simply does not override it. A null here is that case
    // said explicitly, so the default stands and nothing further down can tell
    // the difference between "no fork" and "fork, no page host yet".
    val surface = pageSurface ?: LocalPageSurface.current

    CompositionLocalProvider(
        LocalViewModelStoreOwner provides owner,
        LocalAppDispatchers provides windowComponent.dispatchers(),
        LocalBackupDocumentPicker provides windowComponent.backupDocumentPicker(),
        LocalPageSurface provides surface,
        LocalCountryFlagSource provides windowComponent.countryFlags(),
        LocalStartSceneSource provides windowComponent.startScenes(),
    ) {
        val shell: ShellViewModel = viewModel(viewModelStoreOwner = owner)
        val state by shell.state.collectAsStateWithLifecycle()

        // The one way an intent from another application reaches this back
        // stack. Attached for as long as there is a composition to navigate,
        // and detached with it, so nothing holds a destroyed activity's
        // navigator. The stack is read through the view model's own flow rather
        // than through `state` above, because this is not a reason to
        // recompose: the seam asks for the current value at the moment an
        // intent arrives.
        DisposableEffect(navigation, shell) {
            navigation?.attach(shell.navigator) { shell.state.value.backStack }
            onDispose { navigation?.detach() }
        }
        val hostContext = LocalContext.current
        DisposableEffect(shell, hostContext) {
            shell.bindLeaveToBackground {
                (hostContext as? Activity)?.moveTaskToBack(false) == true
            }
            onDispose { shell.bindLeaveToBackground { false } }
        }
        val darkTheme = state.theme.isDark(isSystemInDarkTheme())

        // The language the interface is drawn in, kept level with the stored
        // choice. Applying a language rebuilds this activity — the platform
        // does it above API 33 and [TaffyAppLanguage] asks for it below — so
        // this waits for the stored value rather than acting on the flow's
        // placeholder default, which would rebuild it twice on every launch.
        // The profile PrefService answers `loaded` on the first read, so the
        // guard documents and enforces the graph's initialization ordering.
        val context = LocalContext.current
        LaunchedEffect(
            context,
            state.appLanguage,
            state.regionCode,
            state.preferencesLoaded,
        ) {
            if (state.preferencesLoaded) {
                TaffyAppLanguage.apply(context, state.appLanguage, state.regionCode)
            }
        }

        CompositionLocalProvider(
            LocalStringTransformer provides if (state.pseudoLocalization) {
                StringTransformer.PseudoLocalizing
            } else {
                StringTransformer.Identity
            },
        ) {
            // The theme change rises the product's own sun and moon. The
            // design system draws the body and cannot reach `TaffyIcon` from
            // the layer it is on, so the pair is handed down from here — see
            // `TaffyThemeGlyphs`. This is the shipping call site; the preview
            // wrapper passes the same value.
            TaffyTheme(darkTheme = darkTheme, glyphs = TaffyProductThemeGlyphs) {
                // The window's bars, kept level with the ground actually under
                // them. The colour is never painted by the window — it is
                // transparent — so this decides one thing only: whether the
                // clock and the battery are drawn dark or light. On the
                // browsing surface that ground is the page's own background
                // when there is one; everywhere else it is the theme surface.
                val themeSurface = TaffyTheme.colors.surface
                val appearance by windowComponent.browserRepository()
                    .pageAppearance
                    .collectAsStateWithLifecycle()
                val onBrowser = state.backStack.current is TaffyDestination.BrowserMain
                val pageArgb = appearance.backgroundArgb
                // Since decision 0119 the page is not always what is behind the
                // clock. Over a page the top bar paints its own ground all the
                // way to the glass — `BrowserTopBar` takes its background
                // before it takes the top inset — so while that bar is showing
                // the surface under the status bar is the chrome's, not the
                // page's. Reading the page here regardless is how a dark page
                // under the light theme gets light icons drawn onto light
                // chrome, invisible, flipping to correct every time a scroll
                // hides the bar and back to wrong when it returns. The one
                // signal that hides the bar is the one that gives the status
                // bar back to the page, so the polarity follows it.
                val topBarOverThePage = onBrowser && pageArgb != null &&
                    appearance.topBarVisible
                val ground = if (onBrowser && pageArgb != null && !topBarOverThePage) {
                    Color(pageArgb)
                } else {
                    themeSurface
                }
                LaunchedEffect(bars, ground) { bars?.onTaffySurfaceColor(ground.toArgb()) }

                Box(
                    modifier = modifier
                        .fillMaxSize()
                        .background(TaffyTheme.colors.surface),
                ) {
                    if (state.preferencesLoaded) {
                        TaffyNavHost(
                            backStack = state.backStack,
                            navigator = shell.navigator,
                        )
                    }
                }
            }
        }
    }
}
