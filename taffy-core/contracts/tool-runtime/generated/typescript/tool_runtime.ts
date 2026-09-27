// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Generated from schema/contract.json. Do not edit.
// Contract tool_runtime 2.4.

export const MAX_JOB_INPUT_BYTES = 16777216 as const;
export const MAX_JOB_OUTPUT_BYTES = 16777216 as const;
export const MAX_JOB_ID_BYTES = 128 as const;
export const MAX_OPERATION_ID_BYTES = 128 as const;
export const MAX_IDEMPOTENCY_KEY_BYTES = 128 as const;
export const MAX_TOOL_VERSION_BYTES = 64 as const;
export const MAX_CONVERSATION_ID_BYTES = 128 as const;
export const MAX_JOB_MEMORY_BYTES = 4294967296 as const;
export const MAX_JOB_CPU_MS = 600000 as const;
export const MAX_JOB_TEMPORARY_BYTES = 1073741824 as const;
export const MAX_JOB_WALL_TIME_MS = 600000 as const;
export const MAX_OUTPUT_CHUNK_BYTES = 65536 as const;
export const MAX_OUTPUT_CHUNKS = 256 as const;
export const MAX_PROGRESS_EVENTS = 256 as const;
export const MAX_TOOL_ID_BYTES = 128 as const;
export const MAX_ENTRYPOINT_ID_BYTES = 128 as const;
export const MAX_HANDLE_ID_BYTES = 256 as const;
export const MAX_PRESET_ID_BYTES = 128 as const;
export const MAX_INLINE_PAYLOAD_BYTES = 262144 as const;
export const MAX_STREAM_OUTPUT_CHUNKS = 65536 as const;
export const MAX_STREAM_CHUNK_BYTES = 4096 as const;
export const MAX_MODEL_ID_BYTES = 128 as const;
export const MAX_MODEL_REVISION_BYTES = 64 as const;
export const MAX_MODEL_ARTIFACT_BYTES = 2147483648 as const;
export const MAX_EMBEDDING_DIMENSIONS = 4096 as const;
export const MAX_EMBEDDING_VALUE_BYTES = 16384 as const;
export const MAX_PYTHON_LIBRARY_ID_BYTES = 128 as const;
export const MAX_PYTHON_LIBRARY_VERSION_BYTES = 64 as const;
export const MAX_PYTHON_LIBRARY_BYTES = 134217728 as const;

export enum ToolRuntimeKind {
  Python = 0,
  LocalModel = 1,
  Media = 2,
  Wasm = 3,
}

export enum ToolOperation {
  RunBundledPythonModule = 0,
  GenerateLocalModel = 1,
  ProbeMedia = 2,
  ExtractAudio = 3,
  SampleFrames = 4,
  TranscodePreset = 5,
  RunSignedWasmTransform = 6,
  EmbedLocalModel = 7,
}

export enum ToolModelArtifactKind {
  LitertTflite = 0,
  OnnxRuntime = 1,
  Gguf = 2,
}

export enum ToolInputTransport {
  Inline = 0,
  SharedMemory = 1,
}

export enum ToolOutputTransport {
  InlineChunks = 0,
  StreamChunks = 1,
  DataPipe = 2,
}

export enum EmbeddingElementKind {
  Float32Le = 0,
}

export enum ToolChunkKind {
  TextUtf8 = 0,
  Binary = 1,
}

export enum LocalModelFinishReason {
  Stop = 0,
  TokenLimit = 1,
}

export enum ToolTerminalStatus {
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

export enum ToolAdmissionStatus {
  Accepted = 0,
  Unsupported = 1,
  InvalidJob = 2,
  Backpressure = 3,
  DeadlineExceeded = 4,
  ModelArtifactMissing = 5,
  ModelArtifactIncompatible = 6,
  LocalRuntimeUnavailable = 7,
}

export interface OperationEnvelope {
  readonly operation_id: string;
  readonly service_generation: bigint;
  readonly task_revision: bigint;
  readonly deadline_monotonic_ms: bigint;
  readonly idempotency_key: string;
}

export interface ResourceBudget {
  readonly max_input_bytes: bigint;
  readonly max_output_bytes: bigint;
  readonly max_memory_bytes: bigint;
  readonly max_cpu_ms: bigint;
  readonly max_temporary_bytes: bigint;
  readonly max_output_chunks: number;
}

export interface ToolModelArtifact {
  readonly model_id: string;
  readonly model_revision: string;
  readonly kind: ToolModelArtifactKind;
  readonly artifact_bytes: bigint;
  readonly artifact_digest: Uint8Array;
  readonly adapter_id: string;
  readonly adapter_bytes: bigint;
  readonly adapter_digest: Uint8Array;
}

export interface ToolPythonLibrary {
  readonly library_id: string;
  readonly library_version: string;
  readonly archive_bytes: bigint;
  readonly archive_digest: Uint8Array;
}

export interface ToolPayloadInput {
  readonly transport: ToolInputTransport;
  readonly byte_length: bigint;
  readonly digest: Uint8Array;
}

export interface ToolPayloadOutput {
  readonly transport: ToolOutputTransport;
  readonly max_byte_length: bigint;
}

export interface BundledPythonArguments {
  readonly entrypoint_id: string;
  readonly input: Uint8Array;
}

export interface LocalModelArguments {
  readonly conversation_id: string;
  readonly prompt_utf8: Uint8Array;
  readonly max_output_tokens: number;
  readonly model: ToolModelArtifact;
}

export interface LocalEmbeddingArguments {
  readonly model: ToolModelArtifact;
  readonly input_utf8: Uint8Array;
  readonly max_input_tokens: number;
  readonly normalize: boolean;
}

export interface MediaProbeArguments {
  readonly input_handle: string;
}

export interface AudioExtractArguments {
  readonly input_handle: string;
  readonly output_handle: string;
  readonly preset_id: string;
}

export interface FrameSampleArguments {
  readonly input_handle: string;
  readonly output_handle: string;
  readonly preset_id: string;
  readonly max_frames: number;
}

export interface TranscodeArguments {
  readonly input_handle: string;
  readonly output_handle: string;
  readonly preset_id: string;
}

export interface SignedWasmArguments {
  readonly entrypoint_id: string;
  readonly input: Uint8Array;
}

export interface ToolJob {
  readonly operation: OperationEnvelope;
  readonly job_id: string;
  readonly runtime: ToolRuntimeKind;
  readonly tool_id: string;
  readonly tool_version: string;
  readonly operation_kind: ToolOperation;
  readonly budget: ResourceBudget;
  readonly bundled_python: BundledPythonArguments | null;
  readonly local_model: LocalModelArguments | null;
  readonly media_probe: MediaProbeArguments | null;
  readonly audio_extract: AudioExtractArguments | null;
  readonly frame_sample: FrameSampleArguments | null;
  readonly transcode: TranscodeArguments | null;
  readonly signed_wasm: SignedWasmArguments | null;
  readonly input_payload: ToolPayloadInput;
  readonly output_payload: ToolPayloadOutput;
  readonly local_embedding: LocalEmbeddingArguments | null;
  readonly python_library: ToolPythonLibrary;
}

export function isValidToolJob(value: ToolJob): boolean {
  const bodyCount = [
    value.bundled_python !== null,
    value.local_model !== null,
    value.media_probe !== null,
    value.audio_extract !== null,
    value.frame_sample !== null,
    value.transcode !== null,
    value.signed_wasm !== null,
    value.local_embedding !== null,
  ].filter(Boolean).length;
  if (bodyCount !== 1) return false;
  switch (value.operation_kind) {
    case ToolOperation.RunBundledPythonModule:
      if (value.bundled_python === null) return false;
      break;
    case ToolOperation.GenerateLocalModel:
      if (value.local_model === null) return false;
      break;
    case ToolOperation.ProbeMedia:
      if (value.media_probe === null) return false;
      break;
    case ToolOperation.ExtractAudio:
      if (value.audio_extract === null) return false;
      break;
    case ToolOperation.SampleFrames:
      if (value.frame_sample === null) return false;
      break;
    case ToolOperation.TranscodePreset:
      if (value.transcode === null) return false;
      break;
    case ToolOperation.RunSignedWasmTransform:
      if (value.signed_wasm === null) return false;
      break;
    case ToolOperation.EmbedLocalModel:
      if (value.local_embedding === null) return false;
      break;
  }
  switch (value.runtime) {
    case ToolRuntimeKind.Python:
      return value.operation_kind === ToolOperation.RunBundledPythonModule;
    case ToolRuntimeKind.LocalModel:
      return value.operation_kind === ToolOperation.GenerateLocalModel || value.operation_kind === ToolOperation.EmbedLocalModel;
    case ToolRuntimeKind.Media:
      return value.operation_kind === ToolOperation.ProbeMedia || value.operation_kind === ToolOperation.ExtractAudio || value.operation_kind === ToolOperation.SampleFrames || value.operation_kind === ToolOperation.TranscodePreset;
    case ToolRuntimeKind.Wasm:
      return value.operation_kind === ToolOperation.RunSignedWasmTransform;
  }
}

export interface ToolProgress {
  readonly job_id: string;
  readonly sequence: number;
  readonly progress_basis_points: number;
}

export interface TextOutputChunk {
  readonly utf8: Uint8Array;
}

export interface BinaryOutputChunk {
  readonly data: Uint8Array;
}

export interface ToolOutputChunk {
  readonly operation: OperationEnvelope;
  readonly job_id: string;
  readonly sequence: number;
  readonly kind: ToolChunkKind;
  readonly text: TextOutputChunk | null;
  readonly binary: BinaryOutputChunk | null;
  readonly is_final: boolean;
}

export function isValidToolOutputChunk(value: ToolOutputChunk): boolean {
  const bodyCount = [
    value.text !== null,
    value.binary !== null,
  ].filter(Boolean).length;
  if (bodyCount !== 1) return false;
  switch (value.kind) {
    case ToolChunkKind.TextUtf8:
      return value.text !== null;
    case ToolChunkKind.Binary:
      return value.binary !== null;
  }
}

export interface ToolAdmission {
  readonly job_id: string;
  readonly status: ToolAdmissionStatus;
}

export interface BundledPythonResult {
  readonly output: Uint8Array;
}

export interface LocalModelResult {
  readonly finish_reason: LocalModelFinishReason;
  readonly input_tokens: number;
  readonly output_tokens: number;
}

export interface LocalEmbeddingResult {
  readonly dimensions: number;
  readonly element_kind: EmbeddingElementKind;
  readonly values: Uint8Array;
  readonly input_tokens: number;
}

export interface MediaProbeResult {
  readonly duration_ms: bigint;
  readonly audio_streams: number;
  readonly video_streams: number;
  readonly width: number;
  readonly height: number;
}

export interface AudioExtractResult {
  readonly output_handle: string;
  readonly output_bytes: bigint;
  readonly output_digest: Uint8Array;
}

export interface FrameSampleResult {
  readonly output_handle: string;
  readonly frame_count: number;
  readonly output_bytes: bigint;
  readonly output_digest: Uint8Array;
}

export interface TranscodeResult {
  readonly output_handle: string;
  readonly output_bytes: bigint;
  readonly output_digest: Uint8Array;
}

export interface SignedWasmResult {
  readonly output: Uint8Array;
}

export interface ToolSuccess {
  readonly operation_kind: ToolOperation;
  readonly bundled_python: BundledPythonResult | null;
  readonly local_model: LocalModelResult | null;
  readonly media_probe: MediaProbeResult | null;
  readonly audio_extract: AudioExtractResult | null;
  readonly frame_sample: FrameSampleResult | null;
  readonly transcode: TranscodeResult | null;
  readonly signed_wasm: SignedWasmResult | null;
  readonly local_embedding: LocalEmbeddingResult | null;
}

export function isValidToolSuccess(value: ToolSuccess): boolean {
  const bodyCount = [
    value.bundled_python !== null,
    value.local_model !== null,
    value.media_probe !== null,
    value.audio_extract !== null,
    value.frame_sample !== null,
    value.transcode !== null,
    value.signed_wasm !== null,
    value.local_embedding !== null,
  ].filter(Boolean).length;
  if (bodyCount !== 1) return false;
  switch (value.operation_kind) {
    case ToolOperation.RunBundledPythonModule:
      return value.bundled_python !== null;
    case ToolOperation.GenerateLocalModel:
      return value.local_model !== null;
    case ToolOperation.ProbeMedia:
      return value.media_probe !== null;
    case ToolOperation.ExtractAudio:
      return value.audio_extract !== null;
    case ToolOperation.SampleFrames:
      return value.frame_sample !== null;
    case ToolOperation.TranscodePreset:
      return value.transcode !== null;
    case ToolOperation.RunSignedWasmTransform:
      return value.signed_wasm !== null;
    case ToolOperation.EmbedLocalModel:
      return value.local_embedding !== null;
  }
}

export interface ToolCompletion {
  readonly operation: OperationEnvelope;
  readonly job_id: string;
  readonly status: ToolTerminalStatus;
  readonly success: ToolSuccess | null;
}

export function isValidToolCompletionPresence(value: ToolCompletion): boolean {
  return ((value.status === ToolTerminalStatus.Completed) ? value.success !== null : value.success === null);
}
