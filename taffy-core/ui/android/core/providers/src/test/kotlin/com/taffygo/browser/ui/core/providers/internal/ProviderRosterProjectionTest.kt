// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.providers.internal

import com.taffygo.browser.ui.core.model.ModelInputModality
import com.taffygo.browser.ui.core.model.ModelRole
import com.taffygo.browser.ui.core.model.RosterAuthMethod
import com.taffygo.browser.ui.core.model.RosterCatalogLayer
import com.taffygo.browser.ui.core.model.RosterCredentialState
import com.taffygo.browser.ui.core.model.RosterProviderOrigin
import com.taffygo.browser.ui.core.model.RosterProviderRefusal
import com.taffygo.browser.ui.core.model.ThinkingLevel
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.CatalogLayerView
import taffy.core_api.CoreAvailability
import taffy.core_api.CoreStatus
import taffy.core_api.InputModalityView
import taffy.core_api.ModelRoleView
import taffy.core_api.ProviderAuthMethodView
import taffy.core_api.ProviderCredentialStateView
import taffy.core_api.ProviderModelView
import taffy.core_api.ProviderOriginView
import taffy.core_api.ProviderPresentationView
import taffy.core_api.ProviderRefusalStateView
import taffy.core_api.ProviderRefusalView
import taffy.core_api.ProviderRosterEntry
import taffy.core_api.StoredCredentialView
import taffy.core_api.ThinkingLevelView
import taffy.core_api.ThinkingPreferenceView

internal class ProviderRosterProjectionTest {

    private fun status(
        availability: CoreAvailability,
        roster: List<ProviderRosterEntry> = emptyList(),
        models: List<ProviderModelView> = emptyList(),
    ) = CoreStatus(
        availability = availability,
        generation = 1u,
        active_tasks = emptyList(),
        auth_state = null,
        workspaces = emptyList(),
        workspace_export = null,
        asset_delivery = null,
        provider_roster = roster,
        provider_probes = emptyList(),
        provider_models = models,
        library = taffy.core_api.LibraryViewState(
            availability = taffy.core_api.LibraryAvailability.AVAILABLE,
            revision = 0uL,
            entries = emptyList(),
            search = null,
            refresh_previews = emptyList(),
            refresh_results = emptyList(),
        ),
        library_export = null,
        memory = taffy.core_api.MemoryViewState(
            availability = taffy.core_api.MemoryAvailability.AVAILABLE,
            revision = 0uL,
            records = emptyList(),
            search = null,
        ),
        assistant_configuration = taffy.core_api.AssistantConfigurationView(
            revision = 0uL,
            disabled_abilities = emptyList(),
            preset = taffy.core_api.PersonalityPresetView.CAREFUL_RESEARCHER,
            pace = 0u,
            length = 1u,
            check_in = 0u,
        ),
        saved_sign_ins = taffy.core_api.SavedSignInsView(
            availability = taffy.core_api.SavedDataAvailability.UNAVAILABLE,
            revision = 0uL,
            records = emptyList(),
        ),
        saved_details = taffy.core_api.SavedDetailsView(
            availability = taffy.core_api.SavedDataAvailability.UNAVAILABLE,
            revision = 0uL,
            people = emptyList(),
        ),
        site_skills = emptyList(),
        builtin_skills = emptyList(),
        projection_mode = taffy.core_api.CoreStatusProjectionMode.COMPLETE,
        projection_omissions = emptyList(),
    )

    private fun entry(
        providerId: String = "anthropic",
        stored: StoredCredentialView? = null,
        layer: CatalogLayerView = CatalogLayerView.EMBEDDED_BASELINE,
        selectedModelId: String? = null,
        thinking: ThinkingPreferenceView? = null,
        presentation: ProviderPresentationView? = null,
        endpointBase: String? = null,
        lastRefusal: ProviderRefusalStateView? = null,
        modelCount: UInt = 0u,
    ) = ProviderRosterEntry(
        provider_id = providerId,
        display_name = "Anthropic",
        origin = ProviderOriginView.CATALOG,
        auth_methods = listOf(ProviderAuthMethodView.API_KEY, ProviderAuthMethodView.OAUTH),
        stored = stored,
        signing_in = false,
        enabled = true,
        endpoint_host = null,
        configurable = true,
        // Both of these still exist on the record and leave at the contract
        // reset; nothing varies them because the projection no longer reads
        // either (decision 0200 left no served catalog to move an address).
        endpoint_changed = false,
        catalog_layer = layer,
        selected_model_id = selectedModelId,
        thinking = thinking,
        presentation = presentation,
        endpoint_base = endpointBase,
        last_refusal = lastRefusal,
        model_count = modelCount,
        refused_endpoint_host = null,
        subscription = false,
    )

    private fun model(
        providerId: String,
        modelId: String,
        thinkingLevels: List<ThinkingLevelView> = emptyList(),
    ) = ProviderModelView(
        provider_id = providerId,
        model_id = modelId,
        display_name = modelId.uppercase(),
        context_window = 200_000uL,
        max_output_tokens = 64_000uL,
        reasoning = true,
        tool_calling = true,
        roles = listOf(ModelRoleView.PRIMARY_REASONING, ModelRoleView.VISION),
        input_modalities = listOf(InputModalityView.TEXT, InputModalityView.IMAGE),
        thinking_levels = thinkingLevels,
    )

    @Test
    fun `a starting core's empty roster reads as not told yet`() {
        val projected = status(CoreAvailability.STARTING).toRosterState()
        assertFalse(projected.ready)
        assertTrue(projected.rows.isEmpty())
    }

    @Test
    fun `a ready snapshot carries every roster fact across unchanged`() {
        val projected = status(
            CoreAvailability.READY,
            listOf(
                entry(
                    stored = StoredCredentialView(
                        auth_method = ProviderAuthMethodView.OAUTH,
                        state = ProviderCredentialStateView.NEEDS_SIGN_IN,
                        subscription_backed = true,
                        account_label = "reader@example.test",
                        plan_label = "Pro",
                    ),
                    layer = CatalogLayerView.REMOTE_OVERLAY,
                    selectedModelId = "claude-sonnet",
                    thinking = ThinkingPreferenceView(ThinkingLevelView.HIGH),
                    presentation = ProviderPresentationView(
                        key_prefix = "sk-ant-",
                        get_key_url = "https://console.example.test/keys",
                        docs_url = "https://docs.example.test",
                    ),
                    endpointBase = "http://192.168.1.9:11434/v1",
                    lastRefusal = ProviderRefusalStateView(
                        refusal = ProviderRefusalView.BILLING,
                        at_monotonic_ms = 9_000uL,
                    ),
                    modelCount = 34u,
                ),
            ),
            models = listOf(model("anthropic", "claude-opus")),
        ).toRosterState()
        assertTrue(projected.ready)
        val row = projected.rows.single()
        assertEquals("anthropic", row.providerId)
        assertEquals("Anthropic", row.displayName)
        assertEquals(RosterProviderOrigin.CATALOG, row.origin)
        assertEquals(
            listOf(RosterAuthMethod.API_KEY, RosterAuthMethod.OAUTH),
            row.authMethods,
        )
        val stored = row.stored ?: error("stored facts must survive projection")
        assertEquals(RosterAuthMethod.OAUTH, stored.authMethod)
        assertEquals(RosterCredentialState.NEEDS_SIGN_IN, stored.state)
        assertTrue(stored.subscriptionBacked)
        assertEquals("reader@example.test", stored.accountLabel)
        assertEquals("Pro", stored.planLabel)
        assertTrue(row.configurable)
        assertEquals(RosterCatalogLayer.REMOTE_OVERLAY, row.catalogLayer)
        assertEquals("claude-sonnet", row.selectedModelId)
        assertEquals(ThinkingLevel.HIGH, row.thinking)
        val presentation = row.presentation ?: error("presentation must survive projection")
        assertEquals("sk-ant-", presentation.keyPrefix)
        assertEquals("https://console.example.test/keys", presentation.getKeyUrl)
        assertEquals("https://docs.example.test", presentation.docsUrl)
        // The whole address, not the host: a base with a port and a path is the
        // thing an edit screen has to be able to fill a field back in with.
        assertEquals("http://192.168.1.9:11434/v1", row.endpointBase)
        val refusal = row.lastRefusal ?: error("a refusal the core named must survive projection")
        assertEquals(RosterProviderRefusal.BILLING, refusal.refusal)
        assertEquals(9_000uL, refusal.atMonotonicMs)
        // The count is the core's, not this snapshot's row count: the flat list
        // carries one model for this provider and the catalog carries 34. A
        // projection that counted the list instead would say 1 and lose the
        // thirty-three rows the shared budget took.
        assertEquals(34, row.modelCount)
    }

    /**
     * The gap the field exists to state, read the way a surface would read it:
     * the roster says thirty-four and the flat list holds one, so thirty-three
     * of this provider's models were taken by the budget every provider shares.
     * A row model that dropped the count could only ever say "one".
     */
    @Test
    fun `the count and the flat list together say how many rows were lost`() {
        val snapshot = status(
            CoreAvailability.READY,
            roster = listOf(entry(providerId = "anthropic", modelCount = 34u)),
            models = listOf(model("anthropic", "claude-opus")),
        )
        val row = snapshot.toRosterState().rows.single()
        val shown = snapshot.toModelsByProvider()[row.providerId].orEmpty().size
        assertEquals(34, row.modelCount)
        assertEquals(1, shown)
        assertEquals(33, row.modelCount - shown)
    }

    /**
     * Nothing in the merged catalog is zero, and it stays zero rather than
     * becoming the row count of an empty list by another name: a provider with
     * no models is a different answer from a provider whose models were cut.
     */
    @Test
    fun `a provider the catalog names no model for counts zero`() {
        val row = status(CoreAvailability.READY, listOf(entry())).toRosterState().rows.single()
        assertEquals(0, row.modelCount)
    }

    /**
     * Each member of the refusal enum reaches the app's own vocabulary as
     * itself. It is a closed enum on both sides, so a mapping that folded two
     * members together would tell a person to top up an account that is merely
     * busy.
     */
    @Test
    fun `every refusal the vendor can name projects to its own member`() {
        val projected = ProviderRefusalView.entries.map { refusal ->
            status(
                CoreAvailability.READY,
                listOf(
                    entry(
                        lastRefusal = ProviderRefusalStateView(
                            refusal = refusal,
                            at_monotonic_ms = 1uL,
                        ),
                    ),
                ),
            ).toRosterState().rows.single().lastRefusal?.refusal
        }
        assertEquals(RosterProviderRefusal.entries.toList(), projected)
    }

    /**
     * A catalog row carries no address and no refusal, and both stay absent
     * rather than acquiring a stand-in: an empty string is an address a person
     * could have typed, and a refusal nobody made is not a rate limit.
     */
    @Test
    fun `a row with no address or refusal projects as having none`() {
        val row = status(CoreAvailability.READY, listOf(entry())).toRosterState().rows.single()
        assertNull(row.endpointBase)
        assertNull(row.lastRefusal)
    }

    @Test
    fun `nothing stored projects as nothing stored`() {
        val projected =
            status(CoreAvailability.READY, listOf(entry())).toRosterState()
        assertNull(projected.rows.single().stored)
    }

    /**
     * Each of the three is a distinct absence the core states by leaving the
     * field out, and none of them may acquire a stand-in on this side: no
     * pinned model, no chosen rung, and nothing the catalog said about setup.
     */
    @Test
    fun `an unchosen preference and a silent catalog project as absences`() {
        val row = status(CoreAvailability.READY, listOf(entry())).toRosterState().rows.single()
        assertNull(row.selectedModelId)
        assertNull(row.thinking)
        assertNull(row.presentation)
    }

    @Test
    fun `a vendor that named no account or plan projects as naming neither`() {
        val projected = status(
            CoreAvailability.READY,
            listOf(
                entry(
                    stored = StoredCredentialView(
                        auth_method = ProviderAuthMethodView.API_KEY,
                        state = ProviderCredentialStateView.USABLE,
                        subscription_backed = false,
                        account_label = null,
                        plan_label = null,
                    ),
                ),
            ),
        ).toRosterState()
        val stored = projected.rows.single().stored ?: error("a stored credential was named")
        assertNull(stored.accountLabel)
        assertNull(stored.planLabel)
    }

    @Test
    fun `every rung of the thinking ladder projects to its own rung`() {
        val rungs = ThinkingLevelView.entries.map { rung ->
            status(
                CoreAvailability.READY,
                listOf(entry(thinking = ThinkingPreferenceView(rung))),
            ).toRosterState().rows.single().thinking
        }
        assertEquals(ThinkingLevel.entries.toList(), rungs)
    }

    @Test
    fun `the flat model list groups under its provider in the order it arrived`() {
        val grouped = status(
            CoreAvailability.READY,
            models = listOf(
                model("anthropic", "claude-opus"),
                model("anthropic", "claude-haiku"),
                model("openai", "gpt-mini"),
            ),
        ).toModelsByProvider()

        assertEquals(listOf("anthropic", "openai"), grouped.keys.toList())
        assertEquals(
            listOf("claude-opus", "claude-haiku"),
            grouped["anthropic"].orEmpty().map { it.modelId },
        )
        assertEquals(listOf("gpt-mini"), grouped["openai"].orEmpty().map { it.modelId })
    }

    @Test
    fun `a provider the core named no model for has no entry at all`() {
        val grouped = status(
            CoreAvailability.READY,
            roster = listOf(entry(providerId = "anthropic")),
            models = listOf(model("openai", "gpt-mini")),
        ).toModelsByProvider()

        assertNull(grouped["anthropic"])
    }

    @Test
    fun `every model fact survives the projection`() {
        val grouped = status(
            CoreAvailability.READY,
            models = listOf(
                model(
                    "anthropic",
                    "claude-opus",
                    thinkingLevels = listOf(
                        ThinkingLevelView.OFF,
                        ThinkingLevelView.LOW,
                        ThinkingLevelView.MAX,
                    ),
                ),
            ),
        ).toModelsByProvider()

        val projected = grouped["anthropic"].orEmpty().single()
        assertEquals("anthropic", projected.providerId)
        assertEquals("claude-opus", projected.modelId)
        assertEquals("CLAUDE-OPUS", projected.displayName)
        assertEquals(200_000uL, projected.contextWindow)
        assertEquals(64_000uL, projected.maxOutputTokens)
        assertTrue(projected.reasoning)
        assertTrue(projected.toolCalling)
        assertEquals(
            listOf(ModelRole.PRIMARY_REASONING, ModelRole.VISION),
            projected.roles,
        )
        assertEquals(
            listOf(ModelInputModality.TEXT, ModelInputModality.IMAGE),
            projected.inputModalities,
        )
        assertEquals(
            listOf(ThinkingLevel.OFF, ThinkingLevel.LOW, ThinkingLevel.MAX),
            projected.thinkingLevels,
        )
    }

    @Test
    fun `unrelated status publication keeps both provider projection versions`() {
        val baseline = status(
            CoreAvailability.READY,
            roster = listOf(entry()),
            models = listOf(model("anthropic", "claude-opus")),
        )
        val unrelated = baseline.copy(generation = 9uL)

        assertEquals(
            baseline.providerRosterProjectionVersion(),
            unrelated.providerRosterProjectionVersion(),
        )
        assertEquals(
            baseline.providerModelsProjectionVersion(),
            unrelated.providerModelsProjectionVersion(),
        )
        assertNotEquals(
            baseline.providerRosterProjectionVersion(),
            baseline.copy(provider_roster = listOf(entry(modelCount = 34u)))
                .providerRosterProjectionVersion(),
        )
        assertNotEquals(
            baseline.providerModelsProjectionVersion(),
            baseline.copy(provider_models = emptyList()).providerModelsProjectionVersion(),
        )
    }
}
