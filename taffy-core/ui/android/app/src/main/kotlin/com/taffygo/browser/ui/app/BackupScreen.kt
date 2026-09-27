// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.screenViewModel

/** SCR-704: encrypted backup, check-only import and explicit new-profile restore choices. */
@Composable
fun BackupScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
) {
    val viewModel: BackupViewModel = screenViewModel(TaffyDestination.Backup)
    val state by viewModel.state.collectAsStateWithLifecycle()
    val lifecycle = LocalLifecycleOwner.current.lifecycle
    val pickerOwner = LocalBackupDocumentPicker.current
    val picker = remember(viewModel, pickerOwner) {
        pickerOwner?.attach(viewModel::onDocumentResult)
    }
    val deletionPicker = remember(viewModel, pickerOwner) {
        pickerOwner?.attachDeletion(viewModel::onDeletionDocumentResult)
    }
    DisposableEffect(viewModel, lifecycle, picker, deletionPicker) {
        val observer = LifecycleEventObserver { _, _ ->
            viewModel.onResumed(lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED))
        }
        lifecycle.addObserver(observer)
        viewModel.onResumed(lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED))
        onDispose {
            lifecycle.removeObserver(observer)
            picker?.close()
            deletionPicker?.close()
            viewModel.onHidden()
        }
    }
    LaunchedEffect(viewModel) { viewModel.onShown() }
    LaunchedEffect(viewModel, picker, state.step) {
        val owned = viewModel.claimDocumentRequest() ?: return@LaunchedEffect
        if (picker?.launch(owned) != true) viewModel.onPickerUnavailable(owned)
    }
    LaunchedEffect(viewModel, deletionPicker, state.step) {
        val owned = viewModel.claimDeletionDocumentRequest() ?: return@LaunchedEffect
        if (deletionPicker?.launch(owned) != true) viewModel.onDeletionPickerUnavailable(owned)
    }
    BackupContent(
        state = state,
        onIntent = viewModel::onIntent,
        onBack = if (showUp) ({ navigator.goBack() }) else null,
        modifier = modifier,
    )
    viewModel.keySession()?.let { owned ->
        BackupRecoveryKeyDialog(owned) { viewModel.onKeyOutcome(owned, it) }
    }
}
