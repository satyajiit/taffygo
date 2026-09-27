// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import com.taffygo.browser.ui.core.model.PageLoadFailure
import com.taffygo.browser.ui.core.model.TaskTemplate
import java.util.UUID

/**
 * The navigation contract (android-app-architecture section 5).
 *
 * Every screen a feature can reach is named here, in `:core:ui`, which is how a
 * feature navigates to another feature's screen without depending on it. A
 * feature that wanted a new destination adds it here and the module-graph check
 * stays satisfied; a feature that reached for another feature's type would not
 * compile.
 *
 * [screenId] is the screen catalog's stable identifier. It is what a test
 * asserts against and what a content-free analytics screen event carries — the
 * identifier, never the content.
 */
sealed interface TaffyDestination {

    /** The catalog identifier for this surface. */
    val screenId: String

    /**
     * A stable key for this destination, which is what scopes a screen's view
     * model and its saved state, and what the shell saves the back stack as.
     *
     * A destination writes an argument here when the argument is what
     * identifies it — one workspace's identifier is not the person's words, and
     * a route that names it is what brings that workspace back after process
     * death. A destination whose arguments are the person's own words writes an
     * identity of its own instead and carries the words as state; [AssistantBar]
     * says why at length.
     */
    val route: String
        get() = screenId

    /**
     * The destination's own arguments, handed to its view model as default
     * arguments so a screen reads them from its `SavedStateHandle` and they
     * survive process death without the screen holding them.
     */
    val arguments: Map<String, String>
        get() = emptyMap()

    /**
     * Whether the shell keeps the screen beneath composed and draws this one
     * over it — the Ask overlay, standing over the page it is about.
     */
    val presentsOverBase: Boolean
        get() = false

    /** The first screen of the first run: the promise, and one way forward. */
    data object OnboardingWelcome : TaffyDestination {
        override val screenId: String = "SCR-001"
    }

    /** The assistant, shown as four films of it working beside a page. */
    data object MeetTaffy : TaffyDestination {
        override val screenId: String = "SCR-002"
    }

    /**
     * The interface's language, and the region rows that do not store one yet.
     *
     * A side door off the first-run sequence rather than a step in it: it is
     * entered from the chip in the corner and left with back, which is why the
     * band above it carries no dots.
     */
    data object LanguageRegion : TaffyDestination {
        override val screenId: String = "SCR-006"
    }

    /**
     * A name and a picture this phone keeps, both optional.
     *
     * It stands where account sign-in stood and asks for nothing a server
     * would hold (decision 0201). SCR-701 is retired rather than reused: an
     * id that once named a credential form must not come back naming
     * something a person answers with no account at all.
     */
    data object GetStarted : TaffyDestination {
        override val screenId: String = "SCR-007"
    }

    /** How Taffy reaches a model. A route must be chosen before browsing. */
    data object AiSetup : TaffyDestination {
        override val screenId: String = "SCR-004"
    }

    /** Durable regular browser profiles and the boundary between them. */
    data object BrowserProfiles : TaffyDestination {
        override val screenId: String = "SCR-708"
    }

    /** Explicit encrypted backup files; no key or document identity in the route. */
    data object Backup : TaffyDestination {
        override val screenId: String = "SCR-704"
    }

    /** Web content, address bar, tab count, and the Assistant bar. */
    data object BrowserMain : TaffyDestination {
        override val screenId: String = "SCR-101"
    }

    /** A clean start page: address bar focus, recent and pinned, no feed. */
    data object NewTab : TaffyDestination {
        override val screenId: String = "SCR-102"
    }

    /** The focused address bar, with its interpretations and suggestions. */
    data object AddressBar : TaffyDestination {
        override val screenId: String = "SCR-103"
    }

    /** The tab grid, with Taffy's tabs in their own group. */
    data object TabSwitcher : TaffyDestination {
        override val screenId: String = "SCR-104"
    }

    /** An honest error page for one failure. */
    data class PageError(val failure: PageLoadFailure) : TaffyDestination {
        override val screenId: String = "SCR-108"
        override val route: String = "$screenId/${failure.label}"
    }

    /**
     * One page the product opened for an errand (SCR-110).
     *
     * A vendor's sign-in, the page a key is fetched from, a vendor's own
     * documentation: a page a person was *sent* to and comes back from. It
     * carries a toolbar with the way back, the origin read-only, and a close —
     * and none of the browsing surface's chrome, because there is nothing to
     * type here and nowhere else to go.
     *
     * ## The route carries an identity and never an address
     *
     * The same rule [AssistantBar] states, and here it is at
     * its sharpest: a route is saved to survive process death, and an
     * authorization address saved into one is a `state` and a `code_challenge`
     * outliving the flow that minted them, in a string the shell writes to
     * disk. The address lives in the page this identity names, and when that
     * page is gone the screen says so and leaves rather than reopening
     * something it found written down.
     */
    data class ErrandPage(val errandId: String) : TaffyDestination {
        override val screenId: String = "SCR-110"
        override val route: String = "$screenId/$errandId"
        override val arguments: Map<String, String> = mapOf(ERRAND_ID to errandId)
    }

    /** The download list. */
    data object Downloads : TaffyDestination {
        override val screenId: String = "SCR-203"
    }

    /**
     * The Assistant bar, expanded — and what it was opened with.
     *
     * [question] is the person's own words, carried in the arguments a screen
     * reads out of its saved state, never in the route. The address bar's "Ask Taffy" reading resolved a
     * question and then dropped it on the floor, so the bar opened knowing only
     * that somebody had asked *something*.
     *
     * ## Why an ask has an identity and the bar itself does not
     *
     * A route is the key a screen's saved state is filed under, and an argument
     * is written into that state only where it does not already answer for one
     * — otherwise reopening a screen would undo what was typed into it. With
     * one fixed key for this destination, the first question a person ever
     * asked would be written and every later one silently discarded, which is
     * the fault the retired task preview met first, reached by a different road.
     *
     * So each ask mints an identity of its own. The bar with nothing to ask
     * keeps a single fixed identity instead of a minted one, because it is the
     * collapsed affordance in the browser's own chrome: it is composed on every
     * browsing frame, it is never on the back stack, and a fresh key per
     * composition would rebuild its view model on every recomposition and leave
     * a saved-state entry behind that nothing ever matches again.
     */
    data class AssistantBar(
        val question: String = "",
        val askId: String = if (question.isEmpty()) NO_QUESTION else newAskId(),
        /**
         * The shape of the job, when the composer that opened the sheet had one
         * stated: a research shape the start page's box cannot start alone,
         * because it reads named pages and the sheet is where pages are named.
         */
        val shape: TaskTemplate? = null,
        /**
         * The tabs to open the sheet with attached, when the caller chose them
         * — the tab switcher's Ask, with the picked tabs. Absent, the sheet
         * attaches the page it stands over, as it always did; the key is
         * written only when there is a choice to carry, so an empty list is
         * not mistaken for "attach nothing".
         */
        val attachedTabIds: List<String> = emptyList(),
    ) : TaffyDestination {
        override val screenId: String = SCREEN_ID
        override val route: String = "$screenId/$askId"
        override val arguments: Map<String, String> = buildMap {
            put(QUESTION, question)
            shape?.let { put(SHAPE, it.label) }
            if (attachedTabIds.isNotEmpty()) put(ATTACHED_TAB_IDS, attachedTabIds.joinToString(","))
        }
        override val presentsOverBase: Boolean get() = true

        companion object {
            /** The catalog identifier, for a caller that wants only that. */
            const val SCREEN_ID: String = "SCR-301"

            /** The identity of the bar that was not asked anything. */
            const val NO_QUESTION: String = "bar"

            /** One ask's identity, opaque and unrelated to what was asked. */
            private fun newAskId(): String = UUID.randomUUID().toString()
        }
    }

    /** The full-screen truth about one task. */
    data object TaskView : TaffyDestination {
        override val screenId: String = "SCR-303"
    }

    /** Every workspace, with the state each one really reached. */
    data object WorkspaceList : TaffyDestination {
        override val screenId: String = "SCR-304"
    }

    /** One workspace: its output, its sources, and its conflicts. */
    data class WorkspaceDetail(val workspaceId: String) : TaffyDestination {
        override val screenId: String = "SCR-305"
        override val route: String = "$screenId/$workspaceId"
        override val arguments: Map<String, String> = mapOf(WORKSPACE_ID to workspaceId)
    }

    /** The page one fact came from, and when it was read. */
    data class SourceViewer(val workspaceId: String, val sourceId: String) : TaffyDestination {
        override val screenId: String = "SCR-306"
        override val route: String = "$screenId/$workspaceId/$sourceId"
        override val arguments: Map<String, String> =
            mapOf(WORKSPACE_ID to workspaceId, SOURCE_ID to sourceId)
    }

    /** The sheet that records the user's value beside the page's. */
    data class FactCorrection(val workspaceId: String, val factId: String) : TaffyDestination {
        override val screenId: String = "SCR-307"
        override val route: String = "$screenId/$workspaceId/$factId"
        override val arguments: Map<String, String> =
            mapOf(WORKSPACE_ID to workspaceId, FACT_ID to factId)
    }

    /** Where the exported file goes, and what it contains. */
    data class ExportSheet(val workspaceId: String) : TaffyDestination {
        override val screenId: String = "SCR-309"
        override val route: String = "$screenId/$workspaceId"
        override val arguments: Map<String, String> = mapOf(WORKSPACE_ID to workspaceId)
    }

    /** Settings, with its sections. */
    data object SettingsHome : TaffyDestination {
        override val screenId: String = "SCR-401"
    }

    /** Where model requests go, and with whose key. */
    data object AiAndProviders : TaffyDestination {
        override val screenId: String = "SCR-404"
    }

    /** Ad and tracker blocking: the toggle, the total, the exceptions. */
    data object AdAndTrackerBlocking : TaffyDestination {
        override val screenId: String = "SCR-206"
    }

    /** What the UI layer may notify about. */
    data object Notifications : TaffyDestination {
        override val screenId: String = "SCR-406"
    }

    /** Theme choice and the text-scaling note. */
    data object Appearance : TaffyDestination {
        override val screenId: String = "SCR-407"
    }

    /** History (SCR-201). */
    data object History : TaffyDestination {
        override val screenId: String = "SCR-201"
    }

    /** Bookmarks (SCR-202). */
    data object Bookmarks : TaffyDestination {
        override val screenId: String = "SCR-202"
    }

    /** Site settings list (SCR-205). */
    data object SiteSettings : TaffyDestination {
        override val screenId: String = "SCR-205"
    }

    /** Clear browsing data (SCR-207). */
    data object ClearBrowsingData : TaffyDestination {
        override val screenId: String = "SCR-207"
    }

    /** General settings (SCR-402). */
    data object General : TaffyDestination {
        override val screenId: String = "SCR-402"
    }

    /** Privacy (SCR-403). */
    data object Privacy : TaffyDestination {
        override val screenId: String = "SCR-403"
    }

    /** Taffy section hub (SCR-405). */
    data object TaffySettings : TaffyDestination {
        override val screenId: String = "SCR-405"
    }

    /** About (SCR-408). */
    data object About : TaffyDestination {
        override val screenId: String = "SCR-408"
    }

    /** Help and feedback (SCR-409). */
    data object HelpAndFeedback : TaffyDestination {
        override val screenId: String = "SCR-409"
    }

    /** You hub (SCR-410). */
    data object You : TaffyDestination {
        override val screenId: String = "SCR-410"
    }

    /** Time on sites (SCR-411). */
    data object TimeOnSites : TaffyDestination {
        override val screenId: String = "SCR-411"
    }

    /** What happened (SCR-412). */
    data object WhatHappened : TaffyDestination {
        override val screenId: String = "SCR-412"
    }

    /** Saved sign-ins (SCR-413). */
    data object SavedSignIns : TaffyDestination {
        override val screenId: String = "SCR-413"
    }

    /** Saved details (SCR-414). */
    data object SavedDetails : TaffyDestination {
        override val screenId: String = "SCR-414"
    }

    /**
     * One provider's own page (SCR-415): its credential, model and thinking.
     *
     * The identifier is in the route because it is what the page is *about*.
     * A provider identifier is a catalog key rather than anything the person
     * typed, so it is safe in a key the shell writes into its saved state —
     * the rule [AssistantBar] states from the other side.
     */
    data class ProviderConfig(val providerId: String) : TaffyDestination {
        override val screenId: String = "SCR-415"
        override val route: String = "$screenId/$providerId"
        override val arguments: Map<String, String> = mapOf(PROVIDER_ID to providerId)
    }

    /** One provider's sign-in, for the vendors this build can complete (SCR-416). */
    data class ProviderSignIn(val providerId: String) : TaffyDestination {
        override val screenId: String = "SCR-416"
        override val route: String = "$screenId/$providerId"
        override val arguments: Map<String, String> = mapOf(PROVIDER_ID to providerId)
    }

    /**
     * The models on offer (SCR-417), for one provider or for the whole catalog.
     *
     * A null [providerId] is a destination in its own right rather than a
     * missing argument: the catalog is a flat list that names its own
     * providers, so "every model" is a screen a person can be sent to. It
     * therefore has a route of its own — the bare screen identifier — and the
     * per-provider form appends the provider. Two shapes, one screen, and each
     * comes back from process death as the one that was open.
     */
    data class ModelSelection(val providerId: String? = null) : TaffyDestination {
        override val screenId: String = "SCR-417"
        override val route: String =
            if (providerId == null) screenId else "$screenId/$providerId"
        override val arguments: Map<String, String> =
            providerId?.let { mapOf(PROVIDER_ID to it) } ?: emptyMap()
    }

    /**
     * Setting up a provider the person supplies the address for (SCR-418).
     *
     * A null [endpointId] is one being added and carries no identity yet,
     * which is why the bare screen identifier is its route; an existing one is
     * named, because that is what brings the same endpoint back.
     */
    data class CustomEndpointSetup(val endpointId: String? = null) : TaffyDestination {
        override val screenId: String = "SCR-418"
        override val route: String =
            if (endpointId == null) screenId else "$screenId/$endpointId"
        override val arguments: Map<String, String> =
            endpointId?.let { mapOf(ENDPOINT_ID to it) } ?: emptyMap()
    }

    /** Every provider this browser can reach a model through (SCR-419). */
    data object ConnectedProviders : TaffyDestination {
        override val screenId: String = "SCR-419"
    }

    /** Library home (SCR-501). */
    data object LibraryHome : TaffyDestination {
        override val screenId: String = "SCR-501"
    }

    /** One collection (SCR-502). */
    data class LibraryCollection(val collectionId: String) : TaffyDestination {
        override val screenId: String = "SCR-502"
        override val route: String = "$screenId/$collectionId"
        override val arguments: Map<String, String> = mapOf(COLLECTION_ID to collectionId)
    }

    /** Trusted export flow opened from one Library collection (SCR-502). */
    data class LibraryExport(val collectionId: String) : TaffyDestination {
        override val screenId: String = "SCR-502"
        override val route: String = "$screenId/$collectionId/export"
        override val arguments: Map<String, String> = mapOf(COLLECTION_ID to collectionId)
    }

    /** One kept item (SCR-503). */
    data class LibraryItem(
        val collectionId: String,
        val itemId: String,
    ) : TaffyDestination {
        override val screenId: String = "SCR-503"
        override val route: String = "$screenId/$collectionId/$itemId"
        override val arguments: Map<String, String> =
            mapOf(COLLECTION_ID to collectionId, ITEM_ID to itemId)
    }

    /** Keep this (SCR-504). */
    data object KeepThis : TaffyDestination {
        override val screenId: String = "SCR-504"
    }

    /** Memory (SCR-505). */
    data object Memory : TaffyDestination {
        override val screenId: String = "SCR-505"
    }

    /** What Taffy can do (SCR-601). */
    data object SkillsList : TaffyDestination {
        override val screenId: String = "SCR-601"
    }

    /** Skill detail (SCR-602). */
    data class SkillDetail(val skillId: String) : TaffyDestination {
        override val screenId: String = "SCR-602"
        override val route: String = "$screenId/$skillId"
        override val arguments: Map<String, String> = mapOf(SKILL_ID to skillId)
    }

    /** How Taffy talks (SCR-603). */
    data object Personality : TaffyDestination {
        override val screenId: String = "SCR-603"
    }

    /** Personality tuning (SCR-604). */
    data object PersonalityTuning : TaffyDestination {
        override val screenId: String = "SCR-604"
    }

    companion object {
        /**
         * Where the application opens.
         *
         * A getter and not a stored value, and the argument-free list
         * [parseTaffyDestination] walks is `by lazy` for the same reason. This
         * interface declares default implementations of `route` and
         * `arguments`, so the JVM initializes it before any destination that
         * implements it — and initializing the interface constructs this
         * companion. A companion field that read
         * `BrowserMain` while `BrowserMain`'s own class initializer was still
         * running would read `null`, because the JVM does not re-enter a class
         * initialization already in progress. The order that does it is
         * ordinary: the first thing to touch a destination rather than the
         * companion.
         *
         * That is not a hypothetical. It was a `NullPointerException` inside
         * `fromRoute`, which is the function the back stack is restored
         * through — so the failure mode was a crash on the way back from
         * process death, on some launches and not others.
         */
        val START: TaffyDestination
            get() = BrowserMain

        /** The argument names a screen reads out of its saved state. */
        const val WORKSPACE_ID: String = "workspaceId"
        const val SOURCE_ID: String = "sourceId"
        const val FACT_ID: String = "factId"
        const val QUESTION: String = "question"
        const val SHAPE: String = "shape"
        const val ATTACHED_TAB_IDS: String = "askAttachedTabIds"
        const val COLLECTION_ID: String = "collectionId"
        const val ITEM_ID: String = "itemId"
        const val SKILL_ID: String = "skillId"
        const val PROVIDER_ID: String = "providerId"
        const val ENDPOINT_ID: String = "endpointId"
        const val ERRAND_ID: String = "errandId"

        /**
         * The destination a route names, or null when the route is not one.
         *
         * This is what lets the back stack survive process death: the shell
         * saves routes and rebuilds destinations from them, so restoring never
         * invents a destination the application does not have.
         */
        fun fromRoute(route: String): TaffyDestination? = parseTaffyDestination(route)
    }
}
