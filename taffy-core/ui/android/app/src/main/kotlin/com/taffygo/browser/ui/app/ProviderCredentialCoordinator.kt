// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.api.hasCompleteProjection
import com.taffygo.browser.ui.core.credentials.ProviderCredentialsRepository
import com.taffygo.browser.ui.core.credentials.ProviderKeyProber
import com.taffygo.browser.ui.core.credentials.ProviderProbeVerdict
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.mapNotNull
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeoutOrNull
import taffy.core_api.ProviderCredentialStateView
import taffy.core_api.ProviderProbeVerdictView

/**
 * The one writer of a provider credential, per decision
 * `docs/decisions/0078-a-provider-credential-has-one-writer.md`.
 *
 * Every provider-credential mutation — key save, forget, and the registry
 * state report; the sealed sign-in record ([completeSignIn]) and the rotated
 * refresh record ([storeRefreshedRecord]) when they land — runs inside one
 * critical section per provider id, held here and nowhere else, and so do the
 * reads whose answer must be one moment with the record it is about
 * ([resolveAccess], [heldCredentialHandle]). The probe is the deliberate
 * exception: [probeApiKey] writes nothing durable and runs outside the
 * section, for the reason given on it.
 * The store-first/command-second save ordering and the revoke-first forget
 * ordering stay exactly where [ProfileProviderCredentialsRepository] argues
 * them; this class wraps that pair rather than replacing it, and adds the one
 * thing the pair deliberately left open as OD-103: a save, a forget and a
 * refresh can no longer race one provider with the last writer winning
 * silently.
 *
 * The authoritative re-check runs *inside* the section, against the store as
 * it is at that moment. A state report that raced a forget acquires the lock
 * after the forget released it, re-reads the store, observes the record gone,
 * and drops itself — the person deleted the credential, and a report about a
 * record that no longer exists must observe the deletion rather than
 * resurrect its row. The core's own refusal of a state about nothing is the
 * backstop behind this check, not a substitute for it.
 *
 * Sections for different providers are independent: a slow save against one
 * vendor never queues a forget against another.
 */
class ProviderCredentialCoordinator(
    private val secureMaterial: AndroidProfileSecureMaterialStore,
    private val repository: ProfileProviderCredentialsRepository,
    private val coreApi: CoreApiClient,
    private val revocation: ProviderRevocationPort,
) : ProviderCredentialsRepository, ProviderKeyProber {

    private val sectionsLock = Any()
    private val sections = mutableMapOf<String, Mutex>()

    override val configuredProviderIds: StateFlow<Set<String>> =
        repository.configuredProviderIds

    override suspend fun saveApiKey(providerId: String, material: ByteArray) =
        withProviderSection(providerId) {
            check(coreApi.status.value.hasCompleteProjection()) {
                "Core projection is unavailable"
            }
            repository.saveApiKey(providerId, material)
        }

    /**
     * Seals one key for a provider a person is defining, and announces nothing.
     *
     * Inside the section like every other write to the store, because that is
     * what decision 0078 says of a mutation rather than of an announcement: the
     * seal for a custom provider's save must not interleave with a forget or a
     * refresh against the same identity. What it does not do is send a command
     * — see [ProfileProviderCredentialsRepository.sealApiKey] for why the save
     * that follows carries that itself.
     */
    override suspend fun sealApiKey(providerId: String, material: ByteArray): String =
        withProviderSection(providerId) {
            repository.sealApiKey(providerId, material)
        }

    /**
     * Answers the handle the store already holds, read inside the section.
     *
     * A read, in the section for [resolveAccess]'s reason: the answer and the
     * record it is about must be one moment. A save that carried a handle read
     * beside a forget would name a record that is already gone.
     */
    override suspend fun heldCredentialHandle(providerId: String): String? =
        withProviderSection(providerId) {
            repository.heldCredentialHandle(providerId)
        }

    /**
     * Deletes the record a removed provider named, inside the section.
     *
     * A write to the store, so it takes the section like every other one
     * (decision 0078): a discard that interleaved with a save against the same
     * identity would delete the record the save had just written.
     */
    override suspend fun discardSealedKey(providerId: String) =
        withProviderSection(providerId) {
            repository.discardSealedKey(providerId)
        }

    override suspend fun forget(providerId: String) = withProviderSection(providerId) {
        // A subscription sign-out revokes the grant at the vendor before the
        // record is deleted — inside the section, so the record read here is
        // the record being deleted. Best-effort by decision 0081: the person's
        // device is theirs to clear, so the forget proceeds whatever the
        // vendor answers, and a port that throws anyway must not stop it —
        // which is the one reason this catch exists.
        if (providerId in secureMaterial.configuredProviderIds()) {
            val material = secureMaterial.resolveProviderCredential(providerId)
            if (ProviderOauthRecordCodec.isOauthMaterial(material)) {
                val record = ProviderOauthRecordCodec.decode(material)
                if (record != null) {
                    // Revoking the refresh token revokes the whole grant where
                    // the vendor supports RFC 7009; a record with no refresh
                    // token offers its access token instead.
                    val token = record.refreshToken ?: record.accessToken
                    try {
                        revocation.revokeBestEffort(providerId, token)
                    } catch (_: RuntimeException) {
                        // The record still gets deleted below.
                    }
                    record.zero()
                }
            }
            material.fill(0)
        }
        repository.forget(providerId)
    }

    /**
     * Seals a completed subscription sign-in's tokens and announces them.
     *
     * The sign-in terminal is one of the mutations decision 0078 names, so it
     * takes the provider's section like every other write: a save that raced
     * a forget lands whole and announced, or not at all — never as a sealed
     * record the core was told nothing about.
     */
    suspend fun completeSignIn(providerId: String, record: ProviderOauthRecord) =
        withProviderSection(providerId) {
            repository.saveOauthRecord(providerId, record)
        }

    /**
     * Answers what a model request may use for this provider right now.
     *
     * A read, but inside the section on purpose: the freshness decision and
     * the record it was made about must be one moment. A resolve that raced a
     * refresh waits for the refresh and answers from the rotated record,
     * instead of answering from the one being replaced.
     */
    suspend fun resolveAccess(providerId: String, nowEpochMs: Long): ProviderAccess =
        withProviderSection(providerId) {
            if (providerId !in secureMaterial.configuredProviderIds()) {
                return@withProviderSection ProviderAccess.NotConfigured
            }
            val material = secureMaterial.resolveProviderCredential(providerId)
            if (!ProviderOauthRecordCodec.isOauthMaterial(material)) {
                return@withProviderSection ProviderAccess.RawKey(material)
            }
            val record = ProviderOauthRecordCodec.decode(material)
            material.fill(0)
            when {
                // A record that no longer decodes is a credential only the
                // person can repair; saying so beats crashing a model call.
                record == null -> ProviderAccess.SignInRequired
                record.expiresAtEpochMs - nowEpochMs > RESOLVE_MARGIN_MS -> {
                    record.refreshToken?.fill(0)
                    ProviderAccess.AccessToken(
                        record.accessToken,
                        record.expiresAtEpochMs,
                        record.credentialHost,
                    )
                }
                record.refreshToken != null -> {
                    record.accessToken.fill(0)
                    ProviderAccess.RefreshRequired(record.refreshToken)
                }
                else -> {
                    record.zero()
                    ProviderAccess.SignInRequired
                }
            }
        }

    /**
     * Replaces the sealed record after the one refresh leg succeeded.
     *
     * Returns whether the rotation was kept. The in-section re-read is the
     * forget race's answer: a person who removed the credential while the
     * refresh was in flight wins, and the rotated tokens are dropped rather
     * than resurrecting the record. A kept rotation re-files `USABLE`, so a
     * row that had reported a failed refresh converges without a restart.
     */
    suspend fun storeRefreshedRecord(providerId: String, record: ProviderOauthRecord): Boolean =
        withProviderSection(providerId) {
            if (providerId !in secureMaterial.configuredProviderIds()) {
                return@withProviderSection false
            }
            secureMaterial.storeProviderCredential(
                providerId,
                ProviderOauthRecordCodec.encode(record),
            )
            coreApi.setProviderCredentialState(providerId, ProviderCredentialStateView.USABLE)
            true
        }

    /**
     * Proves one pasted draft with one bounded model call (decision 0083).
     *
     * Deliberately *outside* the provider's critical section: the probe
     * writes nothing durable, and holding the section across a network
     * answer would park a forget behind a question. One probe at a time is
     * the core's own rule — a second submission is refused as backpressure
     * and surfaces as the exception the interface names.
     *
     * The draft is sealed as a one-shot transient — a copy, because the
     * vault zeroes what it seals and the caller usually saves the same bytes
     * on a usable verdict — and the opaque handle is all the core ever sees.
     * The verdict is awaited from the status snapshots the core publishes:
     * the first `provider_probes` row for this provider that differs from
     * the one held before the ask is this probe's answer. No verdict within
     * the wait is [ProviderProbeVerdict.UNKNOWN] — indefinite, honestly, of
     * a question nothing answered.
     */
    override suspend fun probeApiKey(
        providerId: String,
        material: ByteArray,
    ): ProviderProbeVerdict {
        val currentStatus = coreApi.status.value
        if (!currentStatus.hasCompleteProjection()) return ProviderProbeVerdict.UNKNOWN
        val before = currentStatus.provider_probes
            .firstOrNull { it.provider_id == providerId }
        val handle = secureMaterial.writeTransient(material.copyOf())
        if (!coreApi.status.value.hasCompleteProjection()) {
            check(secureMaterial.delete(handle)) {
                "Transient provider credential disappeared before dispatch"
            }
            return ProviderProbeVerdict.UNKNOWN
        }
        coreApi.probeProviderKey(providerId, handle)
        val landed = withTimeoutOrNull(PROBE_VERDICT_WAIT_MS) {
            coreApi.status
                .mapNotNull { status ->
                    status.takeIf { it.hasCompleteProjection() }
                        ?.provider_probes
                        ?.firstOrNull { it.provider_id == providerId }
                }
                .first { it != before }
        }
        if (landed?.verdict == ProviderProbeVerdictView.NO_MODEL_LISTED) {
            // The core composed no effect for this verdict, so nothing spent
            // the one-shot transient. Every other answer arrives through an
            // effect the browser dispatched, and dispatch is what consumes the
            // handle; left alone this one would sit in the vault until
            // clearTransient at profile close. A false here means it is
            // already gone, which is the same end state.
            secureMaterial.delete(handle)
        }
        return when (landed?.verdict) {
            ProviderProbeVerdictView.USABLE -> ProviderProbeVerdict.USABLE
            ProviderProbeVerdictView.AUTH -> ProviderProbeVerdict.AUTH
            ProviderProbeVerdictView.BILLING -> ProviderProbeVerdict.BILLING
            ProviderProbeVerdictView.RATE_LIMIT -> ProviderProbeVerdict.RATE_LIMIT
            ProviderProbeVerdictView.OVERLOADED -> ProviderProbeVerdict.OVERLOADED
            ProviderProbeVerdictView.TIMEOUT -> ProviderProbeVerdict.TIMEOUT
            ProviderProbeVerdictView.NETWORK -> ProviderProbeVerdict.NETWORK
            ProviderProbeVerdictView.MODEL_NOT_FOUND -> ProviderProbeVerdict.MODEL_NOT_FOUND
            ProviderProbeVerdictView.NO_MODEL_LISTED -> ProviderProbeVerdict.NO_MODEL_LISTED
            // ENDPOINT_REACHED answers about an address rather than about a
            // key, so it says nothing this question asked and is left
            // indefinite. A key probe is never answered with it.
            ProviderProbeVerdictView.ENDPOINT_REACHED,
            ProviderProbeVerdictView.UNKNOWN,
            null,
            -> ProviderProbeVerdict.UNKNOWN
        }
    }

    /**
     * Files the registry state a browser-side operation learned about one
     * stored credential, or drops the report if the record is gone.
     *
     * Returns whether the report was sent. A dropped report is not a failure —
     * it is the coordinator observing a deletion that happened first — so the
     * answer is a boolean rather than an exception. A report the core refuses
     * still raises, because that is a disagreement someone must see.
     */
    suspend fun reportCredentialState(
        providerId: String,
        state: ProviderCredentialStateView,
    ): Boolean = withProviderSection(providerId) {
        if (providerId !in secureMaterial.configuredProviderIds()) {
            return@withProviderSection false
        }
        coreApi.setProviderCredentialState(providerId, state)
        true
    }

    private suspend fun <T> withProviderSection(
        providerId: String,
        block: suspend () -> T,
    ): T = section(providerId).withLock { block() }

    private fun section(providerId: String): Mutex = synchronized(sectionsLock) {
        sections.getOrPut(providerId) {
            // One mutex per provider id ever mutated in this process. The id
            // space a surface can name is bounded by the roster and the store's
            // own record bound, so the map is bounded by use; the cap turns a
            // caller inventing identities into a named refusal instead of
            // unbounded growth.
            check(sections.size < MAX_SECTIONS) {
                "Provider critical sections exceed their bound"
            }
            Mutex()
        }
    }

    private companion object {
        const val MAX_SECTIONS = 256

        /** Decision 0078's resolve margin: refresh sixty seconds early. */
        const val RESOLVE_MARGIN_MS = 60_000L

        /**
         * How long a probe's verdict is awaited. The probe effect carries the
         * command deadline of thirty seconds, and the core files a timeout
         * verdict when it passes, so this is a margin over the protocol's own
         * bound rather than a second bound of its own.
         */
        const val PROBE_VERDICT_WAIT_MS = 45_000L
    }
}
