// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.selection.toggleable
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Checkbox
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.viewinterop.AndroidView
import androidx.compose.ui.window.DialogProperties
import androidx.compose.ui.window.SecureFlagPolicy
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.compose.currentStateAsState
import com.taffygo.browser.ui.core.ui.taffyString

/** Compose owns the ceremony; key text stays exclusively inside the trusted Android view. */
@Composable
fun BackupRecoveryKeyDialog(
    session: BackupRecoveryKeySession,
    onOutcome: (BackupRecoveryKeySession.Outcome) -> Unit,
) {
    key(session) { BackupRecoveryKeyContent(session, onOutcome) }
}

@Composable
private fun BackupRecoveryKeyContent(
    session: BackupRecoveryKeySession,
    onOutcome: (BackupRecoveryKeySession.Outcome) -> Unit,
) {
    val lifecycle = LocalLifecycleOwner.current.lifecycle
    val lifecycleState by lifecycle.currentStateAsState()
    // A replaced operation must settle its own callback during disposal, not
    // the callback belonging to the next operation composed in the same slot.
    val result = remember(session) { onOutcome }
    val field = remember(session) { BackupRecoveryKeyFieldHolder() }
    var closed by remember(session) { mutableStateOf(false) }
    var retained by remember(session) { mutableStateOf(false) }
    var available by remember(session) { mutableStateOf<Boolean?>(null) }
    var refused by remember(session) { mutableStateOf(false) }
    val creating = session.mode == BackupRecoveryKeySession.Mode.CREATE

    fun finish(outcome: BackupRecoveryKeySession.Outcome) {
        if (closed) return
        closed = true
        field.view?.clearSensitiveText()
        if (outcome != BackupRecoveryKeySession.Outcome.CONFIRMED) session.cancel()
        result(outcome)
    }

    DisposableEffect(session, lifecycle) {
        val observer = LifecycleEventObserver { _, event ->
            if (event == Lifecycle.Event.ON_PAUSE || event == Lifecycle.Event.ON_DESTROY) {
                finish(BackupRecoveryKeySession.Outcome.CANCELLED)
            }
        }
        lifecycle.addObserver(observer)
        onDispose {
            lifecycle.removeObserver(observer)
            field.view?.clearSensitiveText()
            field.view = null
            finish(BackupRecoveryKeySession.Outcome.CANCELLED)
        }
    }
    LaunchedEffect(session, available, lifecycleState) {
        if (!lifecycleState.isAtLeast(Lifecycle.State.RESUMED) || available == false) {
            finish(BackupRecoveryKeySession.Outcome.UNAVAILABLE)
        }
    }
    if (closed || !lifecycleState.isAtLeast(Lifecycle.State.RESUMED)) return

    AlertDialog(
        onDismissRequest = { finish(BackupRecoveryKeySession.Outcome.CANCELLED) },
        properties = DialogProperties(securePolicy = SecureFlagPolicy.SecureOn),
        title = {
            Text(taffyString(if (creating) R.string.taffy_backup_keep_key else R.string.taffy_backup_enter_key))
        },
        text = {
            Column {
                Text(taffyString(if (creating) R.string.taffy_backup_key_warning else R.string.taffy_backup_key_restore_help))
                AndroidView(
                    factory = { context ->
                        BackupRecoveryKeyField(context).also { view ->
                            field.view = view
                            if (!creating) view.hint = context.getString(R.string.taffy_backup_enter_key)
                            available = !creating || view.showGeneratedKey(session)
                        }
                    },
                    modifier = Modifier.fillMaxWidth().testTag("backup-recovery-key-field"),
                    onReset = null,
                    onRelease = { view ->
                        view.clearSensitiveText()
                        if (field.view === view) field.view = null
                    },
                )
                if (creating) {
                    Row(
                        modifier = Modifier.fillMaxWidth()
                            .testTag("backup-recovery-key-retained")
                            .toggleable(value = retained, role = Role.Checkbox, onValueChange = { retained = it }),
                    ) {
                        Checkbox(checked = retained, onCheckedChange = null)
                        Text(taffyString(R.string.taffy_backup_key_retained))
                    }
                }
                if (refused) Text(taffyString(R.string.taffy_backup_key_refused))
            }
        },
        confirmButton = {
            TextButton(
                enabled = available == true && (!creating || retained),
                onClick = {
                    val acceptance = if (creating) {
                        try {
                            session.confirmKeyRetained()
                        } catch (_: RuntimeException) {
                            BackupRecoveryKeySession.Acceptance.UNAVAILABLE
                        }
                    } else {
                        field.view?.submitEnteredKey(session)
                            ?: BackupRecoveryKeySession.Acceptance.UNAVAILABLE
                    }
                    when (acceptance) {
                        BackupRecoveryKeySession.Acceptance.ACCEPTED -> finish(BackupRecoveryKeySession.Outcome.CONFIRMED)
                        BackupRecoveryKeySession.Acceptance.REFUSED -> refused = true
                        BackupRecoveryKeySession.Acceptance.UNAVAILABLE -> finish(BackupRecoveryKeySession.Outcome.UNAVAILABLE)
                    }
                },
            ) { Text(taffyString(R.string.taffy_backup_key_continue)) }
        },
        dismissButton = {
            TextButton(onClick = { finish(BackupRecoveryKeySession.Outcome.CANCELLED) }) {
                Text(taffyString(R.string.taffy_backup_key_cancel))
            }
        },
    )
}

private class BackupRecoveryKeyFieldHolder {
    var view: BackupRecoveryKeyField? = null
}
