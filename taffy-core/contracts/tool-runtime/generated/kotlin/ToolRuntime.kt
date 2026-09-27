// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Generated from schema/contract.json. Do not edit.
// Contract tool_runtime 2.4.

package taffy.tool_runtime

const val MAX_JOB_INPUT_BYTES: Int = 16777216
const val MAX_JOB_OUTPUT_BYTES: Int = 16777216
const val MAX_JOB_ID_BYTES: Int = 128
const val MAX_OPERATION_ID_BYTES: Int = 128
const val MAX_IDEMPOTENCY_KEY_BYTES: Int = 128
const val MAX_TOOL_VERSION_BYTES: Int = 64
const val MAX_CONVERSATION_ID_BYTES: Int = 128
const val MAX_JOB_MEMORY_BYTES: Int = 4294967296
const val MAX_JOB_CPU_MS: Int = 600000
const val MAX_JOB_TEMPORARY_BYTES: Int = 1073741824
const val MAX_JOB_WALL_TIME_MS: Int = 600000
const val MAX_OUTPUT_CHUNK_BYTES: Int = 65536
const val MAX_OUTPUT_CHUNKS: Int = 256
const val MAX_PROGRESS_EVENTS: Int = 256
const val MAX_TOOL_ID_BYTES: Int = 128
const val MAX_ENTRYPOINT_ID_BYTES: Int = 128
const val MAX_HANDLE_ID_BYTES: Int = 256
const val MAX_PRESET_ID_BYTES: Int = 128
const val MAX_INLINE_PAYLOAD_BYTES: Int = 262144
const val MAX_STREAM_OUTPUT_CHUNKS: Int = 65536
const val MAX_STREAM_CHUNK_BYTES: Int = 4096
const val MAX_MODEL_ID_BYTES: Int = 128
const val MAX_MODEL_REVISION_BYTES: Int = 64
const val MAX_MODEL_ARTIFACT_BYTES: Int = 2147483648
const val MAX_EMBEDDING_DIMENSIONS: Int = 4096
const val MAX_EMBEDDING_VALUE_BYTES: Int = 16384
const val MAX_PYTHON_LIBRARY_ID_BYTES: Int = 128
const val MAX_PYTHON_LIBRARY_VERSION_BYTES: Int = 64
const val MAX_PYTHON_LIBRARY_BYTES: Int = 134217728

enum class ToolRuntimeKind(val wire: UInt) {
    PYTHON(0u),
    LOCAL_MODEL(1u),
    MEDIA(2u),
    WASM(3u);

    companion object {
        fun fromWire(value: UInt): ToolRuntimeKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class ToolOperation(val wire: UInt) {
    RUN_BUNDLED_PYTHON_MODULE(0u),
    GENERATE_LOCAL_MODEL(1u),
    PROBE_MEDIA(2u),
    EXTRACT_AUDIO(3u),
    SAMPLE_FRAMES(4u),
    TRANSCODE_PRESET(5u),
    RUN_SIGNED_WASM_TRANSFORM(6u),
    EMBED_LOCAL_MODEL(7u);

    companion object {
        fun fromWire(value: UInt): ToolOperation? = entries.firstOrNull { it.wire == value }
    }
}

enum class ToolModelArtifactKind(val wire: UInt) {
    LITERT_TFLITE(0u),
    ONNX_RUNTIME(1u),
    GGUF(2u);

    companion object {
        fun fromWire(value: UInt): ToolModelArtifactKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class ToolInputTransport(val wire: UInt) {
    INLINE(0u),
    SHARED_MEMORY(1u);

    companion object {
        fun fromWire(value: UInt): ToolInputTransport? = entries.firstOrNull { it.wire == value }
    }
}

enum class ToolOutputTransport(val wire: UInt) {
    INLINE_CHUNKS(0u),
    STREAM_CHUNKS(1u),
    DATA_PIPE(2u);

    companion object {
        fun fromWire(value: UInt): ToolOutputTransport? = entries.firstOrNull { it.wire == value }
    }
}

enum class EmbeddingElementKind(val wire: UInt) {
    FLOAT32_LE(0u);

    companion object {
        fun fromWire(value: UInt): EmbeddingElementKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class ToolChunkKind(val wire: UInt) {
    TEXT_UTF8(0u),
    BINARY(1u);

    companion object {
        fun fromWire(value: UInt): ToolChunkKind? = entries.firstOrNull { it.wire == value }
    }
}

enum class LocalModelFinishReason(val wire: UInt) {
    STOP(0u),
    TOKEN_LIMIT(1u);

    companion object {
        fun fromWire(value: UInt): LocalModelFinishReason? = entries.firstOrNull { it.wire == value }
    }
}

enum class ToolTerminalStatus(val wire: UInt) {
    COMPLETED(0u),
    CANCELLED(1u),
    DEADLINE_EXCEEDED(2u),
    RESOURCE_LIMIT(3u),
    RUNTIME_CRASHED(4u),
    INVALID_INPUT(5u),
    UNSUPPORTED(6u),
    OUTCOME_UNKNOWN(7u),
    MODEL_ARTIFACT_MISSING(8u),
    MODEL_ARTIFACT_INCOMPATIBLE(9u),
    LOCAL_RUNTIME_UNAVAILABLE(10u);

    companion object {
        fun fromWire(value: UInt): ToolTerminalStatus? = entries.firstOrNull { it.wire == value }
    }
}

enum class ToolAdmissionStatus(val wire: UInt) {
    ACCEPTED(0u),
    UNSUPPORTED(1u),
    INVALID_JOB(2u),
    BACKPRESSURE(3u),
    DEADLINE_EXCEEDED(4u),
    MODEL_ARTIFACT_MISSING(5u),
    MODEL_ARTIFACT_INCOMPATIBLE(6u),
    LOCAL_RUNTIME_UNAVAILABLE(7u);

    companion object {
        fun fromWire(value: UInt): ToolAdmissionStatus? = entries.firstOrNull { it.wire == value }
    }
}

data class OperationEnvelope(
    val operation_id: String,
    val service_generation: ULong,
    val task_revision: ULong,
    val deadline_monotonic_ms: ULong,
    val idempotency_key: String
)

data class ResourceBudget(
    val max_input_bytes: ULong,
    val max_output_bytes: ULong,
    val max_memory_bytes: ULong,
    val max_cpu_ms: ULong,
    val max_temporary_bytes: ULong,
    val max_output_chunks: UInt
)

data class ToolModelArtifact(
    val model_id: String,
    val model_revision: String,
    val kind: ToolModelArtifactKind,
    val artifact_bytes: ULong,
    val artifact_digest: ByteArray,
    val adapter_id: String,
    val adapter_bytes: ULong,
    val adapter_digest: ByteArray
)

data class ToolPythonLibrary(
    val library_id: String,
    val library_version: String,
    val archive_bytes: ULong,
    val archive_digest: ByteArray
)

data class ToolPayloadInput(
    val transport: ToolInputTransport,
    val byte_length: ULong,
    val digest: ByteArray
)

data class ToolPayloadOutput(
    val transport: ToolOutputTransport,
    val max_byte_length: ULong
)

data class BundledPythonArguments(
    val entrypoint_id: String,
    val input: ByteArray
)

data class LocalModelArguments(
    val conversation_id: String,
    val prompt_utf8: ByteArray,
    val max_output_tokens: UInt,
    val model: ToolModelArtifact
)

data class LocalEmbeddingArguments(
    val model: ToolModelArtifact,
    val input_utf8: ByteArray,
    val max_input_tokens: UInt,
    val normalize: Boolean
)

data class MediaProbeArguments(
    val input_handle: String
)

data class AudioExtractArguments(
    val input_handle: String,
    val output_handle: String,
    val preset_id: String
)

data class FrameSampleArguments(
    val input_handle: String,
    val output_handle: String,
    val preset_id: String,
    val max_frames: UInt
)

data class TranscodeArguments(
    val input_handle: String,
    val output_handle: String,
    val preset_id: String
)

data class SignedWasmArguments(
    val entrypoint_id: String,
    val input: ByteArray
)

data class ToolJob(
    val operation: OperationEnvelope,
    val job_id: String,
    val runtime: ToolRuntimeKind,
    val tool_id: String,
    val tool_version: String,
    val operation_kind: ToolOperation,
    val budget: ResourceBudget,
    val bundled_python: BundledPythonArguments?,
    val local_model: LocalModelArguments?,
    val media_probe: MediaProbeArguments?,
    val audio_extract: AudioExtractArguments?,
    val frame_sample: FrameSampleArguments?,
    val transcode: TranscodeArguments?,
    val signed_wasm: SignedWasmArguments?,
    val input_payload: ToolPayloadInput,
    val output_payload: ToolPayloadOutput,
    val local_embedding: LocalEmbeddingArguments?,
    val python_library: ToolPythonLibrary
) {
    fun hasValidBody(): Boolean {
        val bodyCount = listOf(bundled_python, local_model, media_probe, audio_extract, frame_sample, transcode, signed_wasm, local_embedding).count { it != null }
        if (bodyCount != 1) return false
        val bodyMatches = when (operation_kind) {
            ToolOperation.RUN_BUNDLED_PYTHON_MODULE -> bundled_python != null
            ToolOperation.GENERATE_LOCAL_MODEL -> local_model != null
            ToolOperation.PROBE_MEDIA -> media_probe != null
            ToolOperation.EXTRACT_AUDIO -> audio_extract != null
            ToolOperation.SAMPLE_FRAMES -> frame_sample != null
            ToolOperation.TRANSCODE_PRESET -> transcode != null
            ToolOperation.RUN_SIGNED_WASM_TRANSFORM -> signed_wasm != null
            ToolOperation.EMBED_LOCAL_MODEL -> local_embedding != null
        }
        if (!bodyMatches) return false
        return when (runtime) {
            ToolRuntimeKind.PYTHON -> operation_kind == ToolOperation.RUN_BUNDLED_PYTHON_MODULE
            ToolRuntimeKind.LOCAL_MODEL -> operation_kind == ToolOperation.GENERATE_LOCAL_MODEL || operation_kind == ToolOperation.EMBED_LOCAL_MODEL
            ToolRuntimeKind.MEDIA -> operation_kind == ToolOperation.PROBE_MEDIA || operation_kind == ToolOperation.EXTRACT_AUDIO || operation_kind == ToolOperation.SAMPLE_FRAMES || operation_kind == ToolOperation.TRANSCODE_PRESET
            ToolRuntimeKind.WASM -> operation_kind == ToolOperation.RUN_SIGNED_WASM_TRANSFORM
        }
    }

    companion object {
        fun bundledPython(operation: OperationEnvelope, job_id: String, runtime: ToolRuntimeKind, tool_id: String, tool_version: String, budget: ResourceBudget, input_payload: ToolPayloadInput, output_payload: ToolPayloadOutput, python_library: ToolPythonLibrary, body: BundledPythonArguments): ToolJob =
            ToolJob(
                operation = operation,
                job_id = job_id,
                runtime = runtime,
                tool_id = tool_id,
                tool_version = tool_version,
                operation_kind = ToolOperation.RUN_BUNDLED_PYTHON_MODULE,
                budget = budget,
                bundled_python = body,
                local_model = null,
                media_probe = null,
                audio_extract = null,
                frame_sample = null,
                transcode = null,
                signed_wasm = null,
                input_payload = input_payload,
                output_payload = output_payload,
                local_embedding = null,
                python_library = python_library,
            )

        fun localModel(operation: OperationEnvelope, job_id: String, runtime: ToolRuntimeKind, tool_id: String, tool_version: String, budget: ResourceBudget, input_payload: ToolPayloadInput, output_payload: ToolPayloadOutput, python_library: ToolPythonLibrary, body: LocalModelArguments): ToolJob =
            ToolJob(
                operation = operation,
                job_id = job_id,
                runtime = runtime,
                tool_id = tool_id,
                tool_version = tool_version,
                operation_kind = ToolOperation.GENERATE_LOCAL_MODEL,
                budget = budget,
                bundled_python = null,
                local_model = body,
                media_probe = null,
                audio_extract = null,
                frame_sample = null,
                transcode = null,
                signed_wasm = null,
                input_payload = input_payload,
                output_payload = output_payload,
                local_embedding = null,
                python_library = python_library,
            )

        fun mediaProbe(operation: OperationEnvelope, job_id: String, runtime: ToolRuntimeKind, tool_id: String, tool_version: String, budget: ResourceBudget, input_payload: ToolPayloadInput, output_payload: ToolPayloadOutput, python_library: ToolPythonLibrary, body: MediaProbeArguments): ToolJob =
            ToolJob(
                operation = operation,
                job_id = job_id,
                runtime = runtime,
                tool_id = tool_id,
                tool_version = tool_version,
                operation_kind = ToolOperation.PROBE_MEDIA,
                budget = budget,
                bundled_python = null,
                local_model = null,
                media_probe = body,
                audio_extract = null,
                frame_sample = null,
                transcode = null,
                signed_wasm = null,
                input_payload = input_payload,
                output_payload = output_payload,
                local_embedding = null,
                python_library = python_library,
            )

        fun audioExtract(operation: OperationEnvelope, job_id: String, runtime: ToolRuntimeKind, tool_id: String, tool_version: String, budget: ResourceBudget, input_payload: ToolPayloadInput, output_payload: ToolPayloadOutput, python_library: ToolPythonLibrary, body: AudioExtractArguments): ToolJob =
            ToolJob(
                operation = operation,
                job_id = job_id,
                runtime = runtime,
                tool_id = tool_id,
                tool_version = tool_version,
                operation_kind = ToolOperation.EXTRACT_AUDIO,
                budget = budget,
                bundled_python = null,
                local_model = null,
                media_probe = null,
                audio_extract = body,
                frame_sample = null,
                transcode = null,
                signed_wasm = null,
                input_payload = input_payload,
                output_payload = output_payload,
                local_embedding = null,
                python_library = python_library,
            )

        fun frameSample(operation: OperationEnvelope, job_id: String, runtime: ToolRuntimeKind, tool_id: String, tool_version: String, budget: ResourceBudget, input_payload: ToolPayloadInput, output_payload: ToolPayloadOutput, python_library: ToolPythonLibrary, body: FrameSampleArguments): ToolJob =
            ToolJob(
                operation = operation,
                job_id = job_id,
                runtime = runtime,
                tool_id = tool_id,
                tool_version = tool_version,
                operation_kind = ToolOperation.SAMPLE_FRAMES,
                budget = budget,
                bundled_python = null,
                local_model = null,
                media_probe = null,
                audio_extract = null,
                frame_sample = body,
                transcode = null,
                signed_wasm = null,
                input_payload = input_payload,
                output_payload = output_payload,
                local_embedding = null,
                python_library = python_library,
            )

        fun transcode(operation: OperationEnvelope, job_id: String, runtime: ToolRuntimeKind, tool_id: String, tool_version: String, budget: ResourceBudget, input_payload: ToolPayloadInput, output_payload: ToolPayloadOutput, python_library: ToolPythonLibrary, body: TranscodeArguments): ToolJob =
            ToolJob(
                operation = operation,
                job_id = job_id,
                runtime = runtime,
                tool_id = tool_id,
                tool_version = tool_version,
                operation_kind = ToolOperation.TRANSCODE_PRESET,
                budget = budget,
                bundled_python = null,
                local_model = null,
                media_probe = null,
                audio_extract = null,
                frame_sample = null,
                transcode = body,
                signed_wasm = null,
                input_payload = input_payload,
                output_payload = output_payload,
                local_embedding = null,
                python_library = python_library,
            )

        fun signedWasm(operation: OperationEnvelope, job_id: String, runtime: ToolRuntimeKind, tool_id: String, tool_version: String, budget: ResourceBudget, input_payload: ToolPayloadInput, output_payload: ToolPayloadOutput, python_library: ToolPythonLibrary, body: SignedWasmArguments): ToolJob =
            ToolJob(
                operation = operation,
                job_id = job_id,
                runtime = runtime,
                tool_id = tool_id,
                tool_version = tool_version,
                operation_kind = ToolOperation.RUN_SIGNED_WASM_TRANSFORM,
                budget = budget,
                bundled_python = null,
                local_model = null,
                media_probe = null,
                audio_extract = null,
                frame_sample = null,
                transcode = null,
                signed_wasm = body,
                input_payload = input_payload,
                output_payload = output_payload,
                local_embedding = null,
                python_library = python_library,
            )

        fun localEmbedding(operation: OperationEnvelope, job_id: String, runtime: ToolRuntimeKind, tool_id: String, tool_version: String, budget: ResourceBudget, input_payload: ToolPayloadInput, output_payload: ToolPayloadOutput, python_library: ToolPythonLibrary, body: LocalEmbeddingArguments): ToolJob =
            ToolJob(
                operation = operation,
                job_id = job_id,
                runtime = runtime,
                tool_id = tool_id,
                tool_version = tool_version,
                operation_kind = ToolOperation.EMBED_LOCAL_MODEL,
                budget = budget,
                bundled_python = null,
                local_model = null,
                media_probe = null,
                audio_extract = null,
                frame_sample = null,
                transcode = null,
                signed_wasm = null,
                input_payload = input_payload,
                output_payload = output_payload,
                local_embedding = body,
                python_library = python_library,
            )

    }
}

data class ToolProgress(
    val job_id: String,
    val sequence: UInt,
    val progress_basis_points: UInt
)

data class TextOutputChunk(
    val utf8: ByteArray
)

data class BinaryOutputChunk(
    val data: ByteArray
)

data class ToolOutputChunk(
    val operation: OperationEnvelope,
    val job_id: String,
    val sequence: UInt,
    val kind: ToolChunkKind,
    val text: TextOutputChunk?,
    val binary: BinaryOutputChunk?,
    val is_final: Boolean
) {
    fun hasValidBody(): Boolean {
        val bodyCount = listOf(text, binary).count { it != null }
        if (bodyCount != 1) return false
        return when (kind) {
            ToolChunkKind.TEXT_UTF8 -> text != null
            ToolChunkKind.BINARY -> binary != null
        }
    }

    companion object {
        fun text(operation: OperationEnvelope, job_id: String, sequence: UInt, is_final: Boolean, body: TextOutputChunk): ToolOutputChunk =
            ToolOutputChunk(
                operation = operation,
                job_id = job_id,
                sequence = sequence,
                kind = ToolChunkKind.TEXT_UTF8,
                text = body,
                binary = null,
                is_final = is_final,
            )

        fun binary(operation: OperationEnvelope, job_id: String, sequence: UInt, is_final: Boolean, body: BinaryOutputChunk): ToolOutputChunk =
            ToolOutputChunk(
                operation = operation,
                job_id = job_id,
                sequence = sequence,
                kind = ToolChunkKind.BINARY,
                text = null,
                binary = body,
                is_final = is_final,
            )

    }
}

data class ToolAdmission(
    val job_id: String,
    val status: ToolAdmissionStatus
)

data class BundledPythonResult(
    val output: ByteArray
)

data class LocalModelResult(
    val finish_reason: LocalModelFinishReason,
    val input_tokens: UInt,
    val output_tokens: UInt
)

data class LocalEmbeddingResult(
    val dimensions: UInt,
    val element_kind: EmbeddingElementKind,
    val values: ByteArray,
    val input_tokens: UInt
)

data class MediaProbeResult(
    val duration_ms: ULong,
    val audio_streams: UInt,
    val video_streams: UInt,
    val width: UInt,
    val height: UInt
)

data class AudioExtractResult(
    val output_handle: String,
    val output_bytes: ULong,
    val output_digest: ByteArray
)

data class FrameSampleResult(
    val output_handle: String,
    val frame_count: UInt,
    val output_bytes: ULong,
    val output_digest: ByteArray
)

data class TranscodeResult(
    val output_handle: String,
    val output_bytes: ULong,
    val output_digest: ByteArray
)

data class SignedWasmResult(
    val output: ByteArray
)

data class ToolSuccess(
    val operation_kind: ToolOperation,
    val bundled_python: BundledPythonResult?,
    val local_model: LocalModelResult?,
    val media_probe: MediaProbeResult?,
    val audio_extract: AudioExtractResult?,
    val frame_sample: FrameSampleResult?,
    val transcode: TranscodeResult?,
    val signed_wasm: SignedWasmResult?,
    val local_embedding: LocalEmbeddingResult?
) {
    fun hasValidBody(): Boolean {
        val bodyCount = listOf(bundled_python, local_model, media_probe, audio_extract, frame_sample, transcode, signed_wasm, local_embedding).count { it != null }
        if (bodyCount != 1) return false
        return when (operation_kind) {
            ToolOperation.RUN_BUNDLED_PYTHON_MODULE -> bundled_python != null
            ToolOperation.GENERATE_LOCAL_MODEL -> local_model != null
            ToolOperation.PROBE_MEDIA -> media_probe != null
            ToolOperation.EXTRACT_AUDIO -> audio_extract != null
            ToolOperation.SAMPLE_FRAMES -> frame_sample != null
            ToolOperation.TRANSCODE_PRESET -> transcode != null
            ToolOperation.RUN_SIGNED_WASM_TRANSFORM -> signed_wasm != null
            ToolOperation.EMBED_LOCAL_MODEL -> local_embedding != null
        }
    }

    companion object {
        fun bundledPython(body: BundledPythonResult): ToolSuccess =
            ToolSuccess(
                operation_kind = ToolOperation.RUN_BUNDLED_PYTHON_MODULE,
                bundled_python = body,
                local_model = null,
                media_probe = null,
                audio_extract = null,
                frame_sample = null,
                transcode = null,
                signed_wasm = null,
                local_embedding = null,
            )

        fun localModel(body: LocalModelResult): ToolSuccess =
            ToolSuccess(
                operation_kind = ToolOperation.GENERATE_LOCAL_MODEL,
                bundled_python = null,
                local_model = body,
                media_probe = null,
                audio_extract = null,
                frame_sample = null,
                transcode = null,
                signed_wasm = null,
                local_embedding = null,
            )

        fun mediaProbe(body: MediaProbeResult): ToolSuccess =
            ToolSuccess(
                operation_kind = ToolOperation.PROBE_MEDIA,
                bundled_python = null,
                local_model = null,
                media_probe = body,
                audio_extract = null,
                frame_sample = null,
                transcode = null,
                signed_wasm = null,
                local_embedding = null,
            )

        fun audioExtract(body: AudioExtractResult): ToolSuccess =
            ToolSuccess(
                operation_kind = ToolOperation.EXTRACT_AUDIO,
                bundled_python = null,
                local_model = null,
                media_probe = null,
                audio_extract = body,
                frame_sample = null,
                transcode = null,
                signed_wasm = null,
                local_embedding = null,
            )

        fun frameSample(body: FrameSampleResult): ToolSuccess =
            ToolSuccess(
                operation_kind = ToolOperation.SAMPLE_FRAMES,
                bundled_python = null,
                local_model = null,
                media_probe = null,
                audio_extract = null,
                frame_sample = body,
                transcode = null,
                signed_wasm = null,
                local_embedding = null,
            )

        fun transcode(body: TranscodeResult): ToolSuccess =
            ToolSuccess(
                operation_kind = ToolOperation.TRANSCODE_PRESET,
                bundled_python = null,
                local_model = null,
                media_probe = null,
                audio_extract = null,
                frame_sample = null,
                transcode = body,
                signed_wasm = null,
                local_embedding = null,
            )

        fun signedWasm(body: SignedWasmResult): ToolSuccess =
            ToolSuccess(
                operation_kind = ToolOperation.RUN_SIGNED_WASM_TRANSFORM,
                bundled_python = null,
                local_model = null,
                media_probe = null,
                audio_extract = null,
                frame_sample = null,
                transcode = null,
                signed_wasm = body,
                local_embedding = null,
            )

        fun localEmbedding(body: LocalEmbeddingResult): ToolSuccess =
            ToolSuccess(
                operation_kind = ToolOperation.EMBED_LOCAL_MODEL,
                bundled_python = null,
                local_model = null,
                media_probe = null,
                audio_extract = null,
                frame_sample = null,
                transcode = null,
                signed_wasm = null,
                local_embedding = body,
            )

    }
}

data class ToolCompletion(
    val operation: OperationEnvelope,
    val job_id: String,
    val status: ToolTerminalStatus,
    val success: ToolSuccess?
)

fun ToolCompletion.hasValidPresence(): Boolean =
    (if (status == ToolTerminalStatus.COMPLETED) success != null else success == null)
