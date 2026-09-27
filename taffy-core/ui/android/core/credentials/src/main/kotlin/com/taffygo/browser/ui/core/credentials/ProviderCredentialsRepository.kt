// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.credentials

import kotlinx.coroutines.flow.StateFlow

/** UI-safe projection and secure-store intents for provider credentials. */
interface ProviderCredentialsRepository {
    /** Provider identifiers with a browser-owned secure-store handle. */
    val configuredProviderIds: StateFlow<Set<String>>

    /** Immediately replace user-entered bytes with an opaque browser secure-store handle. */
    suspend fun saveApiKey(providerId: String, material: ByteArray)

    /**
     * Seal the bytes into the browser's secure store and answer the handle,
     * **without telling the core anything**.
     *
     * The omission is the point of the method and is not a missing half.
     * [saveApiKey] exists for a provider the catalog already carries, where
     * sealing and announcing are two steps of one act and either alone is a
     * disagreement nobody can see. A provider a person defines has no such
     * pair: its address, its name, its models and its credential are written
     * together by one command, which carries the announcement itself (decision
     * `docs/decisions/0096-an-endpoint-a-person-typed-is-registered-with-the-browser.md`
     * section 4). Announcing here as well would be the second command that
     * record exists to refuse, and it would name a provider the core has not
     * been given yet.
     *
     * The same handle also carries a key to the endpoint probe, which asks an
     * address what it is before anything names it: a server that answers
     * nothing without a credential can then be asked again with one.
     *
     * The caller hands over ownership of [material]; the store zeroes it.
     */
    suspend fun sealApiKey(providerId: String, material: ByteArray): String

    /**
     * The handle the browser's secure store already holds for [providerId], or
     * null when it holds none.
     *
     * A read, and the answer to a question only a one-write save can ask: the
     * save states a provider whole, so a screen that is changing an address or
     * a name has to carry the credential it is *not* changing, and a screen
     * never sees the key it would otherwise re-seal. Without this, saving an
     * edit with an empty key field would carry no credential and the core
     * would read that as the person removing one — an endpoint that had a key
     * would quietly start reaching their server with none.
     *
     * Never the material, and never a claim that the credential still works:
     * only the provider can say that, and a stored key that has stopped
     * working stays refused rather than being read as an endpoint that needs
     * none.
     */
    suspend fun heldCredentialHandle(providerId: String): String?

    /**
     * Delete the sealed record for a provider that is already gone from the
     * core, **without telling the core anything**.
     *
     * The exact mirror of [sealApiKey], and it exists for the same reason. A
     * provider a person defines carries its credential on the one command that
     * defines it, so the one command that removes it withdraws the credential
     * too — the core has already dropped it by the time this runs, and
     * [forget] would send a second command naming a provider that no longer
     * exists. Without this the sealed record simply stays: a key on somebody's
     * phone for a provider no surface can see, name, or remove.
     *
     * Nothing is revoked at a vendor, because there is no vendor. A person's
     * own server issued the key and only they can withdraw it there.
     */
    suspend fun discardSealedKey(providerId: String)

    /** Revoke and remove the browser-owned secure-store handle. */
    suspend fun forget(providerId: String)
}
