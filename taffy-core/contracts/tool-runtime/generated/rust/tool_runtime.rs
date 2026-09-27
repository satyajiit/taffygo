// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Generated from schema/contract.json. Do not edit.
// Contract tool_runtime 2.4.

#![allow(
    clippy::module_name_repetitions,
    clippy::needless_question_mark,
    clippy::struct_excessive_bools,
    clippy::too_many_lines,
    clippy::trivially_copy_pass_by_ref
)]

pub const MAX_JOB_INPUT_BYTES: usize = 16_777_216;
pub const MAX_JOB_OUTPUT_BYTES: usize = 16_777_216;
pub const MAX_JOB_ID_BYTES: usize = 128;
pub const MAX_OPERATION_ID_BYTES: usize = 128;
pub const MAX_IDEMPOTENCY_KEY_BYTES: usize = 128;
pub const MAX_TOOL_VERSION_BYTES: usize = 64;
pub const MAX_CONVERSATION_ID_BYTES: usize = 128;
pub const MAX_JOB_MEMORY_BYTES: usize = 4_294_967_296;
pub const MAX_JOB_CPU_MS: usize = 600_000;
pub const MAX_JOB_TEMPORARY_BYTES: usize = 1_073_741_824;
pub const MAX_JOB_WALL_TIME_MS: usize = 600_000;
pub const MAX_OUTPUT_CHUNK_BYTES: usize = 65_536;
pub const MAX_OUTPUT_CHUNKS: usize = 256;
pub const MAX_PROGRESS_EVENTS: usize = 256;
pub const MAX_TOOL_ID_BYTES: usize = 128;
pub const MAX_ENTRYPOINT_ID_BYTES: usize = 128;
pub const MAX_HANDLE_ID_BYTES: usize = 256;
pub const MAX_PRESET_ID_BYTES: usize = 128;
pub const MAX_INLINE_PAYLOAD_BYTES: usize = 262_144;
pub const MAX_STREAM_OUTPUT_CHUNKS: usize = 65_536;
pub const MAX_STREAM_CHUNK_BYTES: usize = 4_096;
pub const MAX_MODEL_ID_BYTES: usize = 128;
pub const MAX_MODEL_REVISION_BYTES: usize = 64;
pub const MAX_MODEL_ARTIFACT_BYTES: usize = 2_147_483_648;
pub const MAX_EMBEDDING_DIMENSIONS: usize = 4_096;
pub const MAX_EMBEDDING_VALUE_BYTES: usize = 16_384;
pub const MAX_PYTHON_LIBRARY_ID_BYTES: usize = 128;
pub const MAX_PYTHON_LIBRARY_VERSION_BYTES: usize = 64;
pub const MAX_PYTHON_LIBRARY_BYTES: usize = 134_217_728;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum ToolRuntimeKind {
    Python = 0,
    LocalModel = 1,
    Media = 2,
    Wasm = 3,
}

impl ToolRuntimeKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Python),
            1 => Some(Self::LocalModel),
            2 => Some(Self::Media),
            3 => Some(Self::Wasm),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum ToolOperation {
    RunBundledPythonModule = 0,
    GenerateLocalModel = 1,
    ProbeMedia = 2,
    ExtractAudio = 3,
    SampleFrames = 4,
    TranscodePreset = 5,
    RunSignedWasmTransform = 6,
    EmbedLocalModel = 7,
}

impl ToolOperation {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::RunBundledPythonModule),
            1 => Some(Self::GenerateLocalModel),
            2 => Some(Self::ProbeMedia),
            3 => Some(Self::ExtractAudio),
            4 => Some(Self::SampleFrames),
            5 => Some(Self::TranscodePreset),
            6 => Some(Self::RunSignedWasmTransform),
            7 => Some(Self::EmbedLocalModel),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum ToolModelArtifactKind {
    LitertTflite = 0,
    OnnxRuntime = 1,
    Gguf = 2,
}

impl ToolModelArtifactKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::LitertTflite),
            1 => Some(Self::OnnxRuntime),
            2 => Some(Self::Gguf),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum ToolInputTransport {
    Inline = 0,
    SharedMemory = 1,
}

impl ToolInputTransport {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Inline),
            1 => Some(Self::SharedMemory),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum ToolOutputTransport {
    InlineChunks = 0,
    StreamChunks = 1,
    DataPipe = 2,
}

impl ToolOutputTransport {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::InlineChunks),
            1 => Some(Self::StreamChunks),
            2 => Some(Self::DataPipe),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum EmbeddingElementKind {
    Float32Le = 0,
}

impl EmbeddingElementKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Float32Le),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum ToolChunkKind {
    TextUtf8 = 0,
    Binary = 1,
}

impl ToolChunkKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::TextUtf8),
            1 => Some(Self::Binary),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum LocalModelFinishReason {
    Stop = 0,
    TokenLimit = 1,
}

impl LocalModelFinishReason {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Stop),
            1 => Some(Self::TokenLimit),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum ToolTerminalStatus {
    Completed = 0,
    Cancelled = 1,
    DeadlineExceeded = 2,
    ResourceLimit = 3,
    RuntimeCrashed = 4,
    InvalidInput = 5,
    Unsupported = 6,
    OutcomeUnknown = 7,
    ModelArtifactMissing = 8,
    ModelArtifactIncompatible = 9,
    LocalRuntimeUnavailable = 10,
}

impl ToolTerminalStatus {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Completed),
            1 => Some(Self::Cancelled),
            2 => Some(Self::DeadlineExceeded),
            3 => Some(Self::ResourceLimit),
            4 => Some(Self::RuntimeCrashed),
            5 => Some(Self::InvalidInput),
            6 => Some(Self::Unsupported),
            7 => Some(Self::OutcomeUnknown),
            8 => Some(Self::ModelArtifactMissing),
            9 => Some(Self::ModelArtifactIncompatible),
            10 => Some(Self::LocalRuntimeUnavailable),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum ToolAdmissionStatus {
    Accepted = 0,
    Unsupported = 1,
    InvalidJob = 2,
    Backpressure = 3,
    DeadlineExceeded = 4,
    ModelArtifactMissing = 5,
    ModelArtifactIncompatible = 6,
    LocalRuntimeUnavailable = 7,
}

impl ToolAdmissionStatus {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Accepted),
            1 => Some(Self::Unsupported),
            2 => Some(Self::InvalidJob),
            3 => Some(Self::Backpressure),
            4 => Some(Self::DeadlineExceeded),
            5 => Some(Self::ModelArtifactMissing),
            6 => Some(Self::ModelArtifactIncompatible),
            7 => Some(Self::LocalRuntimeUnavailable),
            _ => None,
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct OperationEnvelope {
    pub operation_id: String,
    pub service_generation: u64,
    pub task_revision: u64,
    pub deadline_monotonic_ms: u64,
    pub idempotency_key: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ResourceBudget {
    pub max_input_bytes: u64,
    pub max_output_bytes: u64,
    pub max_memory_bytes: u64,
    pub max_cpu_ms: u64,
    pub max_temporary_bytes: u64,
    pub max_output_chunks: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ToolModelArtifact {
    pub model_id: String,
    pub model_revision: String,
    pub kind: ToolModelArtifactKind,
    pub artifact_bytes: u64,
    pub artifact_digest: [u8; 32],
    pub adapter_id: String,
    pub adapter_bytes: u64,
    pub adapter_digest: [u8; 32],
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ToolPythonLibrary {
    pub library_id: String,
    pub library_version: String,
    pub archive_bytes: u64,
    pub archive_digest: [u8; 32],
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ToolPayloadInput {
    pub transport: ToolInputTransport,
    pub byte_length: u64,
    pub digest: [u8; 32],
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ToolPayloadOutput {
    pub transport: ToolOutputTransport,
    pub max_byte_length: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BundledPythonArguments {
    pub entrypoint_id: String,
    pub input: Vec<u8>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct LocalModelArguments {
    pub conversation_id: String,
    pub prompt_utf8: Vec<u8>,
    pub max_output_tokens: u32,
    pub model: ToolModelArtifact,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct LocalEmbeddingArguments {
    pub model: ToolModelArtifact,
    pub input_utf8: Vec<u8>,
    pub max_input_tokens: u32,
    pub normalize: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MediaProbeArguments {
    pub input_handle: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AudioExtractArguments {
    pub input_handle: String,
    pub output_handle: String,
    pub preset_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct FrameSampleArguments {
    pub input_handle: String,
    pub output_handle: String,
    pub preset_id: String,
    pub max_frames: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TranscodeArguments {
    pub input_handle: String,
    pub output_handle: String,
    pub preset_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SignedWasmArguments {
    pub entrypoint_id: String,
    pub input: Vec<u8>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ToolJob {
    pub operation: OperationEnvelope,
    pub job_id: String,
    pub runtime: ToolRuntimeKind,
    pub tool_id: String,
    pub tool_version: String,
    pub operation_kind: ToolOperation,
    pub budget: ResourceBudget,
    pub bundled_python: Option<BundledPythonArguments>,
    pub local_model: Option<LocalModelArguments>,
    pub media_probe: Option<MediaProbeArguments>,
    pub audio_extract: Option<AudioExtractArguments>,
    pub frame_sample: Option<FrameSampleArguments>,
    pub transcode: Option<TranscodeArguments>,
    pub signed_wasm: Option<SignedWasmArguments>,
    pub input_payload: ToolPayloadInput,
    pub output_payload: ToolPayloadOutput,
    pub local_embedding: Option<LocalEmbeddingArguments>,
    pub python_library: ToolPythonLibrary,
}

impl ToolJob {
    pub fn has_valid_body(&self) -> bool {
        let body_count = [
            self.bundled_python.is_some(),
            self.local_model.is_some(),
            self.media_probe.is_some(),
            self.audio_extract.is_some(),
            self.frame_sample.is_some(),
            self.transcode.is_some(),
            self.signed_wasm.is_some(),
            self.local_embedding.is_some(),
        ]
        .into_iter()
        .filter(|present| *present)
        .count();
        body_count == 1
            && match self.operation_kind {
                ToolOperation::RunBundledPythonModule => self.bundled_python.is_some(),
                ToolOperation::GenerateLocalModel => self.local_model.is_some(),
                ToolOperation::ProbeMedia => self.media_probe.is_some(),
                ToolOperation::ExtractAudio => self.audio_extract.is_some(),
                ToolOperation::SampleFrames => self.frame_sample.is_some(),
                ToolOperation::TranscodePreset => self.transcode.is_some(),
                ToolOperation::RunSignedWasmTransform => self.signed_wasm.is_some(),
                ToolOperation::EmbedLocalModel => self.local_embedding.is_some(),
            }
            && matches!(
                (self.runtime, self.operation_kind),
                (ToolRuntimeKind::Python, ToolOperation::RunBundledPythonModule)
                    | (ToolRuntimeKind::LocalModel, ToolOperation::GenerateLocalModel | ToolOperation::EmbedLocalModel)
                    | (ToolRuntimeKind::Media, ToolOperation::ProbeMedia | ToolOperation::ExtractAudio | ToolOperation::SampleFrames | ToolOperation::TranscodePreset)
                    | (ToolRuntimeKind::Wasm, ToolOperation::RunSignedWasmTransform)
            )
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ToolProgress {
    pub job_id: String,
    pub sequence: u32,
    pub progress_basis_points: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TextOutputChunk {
    pub utf8: Vec<u8>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BinaryOutputChunk {
    pub data: Vec<u8>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ToolOutputChunk {
    pub operation: OperationEnvelope,
    pub job_id: String,
    pub sequence: u32,
    pub kind: ToolChunkKind,
    pub text: Option<TextOutputChunk>,
    pub binary: Option<BinaryOutputChunk>,
    pub is_final: bool,
}

impl ToolOutputChunk {
    pub fn has_valid_body(&self) -> bool {
        let body_count = [
            self.text.is_some(),
            self.binary.is_some(),
        ]
        .into_iter()
        .filter(|present| *present)
        .count();
        body_count == 1
            && match self.kind {
                ToolChunkKind::TextUtf8 => self.text.is_some(),
                ToolChunkKind::Binary => self.binary.is_some(),
            }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ToolAdmission {
    pub job_id: String,
    pub status: ToolAdmissionStatus,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BundledPythonResult {
    pub output: Vec<u8>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct LocalModelResult {
    pub finish_reason: LocalModelFinishReason,
    pub input_tokens: u32,
    pub output_tokens: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct LocalEmbeddingResult {
    pub dimensions: u32,
    pub element_kind: EmbeddingElementKind,
    pub values: Vec<u8>,
    pub input_tokens: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MediaProbeResult {
    pub duration_ms: u64,
    pub audio_streams: u32,
    pub video_streams: u32,
    pub width: u32,
    pub height: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AudioExtractResult {
    pub output_handle: String,
    pub output_bytes: u64,
    pub output_digest: Vec<u8>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct FrameSampleResult {
    pub output_handle: String,
    pub frame_count: u32,
    pub output_bytes: u64,
    pub output_digest: Vec<u8>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TranscodeResult {
    pub output_handle: String,
    pub output_bytes: u64,
    pub output_digest: Vec<u8>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SignedWasmResult {
    pub output: Vec<u8>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ToolSuccess {
    pub operation_kind: ToolOperation,
    pub bundled_python: Option<BundledPythonResult>,
    pub local_model: Option<LocalModelResult>,
    pub media_probe: Option<MediaProbeResult>,
    pub audio_extract: Option<AudioExtractResult>,
    pub frame_sample: Option<FrameSampleResult>,
    pub transcode: Option<TranscodeResult>,
    pub signed_wasm: Option<SignedWasmResult>,
    pub local_embedding: Option<LocalEmbeddingResult>,
}

impl ToolSuccess {
    pub fn has_valid_body(&self) -> bool {
        let body_count = [
            self.bundled_python.is_some(),
            self.local_model.is_some(),
            self.media_probe.is_some(),
            self.audio_extract.is_some(),
            self.frame_sample.is_some(),
            self.transcode.is_some(),
            self.signed_wasm.is_some(),
            self.local_embedding.is_some(),
        ]
        .into_iter()
        .filter(|present| *present)
        .count();
        body_count == 1
            && match self.operation_kind {
                ToolOperation::RunBundledPythonModule => self.bundled_python.is_some(),
                ToolOperation::GenerateLocalModel => self.local_model.is_some(),
                ToolOperation::ProbeMedia => self.media_probe.is_some(),
                ToolOperation::ExtractAudio => self.audio_extract.is_some(),
                ToolOperation::SampleFrames => self.frame_sample.is_some(),
                ToolOperation::TranscodePreset => self.transcode.is_some(),
                ToolOperation::RunSignedWasmTransform => self.signed_wasm.is_some(),
                ToolOperation::EmbedLocalModel => self.local_embedding.is_some(),
            }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ToolCompletion {
    pub operation: OperationEnvelope,
    pub job_id: String,
    pub status: ToolTerminalStatus,
    pub success: Option<ToolSuccess>,
}

impl ToolCompletion {
    pub fn has_valid_presence(&self) -> bool {
        match self.status {
            ToolTerminalStatus::Completed => self.success.is_some(),
            _ => self.success.is_none(),
        }
    }
}
