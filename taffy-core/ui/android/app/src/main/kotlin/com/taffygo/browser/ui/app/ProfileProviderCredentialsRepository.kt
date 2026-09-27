// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.api.hasCompleteProjection
import com.taffygo.browser.ui.core.credentials.ProviderCredentialsRepository
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import taffy.core_api.ProviderAuthMethodView

/**
 * UI-safe provider credential projection backed by the profile secure-material
 * store, and the one place a stored key is announced to the core.
 *
 * Both halves are needed and neither is enough. Sealing the key without telling
 * the core leaves the router refusing every direct route for want of a
 * credential that is genuinely on the disk — a disagreement no log and no test
 * reports, because both sides are behaving exactly as written. Telling the core
 * without sealing the key would name a record the browser cannot resolve.
 *
 * So the store is written first and the command is sent second, and a command
 * that does not land takes the record with it: an unreferenced credential would
 * draw a configured row over a provider the core will refuse. What this class
 * deliberately does **not** do is serialize the pair — a save, a forget and a
 * refresh can still race one provider and the last writer wins, which is
 * register entry OD-103 and not a thing to settle here.
 */
class ProfileProviderCredentialsRepository(
    private val secureMaterial: AndroidProfileSecureMaterialStore,
    private val coreApi: CoreApiClient,
) : ProviderCredentialsRepository {
    private val configured = MutableStateFlow(secureMaterial.configuredProviderIds())

    override val configuredProviderIds: StateFlow<Set<String>> = configured.asStateFlow()

    override suspend fun saveApiKey(providerId: String, material: ByteArray) {
        // A pasted key that begins with the subscription-record marker would
        // make the two record kinds indistinguishable at resolve time, so it
        // is refused at the door. No real vendor key is a marker line.
        require(!ProviderOauthRecordCodec.isOauthMaterial(material)) {
            "A pasted key may not begin with the subscription-record marker"
        }
        store(providerId, material, ProviderAuthMethodView.API_KEY)
    }

    /**
     * Seals the key and answers the record's name, telling the core nothing.
     *
     * The half of [saveApiKey] that is deliberately missing here is the command,
     * and leaving it out is what makes this method correct rather than
     * incomplete: the caller is the one write that defines a provider a person
     * typed the address of, and that command carries the credential with the
     * address, the name and the models. A second command would be the
     * half-completed pair decision 0096 section 4 refuses.
     *
     * There is no rollback either, and nothing to roll back to. [store]'s
     * rollback exists because an announced-nothing record draws a configured
     * row over a provider the core will refuse; here no provider exists yet, so
     * a sealed record that is never named by a save is a record for a provider
     * that was never defined — invisible rather than wrong. The save that names
     * it is the next thing that happens, and a save that drops the credential
     * releases the handle through the core's own effect.
     */
    override suspend fun sealApiKey(providerId: String, material: ByteArray): String {
        require(!ProviderOauthRecordCodec.isOauthMaterial(material)) {
            "A pasted key may not begin with the subscription-record marker"
        }
        val credentialHandle = secureMaterial.storeProviderCredential(providerId, material)
        configured.value = secureMaterial.configuredProviderIds()
        return credentialHandle
    }

    override suspend fun heldCredentialHandle(providerId: String): String? =
        secureMaterial.providerCredentialHandle(providerId)

    /**
     * Deletes the record and announces nothing, for [sealApiKey]'s reason read
     * the other way round.
     *
     * There is no rollback here and nothing to roll back to. [store]'s exists
     * because an announced-nothing record draws a configured row over a
     * provider the core will refuse; here the provider is already gone from the
     * core, so a record left behind is a key for a provider that does not
     * exist — invisible rather than wrong, and exactly what this removes.
     */
    override suspend fun discardSealedKey(providerId: String) {
        secureMaterial.removeProviderCredential(providerId)
        configured.value = secureMaterial.configuredProviderIds()
    }

    /**
     * Seals a completed subscription sign-in's token triple and announces it.
     *
     * The same store-first/command-second ordering as [saveApiKey], with the
     * same rollback: an unannounced record would draw a configured row over a
     * provider the core will refuse, so a command that does not land takes
     * the sealed record with it. The record is encoded here and zeroed here —
     * the caller keeps ownership of the tokens it passed.
     */
    suspend fun saveOauthRecord(providerId: String, record: ProviderOauthRecord) {
        store(providerId, ProviderOauthRecordCodec.encode(record), ProviderAuthMethodView.OAUTH)
    }

    private suspend fun store(
        providerId: String,
        material: ByteArray,
        authMethod: ProviderAuthMethodView,
    ) {
        val credentialHandle = secureMaterial.storeProviderCredential(providerId, material)
        configured.value = secureMaterial.configuredProviderIds()
        try {
            if (authMethod == ProviderAuthMethodView.API_KEY) {
                check(coreApi.status.value.hasCompleteProjection()) {
                    "Core projection is unavailable"
                }
            }
            coreApi.saveProviderCredential(
                providerId = providerId,
                authMethod = authMethod,
                credentialHandle = credentialHandle,
            )
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (refused: Exception) {
            // Never logged and never read: a refusal here is a submission
            // status, and the caller's own copy is what a person is told.
            secureMaterial.removeProviderCredential(providerId)
            configured.value = secureMaterial.configuredProviderIds()
            throw refused
        }
    }

    /**
     * Revoke the key, then tell the core.
     *
     * This order and not the mirror of [saveApiKey], because the two failures
     * are not the same size. A person must always be able to remove a key from
     * their own device, and the core refuses a revoke for a provider it has no
     * credential for — which is every key stored before this class told it
     * anything. Sending first would make those keys permanently unremovable.
     *
     * The other order costs a core that still names a record the store no
     * longer holds, and that resolves to nothing: the browser answers the
     * broker no credential, and the request is refused rather than sent. The
     * refusal is still raised, so the surface reports it.
     */
    override suspend fun forget(providerId: String) {
        secureMaterial.removeProviderCredential(providerId)
        configured.value = secureMaterial.configuredProviderIds()
        coreApi.forgetProviderCredential(providerId)
    }
}
