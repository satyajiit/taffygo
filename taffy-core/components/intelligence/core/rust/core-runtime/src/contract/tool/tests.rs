// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::{ToolContractError, ToolEffect};
use core_service_types as wire;

pub(super) fn job() -> wire::ToolJobEffect {
    wire::ToolJobEffect {
        job_id: "job-1".to_owned(),
        runtime: wire::ToolRuntimeKind::Python,
        tool_id: "python.summary".to_owned(),
        tool_version: "v1".to_owned(),
        operation_kind: wire::ToolOperation::RunBundledPythonModule,
        budget: wire::ToolResourceBudget {
            max_input_bytes: 8,
            max_output_bytes: 8,
            max_memory_bytes: 1024,
            max_cpu_ms: 100,
            max_temporary_bytes: 0,
            max_output_chunks: 1,
        },
        bundled_python: Some(wire::BundledPythonArguments {
            entrypoint_id: "spreadsheet.build".to_owned(),
            input: b"input".to_vec(),
        }),
        local_model: None,
        media_probe: None,
        audio_extract: None,
        frame_sample: None,
        transcode: None,
        signed_wasm: None,
        local_embedding: None,
        task_id: "task-1".to_owned(),
    }
}

/// A job nothing owns is a direct request, and a job nothing can name is not.
///
/// Cancellation reaches a running worker by matching this field against the
/// cancelled task. Empty therefore has to stay legal - it is what a request
/// made outside any task looks like - while a malformed one has to be refused
/// here rather than left to fail silently later, because a task id that can
/// never match spends a process, a budget and an opened resource on work
/// nobody is able to stop.
#[test]
fn a_job_may_be_owned_by_no_task_and_never_by_an_unnameable_one() {
    let mut unowned = job();
    unowned.task_id = String::new();
    assert!(ToolEffect::new(unowned).is_ok());

    let mut malformed = job();
    malformed.task_id = "task 1".to_owned();
    assert_eq!(
        ToolEffect::new(malformed),
        Err(ToolContractError::InvalidIdentifier)
    );

    let mut oversized = job();
    oversized.task_id = "t".repeat(wire::MAX_IDENTIFIER_BYTES + 1);
    assert_eq!(
        ToolEffect::new(oversized),
        Err(ToolContractError::InvalidIdentifier)
    );
}

#[test]
fn runtime_operation_mismatch_fails_closed() {
    let mut job = job();
    job.runtime = wire::ToolRuntimeKind::Media;
    assert_eq!(ToolEffect::new(job), Err(ToolContractError::InvalidBody));
}

/// A perfectly well-formed entrypoint the build never heard of.
///
/// `validate_identifier` passes it - the alphabet and the length are fine -
/// which is exactly why this test exists: before the frozen registry, a
/// well-formed string was the whole of the check, and any name at all reached
/// a worker (decision 0064).
#[test]
fn an_unregistered_entrypoint_never_reaches_a_worker() {
    let mut job = job();
    if let Some(body) = job.bundled_python.as_mut() {
        body.entrypoint_id = "shell.run".to_owned();
    }
    assert_eq!(
        ToolEffect::new(job),
        Err(ToolContractError::UnknownEntrypoint)
    );
}

/// The rule the registry exists for, fired through the core's own gate.
///
/// `table.reshape` is a registry row and it is still refused, because the
/// portable core owns reshaping. A sandboxed interpreter started for it would
/// be an interpreter bought to do arithmetic that already has a home.
#[test]
fn an_entrypoint_with_a_native_alternative_is_refused_by_the_core() {
    let mut job = job();
    if let Some(body) = job.bundled_python.as_mut() {
        body.entrypoint_id = "table.reshape".to_owned();
    }
    assert_eq!(
        ToolEffect::new(job),
        Err(ToolContractError::EntrypointHasNativeAlternative(
            "core.table.reshape"
        ))
    );
}

/// The admitted half, so the two refusals above are not passing vacuously.
#[test]
fn a_registered_entrypoint_with_no_native_owner_is_admitted() {
    for entrypoint in ["document.build", "spreadsheet.build"] {
        let mut job = job();
        if let Some(body) = job.bundled_python.as_mut() {
            body.entrypoint_id = entrypoint.to_owned();
        }
        assert!(
            ToolEffect::new(job).is_ok(),
            "{entrypoint} was refused by the core"
        );
    }
}

/// The signed-WASM family carries an entrypoint and is deliberately not held
/// to the Python registry.
///
/// The rows say what a Python worker may do. Admitting them for a second
/// runtime would be this registry claiming something it never said, and
/// refusing every WASM name against them would be the same mistake wearing the
/// other sign. That runtime has no service and no registry of its own yet, and
/// this test records which of the two it is.
#[test]
fn the_wasm_family_is_not_held_to_the_python_registry() {
    let mut job = job();
    job.runtime = wire::ToolRuntimeKind::Wasm;
    job.operation_kind = wire::ToolOperation::RunSignedWasmTransform;
    job.bundled_python = None;
    job.signed_wasm = Some(wire::SignedWasmArguments {
        entrypoint_id: "transform".to_owned(),
        input: b"input".to_vec(),
    });
    assert!(ToolEffect::new(job).is_ok());
}

#[test]
fn completion_must_match_job_operation_and_order() {
    let effect = ToolEffect::new(job()).unwrap_or_else(|_| unreachable!());
    let result = wire::ToolEffectResult {
        job_id: "job-1".to_owned(),
        status: wire::ToolTerminalStatus::Completed,
        progress: vec![wire::ToolProgress {
            job_id: "job-1".to_owned(),
            sequence: 1,
            progress_basis_points: 10_000,
        }],
        chunks: Vec::new(),
        success: Some(wire::ToolSuccess {
            operation_kind: wire::ToolOperation::RunBundledPythonModule,
            bundled_python: Some(wire::BundledPythonResult {
                output: b"done".to_vec(),
            }),
            local_model: None,
            media_probe: None,
            audio_extract: None,
            frame_sample: None,
            transcode: None,
            signed_wasm: None,
            local_embedding: None,
        }),
        streamed_chunks: 0,
    };
    assert!(effect.accept_completion(result).is_ok());
}

#[test]
fn completion_cannot_exceed_its_job_chunk_budget() {
    let mut request = job();
    request.budget.max_output_chunks = 0;
    let effect = ToolEffect::new(request).unwrap_or_else(|_| unreachable!());
    let result = wire::ToolEffectResult {
        job_id: "job-1".to_owned(),
        status: wire::ToolTerminalStatus::RuntimeCrashed,
        progress: Vec::new(),
        chunks: vec![wire::ToolOutputChunk {
            job_id: "job-1".to_owned(),
            sequence: 1,
            kind: wire::ToolChunkKind::TextUtf8,
            text: Some(wire::TextOutputChunk {
                utf8: b"late".to_vec(),
            }),
            binary: None,
            is_final: true,
        }],
        success: None,
        streamed_chunks: 0,
    };
    assert_eq!(
        effect.accept_completion(result),
        Err(ToolContractError::TooManyOutputChunks)
    );
}

fn artifact() -> wire::ToolModelArtifact {
    wire::ToolModelArtifact {
        model_id: "embedding.passage.small".to_owned(),
        model_revision: "2026-08-01".to_owned(),
        kind: wire::ToolModelArtifactKind::LitertTflite,
        artifact_bytes: 0,
        artifact_digest: [0; 32],
        adapter_id: String::new(),
        adapter_bytes: 0,
        adapter_digest: [0; 32],
    }
}

fn embedding_job() -> wire::ToolJobEffect {
    let mut job = job();
    job.job_id = "job-embed".to_owned();
    job.runtime = wire::ToolRuntimeKind::LocalModel;
    job.operation_kind = wire::ToolOperation::EmbedLocalModel;
    job.bundled_python = None;
    job.local_embedding = Some(wire::LocalEmbeddingArguments {
        model: artifact(),
        input_utf8: b"input".to_vec(),
        max_input_tokens: 64,
        normalize: true,
    });
    job
}

fn embedding_completion(dimensions: u32, values: Vec<u8>) -> wire::ToolEffectResult {
    wire::ToolEffectResult {
        job_id: "job-embed".to_owned(),
        status: wire::ToolTerminalStatus::Completed,
        progress: Vec::new(),
        chunks: Vec::new(),
        success: Some(wire::ToolSuccess {
            operation_kind: wire::ToolOperation::EmbedLocalModel,
            bundled_python: None,
            local_model: None,
            media_probe: None,
            audio_extract: None,
            frame_sample: None,
            transcode: None,
            signed_wasm: None,
            local_embedding: Some(wire::LocalEmbeddingResult {
                dimensions,
                element_kind: wire::EmbeddingElementKind::Float32Le,
                values,
                input_tokens: 1,
            }),
        }),
        streamed_chunks: 0,
    }
}

#[test]
fn an_embedding_returns_one_bounded_vector() {
    let effect = ToolEffect::new(embedding_job()).unwrap_or_else(|_| unreachable!());
    assert!(effect
        .accept_completion(embedding_completion(2, vec![0; 8]))
        .is_ok());
}

#[test]
fn a_vector_whose_length_contradicts_its_bytes_fails_closed() {
    let effect = ToolEffect::new(embedding_job()).unwrap_or_else(|_| unreachable!());
    // Seven bytes cannot be two four-byte elements, and believing either number
    // over the other is how a reader ends up sizing itself from a worker.
    assert_eq!(
        effect.accept_completion(embedding_completion(2, vec![0; 7])),
        Err(ToolContractError::InvalidBody)
    );
}

#[test]
fn an_embedding_is_only_reachable_from_the_local_model_runtime() {
    let mut job = embedding_job();
    job.runtime = wire::ToolRuntimeKind::Python;
    assert_eq!(ToolEffect::new(job), Err(ToolContractError::InvalidBody));
}
