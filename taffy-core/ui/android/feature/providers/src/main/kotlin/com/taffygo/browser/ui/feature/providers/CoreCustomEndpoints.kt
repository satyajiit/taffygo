// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.api.hasCompleteProjection
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.mapNotNull
import kotlinx.coroutines.withTimeoutOrNull
import taffy.core_api.CustomModelSpecView
import taffy.core_api.DetectedServerView
import taffy.core_api.ProbeEndpointView
import taffy.core_api.ProviderProbeVerdictView
import taffy.core_api.ProviderProbeView
import taffy.core_api.ProviderWireApiView
import taffy.core_api.ServerKindView

/**
 * Screen SCR-418's seam over the browser Core API.
 *
 * Two commands and one wait. The commands are `probeCustomEndpoint` and
 * `saveCustomProvider`; the wait is for the verdict, which does not come back
 * from the call — the core publishes it on the next status snapshot's
 * `provider_probes`, exactly as decision 0083's key probe does.
 *
 * **Which row is ours.** The same answer as the key probe's, now that Core API
 * 3.18 lets the probe name the draft identity its verdict is filed under: the
 * row for that provider held before the ask, and the first row for that
 * provider which differs from it. Until 3.18 this screen matched by a row
 * being *new*, which held only for as long as exactly one probe was ever in
 * flight — true by the core's own backpressure rule, and true for a reason
 * that had nothing to do with this screen.
 */
internal class CoreCustomEndpoints(
    private val core: CoreApiClient,
    private val verdictWaitMillis: Long = PROBE_VERDICT_WAIT_MILLIS,
) : CustomEndpoints {

    override val hosts: Flow<Map<String, String>> = core.status
        .distinctUntilChanged { previous, current ->
            previous.hasCompleteProjection() == current.hasCompleteProjection() &&
                sameProviderHostProjection(previous.provider_roster, current.provider_roster)
        }
        .map { status ->
            status.provider_roster.takeIf { status.hasCompleteProjection() }.orEmpty()
                .mapNotNull { entry ->
                entry.endpoint_host?.let { entry.provider_id to it }
            }.toMap()
        }
        .distinctUntilChanged()

    override val addresses: Flow<Map<String, String>> = core.status
        .distinctUntilChanged { previous, current ->
            previous.hasCompleteProjection() == current.hasCompleteProjection() &&
                sameProviderAddressProjection(previous.provider_roster, current.provider_roster)
        }
        .map { status ->
            status.provider_roster.takeIf { status.hasCompleteProjection() }.orEmpty()
                .mapNotNull { entry ->
                entry.endpoint_base?.let { entry.provider_id to it }
            }.toMap()
        }
        .distinctUntilChanged()

    /**
     * Ask the address what it is, with a credential where the server wants one.
     *
     * [credentialHandle] is opaque here as everywhere: the name of a record the
     * browser's secure store already holds, sealed before this call. Absent is
     * the ordinary answer — a model server on somebody's own machine usually
     * asks for nothing — and is what a keyless endpoint has always sent.
     */
    override suspend fun probe(
        address: String,
        providerId: String,
        credentialHandle: String?,
    ): CustomEndpointOutcome {
        val currentStatus = core.status.value
        if (!currentStatus.hasCompleteProjection()) {
            return refused(CustomEndpointOutcome.Problem.NO_ANSWER)
        }
        val before = currentStatus.provider_probes
            .firstOrNull { it.provider_id == providerId }
        try {
            core.probeCustomEndpoint(
                address,
                WIRE_API,
                credentialHandle = credentialHandle,
                providerId = providerId,
            )
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (_: Exception) {
            // The core would not run it — a probe already in flight, or a core
            // that is not ready. Nothing was asked, so nothing was learned.
            return CustomEndpointOutcome.Refused(CustomEndpointOutcome.Problem.NO_ANSWER)
        }
        val landed = withTimeoutOrNull(verdictWaitMillis) {
            core.status
                .mapNotNull { status ->
                    status.takeIf { it.hasCompleteProjection() }
                        ?.provider_probes
                        ?.firstOrNull { it.provider_id == providerId }
                }
                .first { it != before }
        }
        return outcomeOf(landed)
    }

    /**
     * One write, with the models and the detected runtime on it.
     *
     * Every figure is the probe's. Before Core API 3.18 this screen had only a
     * count and had to file a placeholder per model with a zero window and no
     * capabilities — the only honest thing it could say, and still a claim
     * about a server nobody had read. The probe now carries the specs, so the
     * write carries them too and the merged catalog holds what is actually
     * there.
     *
     * The credential travels on the same command, as the name of a sealed
     * record. Passing it here rather than sending a second command is the whole
     * of section 4: a provider that was defined and then failed to be given its
     * key would be a row a person can see, select and never reach.
     */
    override suspend fun save(
        providerId: String,
        displayName: String,
        address: String,
        models: List<CustomEndpointOutcome.Model>,
        server: CustomEndpointOutcome.ServerKind?,
        credentialHandle: String?,
    ) {
        check(core.status.value.hasCompleteProjection()) {
            "Core projection is unavailable"
        }
        core.saveCustomProvider(
            providerId = providerId,
            displayName = displayName,
            endpoint = address,
            wireApi = WIRE_API,
            credentialHandle = credentialHandle,
            models = models.map { it.toWire() },
            detectedServer = server?.let { DetectedServerView(it.toWire()) },
        )
    }

    override suspend fun remove(providerId: String) {
        check(core.status.value.hasCompleteProjection()) {
            "Core projection is unavailable"
        }
        core.removeCustomProvider(providerId)
    }

    /** The probe row read as an answer about an address. */
    private fun outcomeOf(row: ProviderProbeView?): CustomEndpointOutcome = when (row?.verdict) {
        ProviderProbeVerdictView.ENDPOINT_REACHED -> row.endpoint.toReached()
        ProviderProbeVerdictView.AUTH -> refused(CustomEndpointOutcome.Problem.WANTS_A_CREDENTIAL)
        ProviderProbeVerdictView.NETWORK -> refused(CustomEndpointOutcome.Problem.NOTHING_ANSWERED)
        ProviderProbeVerdictView.TIMEOUT, null -> refused(CustomEndpointOutcome.Problem.NO_ANSWER)
        // Everything else is a verdict about a credential rather than about an
        // address, and none of them names what is at the far end. What they do
        // all say is the one thing this screen can report: something answered
        // and no model listing was read out of it.
        else -> refused(CustomEndpointOutcome.Problem.NOT_A_MODEL_SERVER)
    }

    /**
     * Reached, with what the server said about itself.
     *
     * A verdict that says reached and carries no detail has named neither a
     * runtime nor a count, so it is read as the answer it actually is rather
     * than filled in with a runtime nobody reported.
     *
     * The count and the list cross separately and stay separate. `model_count`
     * is what the server named; `models` is what survived the contract's bound.
     * Deriving either from the other is the silent truncation decisions 0096
     * section 5 and 0098 section 4 both refuse.
     */
    private fun ProbeEndpointView?.toReached(): CustomEndpointOutcome =
        this?.let {
            CustomEndpointOutcome.Reached(
                server = it.server_kind.toApp(),
                modelCount = it.model_count.toInt(),
                models = it.models.map { spec -> spec.toApp() },
                provedBase = it.proved_base,
            )
        } ?: refused(CustomEndpointOutcome.Problem.NOT_A_MODEL_SERVER)

    private fun refused(problem: CustomEndpointOutcome.Problem): CustomEndpointOutcome =
        CustomEndpointOutcome.Refused(problem)

    private companion object {
        /**
         * The wire family every runtime this screen is for speaks.
         *
         * Ollama, LM Studio, vLLM and llama.cpp all serve the OpenAI-shaped
         * chat completion, which is why this page offers no wire-family
         * chooser: a control with one honest answer is a control that only
         * gives a person a way to get it wrong.
         */
        val WIRE_API = ProviderWireApiView.OPEN_AI_COMPLETIONS

        /**
         * How long a verdict is awaited. The probe effect carries the command
         * deadline the core enforces and files a timeout verdict when it
         * passes, so this is a margin over that bound rather than a second
         * bound of its own — the same margin the key probe takes.
         */
        const val PROBE_VERDICT_WAIT_MILLIS = 45_000L

        fun ServerKindView.toApp(): CustomEndpointOutcome.ServerKind = when (this) {
            ServerKindView.OPENAI_COMPATIBLE -> CustomEndpointOutcome.ServerKind.OPENAI_COMPATIBLE
            ServerKindView.OLLAMA -> CustomEndpointOutcome.ServerKind.OLLAMA
            ServerKindView.LM_STUDIO -> CustomEndpointOutcome.ServerKind.LM_STUDIO
            ServerKindView.VLLM -> CustomEndpointOutcome.ServerKind.VLLM
            ServerKindView.LLAMA_CPP -> CustomEndpointOutcome.ServerKind.LLAMA_CPP
        }

        fun CustomEndpointOutcome.ServerKind.toWire(): ServerKindView = when (this) {
            CustomEndpointOutcome.ServerKind.OPENAI_COMPATIBLE -> ServerKindView.OPENAI_COMPATIBLE
            CustomEndpointOutcome.ServerKind.OLLAMA -> ServerKindView.OLLAMA
            CustomEndpointOutcome.ServerKind.LM_STUDIO -> ServerKindView.LM_STUDIO
            CustomEndpointOutcome.ServerKind.VLLM -> ServerKindView.VLLM
            CustomEndpointOutcome.ServerKind.LLAMA_CPP -> ServerKindView.LLAMA_CPP
        }

        fun CustomModelSpecView.toApp(): CustomEndpointOutcome.Model =
            CustomEndpointOutcome.Model(
                modelId = model_id,
                displayName = display_name,
                contextWindow = context_window,
                maxOutputTokens = max_output_tokens,
                reasoning = reasoning,
                toolCalling = tool_calling,
            )

        fun CustomEndpointOutcome.Model.toWire(): CustomModelSpecView = CustomModelSpecView(
            model_id = modelId,
            display_name = displayName,
            context_window = contextWindow,
            max_output_tokens = maxOutputTokens,
            reasoning = reasoning,
            tool_calling = toolCalling,
        )
    }
}

/** The seam this feature uses when a core is in the graph. */
internal fun customEndpoints(core: CoreApiClient): CustomEndpoints = CoreCustomEndpoints(core)

internal fun sameProviderHostProjection(
    previous: List<taffy.core_api.ProviderRosterEntry>,
    current: List<taffy.core_api.ProviderRosterEntry>,
): Boolean = sameProviderEndpointProjection(previous, current) { it.endpoint_host }

internal fun sameProviderAddressProjection(
    previous: List<taffy.core_api.ProviderRosterEntry>,
    current: List<taffy.core_api.ProviderRosterEntry>,
): Boolean = sameProviderEndpointProjection(previous, current) { it.endpoint_base }

private inline fun sameProviderEndpointProjection(
    previous: List<taffy.core_api.ProviderRosterEntry>,
    current: List<taffy.core_api.ProviderRosterEntry>,
    value: (taffy.core_api.ProviderRosterEntry) -> String?,
): Boolean {
    if (previous.size != current.size) return false
    return previous.indices.all { index ->
        val before = previous[index]
        val after = current[index]
        before.provider_id == after.provider_id && value(before) == value(after)
    }
}
