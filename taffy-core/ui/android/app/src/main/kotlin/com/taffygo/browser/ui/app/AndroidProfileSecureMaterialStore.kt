// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import java.io.Closeable
import java.security.SecureRandom
import java.util.Base64
import kotlinx.coroutines.CoroutineDispatcher

/**
 * Regular-profile-owned Android credential vault.
 *
 * Plaintext is encrypted before a handle is returned, with Android Keystore work dispatched off
 * the rendering thread. Transient credential ciphertext remains only in this profile object;
 * persistent provider records are authenticated ciphertext in Chromium PrefService.
 */
class AndroidProfileSecureMaterialStore internal constructor(
    private val preferences: ProfilePreferenceStore,
    private val secretBox: SecretBox,
    private val random: SecureRandom,
) : CredentialHandleBroker,
    SecureStoreEndpoint,
    SecureMaterialResolver,
    ProviderCredentialResolver,
    Closeable {
    private data class SessionRecord(
        val rotation: ULong,
        val ciphertext: String,
    )

    private val materialLock = Any()
    private val transientCiphertexts = mutableMapOf<String, String>()

    override suspend fun store(material: ByteArray): String {
        return writeTransient(material)
    }

    override suspend fun generateEntropy(byteCount: Int): ByteArray {
        require(byteCount in 1..MAX_ENTROPY_BYTES) {
            "Requested entropy is empty or exceeds its bound"
        }
        return ByteArray(byteCount).also(random::nextBytes)
    }

    override suspend fun writeTransient(material: ByteArray): String {
        checkMaterial(material)
        val ciphertext = try {
            secretBox.seal(material)
        } finally {
            material.fill(0)
        }
        return synchronized(materialLock) {
            check(transientCiphertexts.size < MAX_TRANSIENT_RECORDS) {
                "Secure-material transient capacity is exhausted"
            }
            val handle = newHandleLocked(readSessionRecordsLocked())
            transientCiphertexts[handle] = ciphertext
            handle
        }
    }

    override suspend fun delete(handle: String): Boolean {
        require(OPAQUE_HANDLE.matches(handle)) { "Malformed secure-material handle" }
        return synchronized(materialLock) { transientCiphertexts.remove(handle) != null }
    }

    override suspend fun consume(handle: String): ByteArray {
        require(OPAQUE_HANDLE.matches(handle)) { "Malformed secure-material handle" }
        val ciphertext = synchronized(materialLock) {
            transientCiphertexts.remove(handle)
        } ?: throw IllegalStateException("Secure-material handle is absent or already spent")
        return secretBox.open(ciphertext)
    }

    /** Wipes all generation-bound one-shot material and returns the exact count. */
    fun clearTransient(): Int = synchronized(materialLock) {
        val count = transientCiphertexts.size
        transientCiphertexts.clear()
        count
    }

    /**
     * Atomically installs one encrypted session version and retires the prior
     * handle. The regular-profile ciphertext survives process death.
     */
    suspend fun rotateSession(
        previousHandle: String?,
        expectedRotation: ULong,
        material: ByteArray,
    ): SessionMaterialHandle {
        checkMaterial(material)
        require((previousHandle == null) == (expectedRotation == 0uL)) {
            "Initial and rotating session writes have inconsistent identity"
        }
        if (previousHandle != null) require(OPAQUE_HANDLE.matches(previousHandle)) {
            "Malformed account-session handle"
        }
        val ciphertext = try {
            secretBox.seal(material)
        } finally {
            material.fill(0)
        }
        return synchronized(materialLock) {
            val records = readSessionRecordsLocked()
            val nextHandle = if (previousHandle != null) {
                val prior = records[previousHandle]
                    ?: throw IllegalStateException("Account-session handle is absent")
                check(prior.rotation != ULong.MAX_VALUE && prior.rotation + 1uL == expectedRotation) {
                    "Account-session rotation is stale"
                }
                previousHandle
            } else {
                check(records.isEmpty()) {
                    "An account session already exists for this profile"
                }
                newHandleLocked(records)
            }
            records[nextHandle] = SessionRecord(expectedRotation, ciphertext)
            writeSessionRecordsLocked(records)
            SessionMaterialHandle(nextHandle, expectedRotation)
        }
    }

    /** Reads a session version without spending it; refresh rotation retires it atomically. */
    suspend fun readSession(handle: String, expectedRotation: ULong): ByteArray {
        require(OPAQUE_HANDLE.matches(handle)) { "Malformed account-session handle" }
        val ciphertext = synchronized(materialLock) {
            val record = readSessionRecordsLocked()[handle]
                ?: throw IllegalStateException("Account-session handle is absent")
            check(record.rotation == expectedRotation) { "Account-session rotation is stale" }
            record.ciphertext
        }
        return secretBox.open(ciphertext)
    }

    /** Resolves the exact current version for revoke, whose portable effect is handle-only. */
    suspend fun readCurrentSession(handle: String): Pair<ULong, ByteArray> {
        require(OPAQUE_HANDLE.matches(handle)) { "Malformed account-session handle" }
        val record = synchronized(materialLock) {
            readSessionRecordsLocked()[handle]
                ?: throw IllegalStateException("Account-session handle is absent")
        }
        return record.rotation to secretBox.open(record.ciphertext)
    }

    /** Reads only the canonical opaque identity; ciphertext is never opened. */
    fun inspectCurrentSession(): SessionMaterialHandle? = synchronized(materialLock) {
        val entry = readSessionRecordsLocked().entries.singleOrNull()
            ?: return@synchronized null
        SessionMaterialHandle(entry.key, entry.value.rotation)
    }

    /** Deletes exactly one expected session version. */
    fun deleteSession(handle: String, expectedRotation: ULong): Boolean {
        require(OPAQUE_HANDLE.matches(handle)) { "Malformed account-session handle" }
        return synchronized(materialLock) {
            val records = readSessionRecordsLocked()
            val record = records[handle] ?: return@synchronized false
            check(record.rotation == expectedRotation) { "Account-session rotation is stale" }
            records.remove(handle)
            writeSessionRecordsLocked(records)
            true
        }
    }

    /** Deletes the exact opaque version when the portable revoke effect has no counter field. */
    fun deleteSession(handle: String): Boolean {
        require(OPAQUE_HANDLE.matches(handle)) { "Malformed account-session handle" }
        return synchronized(materialLock) {
            val records = readSessionRecordsLocked()
            if (records.remove(handle) == null) return@synchronized false
            writeSessionRecordsLocked(records)
            true
        }
    }

    /** Wipes the sole canonical account session during fail-closed reconciliation. */
    fun clearSession(): Boolean = synchronized(materialLock) {
        val hadRecord = preferences
            .getString(ProfilePreferenceNames.ACCOUNT_SESSION_RECORDS)
            .isNotEmpty()
        preferences.putString(ProfilePreferenceNames.ACCOUNT_SESSION_RECORDS, "")
        hadRecord
    }

    override suspend fun resolveProviderCredential(providerId: String): ByteArray {
        checkProviderId(providerId)
        val ciphertext = readProviderRecords()[providerId]
            ?: throw IllegalStateException("Provider credential is not configured")
        return secretBox.open(ciphertext)
    }

    /**
     * Seals one provider credential and answers the name of the record holding
     * it — the reference the core is given, never the material.
     *
     * The name is the provider identifier because that is what this store files
     * a provider record under and the only thing
     * [resolveProviderCredential] is ever asked. Returning it rather than
     * letting a caller assume it is what keeps the two ends of that lookup in
     * one file: a store that later files records under a minted identity
     * changes here alone, and every caller carries whatever it hands back.
     */
    suspend fun storeProviderCredential(providerId: String, material: ByteArray): String {
        checkProviderId(providerId)
        require(material.isNotEmpty() && material.size <= MAX_PROVIDER_MATERIAL_BYTES) {
            "Provider credential is empty or exceeds its bound"
        }
        val ciphertext = try {
            secretBox.seal(material)
        } finally {
            material.fill(0)
        }
        val records = readProviderRecords().apply { put(providerId, ciphertext) }
        writeProviderRecords(records)
        return providerId
    }

    suspend fun removeProviderCredential(providerId: String) {
        checkProviderId(providerId)
        val records = readProviderRecords()
        records.remove(providerId)
        writeProviderRecords(records)
    }

    fun configuredProviderIds(): Set<String> = readProviderRecords().keys.toSet()

    /**
     * The name of the record this store holds for [providerId], or null when it
     * holds none.
     *
     * Here rather than at a caller for [storeProviderCredential]'s reason: the
     * naming convention is this file's, so a caller that needs to hand an
     * existing record's name to a command asks for it instead of assuming it,
     * and a store that later files records under a minted identity still
     * answers correctly.
     */
    fun providerCredentialHandle(providerId: String): String? =
        providerId.takeIf { readProviderRecords().containsKey(it) }

    /**
     * Wipes every locally held credential record and destroys its non-exportable profile key.
     *
     * Each leg is attempted even if another fails. The application-data eraser invokes Android's
     * all-data primitive afterward; this method's separate purpose is to revoke usable credential
     * authority before that asynchronous process kill.
     */
    suspend fun destroyAll() {
        var failure: Throwable? = null
        fun attempt(block: () -> Unit) {
            try {
                block()
            } catch (caught: Throwable) {
                failure?.addSuppressed(caught) ?: run { failure = caught }
            }
        }
        attempt { clearTransient() }
        attempt { preferences.putString(ProfilePreferenceNames.ACCOUNT_SESSION_RECORDS, "") }
        attempt { preferences.putString(ProfilePreferenceNames.PROVIDER_CREDENTIAL_RECORDS, "") }
        try {
            secretBox.destroy()
        } catch (caught: Throwable) {
            failure?.addSuppressed(caught) ?: run { failure = caught }
        }
        failure?.let { throw it }
    }

    override fun close() {
        synchronized(materialLock) {
            transientCiphertexts.clear()
        }
        secretBox.close()
    }

    private fun readSessionRecordsLocked(): MutableMap<String, SessionRecord> {
        val encoded = preferences.getString(ProfilePreferenceNames.ACCOUNT_SESSION_RECORDS)
        if (encoded.isEmpty()) return linkedMapOf()
        require(encoded.length <= MAX_SESSION_RECORDS_CHARACTERS) {
            "Account-session registry exceeds its bound"
        }
        val records = linkedMapOf<String, SessionRecord>()
        encoded.lineSequence().forEach { line ->
            require(records.size < MAX_SESSION_RECORDS) {
                "Account-session registry has too many records"
            }
            val fields = line.split(RECORD_SEPARATOR, limit = 3)
            require(fields.size == 3 && OPAQUE_HANDLE.matches(fields[0])) {
                "Malformed account-session record"
            }
            val rotation = fields[1].toULongOrNull()
            require(rotation != null) {
                "Malformed account-session rotation"
            }
            val ciphertext = fields[2]
            require(
                ciphertext.length <= MAX_SESSION_CIPHERTEXT_CHARACTERS &&
                    ciphertext.all(::isEnvelopeCharacter),
            ) { "Account-session envelope exceeds its bound" }
            require(records.putIfAbsent(fields[0], SessionRecord(rotation, ciphertext)) == null) {
                "Duplicate account-session record"
            }
        }
        return records
    }

    private fun writeSessionRecordsLocked(records: Map<String, SessionRecord>) {
        require(records.size <= MAX_SESSION_RECORDS) {
            "Account-session registry has too many records"
        }
        val encoded = records.toSortedMap().entries.joinToString("\n") { (handle, record) ->
            require(OPAQUE_HANDLE.matches(handle)) {
                "Malformed account-session record"
            }
            require(
                record.ciphertext.length <= MAX_SESSION_CIPHERTEXT_CHARACTERS &&
                    record.ciphertext.all(::isEnvelopeCharacter),
            ) { "Account-session envelope exceeds its bound" }
            "$handle$RECORD_SEPARATOR${record.rotation}$RECORD_SEPARATOR${record.ciphertext}"
        }
        require(encoded.length <= MAX_SESSION_RECORDS_CHARACTERS) {
            "Account-session registry exceeds its bound"
        }
        // One PrefService value is the commit unit: a restored CoreStorage
        // receipt therefore sees either the old complete record or the new one.
        preferences.putString(ProfilePreferenceNames.ACCOUNT_SESSION_RECORDS, encoded)
    }

    private fun readProviderRecords(): MutableMap<String, String> {
        val encoded = preferences.getString(ProfilePreferenceNames.PROVIDER_CREDENTIAL_RECORDS)
        if (encoded.isEmpty()) return linkedMapOf()
        require(encoded.length <= MAX_PROVIDER_RECORDS_CHARACTERS) {
            "Provider credential registry exceeds its bound"
        }
        require(!encoded.endsWith('\n') && '\r' !in encoded) {
            "Malformed provider credential registry"
        }
        val records = linkedMapOf<String, String>()
        encoded.lineSequence().forEach { line ->
            require(records.size < MAX_PROVIDER_RECORDS) {
                "Provider credential registry has too many records"
            }
            val separator = line.indexOf(RECORD_SEPARATOR)
            require(
                separator > 0 &&
                    separator < line.lastIndex &&
                    line.indexOf(RECORD_SEPARATOR, separator + 1) == -1,
            ) {
                "Malformed provider credential record"
            }
            val providerId = line.substring(0, separator)
            val ciphertext = line.substring(separator + 1)
            checkProviderId(providerId)
            require(
                ciphertext.length <= MAX_PROVIDER_CIPHERTEXT_CHARACTERS &&
                    ciphertext.all(::isEnvelopeCharacter),
            ) {
                "Provider credential envelope exceeds its bound"
            }
            require(records.putIfAbsent(providerId, ciphertext) == null) {
                "Duplicate provider credential record"
            }
        }
        return records
    }

    private fun writeProviderRecords(records: Map<String, String>) {
        require(records.size <= MAX_PROVIDER_RECORDS) {
            "Provider credential registry has too many records"
        }
        val encoded = records.toSortedMap().entries.joinToString("\n") { (providerId, value) ->
            checkProviderId(providerId)
            require(
                value.length <= MAX_PROVIDER_CIPHERTEXT_CHARACTERS &&
                    value.all(::isEnvelopeCharacter),
            ) {
                "Provider credential envelope exceeds its bound"
            }
            "$providerId$RECORD_SEPARATOR$value"
        }
        require(encoded.length <= MAX_PROVIDER_RECORDS_CHARACTERS) {
            "Provider credential registry exceeds its bound"
        }
        preferences.putString(ProfilePreferenceNames.PROVIDER_CREDENTIAL_RECORDS, encoded)
    }

    private fun newHandleLocked(sessionRecords: Map<String, SessionRecord>): String {
        repeat(HANDLE_COLLISION_RETRIES) {
            val bytes = ByteArray(HANDLE_ENTROPY_BYTES).also(random::nextBytes)
            val candidate = Base64.getUrlEncoder().withoutPadding().encodeToString(bytes)
            bytes.fill(0)
            if (!transientCiphertexts.containsKey(candidate) &&
                !sessionRecords.containsKey(candidate)
            ) {
                return candidate
            }
        }
        throw IllegalStateException("Could not allocate a unique secure-material handle")
    }

    private fun checkProviderId(providerId: String) {
        require(PROVIDER_ID.matches(providerId)) { "Malformed provider identifier" }
    }

    private fun checkMaterial(material: ByteArray) {
        require(material.isNotEmpty() && material.size <= MAX_MATERIAL_BYTES) {
            "Secure material is empty or exceeds its bound"
        }
    }

    private fun isEnvelopeCharacter(character: Char): Boolean =
        character in 'A'..'Z' ||
            character in 'a'..'z' ||
            character in '0'..'9' ||
            character == '-' ||
            character == '_' ||
            character == '.'

    companion object {
        fun create(
            preferences: ProfilePreferenceStore,
            profileStorageNamespace: String,
            cryptographyDispatcher: CoroutineDispatcher,
        ): AndroidProfileSecureMaterialStore {
            val random = SecureRandom()
            require(STORAGE_NAMESPACE.matches(profileStorageNamespace)) {
                "Malformed profile secure-storage namespace"
            }
            return AndroidProfileSecureMaterialStore(
                preferences = preferences,
                secretBox = AndroidKeystoreSecretBox(
                    alias = "$KEY_ALIAS_PREFIX$profileStorageNamespace",
                    dispatcher = cryptographyDispatcher,
                ),
                random = random,
            )
        }

        private const val KEY_ALIAS_PREFIX = "taffygo.profile."
        private const val MAX_MATERIAL_BYTES = SecureEnvelopeCodec.MAX_MATERIAL_BYTES
        private const val MAX_TRANSIENT_RECORDS = 16
        // Raised from 4 KiB / 8192 when the subscription triple landed: an
        // OAuth record is an access token, a refresh token, an expiry and a
        // scope list sealed as one value, and vendor tokens alone can exceed
        // the old key-sized bound. The registry bound scales with it below.
        private const val MAX_PROVIDER_MATERIAL_BYTES = 8 * 1024
        private const val MAX_PROVIDER_RECORDS = 32
        private const val MAX_PROVIDER_CIPHERTEXT_CHARACTERS = 16 * 1024
        private const val MAX_PROVIDER_RECORDS_CHARACTERS =
            MAX_PROVIDER_RECORDS * (64 + 1 + MAX_PROVIDER_CIPHERTEXT_CHARACTERS + 1)
        // A profile has exactly one canonical account session. Rotation
        // replaces its ciphertext under the stable opaque handle, preventing
        // crash/retry paths from accumulating unreachable token records.
        private const val MAX_SESSION_RECORDS = 1
        private const val MAX_SESSION_CIPHERTEXT_CHARACTERS = 96 * 1024
        private const val MAX_SESSION_RECORDS_CHARACTERS =
            MAX_SESSION_RECORDS * (32 + 1 + 20 + 1 + MAX_SESSION_CIPHERTEXT_CHARACTERS + 1)
        private const val MAX_ENTROPY_BYTES = 64
        private const val HANDLE_ENTROPY_BYTES = 24
        private const val HANDLE_COLLISION_RETRIES = 4
        private const val RECORD_SEPARATOR = '\t'
        private val PROVIDER_ID = Regex("[a-z0-9][a-z0-9-]{0,63}")
        private val OPAQUE_HANDLE = Regex("[A-Za-z0-9_-]{32}")
        private val STORAGE_NAMESPACE = Regex("[A-Za-z0-9_-]{43}")
    }
}
