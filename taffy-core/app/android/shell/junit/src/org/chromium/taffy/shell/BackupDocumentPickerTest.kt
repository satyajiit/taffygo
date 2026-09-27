// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import android.app.Activity
import android.content.ActivityNotFoundException
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
import com.taffygo.browser.ui.app.BackupDocumentPicker
import com.taffygo.browser.ui.app.BackupRecoveryKeySession
import com.taffygo.browser.ui.app.OpenBackupDocument
import org.chromium.base.test.BaseRobolectricTestRunner
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RuntimeEnvironment
import org.robolectric.shadows.ShadowLooper

/** Drives Android's real result and saved-state registries without launching an external Activity. */
@RunWith(BaseRobolectricTestRunner::class)
class BackupDocumentPickerTest {
    @Test fun modesUseTheirOwnDocumentContractsAndOriginalSession() {
        for (mode in BackupRecoveryKeySession.Mode.entries) {
            val registry = Registry()
            val picker = BackupDocumentPicker(registry, StateOwner().savedStateRegistry)
            val key = Key(mode)
            val seen = mutableListOf<Pair<BackupRecoveryKeySession, Uri?>>()
            val attached = picker.attach { session, uri -> seen += session to uri }
            assertTrue(attached.launch(key))
            assertEquals(
                if (mode == BackupRecoveryKeySession.Mode.CREATE) {
                    Intent.ACTION_CREATE_DOCUMENT
                } else {
                    Intent.ACTION_OPEN_DOCUMENT
                },
                registry.actions.single(),
            )
            val uri = Uri.parse("content://backup.test/selected")
            registry.result(registry.codes.single(), uri)
            ShadowLooper.idleMainLooper()
            assertEquals(listOf(key to uri), seen)
            attached.close()
            picker.close()
        }
    }

    @Test fun repeatedExactRequestIsIdempotentAndCannotBeReplaced() {
        val registry = Registry()
        val picker = BackupDocumentPicker(registry, StateOwner().savedStateRegistry)
        val attached = picker.attach { _, _ -> }
        val owned = Key()
        assertTrue(attached.launch(owned))
        assertTrue(attached.launch(owned))
        assertFalse(attached.launch(Key()))
        assertFalse(picker.attach { _, _ -> }.launch(owned))
        assertEquals(1, registry.codes.size)
        attached.close()
        picker.close()
    }

    @Test fun screenCloseKeepsDropOnlyRegistrationUntilLateResultIsPurged() {
        val registry = Registry()
        val owner = StateOwner()
        val picker = BackupDocumentPicker(registry, owner.savedStateRegistry)
        var calls = 0
        val attached = picker.attach { _, _ -> calls++ }
        assertTrue(attached.launch(Key()))
        val oldCode = registry.codes.single()
        attached.close()

        val oldUri = Uri.parse("content://backup.test/late")
        registry.result(oldCode, oldUri)
        ShadowLooper.idleMainLooper()
        assertEquals(0, calls)
        assertFalse(registry.save().containsDocument(oldUri))

        val current = Key()
        val next = picker.attach { _, _ -> }
        assertTrue(next.launch(current))
        next.close()
        picker.close()
    }

    @Test fun recreatedWindowDrainsOldResultBeforeAllowingANewRequest() {
        val oldRegistry = Registry()
        val oldOwner = StateOwner()
        val oldPicker = BackupDocumentPicker(oldRegistry, oldOwner.savedStateRegistry)
        val oldAttachment = oldPicker.attach { _, _ -> error("old callback escaped") }
        assertTrue(oldAttachment.launch(Key()))
        val oldCode = oldRegistry.codes.single()
        val registryState = oldRegistry.save()
        val pickerState = oldOwner.save()
        oldPicker.close()

        val registry = Registry().apply { restore(registryState) }
        val owner = StateOwner(pickerState)
        val picker = BackupDocumentPicker(registry, owner.savedStateRegistry)
        val seen = mutableListOf<BackupRecoveryKeySession>()
        val attached = picker.attach { session, _ -> seen += session }
        val current = Key()
        assertFalse(attached.launch(current))

        val oldUri = Uri.parse("content://backup.test/restored-old")
        registry.result(oldCode, oldUri)
        ShadowLooper.idleMainLooper()
        assertTrue(seen.isEmpty())
        assertFalse(registry.save().containsDocument(oldUri))
        assertTrue(attached.launch(current))
        registry.result(registry.codes.last(), Uri.parse("content://backup.test/current"))
        ShadowLooper.idleMainLooper()
        assertEquals(listOf(current), seen)
        attached.close()
        picker.close()
    }

    @Test fun rawResultAlreadyPendingAtRecreationIsConsumedRatherThanResaved() {
        val oldRegistry = Registry()
        val oldOwner = StateOwner()
        oldRegistry.register(UNRELATED_KEY, OpenBackupDocument()) { error("old callback escaped") }
            .launch(Unit)
        val unrelatedCode = oldRegistry.codes.single()
        val oldPicker = BackupDocumentPicker(oldRegistry, oldOwner.savedStateRegistry)
        assertTrue(oldPicker.attach { _, _ -> }.launch(Key()))
        val oldCode = oldRegistry.codes.last()
        val registryState = oldRegistry.save()
        val pickerState = oldOwner.save()
        oldPicker.close()

        val oldUri = Uri.parse("content://backup.test/pending-before-owner")
        val registry = Registry().apply {
            restore(registryState)
            result(oldCode, oldUri)
        }
        var unrelatedCalls = 0
        registry.register(UNRELATED_KEY, OpenBackupDocument()) { unrelatedCalls++ }
        assertTrue(registry.save().containsDocument(oldUri))

        val owner = StateOwner(pickerState)
        val picker = BackupDocumentPicker(registry, owner.savedStateRegistry)
        ShadowLooper.idleMainLooper()
        assertFalse(registry.save().containsDocument(oldUri))
        val drainedRegistryState = registry.save()
        val drainedPickerState = owner.save()
        assertEquals(0, drainedRegistryState.countStringsWithPrefix(TOKEN_PREFIX))
        assertEquals(0, drainedPickerState.countStringsWithPrefix(TOKEN_PREFIX))
        assertEquals(
            listOf(UNRELATED_KEY),
            drainedRegistryState.getStringArrayList(REGISTRY_LAUNCHED_KEYS),
        )

        val duplicateUri = Uri.parse("content://backup.test/duplicate-after-drain")
        assertFalse(registry.result(oldCode, duplicateUri))
        assertFalse(registry.save().containsDocument(duplicateUri))
        assertTrue(registry.result(unrelatedCode, Uri.parse("content://other.test/result")))
        assertEquals(1, unrelatedCalls)
        picker.close()

        val nextRegistry = Registry().apply { restore(drainedRegistryState) }
        val afterDrain = BackupDocumentPicker(nextRegistry, StateOwner(drainedPickerState).savedStateRegistry)
        assertTrue(afterDrain.attach { _, _ -> }.launch(Key()))
        afterDrain.close()
    }

    @Test fun resultSavedBeforePostedSettlementDoesNotBecomeAnOrphan() {
        val registry = Registry()
        val owner = StateOwner()
        val picker = BackupDocumentPicker(registry, owner.savedStateRegistry)
        var calls = 0
        assertTrue(picker.attach { _, _ -> calls++ }.launch(Key()))
        val oldCode = registry.codes.single()
        val oldUri = Uri.parse("content://backup.test/consumed-before-save")
        assertTrue(registry.result(oldCode, oldUri))
        assertEquals(0, calls)

        val registryState = registry.save()
        val pickerState = owner.save()
        picker.close()

        val nextRegistry = Registry().apply { restore(registryState) }
        val nextPicker = BackupDocumentPicker(
            nextRegistry,
            StateOwner(pickerState).savedStateRegistry,
        )
        assertTrue(nextPicker.attach { _, _ -> }.launch(Key()))
        assertFalse(nextRegistry.save().containsDocument(oldUri))
        ShadowLooper.idleMainLooper()
        assertEquals(0, calls)
        nextPicker.close()
    }

    @Test fun restoredTokenWithoutOriginalRegistryIdentityStaysBlocked() {
        val owner = StateOwner()
        val picker = BackupDocumentPicker(Registry(), owner.savedStateRegistry)
        assertTrue(picker.attach { _, _ -> }.launch(Key()))
        val pickerState = owner.save()
        picker.close()

        val missingRegistry = Registry()
        val restoredOwner = StateOwner(pickerState)
        val restored = BackupDocumentPicker(missingRegistry, restoredOwner.savedStateRegistry)
        assertFalse(restored.attach { _, _ -> }.launch(Key()))
        assertEquals(0, missingRegistry.save().countStringsWithPrefix(TOKEN_PREFIX))
        assertEquals(1, restoredOwner.save().countStringsWithPrefix(TOKEN_PREFIX))
        restored.close()
    }

    @Test fun windowClosePreservesOnlyOpaqueTokenForTheNextOwner() {
        val registry = Registry()
        val owner = StateOwner()
        val picker = BackupDocumentPicker(registry, owner.savedStateRegistry)
        var calls = 0
        assertTrue(picker.attach { _, _ -> calls++ }.launch(Key()))
        val code = registry.codes.single()
        picker.close()
        val pickerState = owner.save()
        val registryState = registry.save()

        val uri = Uri.parse("content://backup.test/after-window-close")
        val nextRegistry = Registry().apply { restore(registryState) }
        val nextPicker = BackupDocumentPicker(
            nextRegistry,
            StateOwner(pickerState).savedStateRegistry,
        )
        nextRegistry.result(code, uri)
        ShadowLooper.idleMainLooper()
        assertEquals(0, calls)
        assertFalse(nextRegistry.save().containsDocument(uri))
        nextPicker.close()
    }

    @Test fun failedLaunchReleasesRegistrationBeforeAnotherRequest() {
        val registry = Registry().apply { failLaunch = true }
        val picker = BackupDocumentPicker(registry, StateOwner().savedStateRegistry)
        val seen = mutableListOf<BackupRecoveryKeySession>()
        val attached = picker.attach { session, _ -> seen += session }
        assertFalse(attached.launch(Key()))
        registry.failLaunch = false
        val current = Key()
        assertTrue(attached.launch(current))
        assertTrue(seen.isEmpty())
        registry.result(registry.codes.last(), Uri.parse("content://backup.test/current"))
        ShadowLooper.idleMainLooper()
        assertEquals(listOf(current), seen)
        attached.close()
        picker.close()
    }

    @Test fun ungrantedAndCancelledResultsCarryNoDocument() {
        val registry = Registry()
        val picker = BackupDocumentPicker(registry, StateOwner().savedStateRegistry)
        val seen = mutableListOf<Uri?>()
        val attached = picker.attach { _, uri -> seen += uri }
        assertTrue(attached.launch(Key()))
        registry.result(registry.codes.last(), Uri.parse("file:///not-granted.aib"))
        ShadowLooper.idleMainLooper()
        assertTrue(attached.launch(Key()))
        registry.dispatchResult(registry.codes.last(), Activity.RESULT_CANCELED, Intent())
        ShadowLooper.idleMainLooper()
        assertEquals(listOf<Uri?>(null, null), seen)
        attached.close()
        picker.close()
    }

    @Test fun synchronousResultAndDuplicateDeliveryAreSettledOnce() {
        val registry = Registry().apply { synchronous = true }
        val key = Key()
        val seen = mutableListOf<BackupRecoveryKeySession>()
        val picker = BackupDocumentPicker(registry, StateOwner().savedStateRegistry)
        val attached = picker.attach { session, _ -> seen += session }
        assertTrue(attached.launch(key))
        assertTrue(seen.isEmpty())
        val duplicate = Uri.parse("content://backup.test/duplicate")
        registry.result(registry.codes.single(), duplicate)
        ShadowLooper.idleMainLooper()
        assertEquals(listOf(key), seen)
        assertFalse(registry.save().containsDocument(duplicate))
        assertTrue(attached.launch(Key()))
        ShadowLooper.idleMainLooper()
        assertEquals(2, seen.size)
        attached.close()
        picker.close()
    }

    @Test fun malformedRestoredTokenFailsClosedWithoutRepersistingItsPayload() {
        val oversized = "x".repeat(4096)
        val seed = StateOwner().apply {
            savedStateRegistry.registerSavedStateProvider(SAVED_STATE_PROVIDER) {
                Bundle().apply { putString(STATE_TOKEN, oversized) }
            }
        }
        val owner = StateOwner(seed.save())
        val picker = BackupDocumentPicker(Registry(), owner.savedStateRegistry)
        assertFalse(picker.attach { _, _ -> }.launch(Key()))
        assertFalse(owner.save().containsString(oversized))
        picker.close()
    }

    private class Registry : ActivityResultRegistry() {
        val codes = mutableListOf<Int>()
        val actions = mutableListOf<String?>()
        var failLaunch = false
        var synchronous = false

        override fun <I, O> onLaunch(
            requestCode: Int,
            contract: ActivityResultContract<I, O>,
            input: I,
            options: ActivityOptionsCompat?,
        ) {
            codes += requestCode
            actions += contract.createIntent(RuntimeEnvironment.getApplication(), input).action
            if (failLaunch) throw ActivityNotFoundException("test picker unavailable")
            if (synchronous) result(requestCode, Uri.parse("content://backup.test/synchronous"))
        }

        fun result(code: Int, uri: Uri): Boolean =
            dispatchResult(code, Activity.RESULT_OK, Intent().setData(uri))

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

    private class Key(
        override val mode: BackupRecoveryKeySession.Mode = BackupRecoveryKeySession.Mode.CREATE,
    ) : BackupRecoveryKeySession {
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
    private fun Bundle.containsString(expected: String): Boolean = keySet().any { key ->
        when (val value = get(key)) {
            expected -> true
            is Bundle -> value.containsString(expected)
            is Collection<*> -> value.any { it == expected }
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
        const val SAVED_STATE_PROVIDER = "com.taffygo.backup.document-picker"
        const val STATE_TOKEN = "outstanding-registry-token"
        const val TOKEN_PREFIX = "taffy-backup-"
        const val UNRELATED_KEY = "unrelated-document-consumer"
        const val REGISTRY_LAUNCHED_KEYS = "KEY_COMPONENT_ACTIVITY_LAUNCHED_KEYS"
    }
}
