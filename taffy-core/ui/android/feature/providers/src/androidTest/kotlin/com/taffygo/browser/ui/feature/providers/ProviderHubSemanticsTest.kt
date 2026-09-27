// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.assertHasNoClickAction
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertTextContains
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.unit.Density
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.model.ProviderRosterRow
import com.taffygo.browser.ui.core.model.ProviderRosterState
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.model.RosterAuthMethod
import com.taffygo.browser.ui.core.model.RosterCatalogLayer
import com.taffygo.browser.ui.core.model.RosterCredentialState
import com.taffygo.browser.ui.core.model.RosterProviderOrigin
import com.taffygo.browser.ui.core.model.RosterStoredCredential
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-404's four tabs, its three empty answers and its row dispatch.
 *
 * Every test hands the screen a fixture roster, which is the shape the
 * projection receives from the core (decision 0080) — no test names a shipped
 * provider as though the catalog were compiled.
 *
 * `ProviderHubContent` is the stateless half, so pressing a tab here records an
 * intent and changes nothing. A test that wants a different category on screen
 * says so in the state it hands in, exactly as the view model would after
 * folding that intent — which `ProviderHubReducerTest` proves on a host.
 */
class ProviderHubSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val intents = mutableListOf<ProviderHubIntent>()

    private fun setContent(
        state: ProviderHubUiState,
        darkTheme: Boolean = false,
        fontScale: Float = 1f,
    ) {
        compose.setContent {
            val density = LocalDensity.current
            CompositionLocalProvider(
                LocalDensity provides Density(density.density, fontScale),
            ) {
                TaffyPreview(darkTheme = darkTheme, reducedMotion = true) {
                    ProviderHubContent(state = state, onIntent = { intents += it })
                }
            }
        }
    }

    private fun projected(
        rows: List<ProviderRosterRow>,
        heldCredentialIds: Set<String> = emptySet(),
    ): ProviderHubUiState = ProviderHubProjection.project(
        ProviderRosterState(ready = true, rows = rows),
        heldCredentialIds,
        signInFlows = mapOf(SIGN_IN_VENDOR to true),
    )

    @Test
    fun theListIsASkeletonUntilTheRosterArrives() {
        setContent(ProviderHubUiState())

        compose.onNodeWithTag(PROVIDER_HUB_LOADING_TEST_TAG).assertExists()
        compose.onNodeWithTag(PROVIDER_HUB_LIST_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(PROVIDER_HUB_EMPTY_TEST_TAG).assertDoesNotExist()
        // Four tabs over nothing would be four controls a person cannot use.
        compose.onNodeWithTag(PROVIDER_HUB_TABS_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun anEmptyCatalogSaysSoAndStillOffersAnEndpoint() {
        setContent(projected(rows = emptyList()))

        compose.onNodeWithTag(PROVIDER_HUB_EMPTY_TEST_TAG).assertExists()
        compose.onNodeWithText(
            context.getString(R.string.taffy_providers_hub_empty_body),
        ).assertExists()
        // An address the person supplies is the way out of an empty catalog,
        // so the one control that is not a row survives this state.
        compose.onNodeWithTag(PROVIDER_HUB_ADD_TEST_TAG).assertExists()
        compose.onNodeWithTag(PROVIDER_HUB_TABS_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun everythingBlockedIsSaidOnceAndThenRowByRow() {
        setContent(
            projected(
                rows = listOf(
                    row("held", enabled = false),
                    row("unbuilt", configurable = false),
                ),
            ),
        )

        compose.onNodeWithTag(PROVIDER_HUB_BLOCKED_TEST_TAG).assertExists()
        compose.onNodeWithText(
            context.getString(R.string.taffy_providers_block_held_shut),
        ).assertExists()
        compose.onNodeWithText(
            context.getString(R.string.taffy_providers_block_not_actionable),
        ).assertExists()
    }

    @Test
    fun everyTabIsAlwaysDrawnAndOnlyOneCategoryIsOnScreen() {
        setContent(
            projected(
                rows = listOf(
                    row("connected").copy(stored = credential()),
                    row("planned", methods = listOf(RosterAuthMethod.OAUTH)),
                    row("keyed"),
                    row("mine", origin = RosterProviderOrigin.CUSTOM),
                ),
            ),
        )

        // Every tab, whatever the roster holds: a row of tabs that changes
        // length as providers arrive is a row nobody can learn. Counted from
        // the enum rather than written out, because this case was named for
        // four tabs and there are three.
        compose.onNodeWithTag(PROVIDER_HUB_TABS_TEST_TAG).assertExists()
        ProviderHubGroup.entries.forEach { group ->
            compose.onNodeWithTag(tabTag(group)).assertExists().assertHasClickAction()
        }
        // The screen opens on the first category that holds anything, and with
        // this roster that is Bring your own key. "planned" carries OAUTH but
        // not `subscription`, and a vendor whose exchange mints a metered key
        // is filed with the keys it is billed like, so both rows sit in that
        // one category and are on screen together.
        compose.onNodeWithTag("${PROVIDER_ROW_TEST_TAG_PREFIX}keyed").assertExists()
        compose.onNodeWithTag("${PROVIDER_ROW_TEST_TAG_PREFIX}planned").assertExists()
        // A row of another category is not on screen with them. Sound as an
        // absence assertion despite the LazyColumn, because the list is built
        // from rows already filtered to the showing category: this row is
        // absent from the list itself rather than merely below the fold.
        compose.onNodeWithTag("${PROVIDER_ROW_TEST_TAG_PREFIX}mine").assertDoesNotExist()
        // And a provider that is already connected is offered under no
        // category at all. The hub is what there is to add;
        // ProviderRowDispatch.groupsFor returns no groups for any row whose
        // credential availability is not ABSENT, and SCR-419 is where a
        // connected provider is read instead.
        compose.onNodeWithTag("${PROVIDER_ROW_TEST_TAG_PREFIX}connected").assertDoesNotExist()
    }

    @Test
    fun pressingATabAsksForThatCategoryAndGoesNowhere() {
        setContent(
            projected(
                rows = listOf(row("keyed"), row("mine", origin = RosterProviderOrigin.CUSTOM)),
            ),
        )

        compose
            .onNodeWithTag(tabTag(ProviderHubGroup.YOUR_OWN_ENDPOINT))
            .performClick()

        assertEquals(
            listOf(ProviderHubIntent.ShowCategory(ProviderHubGroup.YOUR_OWN_ENDPOINT)),
            intents,
        )
    }

    @Test
    fun aTabWithNothingUnderItSaysSoRatherThanDrawingNothing() {
        val state = projected(rows = listOf(row("keyed")))
            .copy(showing = ProviderHubGroup.SUBSCRIPTION)

        setContent(state)

        compose
            .onNodeWithTag(
                PROVIDER_CATEGORY_EMPTY_TEST_TAG_PREFIX + ProviderHubGroup.SUBSCRIPTION.name,
            )
            .assertExists()
        compose.onNodeWithText(
            context.getString(R.string.taffy_providers_group_empty_subscription_body),
        ).assertExists()
        compose.onNodeWithTag(PROVIDER_HUB_LIST_TEST_TAG).assertDoesNotExist()
        // The tabs are still there, so an empty category is one press from a
        // full one rather than a dead end.
        compose.onNodeWithTag(PROVIDER_HUB_TABS_TEST_TAG).assertExists()
    }

    @Test
    fun aKeyRowIsPressableAndAsksToOpenItsOwnPage() {
        setContent(projected(rows = listOf(row("keyed"))))

        compose.onNodeWithTag("${PROVIDER_ROW_TEST_TAG_PREFIX}keyed")
            .assertExists()
            .assertHasClickAction()
            .performClick()

        assertEquals(
            listOf(ProviderRowOffer.Configure),
            intents.map { (it as ProviderHubIntent.OpenRow).row.offer },
        )
    }

    @Test
    fun aPlanRowThisBuildCannotRunIsShownAndCannotBePressed() {
        setContent(
            projected(
                rows = listOf(
                    row(SIGN_IN_VENDOR, methods = listOf(RosterAuthMethod.OAUTH)),
                    row("no-flow", methods = listOf(RosterAuthMethod.OAUTH)),
                ),
            ),
        )

        compose.onNodeWithTag("$PROVIDER_ROW_TEST_TAG_PREFIX$SIGN_IN_VENDOR")
            .assertHasClickAction()
        compose.onNodeWithTag("${PROVIDER_ROW_TEST_TAG_PREFIX}no-flow")
            .assertExists()
            .assertHasNoClickAction()
        compose.onNodeWithText(
            context.getString(R.string.taffy_providers_block_sign_in_not_built),
        ).assertExists()

        assertEquals(emptyList<ProviderHubIntent>(), intents)
    }

    // The two badges this screen used to draw — Connected, and Default beside
    // it — described providers that are set up, and a provider that is set up
    // is now on SCR-419 instead. So the assertion is that neither the badge nor
    // the row is here: a person adding a provider is not shown the ones they
    // already have, and the page says why it is blank.
    @Test
    fun aProviderThatIsAlreadySetUpIsNotOnThisScreen() {
        setContent(
            projected(
                rows = listOf(
                    row("only-one").copy(stored = credential()),
                    row("also-held").copy(
                        stored = credential(state = RosterCredentialState.REFRESH_FAILED),
                    ),
                ),
            ),
        )

        compose.onNodeWithTag("${PROVIDER_ROW_TEST_TAG_PREFIX}only-one").assertDoesNotExist()
        compose.onNodeWithTag("${PROVIDER_ROW_TEST_TAG_PREFIX}also-held").assertDoesNotExist()
        compose.onNodeWithTag(PROVIDER_HUB_ALL_CONNECTED_TEST_TAG).assertExists()
    }

    // A catalog nobody could read and a phone with everything set up draw the
    // same blank page and must not say the same thing.
    @Test
    fun anEmptyCatalogAndAFullyConnectedOneSayDifferentThings() {
        setContent(projected(rows = emptyList()))

        compose.onNodeWithTag(PROVIDER_HUB_EMPTY_TEST_TAG).assertExists()
        compose.onNodeWithTag(PROVIDER_HUB_ALL_CONNECTED_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun aProviderThisBuildHasNeverHeardOfStillDrawsAnHonestRow() {
        setContent(projected(rows = listOf(row("my-gateway"))), darkTheme = true)

        // No mark, no compiled sentence: a monogram and what the row would
        // take. It never borrows another vendor's identity.
        compose.onNodeWithTag("${PROVIDER_ROW_TEST_TAG_PREFIX}my-gateway")
            .assertExists()
            .assertHasClickAction()
        compose.onNodeWithText(
            context.getString(R.string.taffy_providers_way_key),
        ).assertExists()
    }

    @Test
    fun addingAProviderOfItsOwnIsAlwaysReachable() {
        setContent(projected(rows = listOf(row("keyed"))))

        compose.onNodeWithTag(PROVIDER_HUB_ADD_TEST_TAG).performClick()

        assertEquals(listOf(ProviderHubIntent.AddYourOwnProvider), intents)
    }

    @Test
    fun theRowsTheTabsAndTheAddActionSurviveTwiceTheFontSize() {
        setContent(projected(rows = listOf(row("keyed"))), fontScale = 2f)

        compose.onNodeWithTag("${PROVIDER_ROW_TEST_TAG_PREFIX}keyed").assertIsDisplayed()
        compose.onNodeWithTag(PROVIDER_HUB_ADD_TEST_TAG).assertIsDisplayed()
        // The track scrolls rather than shrinking its tabs, so the one being
        // read is on screen at any font size.
        compose.onNodeWithTag(tabTag(ProviderHubGroup.BRING_YOUR_OWN_KEY)).assertIsDisplayed()
    }

    private companion object {
        /**
         * A vendor the fixtures declare a compiled flow for. Named here rather
         * than taken from the compiled map, so these tests keep testing the
         * rule when that map changes.
         */
        const val SIGN_IN_VENDOR = "a-vendor"

        /** One tab's tag, spelled once. */
        fun tabTag(group: ProviderHubGroup): String =
            PROVIDER_TAB_TEST_TAG_PREFIX + group.name

        fun credential(
            state: RosterCredentialState = RosterCredentialState.USABLE,
            account: String? = null,
            plan: String? = null,
        ) = RosterStoredCredential(
            authMethod = RosterAuthMethod.API_KEY,
            state = state,
            subscriptionBacked = false,
            accountLabel = account,
            planLabel = plan,
        )

        fun row(
            providerId: String,
            methods: List<RosterAuthMethod> = listOf(RosterAuthMethod.API_KEY),
            origin: RosterProviderOrigin = RosterProviderOrigin.CATALOG,
            enabled: Boolean = true,
            configurable: Boolean = true,
        ): ProviderRosterRow = ProviderRosterRow(
            providerId = providerId,
            displayName = providerId,
            origin = origin,
            authMethods = methods,
            stored = null,
            signingIn = false,
            enabled = enabled,
            configurable = configurable,
            subscription = false,
            catalogLayer = RosterCatalogLayer.EMBEDDED_BASELINE,
            selectedModelId = null,
            thinking = null,
            presentation = null,
            endpointBase = null,
            lastRefusal = null,
            modelCount = 0,
        )
    }
}
