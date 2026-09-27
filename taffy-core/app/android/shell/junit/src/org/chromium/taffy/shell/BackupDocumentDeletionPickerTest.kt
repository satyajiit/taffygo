// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import androidx.activity.result.ActivityResult
import androidx.activity.result.ActivityResultRegistry
import androidx.activity.result.contract.ActivityResultContract
import androidx.core.app.ActivityOptionsCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleRegistry
import androidx.savedstate.SavedStateRegistry
import androidx.savedstate.SavedStateRegistryController
import androidx.savedstate.SavedStateRegistryOwner
import com.taffygo.browser.ui.app.BackupDeletionRequest
import com.taffygo.browser.ui.app.BackupDocumentPicker
import com.taffygo.browser.ui.app.BackupRecoveryKeySession
import org.chromium.base.test.BaseRobolectricTestRunner
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RuntimeEnvironment
import org.robolectric.shadows.ShadowLooper

/** Exercises old-backup selection through the picker's existing one-window orphan drain. */
@RunWith(BaseRobolectricTestRunner::class)
class BackupDocumentDeletionPickerTest {
    @Test
    fun exportAndDeletionSelectionCannotOccupyTheWindowSlotTogether() {
        val registry = Registry()
        val picker = BackupDocumentPicker(registry, StateOwner().savedStateRegistry)
        val session = Key()
        val request = DeletionRequest()
        val exports = picker.attach { _, _ -> }
        val deletions = mutableListOf<Pair<BackupDeletionRequest, Uri?>>()
        val deletion = picker.attachDeletion { owned, uri -> deletions += owned to uri }

        assertTrue(exports.launch(session))
        assertFalse(deletion.launch(request))
        registry.result(registry.codes.single(), EXPORT_URI)
        ShadowLooper.idleMainLooper()

        assertTrue(deletion.launch(request))
        assertFalse(exports.launch(Key()))
        assertEquals(Intent.ACTION_OPEN_DOCUMENT, registry.actions.last())
        assertEquals(REQUIRED_GRANTS, registry.flags.last() and REQUIRED_GRANTS)
        registry.result(registry.codes.last(), DELETE_URI, REQUIRED_GRANTS)
        ShadowLooper.idleMainLooper()

        assertEquals(listOf(request to DELETE_URI), deletions)
        assertEquals(0, request.closeCalls)
        exports.close()
        deletion.close()
        picker.close()
    }

    @Test
    fun exactDeletionRequestIsIdempotentButCannotBeReplaced() {
        val registry = Registry()
        val picker = BackupDocumentPicker(registry, StateOwner().savedStateRegistry)
        val attachment = picker.attachDeletion { _, _ -> }
        val request = DeletionRequest()

        assertTrue(attachment.launch(request))
        assertTrue(attachment.launch(request))
        assertFalse(attachment.launch(DeletionRequest()))
        assertFalse(picker.attachDeletion { _, _ -> }.launch(request))
        assertEquals(1, registry.codes.size)
        assertEquals(0, request.closeCalls)
        attachment.close()
        picker.close()
    }

    @Test
    fun closedDeletionAttachmentDropsItsLateResultAndReleasesTheSharedSlot() {
        val registry = Registry()
        val owner = StateOwner()
        val picker = BackupDocumentPicker(registry, owner.savedStateRegistry)
        val request = DeletionRequest()
        var calls = 0
        val attachment = picker.attachDeletion { _, _ -> calls++ }
        assertTrue(attachment.launch(request))
        val oldCode = registry.codes.single()

        attachment.close()
        registry.result(oldCode, DELETE_URI, REQUIRED_GRANTS)
        ShadowLooper.idleMainLooper()

        assertEquals(0, calls)
        assertEquals(0, request.closeCalls)
        assertFalse(registry.save().containsDocument(DELETE_URI))
        val current = picker.attach { _, _ -> }
        assertTrue(current.launch(Key()))
        current.close()
        picker.close()
    }

    @Test
    fun recreatedDeletionSelectionSavesOnlyATokenAndDrainsBeforeNewWork() {
        val oldRegistry = Registry()
        val oldOwner = StateOwner()
        val oldPicker = BackupDocumentPicker(oldRegistry, oldOwner.savedStateRegistry)
        val request = DeletionRequest()
        var oldCalls = 0
        assertTrue(oldPicker.attachDeletion { _, _ -> oldCalls++ }.launch(request))
        val oldCode = oldRegistry.codes.single()
        val registryState = oldRegistry.save()
        val pickerState = oldOwner.save()
        assertEquals(1, pickerState.countStringsWithPrefix(TOKEN_PREFIX))
        assertFalse(pickerState.containsAndroidResultPayload())
        oldPicker.close()

        val registry = Registry().apply { restore(registryState) }
        val owner = StateOwner(pickerState)
        val picker = BackupDocumentPicker(registry, owner.savedStateRegistry)
        var newCalls = 0
        val current = picker.attachDeletion { _, _ -> newCalls++ }
        assertFalse(current.launch(DeletionRequest()))

        registry.result(oldCode, DELETE_URI, REQUIRED_GRANTS)
        ShadowLooper.idleMainLooper()

        assertEquals(0, oldCalls)
        assertEquals(0, newCalls)
        assertEquals(0, request.closeCalls)
        assertFalse(registry.save().containsDocument(DELETE_URI))
        assertTrue(current.launch(DeletionRequest()))
        current.close()
        picker.close()
    }

    private class Registry : ActivityResultRegistry() {
        val codes = mutableListOf<Int>()
        val actions = mutableListOf<String?>()
        val flags = mutableListOf<Int>()

        override fun <I, O> onLaunch(
            requestCode: Int,
            contract: ActivityResultContract<I, O>,
            input: I,
            options: ActivityOptionsCompat?,
        ) {
            val intent = contract.createIntent(RuntimeEnvironment.getApplication(), input)
            codes += requestCode
            actions += intent.action
            flags += intent.flags
        }

        fun result(code: Int, uri: Uri, flags: Int = 0): Boolean =
            dispatchResult(code, Activity.RESULT_OK, Intent().setData(uri).addFlags(flags))

        fun save(): Bundle = Bundle().also(::onSaveInstanceState)
        fun restore(saved: Bundle) = onRestoreInstanceState(saved)
    }

    private class StateOwner(restored: Bundle? = null) : SavedStateRegistryOwner {
        private val lifecycleRegistry = LifecycleRegistry(this)
        private val controller = SavedStateRegistryController.create(this)

        override val lifecycle: Lifecycle
            get() = lifecycleRegistry
        override val savedStateRegistry: SavedStateRegistry
            get() = controller.savedStateRegistry

        init {
            controller.performRestore(restored)
            lifecycleRegistry.currentState = Lifecycle.State.CREATED
        }

        fun save(): Bundle = Bundle().also(controller::performSave)
    }

    private class DeletionRequest : BackupDeletionRequest {
        var closeCalls = 0

        override fun close() {
            closeCalls++
        }
    }

    private class Key : BackupRecoveryKeySession {
        override val mode = BackupRecoveryKeySession.Mode.CREATE
        override fun takeGeneratedKeyForDisplay(): CharArray? = null
        override fun confirmKeyRetained() = BackupRecoveryKeySession.Acceptance.ACCEPTED
        override fun acceptEnteredKey(text: CharArray) = BackupRecoveryKeySession.Acceptance.ACCEPTED
        override fun cancel() = Unit
    }

    // Bundle.get is deprecated and no typed getter replaces it here: these
    // helpers walk a Bundle whose value types they deliberately do not know,
    // because what they prove is that nothing of a given shape is under ANY key.
    @Suppress("DEPRECATION")
    private fun Bundle.containsDocument(uri: Uri): Boolean = keySet().any { key ->
        when (val value = get(key)) {
            is Bundle -> value.containsDocument(uri)
            is Intent -> value.data == uri
            is ActivityResult -> value.data?.data == uri
            is Collection<*> -> value.any { item ->
                (item as? Bundle)?.containsDocument(uri) == true ||
                    (item as? Intent)?.data == uri ||
                    (item as? ActivityResult)?.data?.data == uri
            }
            else -> false
        }
    }

    @Suppress("DEPRECATION")
    private fun Bundle.containsAndroidResultPayload(): Boolean = keySet().any { key ->
        when (val value = get(key)) {
            is Uri, is Intent, is ActivityResult -> true
            is Bundle -> value.containsAndroidResultPayload()
            is Collection<*> -> value.any {
                it is Uri || it is Intent || it is ActivityResult ||
                    (it as? Bundle)?.containsAndroidResultPayload() == true
            }
            else -> false
        }
    }

    @Suppress("DEPRECATION")
    private fun Bundle.countStringsWithPrefix(prefix: String): Int = keySet().sumOf { key ->
        when (val value = get(key)) {
            is String -> if (value.startsWith(prefix)) 1 else 0
            is Bundle -> value.countStringsWithPrefix(prefix)
            is Collection<*> -> value.count { (it as? String)?.startsWith(prefix) == true }
            else -> 0
        }
    }

    private companion object {
        const val TOKEN_PREFIX = "taffy-backup-"
        const val REQUIRED_GRANTS =
            Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
        val EXPORT_URI: Uri = Uri.parse("content://backup.test/export")
        val DELETE_URI: Uri = Uri.parse("content://backup.test/delete")
    }
}
