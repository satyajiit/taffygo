// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.model.ExportFormat
import com.taffygo.browser.ui.core.model.WorkspaceExport
import com.taffygo.browser.ui.core.model.WorkspaceId
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.workspace.WorkspaceRepository
import javax.inject.Inject
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.flowOf
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch

/**
 * Screen SCR-309's one source of truth.
 *
 * This retains only a bounded preview and write state. Exact output stays in
 * the repository until the trusted Compose surface receives a granted URI;
 * it is never put in saved state, analytics, or a navigation argument.
 */
class ExportSheetViewModel @Inject constructor(
    workspaces: WorkspaceRepository,
    library: LibraryRepository,
    private val analytics: AnalyticsClient,
    savedState: SavedStateHandle,
) : ViewModel() {

    private val target = exportTarget(savedState, workspaces, library)

    private val internalState = MutableStateFlow(initialState())

    /** What screen SCR-309 renders. */
    val state: StateFlow<ExportSheetUiState> = internalState.asStateFlow()

    init {
        if (target.exists()) {
            request(ExportFormat.MARKDOWN)
        }
        viewModelScope.launch {
            target.changes.collect {
                val current = internalState.value
                if (!target.exists()) {
                    if (current.loading || current.missing) {
                        internalState.value = ExportSheetUiState(missing = true)
                    }
                } else if (current.loading || current.missing) {
                    internalState.value = ExportSheetUiState(
                        preview = target
                            .render(ExportFormat.MARKDOWN)
                            ?.let(::exportPreview)
                            .orEmpty(),
                    )
                    request(ExportFormat.MARKDOWN)
                }
            }
        }
        viewModelScope.launch {
            target.exports.collect { completion ->
                val selected = internalState.value.selected
                if (completion?.format == selected) {
                    internalState.value = internalState.value.copy(
                        preview = exportPreview(completion.content),
                        missing = false,
                        loading = false,
                    )
                }
            }
        }
    }

    /** Act on something the user did. */
    fun onIntent(intent: ExportSheetIntent, navigator: TaffyNavigator) {
        val prior = transition(intent)

        when (intent) {
            ExportSheetIntent.WriteSucceeded -> if (
                prior.exportStatus == ExportStatus.WRITING &&
                internalState.value.exportStatus == ExportStatus.SUCCEEDED
            ) {
                analytics.record(AnalyticsEvent.ArtifactExported(internalState.value.selected.label))
            }
            ExportSheetIntent.Close -> navigator.goBack()
            is ExportSheetIntent.Select -> request(intent.format)
            ExportSheetIntent.Export,
            ExportSheetIntent.DestinationSelected,
            ExportSheetIntent.DestinationCancelled,
            ExportSheetIntent.WriteFailed,
            -> Unit
        }
    }

    /** Writes the exact current Rust output without retaining it in UI state. */
    internal fun writeExport(
        format: ExportFormat,
        write: suspend (String) -> Boolean,
    ) {
        val current = internalState.value
        val content = if (
            current.selected == format && current.exportStatus == ExportStatus.WRITING
        ) {
            target.render(format)
        } else {
            null
        }
        if (content == null) {
            finishWrite(succeeded = false)
            return
        }
        viewModelScope.launch {
            val succeeded = try {
                write(content)
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (_: RuntimeException) {
                false
            }
            finishWrite(succeeded)
        }
    }

    /** Record that this screen was shown. */
    fun onShown() {
        analytics.record(
            AnalyticsEvent.ScreenShown(target.destination.screenId),
        )
    }

    private fun initialState(): ExportSheetUiState {
        if (!target.exists()) return ExportSheetUiState(loading = true)
        val rendered = target.render(ExportFormat.MARKDOWN)
        return ExportSheetUiState(preview = rendered?.let(::exportPreview).orEmpty())
    }

    private fun request(format: ExportFormat) {
        viewModelScope.launch {
            target.request(format)
        }
    }

    private fun transition(intent: ExportSheetIntent): ExportSheetUiState {
        val prior = internalState.value
        internalState.value = reduceExportSheet(
            state = prior,
            intent = intent,
            render = target::render,
            workspaceExists = target::exists,
        )
        return prior
    }

    private fun finishWrite(succeeded: Boolean) {
        val intent = if (succeeded) {
            ExportSheetIntent.WriteSucceeded
        } else {
            ExportSheetIntent.WriteFailed
        }
        val prior = transition(intent)
        if (succeeded &&
            prior.exportStatus == ExportStatus.WRITING &&
            internalState.value.exportStatus == ExportStatus.SUCCEEDED
        ) {
            analytics.record(AnalyticsEvent.ArtifactExported(internalState.value.selected.label))
        }
    }
}

private data class ExportArtifact(val format: ExportFormat, val content: String)

private interface ExportTarget {
    val destination: TaffyDestination
    val changes: Flow<*>
    val exports: Flow<ExportArtifact?>

    fun exists(): Boolean
    fun render(format: ExportFormat): String?
    suspend fun request(format: ExportFormat)
}

private class WorkspaceExportTarget(
    private val id: WorkspaceId,
    private val workspaces: WorkspaceRepository,
) : ExportTarget {
    override val destination: TaffyDestination = TaffyDestination.ExportSheet(id.value)
    override val changes: Flow<*> = workspaces.workspaces
    override val exports: Flow<ExportArtifact?> = workspaces.latestExport.map { export ->
        export?.takeIf { it.workspaceId == id }?.toArtifact()
    }

    override fun exists(): Boolean = workspaces.workspace(id) != null
    override fun render(format: ExportFormat): String? = workspaces.renderExport(id, format)
    override suspend fun request(format: ExportFormat) {
        workspaces.requestExport(id, format)
    }
}

private class LibraryExportTarget(
    private val collectionId: String,
    private val library: LibraryRepository,
) : ExportTarget {
    override val destination: TaffyDestination = TaffyDestination.LibraryExport(collectionId)
    override val changes: Flow<*> = library.snapshot
    override val exports: Flow<ExportArtifact?> = library.latestExport.map { export ->
        export?.takeIf { it.collectionId == collectionId }?.let {
            ExportArtifact(format = it.format, content = it.content)
        }
    }

    override fun exists(): Boolean = (library.snapshot.value as? LibraryRepository.Snapshot.Ready)
        ?.collections
        ?.any { it.id == collectionId } == true

    override fun render(format: ExportFormat): String? =
        library.renderExport(collectionId, format)

    override suspend fun request(format: ExportFormat) {
        library.requestExport(collectionId, format)
    }
}

private object MissingExportTarget : ExportTarget {
    override val destination: TaffyDestination = TaffyDestination.ExportSheet("")
    override val changes: Flow<*> = flowOf(Unit)
    override val exports: Flow<ExportArtifact?> = flowOf(null)

    override fun exists(): Boolean = false
    override fun render(format: ExportFormat): String? = null
    override suspend fun request(format: ExportFormat) = Unit
}

private fun exportTarget(
    savedState: SavedStateHandle,
    workspaces: WorkspaceRepository,
    library: LibraryRepository,
): ExportTarget {
    val workspaceId = savedState.get<String>(TaffyDestination.WORKSPACE_ID)
    val collectionId = savedState.get<String>(TaffyDestination.COLLECTION_ID)
    return when {
        !workspaceId.isNullOrEmpty() && collectionId == null ->
            WorkspaceExportTarget(WorkspaceId(workspaceId), workspaces)
        !collectionId.isNullOrEmpty() && workspaceId == null ->
            LibraryExportTarget(collectionId, library)
        else -> MissingExportTarget
    }
}

private fun WorkspaceExport.toArtifact(): ExportArtifact =
    ExportArtifact(format = format, content = content)
