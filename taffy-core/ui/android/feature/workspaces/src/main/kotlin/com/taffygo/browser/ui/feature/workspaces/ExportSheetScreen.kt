// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.ExportFormat
import com.taffygo.browser.ui.core.ui.LocalAppDispatchers
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.TaffyGroupedCard
import com.taffygo.browser.ui.core.ui.TaffyGroupedCardDivider
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffyScreen
import com.taffygo.browser.ui.core.ui.TaffySectionHeader
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString

/** Screen SCR-309 — the export sheet. */
@Composable
fun ExportSheetScreen(
    destination: TaffyDestination.ExportSheet,
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
) {
    val viewModel: ExportSheetViewModel = screenViewModel(destination)
    ExportSheetRoute(
        destination = destination,
        viewModel = viewModel,
        documentSpec = ExportFormat::workspaceDocumentSpec,
        navigator = navigator,
        modifier = modifier,
        showUp = showUp,
    )
}

/** Collection export uses the same trusted picker and exact-content writer. */
@Composable
fun LibraryExportSheetScreen(
    destination: TaffyDestination.LibraryExport,
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
) {
    val viewModel: ExportSheetViewModel = screenViewModel(destination)
    ExportSheetRoute(
        destination = destination,
        viewModel = viewModel,
        documentSpec = ExportFormat::libraryDocumentSpec,
        navigator = navigator,
        modifier = modifier,
        showUp = showUp,
    )
}

@Composable
private fun ExportSheetRoute(
    destination: TaffyDestination,
    viewModel: ExportSheetViewModel,
    documentSpec: (ExportFormat) -> ExportDocumentSpec,
    navigator: TaffyNavigator,
    modifier: Modifier,
    showUp: Boolean,
) {
    val state by viewModel.state.collectAsStateWithLifecycle()
    val context = LocalContext.current.applicationContext
    val ioDispatcher = LocalAppDispatchers.current.io
    val writer = remember(context, ioDispatcher) {
        WorkspaceExportDocumentWriter(context.contentResolver, ioDispatcher)
    }

    val createDocument = rememberLauncherForActivityResult(CreateWorkspaceExportDocument()) { uri ->
        val current = viewModel.state.value
        if (current.exportStatus != ExportStatus.CHOOSING_DESTINATION) {
            return@rememberLauncherForActivityResult
        }
        val format = current.selected
        if (uri == null) {
            viewModel.onIntent(ExportSheetIntent.DestinationCancelled, navigator)
            return@rememberLauncherForActivityResult
        }
        viewModel.onIntent(ExportSheetIntent.DestinationSelected, navigator)
        viewModel.writeExport(format) { content -> writer.write(uri, content) }
    }

    LaunchedEffect(Unit) { viewModel.onShown() }

    ExportSheetContent(
        state = state,
        onIntent = { intent ->
            when {
                intent != ExportSheetIntent.Export -> viewModel.onIntent(intent, navigator)
                !state.canExport -> Unit
                else -> {
                    val format = state.selected
                    viewModel.onIntent(ExportSheetIntent.Export, navigator)
                    try {
                        createDocument.launch(documentSpec(format))
                    } catch (_: RuntimeException) {
                        viewModel.onIntent(ExportSheetIntent.WriteFailed, navigator)
                    }
                }
            }
        },
        destination = destination,
        showUp = showUp,
        modifier = modifier,
    )
}

/** The stateless half. */
@Composable
fun ExportSheetContent(
    state: ExportSheetUiState,
    onIntent: (ExportSheetIntent) -> Unit,
    modifier: Modifier = Modifier,
    destination: TaffyDestination = TaffyDestination.ExportSheet(""),
    showUp: Boolean = true,
) {
    TaffyScreen(
        destination = destination,
        title = taffyString(R.string.taffy_export_title),
        onBack = if (showUp) ({ onIntent(ExportSheetIntent.Close) }) else null,
        modifier = modifier,
    ) {
        when {
            state.loading -> WorkspaceSkeletonList(
                loadingDescription = taffyString(R.string.taffy_export_loading),
                testTag = EXPORT_LOADING_TEST_TAG,
            )
            state.missing -> TaffyEmptyState(
                title = taffyString(R.string.taffy_export_missing_title),
                body = taffyString(R.string.taffy_export_missing_body),
                leading = { WorkspaceEmptyGlyph(TaffyIcon.Export) },
            )
            else -> ExportSheetBody(state = state, onIntent = onIntent)
        }
    }
}

@Composable
private fun ExportSheetBody(
    state: ExportSheetUiState,
    onIntent: (ExportSheetIntent) -> Unit,
) {
    TaffyGroupedCard {
        state.formats.forEachIndexed { index, format ->
            val chosen = format == state.selected
            val name = taffyString(formatName(format))
            WorkspaceRecordRow(
                title = name,
                accessibleDescription = name,
                glyph = if (format == ExportFormat.MARKDOWN) {
                    TaffyIcon.Article
                } else {
                    TaffyIcon.Table
                },
                selected = chosen,
                enabled = !chosen,
                onClick = { onIntent(ExportSheetIntent.Select(format)) },
                testTag = "$FORMAT_TEST_TAG_PREFIX${format.label}",
            )
            if (index < state.formats.lastIndex) TaffyGroupedCardDivider()
        }
    }

    Text(
        text = taffyString(R.string.taffy_export_destination),
        style = TaffyTheme.typography.detail,
        color = TaffyTheme.colors.textSecondary,
        modifier = Modifier.testTag(DESTINATION_TEST_TAG),
    )

    exportStatusMessage(state.exportStatus)?.let { message ->
        Text(
            text = taffyString(message),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
            modifier = Modifier.testTag(EXPORT_STATUS_TEST_TAG),
        )
    }

    TaffySectionHeader(title = taffyString(R.string.taffy_export_preview))
    TaffyGroupedCard {
        Text(
            text = state.preview,
            style = TaffyTheme.typography.numeric,
            color = TaffyTheme.colors.textPrimary,
            modifier = Modifier
                .padding(TaffyTheme.spacing.screenMargin)
                .testTag(PREVIEW_TEST_TAG),
        )
    }

    TaffyPrimaryButton(
        label = taffyString(R.string.taffy_export_action, taffyString(formatName(state.selected))),
        onClick = { onIntent(ExportSheetIntent.Export) },
        enabled = state.canExport,
        testTag = EXPORT_TEST_TAG,
        icon = TaffyIcon.Export,
    )
}

private fun formatName(format: ExportFormat) = when (format) {
    ExportFormat.MARKDOWN -> R.string.taffy_export_format_markdown
    ExportFormat.COMMA_SEPARATED -> R.string.taffy_export_format_comma_separated
}

private fun exportStatusMessage(status: ExportStatus): Int? = when (status) {
    ExportStatus.IDLE -> null
    ExportStatus.CHOOSING_DESTINATION -> R.string.taffy_export_choosing_destination
    ExportStatus.WRITING -> R.string.taffy_export_writing
    ExportStatus.SUCCEEDED -> R.string.taffy_export_succeeded
    ExportStatus.FAILED -> R.string.taffy_export_failed
    ExportStatus.CANCELLED -> R.string.taffy_export_cancelled
}

/** The tags screen SCR-309's semantics tests name. */
const val FORMAT_TEST_TAG_PREFIX: String = "export_format_"
const val DESTINATION_TEST_TAG: String = "export_destination"
const val PREVIEW_TEST_TAG: String = "export_preview"
const val EXPORT_TEST_TAG: String = "export_action"
const val EXPORT_LOADING_TEST_TAG: String = "export_loading"
const val EXPORT_STATUS_TEST_TAG: String = "export_status"
