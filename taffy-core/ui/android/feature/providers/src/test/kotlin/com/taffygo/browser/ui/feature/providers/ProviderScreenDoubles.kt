// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import android.graphics.Bitmap
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.browser.ErrandPage
import com.taffygo.browser.ui.core.browser.ErrandPagePort
import com.taffygo.browser.ui.core.browser.FilteringSettings
import com.taffygo.browser.ui.core.browser.NavigationState
import com.taffygo.browser.ui.core.browser.PageAppearance
import com.taffygo.browser.ui.core.browser.SiteFilteringPlane
import com.taffygo.browser.ui.core.browser.TabArtwork
import com.taffygo.browser.ui.core.credentials.ProviderCredentialsRepository
import com.taffygo.browser.ui.core.credentials.ProviderKeyProber
import com.taffygo.browser.ui.core.credentials.ProviderProbeVerdict
import com.taffygo.browser.ui.core.model.AddressBarInterpretation
import com.taffygo.browser.ui.core.model.AppLanguage
import com.taffygo.browser.ui.core.model.BrowserNotice
import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.model.ModelInputModality
import com.taffygo.browser.ui.core.model.ModelRole
import com.taffygo.browser.ui.core.model.NotificationTopic
import com.taffygo.browser.ui.core.model.ProviderModel
import com.taffygo.browser.ui.core.model.ProviderPresentation
import com.taffygo.browser.ui.core.model.ProviderRosterRow
import com.taffygo.browser.ui.core.model.ProviderRosterState
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.model.RosterAuthMethod
import com.taffygo.browser.ui.core.model.RosterCatalogLayer
import com.taffygo.browser.ui.core.model.RosterCredentialState
import com.taffygo.browser.ui.core.model.RosterProviderOrigin
import com.taffygo.browser.ui.core.model.RosterProviderRefusal
import com.taffygo.browser.ui.core.model.RosterRefusalState
import com.taffygo.browser.ui.core.model.RosterStoredCredential
import com.taffygo.browser.ui.core.model.Suggestion
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import com.taffygo.browser.ui.core.model.ThemePreference
import com.taffygo.browser.ui.core.model.ThinkingLevel
import com.taffygo.browser.ui.core.preferences.UserPreferences
import com.taffygo.browser.ui.core.preferences.UserPreferencesRepository
import com.taffygo.browser.ui.core.providers.ProviderModelPreferences
import com.taffygo.browser.ui.core.providers.ProviderRosterRepository
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * The seams screens SCR-415 and SCR-416 read, modelled only as far as those
 * screens use them.
 *
 * Shared between the two view-model suites because they exercise the same
 * seams from opposite ends — one saves and forgets a credential, the other
 * waits for one to arrive — and two sets of doubles would let the suites
 * disagree about what the seam does.
 */
internal class FakeProviderRoster : ProviderRosterRepository {
    private val state = MutableStateFlow(ProviderRosterState())
    private val catalog = MutableStateFlow(emptyMap<String, List<ProviderModel>>())

    override val roster: StateFlow<ProviderRosterState> = state.asStateFlow()
    override val models: StateFlow<Map<String, List<ProviderModel>>> = catalog.asStateFlow()

    fun publish(vararg rows: ProviderRosterRow) {
        state.value = ProviderRosterState(ready = true, rows = rows.toList())
    }

    fun publishModels(models: Map<String, List<ProviderModel>>) {
        catalog.value = models
    }
}

/** The browser's secure store, as a set of handles. */
internal class FakeProviderCredentials : ProviderCredentialsRepository {
    private val configured = MutableStateFlow(emptySet<String>())

    /** Every key handed to [saveApiKey], as text, so a suite can prove it arrived. */
    val saved = mutableListOf<String>()

    /**
     * Every key handed to [sealApiKey], as text, beside the provider it was
     * sealed under — so a suite can prove a seal happened and that no command
     * went with it.
     */
    val sealed = mutableListOf<Pair<String, String>>()

    /** Every provider [forget] was asked about. */
    val forgotten = mutableListOf<String>()

    /** Every provider [discardSealedKey] was asked about. */
    val discarded = mutableListOf<String>()

    /** Set to fail both writes, which is how a keystore refusal is exercised. */
    var refuses: Boolean = false

    override val configuredProviderIds: StateFlow<Set<String>> = configured.asStateFlow()

    fun hold(providerId: String) {
        configured.value = configured.value + providerId
    }

    override suspend fun saveApiKey(providerId: String, material: ByteArray) {
        if (refuses) error("the store refused")
        saved += material.decodeToString()
        hold(providerId)
    }

    /**
     * Seals and answers the handle, exactly as the profile store does: the
     * record's name is the provider it is filed under.
     */
    override suspend fun sealApiKey(providerId: String, material: ByteArray): String {
        if (refuses) error("the store refused")
        sealed += providerId to material.decodeToString()
        hold(providerId)
        return providerId
    }

    override suspend fun heldCredentialHandle(providerId: String): String? =
        providerId.takeIf { it in configured.value }

    override suspend fun discardSealedKey(providerId: String) {
        if (refuses) error("the store refused")
        discarded += providerId
        configured.value = configured.value - providerId
    }

    override suspend fun forget(providerId: String) {
        if (refuses) error("the store refused")
        forgotten += providerId
        configured.value = configured.value - providerId
    }
}

/** One bounded probe call, with the verdict the suite chooses. */
internal class FakeProviderProber(
    var verdict: ProviderProbeVerdict = ProviderProbeVerdict.USABLE,
) : ProviderKeyProber {
    /** Every provider a probe was spent on, so a suite can prove one was not. */
    val probed = mutableListOf<String>()

    /** Set to refuse the probe outright, as a core that will not run one does. */
    var refuses: Boolean = false

    override suspend fun probeApiKey(
        providerId: String,
        material: ByteArray,
    ): ProviderProbeVerdict {
        probed += providerId
        if (refuses) error("no probe may fly")
        return verdict
    }
}

/** The preferences, with the route this screen can move. */
internal class FakeProviderPreferences(
    route: ProviderRoute = ProviderRoute.NOT_CONFIGURED,
) : UserPreferencesRepository {
    private val stored = MutableStateFlow(UserPreferences(providerRoute = route))

    override val preferences: StateFlow<UserPreferences> = stored.asStateFlow()
    override suspend fun setTheme(theme: ThemePreference) = Unit
    override suspend fun setAppLanguage(language: AppLanguage) = Unit
    override suspend fun setRegionCode(regionCode: String) = Unit
    override suspend fun setPseudoLocalization(enabled: Boolean) = Unit
    override suspend fun setForceDarkWeb(enabled: Boolean) = Unit

    override suspend fun setProviderRoute(route: ProviderRoute) {
        stored.value = stored.value.copy(providerRoute = route)
    }

    override suspend fun setNotificationTopic(topic: NotificationTopic, enabled: Boolean) = Unit
    override suspend fun setOnboardingCompleted(completed: Boolean) = Unit
    override suspend fun setComposerSuggestions(enabled: Boolean) = Unit
}

/**
 * The standing-choice seam, recording the whole of every command.
 *
 * The triple is kept rather than folded into a map, because what these suites
 * have to be able to prove is that model and thinking travelled together in one
 * command — a store that merged them would agree with two writes as readily as
 * with one.
 */
internal class FakeProviderModelPreferences : ProviderModelPreferences {
    /** Every choice stated, in order.  */
    val stated = mutableListOf<Choice>()

    /** Set to refuse, as a core that will not take the command does. */
    var refuses: Boolean = false

    override suspend fun choose(
        providerId: String,
        modelId: String?,
        thinking: ThinkingLevel?,
    ) {
        if (refuses) error("the core refused the choice")
        stated += Choice(providerId, modelId, thinking)
    }

    /** One command, whole. */
    data class Choice(
        val providerId: String,
        val modelId: String?,
        val thinking: ThinkingLevel?,
    )
}

/** The browser seam with only the tab-opening half modelled. */
/**
 * The errand seam, recording what a screen asked to be opened.
 *
 * It replaces [FakeProviderBrowser] at the two call sites that used to prove a
 * vendor's page was opened in a tab, and it is a smaller double than that one
 * for the reason the change was made: a provider screen never needed a whole
 * browser, only somewhere to send a person for one errand.
 */
internal class FakeErrandPage : ErrandPagePort {
    /** Every address an errand was opened on, in order. */
    val opened = mutableListOf<String>()

    /** Set to false to act as a build with no engine behind it. */
    var opens: Boolean = true

    private val state = MutableStateFlow<ErrandPage?>(null)
    override val page: StateFlow<ErrandPage?> = state

    override suspend fun open(url: String): String? {
        if (!opens) return null
        opened += url
        val id = "errand-${opened.size}"
        state.value = ErrandPage(id, NavigationState(host = url, title = ""))
        return id
    }

    override fun goBack(errandId: String): Boolean = false

    override fun close(errandId: String) {
        if (state.value?.errandId == errandId) state.value = null
    }
}

internal class FakeProviderBrowser : BrowserRepository {
    /** Every address a tab was opened on. */
    val opened = mutableListOf<String>()

    override suspend fun openTab(host: String, isPrivate: Boolean): TabId {
        opened += host
        return TabId(host)
    }

    override val tabs: StateFlow<List<Tab>> = MutableStateFlow(emptyList())
    override val navigation: StateFlow<NavigationState> =
        MutableStateFlow(NavigationState(host = "", title = ""))
    override val pageAppearance: StateFlow<PageAppearance> = MutableStateFlow(PageAppearance())
    override val downloads: StateFlow<List<DownloadRecord>> = MutableStateFlow(emptyList())
    override val tabArtwork: StateFlow<Map<TabId, TabArtwork>> = MutableStateFlow(emptyMap())
    override val siteMarks: StateFlow<Map<String, Bitmap>> = MutableStateFlow(emptyMap())
    override val notice: StateFlow<BrowserNotice?> = MutableStateFlow(null)
    override val filtering: StateFlow<FilteringSettings> = MutableStateFlow(FilteringSettings())

    override fun resolve(input: String): AddressBarInterpretation =
        AddressBarInterpretation.GoTo(input, input)

    override fun suggestions(input: String): List<Suggestion> = emptyList()
    override suspend fun commit(interpretation: AddressBarInterpretation) = Unit
    override fun dismissNotice() = Unit
    override suspend fun selectTab(id: TabId) = Unit
    override suspend fun closeTab(id: TabId) = Unit
    override suspend fun goBack(): Boolean = false
    override suspend fun goForward(): Boolean = false
    override suspend fun reload() = Unit
    override suspend fun performDownloadAction(id: DownloadId, action: DownloadAction) = false
    override suspend fun requestSiteMarks(hosts: Collection<String>) = Unit
    override suspend fun setFilteringEnabled(enabled: Boolean) = Unit
    override suspend fun setSiteFilteringException(
        host: String,
        allow: Boolean,
        plane: SiteFilteringPlane,
    ): Boolean = false
    override suspend fun flushFilteringCounts() = Unit
}

/** Every event a screen recorded. */
internal class RecordingProviderAnalytics : AnalyticsClient {
    val recorded = mutableListOf<AnalyticsEvent>()

    override fun record(event: AnalyticsEvent) {
        recorded += event
    }

    override fun recent(): List<AnalyticsEvent> = recorded
}

/**
 * One roster row, with a working provider's defaults.
 *
 * No test names a shipped provider as though the catalog were compiled: the
 * catalog is served (decision 0080), so every fixture here is a shape rather
 * than a claim about what TaffyGo ships with.
 */
internal fun providerRow(
    providerId: String,
    displayName: String = providerId,
    authMethods: List<RosterAuthMethod> = listOf(RosterAuthMethod.API_KEY),
    stored: RosterStoredCredential? = null,
    enabled: Boolean = true,
    configurable: Boolean = true,
    subscription: Boolean = false,
    origin: RosterProviderOrigin = RosterProviderOrigin.CATALOG,
    catalogLayer: RosterCatalogLayer = RosterCatalogLayer.EMBEDDED_BASELINE,
    selectedModelId: String? = null,
    thinking: ThinkingLevel? = null,
    presentation: ProviderPresentation? = null,
    signingIn: Boolean = false,
    endpointBase: String? = null,
    lastRefusal: RosterRefusalState? = null,
    modelCount: Int = 0,
): ProviderRosterRow = ProviderRosterRow(
    providerId = providerId,
    displayName = displayName,
    origin = origin,
    authMethods = authMethods,
    stored = stored,
    signingIn = signingIn,
    enabled = enabled,
    configurable = configurable,
    subscription = subscription,
    catalogLayer = catalogLayer,
    selectedModelId = selectedModelId,
    thinking = thinking,
    presentation = presentation,
    endpointBase = endpointBase,
    lastRefusal = lastRefusal,
    modelCount = modelCount,
)

/**
 * One catalog model, with a working model's defaults.
 *
 * [thinkingLevels] is empty by default because most models offer no choice at
 * all: a fixture that handed every model a full ladder would make the "fewer
 * than two rungs draws nothing" rule invisible in every suite but its own.
 */
internal fun providerModel(
    providerId: String,
    modelId: String,
    displayName: String = modelId,
    roles: List<ModelRole> = listOf(ModelRole.PRIMARY_REASONING),
    thinkingLevels: List<ThinkingLevel> = emptyList(),
): ProviderModel = ProviderModel(
    providerId = providerId,
    modelId = modelId,
    displayName = displayName,
    contextWindow = 0uL,
    maxOutputTokens = 0uL,
    reasoning = true,
    toolCalling = true,
    roles = roles,
    inputModalities = listOf(ModelInputModality.TEXT),
    thinkingLevels = thinkingLevels,
)

/**
 * One refusal the vendor answered with, stamped unless a suite says otherwise.
 *
 * Stamped by default because that is what a surface sees: the profile-owned
 * repository dates every refusal it publishes, and the pure projection's
 * unstamped shape is the exception a suite asks for by name.
 */
internal fun refusal(
    reason: RosterProviderRefusal = RosterProviderRefusal.RATE_LIMIT,
    atMonotonicMs: ULong = 1_000uL,
    observedAtEpochMillis: Long? = 1_700_000_000_000L,
): RosterRefusalState = RosterRefusalState(
    refusal = reason,
    atMonotonicMs = atMonotonicMs,
    observedAtEpochMillis = observedAtEpochMillis,
)

/** One stored credential, usable unless a suite says otherwise. */
internal fun storedCredential(
    authMethod: RosterAuthMethod = RosterAuthMethod.API_KEY,
    state: RosterCredentialState = RosterCredentialState.USABLE,
    subscriptionBacked: Boolean = false,
    accountLabel: String? = null,
    planLabel: String? = null,
): RosterStoredCredential = RosterStoredCredential(
    authMethod = authMethod,
    state = state,
    subscriptionBacked = subscriptionBacked,
    accountLabel = accountLabel,
    planLabel = planLabel,
)

/** Every destination a screen asked for. */
internal class RecordingProviderNavigator : TaffyNavigator {
    val visited = mutableListOf<TaffyDestination>()

    override fun goTo(destination: TaffyDestination) {
        visited += destination
    }

    override fun replaceCurrent(destination: TaffyDestination) = Unit
    override fun goBack(): Boolean = false
    override fun goHome() = Unit
    override fun restart(destination: TaffyDestination) = Unit
    override fun popWhile(shouldPop: (TaffyDestination) -> Boolean) = Unit
}
