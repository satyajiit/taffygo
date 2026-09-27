// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.ActivityResultRegistry
import androidx.activity.result.contract.ActivityResultContract
import androidx.annotation.MainThread
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.savedstate.SavedStateRegistry
import java.io.Closeable
import java.util.UUID

/** The window-owned document picker. A host that has no Android picker fails closed. */
val LocalBackupDocumentPicker = staticCompositionLocalOf<BackupDocumentPicker?> { null }

/**
 * Owns one window's Android document-result registrations.
 *
 * A request captures its original opaque session or deletion request and its
 * screen attachment. Closing the screen strips both references but deliberately
 * leaves a drop-only callback registered until Android returns the already-launched
 * result. AndroidX keeps
 * a launched registry key across `unregister()`, so eagerly unregistering here
 * would strand a later raw Intent in ActivityResultRegistry's saved state.
 *
 * The only state this adapter saves is at most one bounded, random registry
 * token (or a fixed fail-closed marker). It never saves a recovery key, native
 * session, document Uri, Intent, or result.
 * A recreated window re-registers that exact token drop-only before any screen
 * can request another picker. Until the orphan drains, another launch is
 * refused rather than silently adopting or overwriting the old request.
 */
@MainThread
class BackupDocumentPicker(
    private val registry: ActivityResultRegistry,
    private val savedStateRegistry: SavedStateRegistry,
) : Closeable, SavedStateRegistry.SavedStateProvider {
    private val mainHandler = Handler(Looper.getMainLooper())
    private val attachments = mutableSetOf<Attachment>()
    private val deletionAttachments = mutableSetOf<DeletionAttachment>()
    private var request: Request? = null
    private var blocked = false
    private var closed = false

    init {
        val restored = savedStateRegistry.consumeRestoredStateForKey(SAVED_STATE_PROVIDER)
        savedStateRegistry.registerSavedStateProvider(SAVED_STATE_PROVIDER, this)
        restoreToken(restored)?.let(::registerDropOnly)
    }

    /** A screen-local capability whose callback is erased when it closes. */
    fun attach(onResult: (BackupRecoveryKeySession, Uri?) -> Unit): Attachment =
        Attachment(onResult).also { attachment ->
            if (closed) attachment.detach() else attachments += attachment
        }

    /** A screen-local old-backup selection capability, sharing this window's one picker slot. */
    fun attachDeletion(onResult: (BackupDeletionRequest, Uri?) -> Unit): DeletionAttachment =
        DeletionAttachment(onResult).also { attachment ->
            if (closed) attachment.detach() else deletionAttachments += attachment
        }

    override fun saveState(): Bundle = Bundle().apply {
        when {
            blocked -> putString(STATE_TOKEN, BLOCKED_TOKEN)
            request != null -> putString(STATE_TOKEN, request?.token)
        }
    }

    /**
     * Window teardown erases all UI/native references. The provider remains
     * attached to the Activity's own SavedStateRegistry so Android's normal
     * save-before-destroy ordering can still carry an outstanding opaque token.
     */
    override fun close() {
        if (closed) return
        closed = true
        attachments.toList().forEach(Attachment::detach)
        attachments.clear()
        deletionAttachments.toList().forEach(DeletionAttachment::detach)
        deletionAttachments.clear()
        request?.apply {
            authority = null
            unregister(launcher)
            launcher = null
        }
    }

    @MainThread
    inner class Attachment internal constructor(
        private var onResult: ((BackupRecoveryKeySession, Uri?) -> Unit)?,
    ) : Closeable {
        /** False means no Android request was launched; the caller withdraws [session]. */
        fun launch(session: BackupRecoveryKeySession): Boolean {
            if (onResult == null || closed || blocked) return false
            val contract: ActivityResultContract<Unit, Uri?> = when (session.mode) {
                BackupRecoveryKeySession.Mode.CREATE -> CreateBackupDocument()
                BackupRecoveryKeySession.Mode.RESTORE -> OpenBackupDocument()
            }
            return launchRequest(SessionAuthority(this, session), contract)
        }

        override fun close() {
            detach()
        }

        internal fun deliver(session: BackupRecoveryKeySession, uri: Uri?) {
            onResult?.invoke(session, uri)
        }

        internal fun detach() {
            if (onResult == null) return
            onResult = null
            attachments.remove(this)
            request?.takeIf {
                (it.authority as? SessionAuthority)?.attachment === this
            }?.apply {
                authority = null
            }
        }
    }

    @MainThread
    inner class DeletionAttachment internal constructor(
        private var onResult: ((BackupDeletionRequest, Uri?) -> Unit)?,
    ) : Closeable {
        /** False means no Android request was launched; the caller retains [request]. */
        fun launch(request: BackupDeletionRequest): Boolean {
            if (onResult == null || closed || blocked) return false
            return launchRequest(
                DeletionAuthority(this, request),
                SelectBackupDocumentForDeletion(),
            )
        }

        override fun close() {
            detach()
        }

        internal fun deliver(request: BackupDeletionRequest, uri: Uri?) {
            onResult?.invoke(request, uri)
        }

        internal fun detach() {
            if (onResult == null) return
            onResult = null
            deletionAttachments.remove(this)
            this@BackupDocumentPicker.request?.takeIf {
                (it.authority as? DeletionAuthority)?.attachment === this
            }?.apply {
                authority = null
            }
        }
    }

    private fun launchRequest(
        authority: RequestAuthority,
        contract: ActivityResultContract<Unit, Uri?>,
    ): Boolean {
        if (closed || blocked) return false
        request?.let { pending ->
            return pending.authority?.sameRequest(authority) == true &&
                pending.launched && !pending.settlementQueued
        }
        val token = try {
            "$TOKEN_PREFIX${UUID.randomUUID()}"
        } catch (_: RuntimeException) {
            return false
        }
        val pending = Request(token, authority)
        request = pending
        val launcher = try {
            registry.register(token, contract) { uri -> queueSettlement(pending, uri) }
        } catch (_: RuntimeException) {
            request = null
            return false
        }
        pending.launcher = launcher
        // A result delivered by register() belongs to an older request whose
        // random registry token happened to collide. Strip either kind of
        // authority and retain the exact bounded registration instead.
        if (pending.settlementQueued) {
            pending.registryIdentityRetained = true
            pending.authority = null
            return false
        }
        pending.launched = true
        return try {
            launcher.launch(Unit)
            true
        } catch (_: RuntimeException) {
            pending.launched = false
            retire(pending)
            false
        }
    }

    private fun registerDropOnly(token: String) {
        val pending = Request(token)
        request = pending
        val restoredIdentity = try {
            registryIdentity(token)
        } catch (_: RuntimeException) {
            null
        }
        if (restoredIdentity == null || restoredIdentity.launchedCount !in 0..1) {
            pending.registryIdentityRetained = true
            return
        }
        try {
            pending.launcher = registry.register(token, DropDocumentResult()) {
                queueSettlement(pending, null)
            }
            if (pending.settlementQueued) {
                prepareRegisterTimeSettlement(pending)
            } else {
                val currentIdentity = registryIdentity(token)
                val currentLaunches = currentIdentity?.takeIf {
                    it.requestCode == restoredIdentity.requestCode
                }?.launchedCount
                when (restoredIdentity.launchedCount to currentLaunches) {
                    // The live callback consumed its result before the picker
                    // provider was saved, but posted retirement did not run.
                    0 to 0 -> retire(pending)
                    1 to 1 -> Unit
                    else -> pending.registryIdentityRetained = true
                }
            }
        } catch (_: RuntimeException) {
            // Keep the token and refuse new work. A later Activity recreation
            // gets another chance to re-establish the drop-only registration.
        }
    }

    /**
     * ActivityResultRegistry removes an in-process dispatch's launched marker
     * only after its callback returns. Settle at the front of the next main-loop
     * turn so unregister can remove that mapping and any duplicate pending result
     * before UI code runs. A raw result restored before registration is already
     * removed from AndroidX's pending bundle by the time this callback runs; the
     * product never carries its opaque token into another save.
     */
    private fun queueSettlement(pending: Request, uri: Uri?) {
        if (request !== pending || pending.settlementQueued) return
        pending.settlementQueued = true
        if (!mainHandler.postAtFrontOfQueue { settle(pending, uri) }) {
            pending.authority = null
            pending.registryIdentityRetained = true
        }
    }

    private fun settle(pending: Request, uri: Uri?) {
        if (request !== pending) return
        if (pending.registryIdentityRetained) {
            pending.authority = null
            return
        }
        val authority = pending.authority
        val mayDeliver = pending.launched && !closed && authority != null
        retire(pending)
        if (mayDeliver) {
            when (authority) {
                is SessionAuthority -> authority.attachment.deliver(authority.session, uri)
                is DeletionAuthority -> authority.attachment.deliver(authority.request, uri)
                null -> Unit
            }
        }
    }

    private fun retire(pending: Request) {
        pending.authority = null
        unregister(pending.launcher)
        pending.launcher = null
        if (request === pending) request = null
    }

    /**
     * A raw result restored by AndroidX is delivered synchronously from
     * register(), outside dispatchResult(). AndroidX removes the raw Intent but
     * leaves the old key in its launched list, so unregistering would preserve
     * the mapping and a later duplicate could strand another Intent. Use its
     * public saved-state snapshot and typed dispatch API to retire exactly that
     * one marker. Any unexpected pinned-library shape keeps the drop-only
     * registration and token indefinitely instead of accepting new work.
     */
    private fun prepareRegisterTimeSettlement(pending: Request) {
        if (!releaseRegisterTimeLaunchMarker(pending.token)) {
            pending.registryIdentityRetained = true
            pending.authority = null
        }
    }

    private fun releaseRegisterTimeLaunchMarker(token: String): Boolean {
        return try {
            val identity = registryIdentity(token) ?: return false
            when (identity.launchedCount) {
                0 -> true
                1 -> {
                    if (!registry.dispatchResult(identity.requestCode, Unit)) return false
                    registryIdentity(token)?.launchedCount == 0
                }
                else -> false
            }
        } catch (_: RuntimeException) {
            false
        }
    }

    private fun registryIdentity(token: String): RegistryIdentity? {
        if (!isCanonicalToken(token)) return null
        val state = Bundle().also(registry::onSaveInstanceState)
        val keys = state.getStringArrayList(REGISTRY_KEYS) ?: return null
        val codes = state.getIntegerArrayList(REGISTRY_REQUEST_CODES) ?: return null
        val launched = state.getStringArrayList(REGISTRY_LAUNCHED_KEYS) ?: return null
        if (keys.size != codes.size || keys.count { it == token } != 1) return null
        val requestCode = codes[keys.indexOf(token)]
        if (codes.count { it == requestCode } != 1) return null
        return RegistryIdentity(requestCode, launched.count { it == token })
    }

    private fun restoreToken(restored: Bundle?): String? {
        return try {
            if (restored == null || restored.isEmpty) {
                null
            } else if (restored.keySet() != setOf(STATE_TOKEN)) {
                blocked = true
                null
            } else {
                val token = restored.getString(STATE_TOKEN)
                if (token == BLOCKED_TOKEN) {
                    blocked = true
                    null
                } else if (token == null || !isCanonicalToken(token)) {
                    blocked = true
                    null
                } else {
                    token
                }
            }
        } catch (_: RuntimeException) {
            blocked = true
            null
        }
    }

    private fun unregister(launcher: ActivityResultLauncher<Unit>?) {
        try {
            launcher?.unregister()
        } catch (_: RuntimeException) {
            // Local UI/session references have already been withdrawn. The
            // opaque token remains bounded and cannot authorize a new request.
        }
    }

    private class Request(
        val token: String,
        var authority: RequestAuthority? = null,
    ) {
        var launcher: ActivityResultLauncher<Unit>? = null
        var launched = false
        var settlementQueued = false
        var registryIdentityRetained = false
    }

    private sealed interface RequestAuthority {
        fun sameRequest(other: RequestAuthority): Boolean
    }

    private class SessionAuthority(
        val attachment: Attachment,
        val session: BackupRecoveryKeySession,
    ) : RequestAuthority {
        override fun sameRequest(other: RequestAuthority): Boolean =
            other is SessionAuthority && other.attachment === attachment && other.session === session
    }

    private class DeletionAuthority(
        val attachment: DeletionAttachment,
        val request: BackupDeletionRequest,
    ) : RequestAuthority {
        override fun sameRequest(other: RequestAuthority): Boolean =
            other is DeletionAuthority && other.attachment === attachment && other.request === request
    }

    private data class RegistryIdentity(val requestCode: Int, val launchedCount: Int)

    /** Parses and immediately drops a restored raw ActivityResult. Never launched. */
    private class DropDocumentResult : ActivityResultContract<Unit, Unit>() {
        override fun createIntent(context: Context, input: Unit): Intent = Intent()
        override fun parseResult(resultCode: Int, intent: Intent?) = Unit
    }

    private companion object {
        const val SAVED_STATE_PROVIDER = "com.taffygo.backup.document-picker"
        const val STATE_TOKEN = "outstanding-registry-token"
        const val TOKEN_PREFIX = "taffy-backup-"
        const val BLOCKED_TOKEN = "taffy-backup-blocked"
        // Pinned AndroidX implementation-detail keys, read only through
        // ActivityResultRegistry's public snapshot API. A changed shape is
        // always fail-closed; the version pin remains in its machine owner.
        const val REGISTRY_REQUEST_CODES = "KEY_COMPONENT_ACTIVITY_REGISTERED_RCS"
        const val REGISTRY_KEYS = "KEY_COMPONENT_ACTIVITY_REGISTERED_KEYS"
        const val REGISTRY_LAUNCHED_KEYS = "KEY_COMPONENT_ACTIVITY_LAUNCHED_KEYS"

        fun isCanonicalToken(token: String): Boolean {
            if (!token.startsWith(TOKEN_PREFIX)) return false
            val suffix = token.removePrefix(TOKEN_PREFIX)
            if (suffix.length != 36 || suffix.lowercase() != suffix) return false
            return try {
                UUID.fromString(suffix).toString() == suffix
            } catch (_: IllegalArgumentException) {
                false
            }
        }
    }
}
