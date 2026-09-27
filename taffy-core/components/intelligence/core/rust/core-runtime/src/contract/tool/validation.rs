// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use core_service_types as wire;

use super::ToolContractError;

pub(super) fn validate_job(job: &wire::ToolJobEffect) -> Result<(), ToolContractError> {
    if !job.has_valid_body() {
        return Err(ToolContractError::InvalidBody);
    }
    validate_identifier(&job.job_id, wire::MAX_TOOL_JOB_ID_BYTES)?;
    validate_identifier(&job.tool_id, wire::MAX_TOOL_ID_BYTES)?;
    validate_identifier(&job.tool_version, wire::MAX_TOOL_VERSION_BYTES)?;
    validate_owning_task(&job.task_id)?;
    validate_budget(&job.budget)?;
    validate_arguments(job)
}

/// The task a cancellation has to name to reach this job's worker.
///
/// Empty is the one thing this field may be without being wrong: it means no
/// task owns the job, which is a direct request rather than a defect. A
/// non-empty one is matched against the cancelled task by the browser, so it
/// is held to the same shape every other identity here is - an over-long or
/// exotic one would simply never match, and a job nothing can cancel is the
/// state this field exists to end.
fn validate_owning_task(task_id: &str) -> Result<(), ToolContractError> {
    if task_id.is_empty() {
        return Ok(());
    }
    validate_identifier(task_id, wire::MAX_IDENTIFIER_BYTES)
}

pub(super) fn validate_completion(
    effect: &wire::ToolJobEffect,
    result: &wire::ToolEffectResult,
) -> Result<(), ToolContractError> {
    if result.job_id != effect.job_id {
        return Err(ToolContractError::WrongJob);
    }
    if !result.has_valid_presence()
        || result
            .success
            .as_ref()
            .is_some_and(|success| !success.has_valid_body())
    {
        return Err(ToolContractError::InvalidBody);
    }
    if result
        .success
        .as_ref()
        .is_some_and(|success| success.operation_kind != effect.operation_kind)
    {
        return Err(ToolContractError::WrongOperation);
    }
    validate_embedding(result)?;
    validate_progress(result)?;
    validate_chunks(
        result,
        effect.budget.max_output_bytes,
        effect.budget.max_output_chunks,
    )?;
    validate_success(result, effect.budget.max_output_bytes)
}

fn validate_identifier(value: &str, max_bytes: usize) -> Result<(), ToolContractError> {
    if value.is_empty()
        || value.len() > max_bytes
        || !value
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'.' | b'_' | b'-'))
    {
        return Err(ToolContractError::InvalidIdentifier);
    }
    Ok(())
}

fn validate_budget(budget: &wire::ToolResourceBudget) -> Result<(), ToolContractError> {
    let valid = budget.max_input_bytes <= wire::MAX_TOOL_JOB_INPUT_BYTES as u64
        && budget.max_output_bytes <= wire::MAX_TOOL_JOB_OUTPUT_BYTES as u64
        && budget.max_memory_bytes > 0
        && budget.max_memory_bytes <= wire::MAX_TOOL_JOB_MEMORY_BYTES as u64
        && budget.max_cpu_ms > 0
        && budget.max_cpu_ms <= wire::MAX_TOOL_JOB_CPU_MS as u64
        && budget.max_temporary_bytes <= wire::MAX_TOOL_JOB_TEMPORARY_BYTES as u64
        && usize::try_from(budget.max_output_chunks)
            .is_ok_and(|chunks| chunks <= wire::MAX_TOOL_OUTPUT_CHUNKS);
    if !valid {
        return Err(ToolContractError::InvalidBudget);
    }
    Ok(())
}

fn validate_arguments(job: &wire::ToolJobEffect) -> Result<(), ToolContractError> {
    let input_bytes = match job.operation_kind {
        wire::ToolOperation::RunBundledPythonModule => {
            let body = job
                .bundled_python
                .as_ref()
                .ok_or(ToolContractError::InvalidBody)?;
            validate_identifier(&body.entrypoint_id, wire::MAX_TOOL_ENTRYPOINT_ID_BYTES)?;
            validate_python_entrypoint(&body.entrypoint_id)?;
            body.input.len()
        }
        wire::ToolOperation::GenerateLocalModel => {
            let body = job
                .local_model
                .as_ref()
                .ok_or(ToolContractError::InvalidBody)?;
            validate_model_artifact(&body.model)?;
            validate_identifier(&body.conversation_id, wire::MAX_TOOL_CONVERSATION_ID_BYTES)?;
            if body.max_output_tokens == 0 {
                return Err(ToolContractError::InvalidBudget);
            }
            body.prompt_utf8.len()
        }
        wire::ToolOperation::ProbeMedia => {
            let body = job
                .media_probe
                .as_ref()
                .ok_or(ToolContractError::InvalidBody)?;
            validate_identifier(&body.input_handle, wire::MAX_TOOL_HANDLE_ID_BYTES)?;
            0
        }
        wire::ToolOperation::ExtractAudio => {
            let body = job
                .audio_extract
                .as_ref()
                .ok_or(ToolContractError::InvalidBody)?;
            validate_media_output(&body.input_handle, &body.output_handle, &body.preset_id)?;
            0
        }
        wire::ToolOperation::SampleFrames => {
            let body = job
                .frame_sample
                .as_ref()
                .ok_or(ToolContractError::InvalidBody)?;
            validate_media_output(&body.input_handle, &body.output_handle, &body.preset_id)?;
            if body.max_frames == 0 {
                return Err(ToolContractError::InvalidBudget);
            }
            0
        }
        wire::ToolOperation::TranscodePreset => {
            let body = job
                .transcode
                .as_ref()
                .ok_or(ToolContractError::InvalidBody)?;
            validate_media_output(&body.input_handle, &body.output_handle, &body.preset_id)?;
            0
        }
        wire::ToolOperation::RunSignedWasmTransform => {
            let body = job
                .signed_wasm
                .as_ref()
                .ok_or(ToolContractError::InvalidBody)?;
            validate_identifier(&body.entrypoint_id, wire::MAX_TOOL_ENTRYPOINT_ID_BYTES)?;
            body.input.len()
        }
        wire::ToolOperation::EmbedLocalModel => {
            let body = job
                .local_embedding
                .as_ref()
                .ok_or(ToolContractError::InvalidBody)?;
            validate_model_artifact(&body.model)?;
            if body.max_input_tokens == 0 {
                return Err(ToolContractError::InvalidBudget);
            }
            body.input_utf8.len()
        }
    };
    if input_bytes > wire::MAX_TOOL_JOB_INPUT_BYTES
        || u64::try_from(input_bytes).unwrap_or(u64::MAX) > job.budget.max_input_bytes
    {
        return Err(ToolContractError::InputTooLarge);
    }
    Ok(())
}

/// The gate that decides whether a Python worker may be started at all.
///
/// A well-formed identifier is not a permission. Everything above this point
/// checks that the field *could* be an entrypoint - the alphabet, the length,
/// the body it arrived in - and none of it checks that this build was compiled
/// with the entrypoint the job names. That check is here, against the frozen
/// registry, and it is the reason a Python worker is bounded: the set of
/// things one can be asked to do is fixed when the binary is built.
///
/// The native-alternative arm is the one worth reading twice. A row that names
/// a native owner is refused even though the registry has it, because starting
/// a sandboxed interpreter to do something the product already owns spends a
/// process, a budget and an attack surface to arrive at the same answer. The
/// refusal carries the owner's identity so the caller learns where the work
/// actually belongs (decision 0064).
///
/// Only the Python runtime is gated here. `RunSignedWasmTransform` carries an
/// entrypoint too, and it is deliberately not checked against this table: the
/// rows describe what a Python worker may do, and admitting them for a second
/// runtime would be this registry claiming something it never said.
fn validate_python_entrypoint(entrypoint_id: &str) -> Result<(), ToolContractError> {
    match tool_entrypoints::admit(entrypoint_id) {
        tool_entrypoints::Admission::Admitted(_) => Ok(()),
        tool_entrypoints::Admission::Unknown => Err(ToolContractError::UnknownEntrypoint),
        tool_entrypoints::Admission::RefusedForNativePath(native) => Err(
            ToolContractError::EntrypointHasNativeAlternative(native.id()),
        ),
    }
}

/// The identity of a registered model, and nothing more.
///
/// The reducer names a model and a revision. The length, digest and format
/// beside them are the browser's to fill from its own register when it opens
/// the artifact, so what is checked here is that the identity is well formed
/// and the proposed sizes are not absurd - never that they are true.
fn validate_model_artifact(artifact: &wire::ToolModelArtifact) -> Result<(), ToolContractError> {
    validate_identifier(&artifact.model_id, wire::MAX_TOOL_MODEL_ID_BYTES)?;
    validate_identifier(
        &artifact.model_revision,
        wire::MAX_TOOL_MODEL_REVISION_BYTES,
    )?;
    if artifact.adapter_id.len() > wire::MAX_TOOL_MODEL_ID_BYTES
        || artifact.artifact_bytes > wire::MAX_TOOL_MODEL_ARTIFACT_BYTES as u64
        || artifact.adapter_bytes > wire::MAX_TOOL_MODEL_ARTIFACT_BYTES as u64
    {
        return Err(ToolContractError::InvalidBody);
    }
    Ok(())
}

fn validate_media_output(input: &str, output: &str, preset: &str) -> Result<(), ToolContractError> {
    validate_identifier(input, wire::MAX_TOOL_HANDLE_ID_BYTES)?;
    validate_identifier(output, wire::MAX_TOOL_HANDLE_ID_BYTES)?;
    validate_identifier(preset, wire::MAX_TOOL_PRESET_ID_BYTES)
}

/// A vector is only a vector when its two independent descriptions agree.
///
/// `dimensions` and `values` are produced separately by a worker, so trusting
/// the first would size a reader from a worker's arithmetic and trusting the
/// second would hand a caller a vector of a length nobody asked for.
fn validate_embedding(result: &wire::ToolEffectResult) -> Result<(), ToolContractError> {
    let Some(embedding) = result
        .success
        .as_ref()
        .and_then(|success| success.local_embedding.as_ref())
    else {
        return Ok(());
    };
    let dimensions = embedding.dimensions as usize;
    if embedding.element_kind != wire::EmbeddingElementKind::Float32Le
        || dimensions == 0
        || dimensions > wire::MAX_TOOL_EMBEDDING_DIMENSIONS
        || embedding.values.len() != dimensions.saturating_mul(4)
        || embedding.values.len() > wire::MAX_TOOL_EMBEDDING_VALUE_BYTES
    {
        return Err(ToolContractError::InvalidBody);
    }
    Ok(())
}

fn validate_progress(result: &wire::ToolEffectResult) -> Result<(), ToolContractError> {
    if result.progress.len() > wire::MAX_TOOL_PROGRESS_EVENTS {
        return Err(ToolContractError::TooManyProgressEvents);
    }
    let mut previous = None;
    for progress in &result.progress {
        if progress.job_id != result.job_id || progress.progress_basis_points > 10_000 {
            return Err(ToolContractError::InvalidProgress);
        }
        if previous.is_some_and(|sequence| progress.sequence <= sequence) {
            return Err(ToolContractError::InvalidSequence);
        }
        previous = Some(progress.sequence);
    }
    Ok(())
}

fn validate_chunks(
    result: &wire::ToolEffectResult,
    max_output: u64,
    max_chunks: u32,
) -> Result<(), ToolContractError> {
    if result.chunks.len() > wire::MAX_TOOL_OUTPUT_CHUNKS
        || u32::try_from(result.chunks.len()).map_or(true, |count| count > max_chunks)
    {
        return Err(ToolContractError::TooManyOutputChunks);
    }
    let mut previous = None;
    let mut total = 0_u64;
    for chunk in &result.chunks {
        if chunk.job_id != result.job_id || !chunk.has_valid_body() {
            return Err(ToolContractError::InvalidBody);
        }
        if previous.is_some_and(|sequence| chunk.sequence <= sequence) {
            return Err(ToolContractError::InvalidSequence);
        }
        previous = Some(chunk.sequence);
        let length = match chunk.kind {
            wire::ToolChunkKind::TextUtf8 => chunk.text.as_ref().map_or(0, |text| text.utf8.len()),
            wire::ToolChunkKind::Binary => {
                chunk.binary.as_ref().map_or(0, |binary| binary.data.len())
            }
        };
        if length > wire::MAX_TOOL_OUTPUT_CHUNK_BYTES {
            return Err(ToolContractError::OutputTooLarge);
        }
        total = total.saturating_add(u64::try_from(length).unwrap_or(u64::MAX));
    }
    if total > max_output || total > wire::MAX_TOOL_JOB_OUTPUT_BYTES as u64 {
        return Err(ToolContractError::OutputTooLarge);
    }
    Ok(())
}

fn validate_success(
    result: &wire::ToolEffectResult,
    max_output: u64,
) -> Result<(), ToolContractError> {
    let bytes = result.success.as_ref().map_or(0, |success| {
        success
            .bundled_python
            .as_ref()
            .map_or(0, |value| value.output.len())
            .saturating_add(
                success
                    .signed_wasm
                    .as_ref()
                    .map_or(0, |value| value.output.len()),
            )
            .saturating_add(
                success
                    .local_embedding
                    .as_ref()
                    .map_or(0, |value| value.values.len()),
            )
    });
    if bytes > wire::MAX_TOOL_JOB_OUTPUT_BYTES
        || u64::try_from(bytes).unwrap_or(u64::MAX) > max_output
    {
        return Err(ToolContractError::OutputTooLarge);
    }
    Ok(())
}
