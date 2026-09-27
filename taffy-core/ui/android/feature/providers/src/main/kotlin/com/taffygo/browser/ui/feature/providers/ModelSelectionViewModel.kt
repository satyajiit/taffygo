// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.credentials.ProviderCredentialsRepository
import com.taffygo.browser.ui.core.model.ThinkingLevel
import com.taffygo.browser.ui.core.providers.ProviderModelPreferences
import com.taffygo.browser.ui.core.providers.ProviderRosterRepository
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import javax.inject.Inject
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

/**
 * Screen SCR-417's one source of truth.
 *
 * The list comes from the published roster, the published catalog and the
 * browser's own handles; the only thing this view model keeps is the draft —
 * what was typed, what is disclosed, and which command was last sent. A press
 * goes straight out through the seam that owns the choice, so there is no
 * in-flight copy of the answer and no way to disagree with the core about what
 * stands. A refused preference simply never comes back, and the screen goes on
 * showing what does.
 */
class ModelSelectionViewModel @Inject constructor(
    private val roster: ProviderRosterRepository,
    private val credentials: ProviderCredentialsRepository,
    private val preferences: ProviderModelPreferences,
    private val analytics: AnalyticsClient,
    savedState: SavedStateHandle,
) : ViewModel() {

    /**
     * Which provider this screen is narrowed to, or null for the whole catalog.
     *
     * The destination carries no provider for the catalog-wide shape, so an
     * absent argument is that shape rather than a missing one — which is why it
     * is read as a nullable rather than defaulted to the empty string.
     */
    private val providerId: String? = savedState.get<String>(TaffyDestination.PROVIDER_ID)

    private val draft = MutableStateFlow(ModelSelectionDraft())

    /** What screen SCR-417 renders. */
    val state: StateFlow<ModelSelectionUiState> = combine(
        roster.roster,
        roster.models,
        credentials.configuredProviderIds,
        draft,
    ) { rosterState, models, heldCredentialIds, local ->
        ModelSelectionProjection.project(
            providerId = providerId,
            roster = rosterState,
            models = models,
            browserHeldCredentialIds = heldCredentialIds,
            draft = local,
        )
    }
        .stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
            initialValue = ModelSelectionUiState(providerId = providerId),
        )

    /** Record the screen. The roster arrives by itself; nothing is loaded. */
    fun onShown() {
        analytics.record(
            AnalyticsEvent.ScreenShown(TaffyDestination.ModelSelection(providerId).screenId),
        )
    }

    /**
     * Act on something the person did.
     *
     * Both choices reach the same single command, because both state the same
     * single thing. Choosing a model names the thinking level that should stand
     * with it, and choosing a rung names the model it stands for; either one
     * sent alone would clear the other, since the command is the whole choice
     * rather than a change to it.
     *
     * A locked provider's row is the exception, and it never sends a command at
     * all: it navigates to where the missing credential can be supplied.
     */
    fun onIntent(intent: ModelSelectionIntent, navigator: TaffyNavigator) {
        draft.update { ModelSelectionReducer.reduce(it, intent) }
        when (intent) {
            is ModelSelectionIntent.ChooseModel -> if (!intent.row.locked) {
                choose(
                    providerId = intent.row.providerId,
                    modelId = intent.row.modelId,
                    thinking = intent.row.thinkingAfterChoosing,
                )
            }

            is ModelSelectionIntent.ChooseThinking -> if (!intent.row.locked) {
                choose(
                    providerId = intent.row.providerId,
                    modelId = intent.row.modelId,
                    thinking = intent.level,
                )
            }

            is ModelSelectionIntent.OpenProvider ->
                ProviderRowDispatch
                    .destinationFor(intent.block.providerId, intent.block.offer)
                    ?.let(navigator::goTo)

            is ModelSelectionIntent.Search, ModelSelectionIntent.ToggleLocked -> Unit
        }
    }

    private fun choose(providerId: String, modelId: String?, thinking: ThinkingLevel?) {
        viewModelScope.launch {
            try {
                preferences.choose(providerId, modelId, thinking)
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (_: Exception) {
                // The command never left, so no roster will ever answer it.
                // Nothing on the page was drawn from what was sent, so dropping
                // the ask restores exactly the page that was there before.
                draft.update { ModelSelectionReducer.abandon(it) }
            }
        }
    }

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
    }
}
