// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Generated from schema/contract.json. Do not edit.
// Contract core_api 3.49 state payload.

package taffy.core_api

enum class CoreStatusPayloadCodecError {
    SIZE_LIMIT, COLLECTION_LIMIT, STRING_LIMIT, VALUE_LIMIT, LENGTH_OVERFLOW,
    TRUNCATED, INVALID_MAGIC, UNSUPPORTED_VERSION, INVALID_BOOLEAN,
    INVALID_ENUM, INVALID_UTF8, MALFORMED, TRAILING_BYTES,
}

sealed interface CoreStatusPayloadEncodeResult {
    data class Success(val bytes: ByteArray) : CoreStatusPayloadEncodeResult
    data class Failure(val error: CoreStatusPayloadCodecError) : CoreStatusPayloadEncodeResult
}

sealed interface CoreStatusPayloadDecodeResult {
    data class Success(val value: CoreStatus) : CoreStatusPayloadDecodeResult
    data class Failure(val error: CoreStatusPayloadCodecError) : CoreStatusPayloadDecodeResult
}

private val CORE_STATUS_PAYLOAD_MAGIC: ByteArray = byteArrayOf(84.toByte(), 65.toByte(), 70.toByte(), 70.toByte(), 89.toByte(), 83.toByte(), 84.toByte(), 65.toByte())
const val CORE_STATUS_PAYLOAD_SCHEMA_VERSION: UInt = 33u

private class CoreStatusCodecFailure(val reason: CoreStatusPayloadCodecError) : Exception()

private fun fail(reason: CoreStatusPayloadCodecError): Nothing = throw CoreStatusCodecFailure(reason)

private class CoreStatusPayloadEncoder {
    private val bytes = ByteArray(MAX_EVENT_PAYLOAD_BYTES)
    private var offset = 0

    fun finish(): ByteArray = bytes.copyOf(offset)

    fun putRaw(value: ByteArray) {
        if (value.size > bytes.size - offset) fail(CoreStatusPayloadCodecError.SIZE_LIMIT)
        value.copyInto(bytes, offset)
        offset += value.size
    }

    fun putBoolean(value: Boolean) = putByte(if (value) 1 else 0)

    private fun putByte(value: Int) {
        if (offset >= bytes.size) fail(CoreStatusPayloadCodecError.SIZE_LIMIT)
        bytes[offset++] = value.toByte()
    }

    fun putUInt(value: UInt) {
        repeat(4) { shift -> putByte((value shr (shift * 8)).toInt()) }
    }

    fun putBoundedUInt(value: UInt, limit: UInt) {
        if (value > limit) fail(CoreStatusPayloadCodecError.VALUE_LIMIT)
        putUInt(value)
    }

    fun putULong(value: ULong) {
        repeat(8) { shift -> putByte((value shr (shift * 8)).toInt()) }
    }

    fun putLength(value: Int, limit: Int) {
        if (value < 0 || value > limit) fail(CoreStatusPayloadCodecError.COLLECTION_LIMIT)
        putUInt(value.toUInt())
    }

    fun putString(value: String, limit: Int) {
        val encoded = value.encodeToByteArray()
        if (encoded.size > limit) fail(CoreStatusPayloadCodecError.STRING_LIMIT)
        putUInt(encoded.size.toUInt())
        putRaw(encoded)
    }
}

private class CoreStatusPayloadDecoder(private val bytes: ByteArray) {
    var offset: Int = 0
        private set

    fun take(length: Int): ByteArray {
        if (length < 0 || length > bytes.size - offset) fail(CoreStatusPayloadCodecError.TRUNCATED)
        val value = bytes.copyOfRange(offset, offset + length)
        offset += length
        return value
    }

    fun readBoolean(): Boolean = when (val value = readByte()) {
        0 -> false
        1 -> true
        else -> fail(CoreStatusPayloadCodecError.INVALID_BOOLEAN)
    }

    private fun readByte(): Int {
        if (offset >= bytes.size) fail(CoreStatusPayloadCodecError.TRUNCATED)
        return bytes[offset++].toUByte().toInt()
    }

    fun readUInt(): UInt {
        var value = 0u
        repeat(4) { shift -> value = value or (readByte().toUInt() shl (shift * 8)) }
        return value
    }

    fun readBoundedUInt(limit: UInt): UInt {
        val value = readUInt()
        if (value > limit) fail(CoreStatusPayloadCodecError.VALUE_LIMIT)
        return value
    }

    fun readULong(): ULong {
        var value = 0uL
        repeat(8) { shift -> value = value or (readByte().toULong() shl (shift * 8)) }
        return value
    }

    private fun readLength(limit: Int): Int {
        val value = readUInt()
        if (value > limit.toUInt()) fail(CoreStatusPayloadCodecError.COLLECTION_LIMIT)
        return value.toInt()
    }

    fun readString(limit: Int): String {
        val length = readUInt()
        if (length > limit.toUInt()) fail(CoreStatusPayloadCodecError.STRING_LIMIT)
        val byteLength = length.toInt()
        if (byteLength > bytes.size - offset) fail(CoreStatusPayloadCodecError.TRUNCATED)
        val start = offset
        val end = start + byteLength
        return try {
            val value = bytes.decodeToString(
                startIndex = start,
                endIndex = end,
                throwOnInvalidSequence = true,
            )
            offset = end
            value
        } catch (_: java.nio.charset.CharacterCodingException) {
            fail(CoreStatusPayloadCodecError.INVALID_UTF8)
        }
    }

    fun <T> readList(limit: Int, read: () -> T): List<T> {
        val length = readLength(limit)
        return buildList(length) { repeat(length) { add(read()) } }
    }
}

private fun encodeAssistantAbilityView(encoder: CoreStatusPayloadEncoder, value: AssistantAbilityView) =
    encoder.putUInt(value.wire)

private fun decodeAssistantAbilityView(decoder: CoreStatusPayloadDecoder): AssistantAbilityView =
    AssistantAbilityView.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeBuiltinSkillIdView(encoder: CoreStatusPayloadEncoder, value: BuiltinSkillIdView) =
    encoder.putUInt(value.wire)

private fun decodeBuiltinSkillIdView(decoder: CoreStatusPayloadDecoder): BuiltinSkillIdView =
    BuiltinSkillIdView.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeBuiltinSkillAvailabilityView(encoder: CoreStatusPayloadEncoder, value: BuiltinSkillAvailabilityView) =
    encoder.putUInt(value.wire)

private fun decodeBuiltinSkillAvailabilityView(decoder: CoreStatusPayloadDecoder): BuiltinSkillAvailabilityView =
    BuiltinSkillAvailabilityView.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeCoreStatusProjectionMode(encoder: CoreStatusPayloadEncoder, value: CoreStatusProjectionMode) =
    encoder.putUInt(value.wire)

private fun decodeCoreStatusProjectionMode(decoder: CoreStatusPayloadDecoder): CoreStatusProjectionMode =
    CoreStatusProjectionMode.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeCoreStatusProjectionFamily(encoder: CoreStatusPayloadEncoder, value: CoreStatusProjectionFamily) =
    encoder.putUInt(value.wire)

private fun decodeCoreStatusProjectionFamily(decoder: CoreStatusPayloadDecoder): CoreStatusProjectionFamily =
    CoreStatusProjectionFamily.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeSiteSkillArgumentKind(encoder: CoreStatusPayloadEncoder, value: SiteSkillArgumentKind) =
    encoder.putUInt(value.wire)

private fun decodeSiteSkillArgumentKind(decoder: CoreStatusPayloadDecoder): SiteSkillArgumentKind =
    SiteSkillArgumentKind.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeSiteSkillProvenanceView(encoder: CoreStatusPayloadEncoder, value: SiteSkillProvenanceView) =
    encoder.putUInt(value.wire)

private fun decodeSiteSkillProvenanceView(decoder: CoreStatusPayloadDecoder): SiteSkillProvenanceView =
    SiteSkillProvenanceView.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeSiteSkillStatusView(encoder: CoreStatusPayloadEncoder, value: SiteSkillStatusView) =
    encoder.putUInt(value.wire)

private fun decodeSiteSkillStatusView(decoder: CoreStatusPayloadDecoder): SiteSkillStatusView =
    SiteSkillStatusView.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodePersonalityPresetView(encoder: CoreStatusPayloadEncoder, value: PersonalityPresetView) =
    encoder.putUInt(value.wire)

private fun decodePersonalityPresetView(decoder: CoreStatusPayloadDecoder): PersonalityPresetView =
    PersonalityPresetView.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeTaskTemplateId(encoder: CoreStatusPayloadEncoder, value: TaskTemplateId) =
    encoder.putUInt(value.wire)

private fun decodeTaskTemplateId(decoder: CoreStatusPayloadDecoder): TaskTemplateId =
    TaskTemplateId.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeTaskProviderRoute(encoder: CoreStatusPayloadEncoder, value: TaskProviderRoute) =
    encoder.putUInt(value.wire)

private fun decodeTaskProviderRoute(decoder: CoreStatusPayloadDecoder): TaskProviderRoute =
    TaskProviderRoute.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeCoreAvailability(encoder: CoreStatusPayloadEncoder, value: CoreAvailability) =
    encoder.putUInt(value.wire)

private fun decodeCoreAvailability(decoder: CoreStatusPayloadDecoder): CoreAvailability =
    CoreAvailability.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeSavedDataAvailability(encoder: CoreStatusPayloadEncoder, value: SavedDataAvailability) =
    encoder.putUInt(value.wire)

private fun decodeSavedDataAvailability(decoder: CoreStatusPayloadDecoder): SavedDataAvailability =
    SavedDataAvailability.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeTaskPhase(encoder: CoreStatusPayloadEncoder, value: TaskPhase) =
    encoder.putUInt(value.wire)

private fun decodeTaskPhase(decoder: CoreStatusPayloadDecoder): TaskPhase =
    TaskPhase.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeTaskControlKind(encoder: CoreStatusPayloadEncoder, value: TaskControlKind) =
    encoder.putUInt(value.wire)

private fun decodeTaskControlKind(decoder: CoreStatusPayloadDecoder): TaskControlKind =
    TaskControlKind.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeCoreFailureCode(encoder: CoreStatusPayloadEncoder, value: CoreFailureCode) =
    encoder.putUInt(value.wire)

private fun decodeCoreFailureCode(decoder: CoreStatusPayloadDecoder): CoreFailureCode =
    CoreFailureCode.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeAuthProvider(encoder: CoreStatusPayloadEncoder, value: AuthProvider) =
    encoder.putUInt(value.wire)

private fun decodeAuthProvider(decoder: CoreStatusPayloadDecoder): AuthProvider =
    AuthProvider.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeAuthMethodAvailability(encoder: CoreStatusPayloadEncoder, value: AuthMethodAvailability) =
    encoder.putUInt(value.wire)

private fun decodeAuthMethodAvailability(decoder: CoreStatusPayloadDecoder): AuthMethodAvailability =
    AuthMethodAvailability.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeAuthPhase(encoder: CoreStatusPayloadEncoder, value: AuthPhase) =
    encoder.putUInt(value.wire)

private fun decodeAuthPhase(decoder: CoreStatusPayloadDecoder): AuthPhase =
    AuthPhase.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeAuthFailureCode(encoder: CoreStatusPayloadEncoder, value: AuthFailureCode) =
    encoder.putUInt(value.wire)

private fun decodeAuthFailureCode(decoder: CoreStatusPayloadDecoder): AuthFailureCode =
    AuthFailureCode.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeWorkspacePhase(encoder: CoreStatusPayloadEncoder, value: WorkspacePhase) =
    encoder.putUInt(value.wire)

private fun decodeWorkspacePhase(decoder: CoreStatusPayloadDecoder): WorkspacePhase =
    WorkspacePhase.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeWorkspaceFactKind(encoder: CoreStatusPayloadEncoder, value: WorkspaceFactKind) =
    encoder.putUInt(value.wire)

private fun decodeWorkspaceFactKind(decoder: CoreStatusPayloadDecoder): WorkspaceFactKind =
    WorkspaceFactKind.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeWorkspaceExportFormat(encoder: CoreStatusPayloadEncoder, value: WorkspaceExportFormat) =
    encoder.putUInt(value.wire)

private fun decodeWorkspaceExportFormat(decoder: CoreStatusPayloadDecoder): WorkspaceExportFormat =
    WorkspaceExportFormat.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeTaskActivityKind(encoder: CoreStatusPayloadEncoder, value: TaskActivityKind) =
    encoder.putUInt(value.wire)

private fun decodeTaskActivityKind(decoder: CoreStatusPayloadDecoder): TaskActivityKind =
    TaskActivityKind.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeTaskArtifactKind(encoder: CoreStatusPayloadEncoder, value: TaskArtifactKind) =
    encoder.putUInt(value.wire)

private fun decodeTaskArtifactKind(decoder: CoreStatusPayloadDecoder): TaskArtifactKind =
    TaskArtifactKind.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeLibraryAvailability(encoder: CoreStatusPayloadEncoder, value: LibraryAvailability) =
    encoder.putUInt(value.wire)

private fun decodeLibraryAvailability(decoder: CoreStatusPayloadDecoder): LibraryAvailability =
    LibraryAvailability.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeLibraryRefreshDisposition(encoder: CoreStatusPayloadEncoder, value: LibraryRefreshDisposition) =
    encoder.putUInt(value.wire)

private fun decodeLibraryRefreshDisposition(decoder: CoreStatusPayloadDecoder): LibraryRefreshDisposition =
    LibraryRefreshDisposition.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeMemoryAvailability(encoder: CoreStatusPayloadEncoder, value: MemoryAvailability) =
    encoder.putUInt(value.wire)

private fun decodeMemoryAvailability(decoder: CoreStatusPayloadDecoder): MemoryAvailability =
    MemoryAvailability.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeMemorySourceKind(encoder: CoreStatusPayloadEncoder, value: MemorySourceKind) =
    encoder.putUInt(value.wire)

private fun decodeMemorySourceKind(decoder: CoreStatusPayloadDecoder): MemorySourceKind =
    MemorySourceKind.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeMemoryScopeKind(encoder: CoreStatusPayloadEncoder, value: MemoryScopeKind) =
    encoder.putUInt(value.wire)

private fun decodeMemoryScopeKind(decoder: CoreStatusPayloadDecoder): MemoryScopeKind =
    MemoryScopeKind.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeMemorySensitivity(encoder: CoreStatusPayloadEncoder, value: MemorySensitivity) =
    encoder.putUInt(value.wire)

private fun decodeMemorySensitivity(decoder: CoreStatusPayloadDecoder): MemorySensitivity =
    MemorySensitivity.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeAssetKindView(encoder: CoreStatusPayloadEncoder, value: AssetKindView) =
    encoder.putUInt(value.wire)

private fun decodeAssetKindView(decoder: CoreStatusPayloadDecoder): AssetKindView =
    AssetKindView.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeAssetPresenceView(encoder: CoreStatusPayloadEncoder, value: AssetPresenceView) =
    encoder.putUInt(value.wire)

private fun decodeAssetPresenceView(decoder: CoreStatusPayloadDecoder): AssetPresenceView =
    AssetPresenceView.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeAssetNetworkCostView(encoder: CoreStatusPayloadEncoder, value: AssetNetworkCostView) =
    encoder.putUInt(value.wire)

private fun decodeAssetNetworkCostView(decoder: CoreStatusPayloadDecoder): AssetNetworkCostView =
    AssetNetworkCostView.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeAssetRefusalView(encoder: CoreStatusPayloadEncoder, value: AssetRefusalView) =
    encoder.putUInt(value.wire)

private fun decodeAssetRefusalView(decoder: CoreStatusPayloadDecoder): AssetRefusalView =
    AssetRefusalView.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeProviderAuthMethodView(encoder: CoreStatusPayloadEncoder, value: ProviderAuthMethodView) =
    encoder.putUInt(value.wire)

private fun decodeProviderAuthMethodView(decoder: CoreStatusPayloadDecoder): ProviderAuthMethodView =
    ProviderAuthMethodView.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeProviderProbeVerdictView(encoder: CoreStatusPayloadEncoder, value: ProviderProbeVerdictView) =
    encoder.putUInt(value.wire)

private fun decodeProviderProbeVerdictView(decoder: CoreStatusPayloadDecoder): ProviderProbeVerdictView =
    ProviderProbeVerdictView.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeProviderCredentialStateView(encoder: CoreStatusPayloadEncoder, value: ProviderCredentialStateView) =
    encoder.putUInt(value.wire)

private fun decodeProviderCredentialStateView(decoder: CoreStatusPayloadDecoder): ProviderCredentialStateView =
    ProviderCredentialStateView.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeProviderRefusalView(encoder: CoreStatusPayloadEncoder, value: ProviderRefusalView) =
    encoder.putUInt(value.wire)

private fun decodeProviderRefusalView(decoder: CoreStatusPayloadDecoder): ProviderRefusalView =
    ProviderRefusalView.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeProviderOriginView(encoder: CoreStatusPayloadEncoder, value: ProviderOriginView) =
    encoder.putUInt(value.wire)

private fun decodeProviderOriginView(decoder: CoreStatusPayloadDecoder): ProviderOriginView =
    ProviderOriginView.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeCatalogLayerView(encoder: CoreStatusPayloadEncoder, value: CatalogLayerView) =
    encoder.putUInt(value.wire)

private fun decodeCatalogLayerView(decoder: CoreStatusPayloadDecoder): CatalogLayerView =
    CatalogLayerView.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeThinkingLevelView(encoder: CoreStatusPayloadEncoder, value: ThinkingLevelView) =
    encoder.putUInt(value.wire)

private fun decodeThinkingLevelView(decoder: CoreStatusPayloadDecoder): ThinkingLevelView =
    ThinkingLevelView.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeModelRoleView(encoder: CoreStatusPayloadEncoder, value: ModelRoleView) =
    encoder.putUInt(value.wire)

private fun decodeModelRoleView(decoder: CoreStatusPayloadDecoder): ModelRoleView =
    ModelRoleView.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeInputModalityView(encoder: CoreStatusPayloadEncoder, value: InputModalityView) =
    encoder.putUInt(value.wire)

private fun decodeInputModalityView(decoder: CoreStatusPayloadDecoder): InputModalityView =
    InputModalityView.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun encodeServerKindView(encoder: CoreStatusPayloadEncoder, value: ServerKindView) =
    encoder.putUInt(value.wire)

private fun decodeServerKindView(decoder: CoreStatusPayloadDecoder): ServerKindView =
    ServerKindView.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)

private fun validateSiteSkillSemanticTarget(value: SiteSkillSemanticTarget) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeSiteSkillSemanticTarget(encoder: CoreStatusPayloadEncoder, value: SiteSkillSemanticTarget) {
    validateSiteSkillSemanticTarget(value)
    encoder.putUInt(value.role)
    encoder.putUInt(value.phrase)
}

private fun decodeSiteSkillSemanticTarget(decoder: CoreStatusPayloadDecoder): SiteSkillSemanticTarget {
    val value = SiteSkillSemanticTarget(
        role = decoder.readUInt(),
        phrase = decoder.readUInt(),
    )
    validateSiteSkillSemanticTarget(value)
    return value
}

private fun validateSiteSkillObservedArgument(value: SiteSkillObservedArgument) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeSiteSkillObservedArgument(encoder: CoreStatusPayloadEncoder, value: SiteSkillObservedArgument) {
    validateSiteSkillObservedArgument(value)
    encoder.putUInt(value.parameter)
    encodeSiteSkillArgumentKind(encoder, value.kind)
    encoder.putULong(value.value)
    encoder.putUInt(value.purpose)
    run {
        val optional = value.public_address
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encoder.putString(optional, MAX_TASK_GOAL_BYTES)
        }
    }
    run {
        val optional = value.semantic_target
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encodeSiteSkillSemanticTarget(encoder, optional)
        }
    }
}

private fun decodeSiteSkillObservedArgument(decoder: CoreStatusPayloadDecoder): SiteSkillObservedArgument {
    val value = SiteSkillObservedArgument(
        parameter = decoder.readUInt(),
        kind = decodeSiteSkillArgumentKind(decoder),
        value = decoder.readULong(),
        purpose = decoder.readUInt(),
        public_address = if (decoder.readBoolean()) decoder.readString(MAX_TASK_GOAL_BYTES) else null,
        semantic_target = if (decoder.readBoolean()) decodeSiteSkillSemanticTarget(decoder) else null,
    )
    validateSiteSkillObservedArgument(value)
    return value
}

private fun validateSiteSkillObservedStep(value: SiteSkillObservedStep) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeSiteSkillObservedStep(encoder: CoreStatusPayloadEncoder, value: SiteSkillObservedStep) {
    validateSiteSkillObservedStep(value)
    encoder.putString(value.verb, MAX_SKILL_TOOL_NAME_BYTES)
    encoder.putLength(value.arguments.size, MAX_SKILL_ARGUMENTS_PER_STEP)
    for (item in value.arguments) {
        encodeSiteSkillObservedArgument(encoder, item)
    }
    encoder.putUInt(value.postcondition)
    encoder.putBoolean(value.has_fill)
    encoder.putUInt(value.fill_purpose)
}

private fun decodeSiteSkillObservedStep(decoder: CoreStatusPayloadDecoder): SiteSkillObservedStep {
    val value = SiteSkillObservedStep(
        verb = decoder.readString(MAX_SKILL_TOOL_NAME_BYTES),
        arguments = decoder.readList(MAX_SKILL_ARGUMENTS_PER_STEP) { decodeSiteSkillObservedArgument(decoder) },
        postcondition = decoder.readUInt(),
        has_fill = decoder.readBoolean(),
        fill_purpose = decoder.readUInt(),
    )
    validateSiteSkillObservedStep(value)
    return value
}

private fun validateSiteSkillView(value: SiteSkillView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeSiteSkillView(encoder: CoreStatusPayloadEncoder, value: SiteSkillView) {
    validateSiteSkillView(value)
    encoder.putString(value.skill_id, MAX_SKILL_ID_BYTES)
    encoder.putString(value.origin, MAX_SKILL_ORIGIN_BYTES)
    encodeSiteSkillProvenanceView(encoder, value.provenance)
    encodeSiteSkillStatusView(encoder, value.status)
    encoder.putUInt(value.active_version)
    encoder.putUInt(value.step_count)
    encoder.putULong(value.installed_at_epoch_ms)
    encoder.putULong(value.updated_at_epoch_ms)
    run {
        val optional = value.recorded_from_task_id
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encoder.putString(optional, MAX_IDENTIFIER_BYTES)
        }
    }
    encoder.putLength(value.reviewed_steps.size, MAX_SKILL_STEPS)
    for (item in value.reviewed_steps) {
        encodeSiteSkillObservedStep(encoder, item)
    }
}

private fun decodeSiteSkillView(decoder: CoreStatusPayloadDecoder): SiteSkillView {
    val value = SiteSkillView(
        skill_id = decoder.readString(MAX_SKILL_ID_BYTES),
        origin = decoder.readString(MAX_SKILL_ORIGIN_BYTES),
        provenance = decodeSiteSkillProvenanceView(decoder),
        status = decodeSiteSkillStatusView(decoder),
        active_version = decoder.readUInt(),
        step_count = decoder.readUInt(),
        installed_at_epoch_ms = decoder.readULong(),
        updated_at_epoch_ms = decoder.readULong(),
        recorded_from_task_id = if (decoder.readBoolean()) decoder.readString(MAX_IDENTIFIER_BYTES) else null,
        reviewed_steps = decoder.readList(MAX_SKILL_STEPS) { decodeSiteSkillObservedStep(decoder) },
    )
    validateSiteSkillView(value)
    return value
}

private fun validateAssistantConfigurationView(value: AssistantConfigurationView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeAssistantConfigurationView(encoder: CoreStatusPayloadEncoder, value: AssistantConfigurationView) {
    validateAssistantConfigurationView(value)
    encoder.putULong(value.revision)
    encoder.putLength(value.disabled_abilities.size, MAX_ASSISTANT_ABILITIES)
    for (item in value.disabled_abilities) {
        encodeAssistantAbilityView(encoder, item)
    }
    encodePersonalityPresetView(encoder, value.preset)
    encoder.putBoundedUInt(value.pace, MAX_PERSONALITY_SCALE.toUInt())
    encoder.putBoundedUInt(value.length, MAX_PERSONALITY_SCALE.toUInt())
    encoder.putBoundedUInt(value.check_in, MAX_PERSONALITY_SCALE.toUInt())
}

private fun decodeAssistantConfigurationView(decoder: CoreStatusPayloadDecoder): AssistantConfigurationView {
    val value = AssistantConfigurationView(
        revision = decoder.readULong(),
        disabled_abilities = decoder.readList(MAX_ASSISTANT_ABILITIES) { decodeAssistantAbilityView(decoder) },
        preset = decodePersonalityPresetView(decoder),
        pace = decoder.readBoundedUInt(MAX_PERSONALITY_SCALE.toUInt()),
        length = decoder.readBoundedUInt(MAX_PERSONALITY_SCALE.toUInt()),
        check_in = decoder.readBoundedUInt(MAX_PERSONALITY_SCALE.toUInt()),
    )
    validateAssistantConfigurationView(value)
    return value
}

private fun validateBuiltinSkillReferenceView(value: BuiltinSkillReferenceView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeBuiltinSkillReferenceView(encoder: CoreStatusPayloadEncoder, value: BuiltinSkillReferenceView) {
    validateBuiltinSkillReferenceView(value)
    encodeBuiltinSkillIdView(encoder, value.skill_id)
    encoder.putUInt(value.version)
}

private fun decodeBuiltinSkillReferenceView(decoder: CoreStatusPayloadDecoder): BuiltinSkillReferenceView {
    val value = BuiltinSkillReferenceView(
        skill_id = decodeBuiltinSkillIdView(decoder),
        version = decoder.readUInt(),
    )
    validateBuiltinSkillReferenceView(value)
    return value
}

private fun validateBuiltinSkillView(value: BuiltinSkillView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeBuiltinSkillView(encoder: CoreStatusPayloadEncoder, value: BuiltinSkillView) {
    validateBuiltinSkillView(value)
    encodeBuiltinSkillReferenceView(encoder, value.reference)
    encodeAssistantAbilityView(encoder, value.required_ability)
    encoder.putBoolean(value.enabled)
    encodeBuiltinSkillAvailabilityView(encoder, value.availability)
    encoder.putUInt(value.required_tool_count)
    encoder.putUInt(value.available_tool_count)
    encoder.putUInt(value.required_part_count)
    encoder.putUInt(value.installed_part_count)
}

private fun decodeBuiltinSkillView(decoder: CoreStatusPayloadDecoder): BuiltinSkillView {
    val value = BuiltinSkillView(
        reference = decodeBuiltinSkillReferenceView(decoder),
        required_ability = decodeAssistantAbilityView(decoder),
        enabled = decoder.readBoolean(),
        availability = decodeBuiltinSkillAvailabilityView(decoder),
        required_tool_count = decoder.readUInt(),
        available_tool_count = decoder.readUInt(),
        required_part_count = decoder.readUInt(),
        installed_part_count = decoder.readUInt(),
    )
    validateBuiltinSkillView(value)
    return value
}

private fun validateCoreStatusProjectionOmission(value: CoreStatusProjectionOmission) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeCoreStatusProjectionOmission(encoder: CoreStatusPayloadEncoder, value: CoreStatusProjectionOmission) {
    validateCoreStatusProjectionOmission(value)
    encodeCoreStatusProjectionFamily(encoder, value.family)
    encoder.putULong(value.revision)
    encoder.putUInt(value.item_count)
}

private fun decodeCoreStatusProjectionOmission(decoder: CoreStatusPayloadDecoder): CoreStatusProjectionOmission {
    val value = CoreStatusProjectionOmission(
        family = decodeCoreStatusProjectionFamily(decoder),
        revision = decoder.readULong(),
        item_count = decoder.readUInt(),
    )
    validateCoreStatusProjectionOmission(value)
    return value
}

private fun validateCoreFailure(value: CoreFailure) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeCoreFailure(encoder: CoreStatusPayloadEncoder, value: CoreFailure) {
    validateCoreFailure(value)
    encodeCoreFailureCode(encoder, value.code)
    encoder.putBoolean(value.retryable)
    run {
        val optional = value.message_key
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encoder.putString(optional, MAX_MESSAGE_KEY_BYTES)
        }
    }
}

private fun decodeCoreFailure(decoder: CoreStatusPayloadDecoder): CoreFailure {
    val value = CoreFailure(
        code = decodeCoreFailureCode(decoder),
        retryable = decoder.readBoolean(),
        message_key = if (decoder.readBoolean()) decoder.readString(MAX_MESSAGE_KEY_BYTES) else null,
    )
    validateCoreFailure(value)
    return value
}

private fun validateTaskActivityView(value: TaskActivityView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeTaskActivityView(encoder: CoreStatusPayloadEncoder, value: TaskActivityView) {
    validateTaskActivityView(value)
    encoder.putULong(value.sequence)
    encodeTaskActivityKind(encoder, value.kind)
    run {
        val optional = value.host
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encoder.putString(optional, MAX_SOURCE_HOST_BYTES)
        }
    }
    encoder.putUInt(value.count)
    encoder.putULong(value.at_epoch_ms)
}

private fun decodeTaskActivityView(decoder: CoreStatusPayloadDecoder): TaskActivityView {
    val value = TaskActivityView(
        sequence = decoder.readULong(),
        kind = decodeTaskActivityKind(decoder),
        host = if (decoder.readBoolean()) decoder.readString(MAX_SOURCE_HOST_BYTES) else null,
        count = decoder.readUInt(),
        at_epoch_ms = decoder.readULong(),
    )
    validateTaskActivityView(value)
    return value
}

private fun validateTaskArtifactView(value: TaskArtifactView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeTaskArtifactView(encoder: CoreStatusPayloadEncoder, value: TaskArtifactView) {
    validateTaskArtifactView(value)
    encoder.putString(value.artifact_id, MAX_IDENTIFIER_BYTES)
    encodeTaskArtifactKind(encoder, value.kind)
    encoder.putULong(value.workspace_revision)
    encoder.putBoolean(value.accepted)
}

private fun decodeTaskArtifactView(decoder: CoreStatusPayloadDecoder): TaskArtifactView {
    val value = TaskArtifactView(
        artifact_id = decoder.readString(MAX_IDENTIFIER_BYTES),
        kind = decodeTaskArtifactKind(decoder),
        workspace_revision = decoder.readULong(),
        accepted = decoder.readBoolean(),
    )
    validateTaskArtifactView(value)
    return value
}

private fun validateTaskViewState(value: TaskViewState) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeTaskViewState(encoder: CoreStatusPayloadEncoder, value: TaskViewState) {
    validateTaskViewState(value)
    encoder.putString(value.task_id, MAX_IDENTIFIER_BYTES)
    encoder.putULong(value.revision)
    encodeTaskPhase(encoder, value.phase)
    encoder.putBoundedUInt(value.progress_basis_points, MAX_PROGRESS_BASIS_POINTS.toUInt())
    run {
        val optional = value.status_message_key
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encoder.putString(optional, MAX_MESSAGE_KEY_BYTES)
        }
    }
    run {
        val optional = value.failure
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encodeCoreFailure(encoder, optional)
        }
    }
    encoder.putString(value.goal, MAX_TASK_GOAL_BYTES)
    encodeTaskTemplateId(encoder, value.template_id)
    run {
        val optional = value.pending_action
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encodeActionApprovalView(encoder, optional)
        }
    }
    run {
        val optional = value.workspace_id
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encoder.putString(optional, MAX_IDENTIFIER_BYTES)
        }
    }
    run {
        val optional = value.pending_ask_prompt
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encoder.putString(optional, MAX_USER_INPUT_ANSWER_BYTES)
        }
    }
    run {
        val optional = value.pending_field_value_request
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encoder.putString(optional, MAX_IDENTIFIER_BYTES)
        }
    }
    encoder.putLength(value.allowed_controls.size, MAX_TASK_CONTROLS)
    for (item in value.allowed_controls) {
        encodeTaskControlKind(encoder, item)
    }
    encoder.putLength(value.artifacts.size, MAX_TASK_ARTIFACTS)
    for (item in value.artifacts) {
        encodeTaskArtifactView(encoder, item)
    }
    encoder.putLength(value.activity.size, MAX_TASK_ACTIVITY)
    for (item in value.activity) {
        encodeTaskActivityView(encoder, item)
    }
}

private fun decodeTaskViewState(decoder: CoreStatusPayloadDecoder): TaskViewState {
    val value = TaskViewState(
        task_id = decoder.readString(MAX_IDENTIFIER_BYTES),
        revision = decoder.readULong(),
        phase = decodeTaskPhase(decoder),
        progress_basis_points = decoder.readBoundedUInt(MAX_PROGRESS_BASIS_POINTS.toUInt()),
        status_message_key = if (decoder.readBoolean()) decoder.readString(MAX_MESSAGE_KEY_BYTES) else null,
        failure = if (decoder.readBoolean()) decodeCoreFailure(decoder) else null,
        goal = decoder.readString(MAX_TASK_GOAL_BYTES),
        template_id = decodeTaskTemplateId(decoder),
        pending_action = if (decoder.readBoolean()) decodeActionApprovalView(decoder) else null,
        workspace_id = if (decoder.readBoolean()) decoder.readString(MAX_IDENTIFIER_BYTES) else null,
        pending_ask_prompt = if (decoder.readBoolean()) decoder.readString(MAX_USER_INPUT_ANSWER_BYTES) else null,
        pending_field_value_request = if (decoder.readBoolean()) decoder.readString(MAX_IDENTIFIER_BYTES) else null,
        allowed_controls = decoder.readList(MAX_TASK_CONTROLS) { decodeTaskControlKind(decoder) },
        artifacts = decoder.readList(MAX_TASK_ARTIFACTS) { decodeTaskArtifactView(decoder) },
        activity = decoder.readList(MAX_TASK_ACTIVITY) { decodeTaskActivityView(decoder) },
    )
    validateTaskViewState(value)
    return value
}

private fun validateActionApprovalView(value: ActionApprovalView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeActionApprovalView(encoder: CoreStatusPayloadEncoder, value: ActionApprovalView) {
    validateActionApprovalView(value)
    encoder.putString(value.action_id, MAX_IDENTIFIER_BYTES)
    run {
        val optional = value.host
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encoder.putString(optional, MAX_IDENTIFIER_BYTES)
        }
    }
    encoder.putUInt(value.item_count)
    encoder.putString(value.summary_message_key, MAX_MESSAGE_KEY_BYTES)
}

private fun decodeActionApprovalView(decoder: CoreStatusPayloadDecoder): ActionApprovalView {
    val value = ActionApprovalView(
        action_id = decoder.readString(MAX_IDENTIFIER_BYTES),
        host = if (decoder.readBoolean()) decoder.readString(MAX_IDENTIFIER_BYTES) else null,
        item_count = decoder.readUInt(),
        summary_message_key = decoder.readString(MAX_MESSAGE_KEY_BYTES),
    )
    validateActionApprovalView(value)
    return value
}

private fun validateStoredCredentialView(value: StoredCredentialView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeStoredCredentialView(encoder: CoreStatusPayloadEncoder, value: StoredCredentialView) {
    validateStoredCredentialView(value)
    encodeProviderAuthMethodView(encoder, value.auth_method)
    encodeProviderCredentialStateView(encoder, value.state)
    encoder.putBoolean(value.subscription_backed)
    run {
        val optional = value.account_label
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encoder.putString(optional, MAX_AUTH_DISPLAY_NAME_BYTES)
        }
    }
    run {
        val optional = value.plan_label
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encoder.putString(optional, MAX_PROVIDER_DISPLAY_NAME_BYTES)
        }
    }
}

private fun decodeStoredCredentialView(decoder: CoreStatusPayloadDecoder): StoredCredentialView {
    val value = StoredCredentialView(
        auth_method = decodeProviderAuthMethodView(decoder),
        state = decodeProviderCredentialStateView(decoder),
        subscription_backed = decoder.readBoolean(),
        account_label = if (decoder.readBoolean()) decoder.readString(MAX_AUTH_DISPLAY_NAME_BYTES) else null,
        plan_label = if (decoder.readBoolean()) decoder.readString(MAX_PROVIDER_DISPLAY_NAME_BYTES) else null,
    )
    validateStoredCredentialView(value)
    return value
}

private fun validateThinkingPreferenceView(value: ThinkingPreferenceView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeThinkingPreferenceView(encoder: CoreStatusPayloadEncoder, value: ThinkingPreferenceView) {
    validateThinkingPreferenceView(value)
    encodeThinkingLevelView(encoder, value.level)
}

private fun decodeThinkingPreferenceView(decoder: CoreStatusPayloadDecoder): ThinkingPreferenceView {
    val value = ThinkingPreferenceView(
        level = decodeThinkingLevelView(decoder),
    )
    validateThinkingPreferenceView(value)
    return value
}

private fun validateProviderRefusalStateView(value: ProviderRefusalStateView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeProviderRefusalStateView(encoder: CoreStatusPayloadEncoder, value: ProviderRefusalStateView) {
    validateProviderRefusalStateView(value)
    encodeProviderRefusalView(encoder, value.refusal)
    encoder.putULong(value.at_monotonic_ms)
}

private fun decodeProviderRefusalStateView(decoder: CoreStatusPayloadDecoder): ProviderRefusalStateView {
    val value = ProviderRefusalStateView(
        refusal = decodeProviderRefusalView(decoder),
        at_monotonic_ms = decoder.readULong(),
    )
    validateProviderRefusalStateView(value)
    return value
}

private fun validateProviderPresentationView(value: ProviderPresentationView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeProviderPresentationView(encoder: CoreStatusPayloadEncoder, value: ProviderPresentationView) {
    validateProviderPresentationView(value)
    run {
        val optional = value.key_prefix
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encoder.putString(optional, MAX_PROVIDER_PRESENTATION_BYTES)
        }
    }
    run {
        val optional = value.get_key_url
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encoder.putString(optional, MAX_PROVIDER_PRESENTATION_BYTES)
        }
    }
    run {
        val optional = value.docs_url
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encoder.putString(optional, MAX_PROVIDER_PRESENTATION_BYTES)
        }
    }
}

private fun decodeProviderPresentationView(decoder: CoreStatusPayloadDecoder): ProviderPresentationView {
    val value = ProviderPresentationView(
        key_prefix = if (decoder.readBoolean()) decoder.readString(MAX_PROVIDER_PRESENTATION_BYTES) else null,
        get_key_url = if (decoder.readBoolean()) decoder.readString(MAX_PROVIDER_PRESENTATION_BYTES) else null,
        docs_url = if (decoder.readBoolean()) decoder.readString(MAX_PROVIDER_PRESENTATION_BYTES) else null,
    )
    validateProviderPresentationView(value)
    return value
}

private fun validateProviderModelView(value: ProviderModelView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeProviderModelView(encoder: CoreStatusPayloadEncoder, value: ProviderModelView) {
    validateProviderModelView(value)
    encoder.putString(value.provider_id, MAX_PROVIDER_ID_BYTES)
    encoder.putString(value.model_id, MAX_MODEL_ID_BYTES)
    encoder.putString(value.display_name, MAX_MODEL_DISPLAY_NAME_BYTES)
    encoder.putULong(value.context_window)
    encoder.putULong(value.max_output_tokens)
    encoder.putBoolean(value.reasoning)
    encoder.putBoolean(value.tool_calling)
    encoder.putLength(value.roles.size, MAX_MODEL_ROLES)
    for (item in value.roles) {
        encodeModelRoleView(encoder, item)
    }
    encoder.putLength(value.input_modalities.size, MAX_MODEL_INPUT_MODALITIES)
    for (item in value.input_modalities) {
        encodeInputModalityView(encoder, item)
    }
    encoder.putLength(value.thinking_levels.size, MAX_MODEL_THINKING_LEVELS)
    for (item in value.thinking_levels) {
        encodeThinkingLevelView(encoder, item)
    }
}

private fun decodeProviderModelView(decoder: CoreStatusPayloadDecoder): ProviderModelView {
    val value = ProviderModelView(
        provider_id = decoder.readString(MAX_PROVIDER_ID_BYTES),
        model_id = decoder.readString(MAX_MODEL_ID_BYTES),
        display_name = decoder.readString(MAX_MODEL_DISPLAY_NAME_BYTES),
        context_window = decoder.readULong(),
        max_output_tokens = decoder.readULong(),
        reasoning = decoder.readBoolean(),
        tool_calling = decoder.readBoolean(),
        roles = decoder.readList(MAX_MODEL_ROLES) { decodeModelRoleView(decoder) },
        input_modalities = decoder.readList(MAX_MODEL_INPUT_MODALITIES) { decodeInputModalityView(decoder) },
        thinking_levels = decoder.readList(MAX_MODEL_THINKING_LEVELS) { decodeThinkingLevelView(decoder) },
    )
    validateProviderModelView(value)
    return value
}

private fun validateProviderRosterEntry(value: ProviderRosterEntry) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeProviderRosterEntry(encoder: CoreStatusPayloadEncoder, value: ProviderRosterEntry) {
    validateProviderRosterEntry(value)
    encoder.putString(value.provider_id, MAX_PROVIDER_ID_BYTES)
    encoder.putString(value.display_name, MAX_PROVIDER_DISPLAY_NAME_BYTES)
    encodeProviderOriginView(encoder, value.origin)
    encoder.putLength(value.auth_methods.size, MAX_AUTH_METHODS)
    for (item in value.auth_methods) {
        encodeProviderAuthMethodView(encoder, item)
    }
    run {
        val optional = value.stored
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encodeStoredCredentialView(encoder, optional)
        }
    }
    encoder.putBoolean(value.signing_in)
    encoder.putBoolean(value.enabled)
    run {
        val optional = value.endpoint_host
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encoder.putString(optional, MAX_SOURCE_HOST_BYTES)
        }
    }
    encoder.putBoolean(value.configurable)
    encoder.putBoolean(value.endpoint_changed)
    encodeCatalogLayerView(encoder, value.catalog_layer)
    run {
        val optional = value.selected_model_id
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encoder.putString(optional, MAX_MODEL_ID_BYTES)
        }
    }
    run {
        val optional = value.thinking
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encodeThinkingPreferenceView(encoder, optional)
        }
    }
    run {
        val optional = value.presentation
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encodeProviderPresentationView(encoder, optional)
        }
    }
    run {
        val optional = value.endpoint_base
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encoder.putString(optional, MAX_PROVIDER_ENDPOINT_BYTES)
        }
    }
    run {
        val optional = value.last_refusal
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encodeProviderRefusalStateView(encoder, optional)
        }
    }
    encoder.putUInt(value.model_count)
    encoder.putBoolean(value.subscription)
    run {
        val optional = value.refused_endpoint_host
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encoder.putString(optional, MAX_SOURCE_HOST_BYTES)
        }
    }
}

private fun decodeProviderRosterEntry(decoder: CoreStatusPayloadDecoder): ProviderRosterEntry {
    val value = ProviderRosterEntry(
        provider_id = decoder.readString(MAX_PROVIDER_ID_BYTES),
        display_name = decoder.readString(MAX_PROVIDER_DISPLAY_NAME_BYTES),
        origin = decodeProviderOriginView(decoder),
        auth_methods = decoder.readList(MAX_AUTH_METHODS) { decodeProviderAuthMethodView(decoder) },
        stored = if (decoder.readBoolean()) decodeStoredCredentialView(decoder) else null,
        signing_in = decoder.readBoolean(),
        enabled = decoder.readBoolean(),
        endpoint_host = if (decoder.readBoolean()) decoder.readString(MAX_SOURCE_HOST_BYTES) else null,
        configurable = decoder.readBoolean(),
        endpoint_changed = decoder.readBoolean(),
        catalog_layer = decodeCatalogLayerView(decoder),
        selected_model_id = if (decoder.readBoolean()) decoder.readString(MAX_MODEL_ID_BYTES) else null,
        thinking = if (decoder.readBoolean()) decodeThinkingPreferenceView(decoder) else null,
        presentation = if (decoder.readBoolean()) decodeProviderPresentationView(decoder) else null,
        endpoint_base = if (decoder.readBoolean()) decoder.readString(MAX_PROVIDER_ENDPOINT_BYTES) else null,
        last_refusal = if (decoder.readBoolean()) decodeProviderRefusalStateView(decoder) else null,
        model_count = decoder.readUInt(),
        subscription = decoder.readBoolean(),
        refused_endpoint_host = if (decoder.readBoolean()) decoder.readString(MAX_SOURCE_HOST_BYTES) else null,
    )
    validateProviderRosterEntry(value)
    return value
}

private fun validateProviderProbeView(value: ProviderProbeView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeProviderProbeView(encoder: CoreStatusPayloadEncoder, value: ProviderProbeView) {
    validateProviderProbeView(value)
    encoder.putString(value.provider_id, MAX_PROVIDER_ID_BYTES)
    encodeProviderProbeVerdictView(encoder, value.verdict)
    encoder.putULong(value.at_monotonic_ms)
    run {
        val optional = value.endpoint
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encodeProbeEndpointView(encoder, optional)
        }
    }
}

private fun decodeProviderProbeView(decoder: CoreStatusPayloadDecoder): ProviderProbeView {
    val value = ProviderProbeView(
        provider_id = decoder.readString(MAX_PROVIDER_ID_BYTES),
        verdict = decodeProviderProbeVerdictView(decoder),
        at_monotonic_ms = decoder.readULong(),
        endpoint = if (decoder.readBoolean()) decodeProbeEndpointView(decoder) else null,
    )
    validateProviderProbeView(value)
    return value
}

private fun validateProbeEndpointView(value: ProbeEndpointView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeProbeEndpointView(encoder: CoreStatusPayloadEncoder, value: ProbeEndpointView) {
    validateProbeEndpointView(value)
    encodeServerKindView(encoder, value.server_kind)
    encoder.putUInt(value.model_count)
    encoder.putLength(value.models.size, MAX_CUSTOM_MODEL_ENTRIES)
    for (item in value.models) {
        encodeCustomModelSpecView(encoder, item)
    }
    run {
        val optional = value.proved_base
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encoder.putString(optional, MAX_PROVIDER_ENDPOINT_BYTES)
        }
    }
}

private fun decodeProbeEndpointView(decoder: CoreStatusPayloadDecoder): ProbeEndpointView {
    val value = ProbeEndpointView(
        server_kind = decodeServerKindView(decoder),
        model_count = decoder.readUInt(),
        models = decoder.readList(MAX_CUSTOM_MODEL_ENTRIES) { decodeCustomModelSpecView(decoder) },
        proved_base = if (decoder.readBoolean()) decoder.readString(MAX_PROVIDER_ENDPOINT_BYTES) else null,
    )
    validateProbeEndpointView(value)
    return value
}

private fun validateSavedSignInView(value: SavedSignInView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeSavedSignInView(encoder: CoreStatusPayloadEncoder, value: SavedSignInView) {
    validateSavedSignInView(value)
    encoder.putString(value.id, MAX_IDENTIFIER_BYTES)
    encoder.putString(value.site, MAX_SAVED_SIGN_IN_SITE_BYTES)
    encoder.putString(value.username, MAX_SAVED_SIGN_IN_USERNAME_BYTES)
    encoder.putULong(value.last_used_epoch_ms)
}

private fun decodeSavedSignInView(decoder: CoreStatusPayloadDecoder): SavedSignInView {
    val value = SavedSignInView(
        id = decoder.readString(MAX_IDENTIFIER_BYTES),
        site = decoder.readString(MAX_SAVED_SIGN_IN_SITE_BYTES),
        username = decoder.readString(MAX_SAVED_SIGN_IN_USERNAME_BYTES),
        last_used_epoch_ms = decoder.readULong(),
    )
    validateSavedSignInView(value)
    return value
}

private fun validateSavedSignInsView(value: SavedSignInsView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeSavedSignInsView(encoder: CoreStatusPayloadEncoder, value: SavedSignInsView) {
    validateSavedSignInsView(value)
    encodeSavedDataAvailability(encoder, value.availability)
    encoder.putULong(value.revision)
    encoder.putLength(value.records.size, MAX_SAVED_SIGN_INS)
    for (item in value.records) {
        encodeSavedSignInView(encoder, item)
    }
}

private fun decodeSavedSignInsView(decoder: CoreStatusPayloadDecoder): SavedSignInsView {
    val value = SavedSignInsView(
        availability = decodeSavedDataAvailability(decoder),
        revision = decoder.readULong(),
        records = decoder.readList(MAX_SAVED_SIGN_INS) { decodeSavedSignInView(decoder) },
    )
    validateSavedSignInsView(value)
    return value
}

private fun validateSavedDetailView(value: SavedDetailView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeSavedDetailView(encoder: CoreStatusPayloadEncoder, value: SavedDetailView) {
    validateSavedDetailView(value)
    encoder.putString(value.id, MAX_IDENTIFIER_BYTES)
    encoder.putString(value.given_name, MAX_SAVED_DETAIL_NAME_BYTES)
    encoder.putString(value.family_name, MAX_SAVED_DETAIL_NAME_BYTES)
    encoder.putString(value.email, MAX_SAVED_DETAIL_EMAIL_BYTES)
    encoder.putString(value.phone, MAX_SAVED_DETAIL_PHONE_BYTES)
    encoder.putString(value.address, MAX_SAVED_DETAIL_ADDRESS_BYTES)
    encoder.putString(value.postcode, MAX_SAVED_DETAIL_POSTCODE_BYTES)
    encoder.putString(value.country, MAX_SAVED_DETAIL_COUNTRY_BYTES)
}

private fun decodeSavedDetailView(decoder: CoreStatusPayloadDecoder): SavedDetailView {
    val value = SavedDetailView(
        id = decoder.readString(MAX_IDENTIFIER_BYTES),
        given_name = decoder.readString(MAX_SAVED_DETAIL_NAME_BYTES),
        family_name = decoder.readString(MAX_SAVED_DETAIL_NAME_BYTES),
        email = decoder.readString(MAX_SAVED_DETAIL_EMAIL_BYTES),
        phone = decoder.readString(MAX_SAVED_DETAIL_PHONE_BYTES),
        address = decoder.readString(MAX_SAVED_DETAIL_ADDRESS_BYTES),
        postcode = decoder.readString(MAX_SAVED_DETAIL_POSTCODE_BYTES),
        country = decoder.readString(MAX_SAVED_DETAIL_COUNTRY_BYTES),
    )
    validateSavedDetailView(value)
    return value
}

private fun validateSavedDetailsView(value: SavedDetailsView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeSavedDetailsView(encoder: CoreStatusPayloadEncoder, value: SavedDetailsView) {
    validateSavedDetailsView(value)
    encodeSavedDataAvailability(encoder, value.availability)
    encoder.putULong(value.revision)
    encoder.putLength(value.people.size, MAX_SAVED_DETAILS)
    for (item in value.people) {
        encodeSavedDetailView(encoder, item)
    }
}

private fun decodeSavedDetailsView(decoder: CoreStatusPayloadDecoder): SavedDetailsView {
    val value = SavedDetailsView(
        availability = decodeSavedDataAvailability(decoder),
        revision = decoder.readULong(),
        people = decoder.readList(MAX_SAVED_DETAILS) { decodeSavedDetailView(decoder) },
    )
    validateSavedDetailsView(value)
    return value
}

private fun validateCoreStatus(value: CoreStatus) {
    if (value.projection_mode == CoreStatusProjectionMode.COMPLETE || value.projection_mode == CoreStatusProjectionMode.RECOVERY_REQUIRED) {
        val expected = listOf(BuiltinSkillIdView.GENERAL_WEB_RESEARCH, BuiltinSkillIdView.DEEP_RESEARCH, BuiltinSkillIdView.PRODUCT_COMPARISON, BuiltinSkillIdView.MULTI_TAB_COMPARISON, BuiltinSkillIdView.WEBSITE_SUMMARIZER, BuiltinSkillIdView.PDF_ANALYSIS, BuiltinSkillIdView.DATA_EXTRACTION, BuiltinSkillIdView.FORM_ASSISTANT, BuiltinSkillIdView.SHOPPING, BuiltinSkillIdView.DOWNLOAD_ORGANIZER, BuiltinSkillIdView.TRAVEL_RESEARCH, BuiltinSkillIdView.VIDEO_TRANSCRIPT_ANALYZER, BuiltinSkillIdView.IMAGE_UNDERSTANDING, BuiltinSkillIdView.LIBRARY_BUILDER, BuiltinSkillIdView.SPREADSHEET_BUILDER, BuiltinSkillIdView.DOCUMENT_GENERATOR)
        if (value.builtin_skills.size != expected.size ||
            value.builtin_skills.zip(expected).any { (item, member) ->
                item.reference.skill_id != member
            }
        ) {
            fail(CoreStatusPayloadCodecError.MALFORMED)
        }
    }
    if (value.projection_mode == CoreStatusProjectionMode.RECOVERY_REQUIRED) {
        val expected = listOf(CoreStatusProjectionFamily.ACTIVE_TASKS, CoreStatusProjectionFamily.WORKSPACES, CoreStatusProjectionFamily.WORKSPACE_EXPORT, CoreStatusProjectionFamily.ASSET_DELIVERY, CoreStatusProjectionFamily.PROVIDER_ROSTER, CoreStatusProjectionFamily.PROVIDER_PROBES, CoreStatusProjectionFamily.PROVIDER_MODELS, CoreStatusProjectionFamily.LIBRARY, CoreStatusProjectionFamily.LIBRARY_EXPORT, CoreStatusProjectionFamily.MEMORY, CoreStatusProjectionFamily.SAVED_SIGN_INS, CoreStatusProjectionFamily.SAVED_DETAILS, CoreStatusProjectionFamily.SITE_SKILLS)
        if (value.projection_omissions.size != expected.size ||
            value.projection_omissions.zip(expected).any { (item, member) ->
                item.family != member
            }
        ) {
            fail(CoreStatusPayloadCodecError.MALFORMED)
        }
    }
    if ((value.projection_mode == CoreStatusProjectionMode.COMPLETE) && value.projection_omissions.isNotEmpty()) {
        fail(CoreStatusPayloadCodecError.MALFORMED)
    }
}

private fun encodeCoreStatus(encoder: CoreStatusPayloadEncoder, value: CoreStatus) {
    validateCoreStatus(value)
    encodeCoreAvailability(encoder, value.availability)
    encoder.putULong(value.generation)
    encoder.putLength(value.active_tasks.size, MAX_ACTIVE_TASKS)
    for (item in value.active_tasks) {
        encodeTaskViewState(encoder, item)
    }
    run {
        val optional = value.auth_state
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encodeAuthViewState(encoder, optional)
        }
    }
    encoder.putLength(value.workspaces.size, MAX_WORKSPACES)
    for (item in value.workspaces) {
        encodeWorkspaceViewState(encoder, item)
    }
    run {
        val optional = value.workspace_export
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encodeWorkspaceExportView(encoder, optional)
        }
    }
    run {
        val optional = value.asset_delivery
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encodeAssetDeliveryView(encoder, optional)
        }
    }
    encoder.putLength(value.provider_roster.size, MAX_PROVIDER_ROSTER_ENTRIES)
    for (item in value.provider_roster) {
        encodeProviderRosterEntry(encoder, item)
    }
    encoder.putLength(value.provider_probes.size, MAX_PROVIDER_ROSTER_ENTRIES)
    for (item in value.provider_probes) {
        encodeProviderProbeView(encoder, item)
    }
    encoder.putLength(value.provider_models.size, MAX_PROVIDER_MODEL_ENTRIES)
    for (item in value.provider_models) {
        encodeProviderModelView(encoder, item)
    }
    encodeAssistantConfigurationView(encoder, value.assistant_configuration)
    encodeLibraryViewState(encoder, value.library)
    run {
        val optional = value.library_export
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encodeLibraryExportView(encoder, optional)
        }
    }
    encodeMemoryViewState(encoder, value.memory)
    encodeSavedSignInsView(encoder, value.saved_sign_ins)
    encodeSavedDetailsView(encoder, value.saved_details)
    encoder.putLength(value.site_skills.size, MAX_SITE_SKILLS)
    for (item in value.site_skills) {
        encodeSiteSkillView(encoder, item)
    }
    encoder.putLength(value.builtin_skills.size, MAX_BUILTIN_SKILLS)
    for (item in value.builtin_skills) {
        encodeBuiltinSkillView(encoder, item)
    }
    encodeCoreStatusProjectionMode(encoder, value.projection_mode)
    encoder.putLength(value.projection_omissions.size, MAX_CORE_STATUS_PROJECTION_OMISSIONS)
    for (item in value.projection_omissions) {
        encodeCoreStatusProjectionOmission(encoder, item)
    }
}

private fun decodeCoreStatus(decoder: CoreStatusPayloadDecoder): CoreStatus {
    val value = CoreStatus(
        availability = decodeCoreAvailability(decoder),
        generation = decoder.readULong(),
        active_tasks = decoder.readList(MAX_ACTIVE_TASKS) { decodeTaskViewState(decoder) },
        auth_state = if (decoder.readBoolean()) decodeAuthViewState(decoder) else null,
        workspaces = decoder.readList(MAX_WORKSPACES) { decodeWorkspaceViewState(decoder) },
        workspace_export = if (decoder.readBoolean()) decodeWorkspaceExportView(decoder) else null,
        asset_delivery = if (decoder.readBoolean()) decodeAssetDeliveryView(decoder) else null,
        provider_roster = decoder.readList(MAX_PROVIDER_ROSTER_ENTRIES) { decodeProviderRosterEntry(decoder) },
        provider_probes = decoder.readList(MAX_PROVIDER_ROSTER_ENTRIES) { decodeProviderProbeView(decoder) },
        provider_models = decoder.readList(MAX_PROVIDER_MODEL_ENTRIES) { decodeProviderModelView(decoder) },
        assistant_configuration = decodeAssistantConfigurationView(decoder),
        library = decodeLibraryViewState(decoder),
        library_export = if (decoder.readBoolean()) decodeLibraryExportView(decoder) else null,
        memory = decodeMemoryViewState(decoder),
        saved_sign_ins = decodeSavedSignInsView(decoder),
        saved_details = decodeSavedDetailsView(decoder),
        site_skills = decoder.readList(MAX_SITE_SKILLS) { decodeSiteSkillView(decoder) },
        builtin_skills = decoder.readList(MAX_BUILTIN_SKILLS) { decodeBuiltinSkillView(decoder) },
        projection_mode = decodeCoreStatusProjectionMode(decoder),
        projection_omissions = decoder.readList(MAX_CORE_STATUS_PROJECTION_OMISSIONS) { decodeCoreStatusProjectionOmission(decoder) },
    )
    validateCoreStatus(value)
    return value
}

private fun validateWorkspaceViewState(value: WorkspaceViewState) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeWorkspaceViewState(encoder: CoreStatusPayloadEncoder, value: WorkspaceViewState) {
    validateWorkspaceViewState(value)
    encoder.putString(value.workspace_id, MAX_IDENTIFIER_BYTES)
    encoder.putULong(value.revision)
    encoder.putString(value.goal, MAX_TASK_GOAL_BYTES)
    encodeWorkspacePhase(encoder, value.phase)
    encoder.putULong(value.last_updated_epoch_ms)
    encodeTaskTemplateId(encoder, value.template_id)
    encoder.putLength(value.sources.size, MAX_WORKSPACE_SOURCES)
    for (item in value.sources) {
        encodeWorkspaceSourceView(encoder, item)
    }
    encoder.putLength(value.facts.size, MAX_WORKSPACE_FACTS)
    for (item in value.facts) {
        encodeWorkspaceFactView(encoder, item)
    }
    encoder.putBoolean(value.saved)
    encoder.putString(value.display_name, MAX_WORKSPACE_DISPLAY_NAME_BYTES)
    run {
        val optional = value.deletion_preview
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encodeWorkspaceDeletionPreviewView(encoder, optional)
        }
    }
}

private fun decodeWorkspaceViewState(decoder: CoreStatusPayloadDecoder): WorkspaceViewState {
    val value = WorkspaceViewState(
        workspace_id = decoder.readString(MAX_IDENTIFIER_BYTES),
        revision = decoder.readULong(),
        goal = decoder.readString(MAX_TASK_GOAL_BYTES),
        phase = decodeWorkspacePhase(decoder),
        last_updated_epoch_ms = decoder.readULong(),
        template_id = decodeTaskTemplateId(decoder),
        sources = decoder.readList(MAX_WORKSPACE_SOURCES) { decodeWorkspaceSourceView(decoder) },
        facts = decoder.readList(MAX_WORKSPACE_FACTS) { decodeWorkspaceFactView(decoder) },
        saved = decoder.readBoolean(),
        display_name = decoder.readString(MAX_WORKSPACE_DISPLAY_NAME_BYTES),
        deletion_preview = if (decoder.readBoolean()) decodeWorkspaceDeletionPreviewView(decoder) else null,
    )
    validateWorkspaceViewState(value)
    return value
}

private fun validateWorkspaceDeletionPreviewView(value: WorkspaceDeletionPreviewView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeWorkspaceDeletionPreviewView(encoder: CoreStatusPayloadEncoder, value: WorkspaceDeletionPreviewView) {
    validateWorkspaceDeletionPreviewView(value)
    encoder.putUInt(value.sources)
    encoder.putUInt(value.facts)
    encoder.putUInt(value.artifact_metadata)
    encoder.putUInt(value.derived_indexes)
    encoder.putString(value.confirmation_token, MAX_WORKSPACE_CONFIRMATION_TOKEN_BYTES)
}

private fun decodeWorkspaceDeletionPreviewView(decoder: CoreStatusPayloadDecoder): WorkspaceDeletionPreviewView {
    val value = WorkspaceDeletionPreviewView(
        sources = decoder.readUInt(),
        facts = decoder.readUInt(),
        artifact_metadata = decoder.readUInt(),
        derived_indexes = decoder.readUInt(),
        confirmation_token = decoder.readString(MAX_WORKSPACE_CONFIRMATION_TOKEN_BYTES),
    )
    validateWorkspaceDeletionPreviewView(value)
    return value
}

private fun validateWorkspaceSourceView(value: WorkspaceSourceView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeWorkspaceSourceView(encoder: CoreStatusPayloadEncoder, value: WorkspaceSourceView) {
    validateWorkspaceSourceView(value)
    encoder.putString(value.source_id, MAX_IDENTIFIER_BYTES)
    encoder.putString(value.title, MAX_WORKSPACE_TITLE_BYTES)
    encoder.putString(value.host, MAX_SOURCE_HOST_BYTES)
    encoder.putULong(value.read_at_epoch_ms)
    encoder.putUInt(value.fact_count)
    encoder.putBoolean(value.excluded)
}

private fun decodeWorkspaceSourceView(decoder: CoreStatusPayloadDecoder): WorkspaceSourceView {
    val value = WorkspaceSourceView(
        source_id = decoder.readString(MAX_IDENTIFIER_BYTES),
        title = decoder.readString(MAX_WORKSPACE_TITLE_BYTES),
        host = decoder.readString(MAX_SOURCE_HOST_BYTES),
        read_at_epoch_ms = decoder.readULong(),
        fact_count = decoder.readUInt(),
        excluded = decoder.readBoolean(),
    )
    validateWorkspaceSourceView(value)
    return value
}

private fun validateWorkspaceFactView(value: WorkspaceFactView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeWorkspaceFactView(encoder: CoreStatusPayloadEncoder, value: WorkspaceFactView) {
    validateWorkspaceFactView(value)
    encoder.putString(value.fact_id, MAX_IDENTIFIER_BYTES)
    encoder.putString(value.field, MAX_FACT_FIELD_BYTES)
    encoder.putString(value.value, MAX_FACT_VALUE_BYTES)
    encodeWorkspaceFactKind(encoder, value.kind)
    encoder.putLength(value.sources.size, MAX_FACT_SOURCES)
    for (item in value.sources) {
        encoder.putString(item, MAX_IDENTIFIER_BYTES)
    }
    run {
        val optional = value.correction
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encoder.putString(optional, MAX_FACT_VALUE_BYTES)
        }
    }
    encoder.putBoolean(value.has_conflict)
    encoder.putBoolean(value.needs_new_source)
}

private fun decodeWorkspaceFactView(decoder: CoreStatusPayloadDecoder): WorkspaceFactView {
    val value = WorkspaceFactView(
        fact_id = decoder.readString(MAX_IDENTIFIER_BYTES),
        field = decoder.readString(MAX_FACT_FIELD_BYTES),
        value = decoder.readString(MAX_FACT_VALUE_BYTES),
        kind = decodeWorkspaceFactKind(decoder),
        sources = decoder.readList(MAX_FACT_SOURCES) { decoder.readString(MAX_IDENTIFIER_BYTES) },
        correction = if (decoder.readBoolean()) decoder.readString(MAX_FACT_VALUE_BYTES) else null,
        has_conflict = decoder.readBoolean(),
        needs_new_source = decoder.readBoolean(),
    )
    validateWorkspaceFactView(value)
    return value
}

private fun validateWorkspaceExportView(value: WorkspaceExportView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeWorkspaceExportView(encoder: CoreStatusPayloadEncoder, value: WorkspaceExportView) {
    validateWorkspaceExportView(value)
    encoder.putString(value.request_id, MAX_IDENTIFIER_BYTES)
    encoder.putString(value.workspace_id, MAX_IDENTIFIER_BYTES)
    encoder.putULong(value.revision)
    encodeWorkspaceExportFormat(encoder, value.format)
    encoder.putString(value.content, MAX_EXPORT_CONTENT_BYTES)
}

private fun decodeWorkspaceExportView(decoder: CoreStatusPayloadDecoder): WorkspaceExportView {
    val value = WorkspaceExportView(
        request_id = decoder.readString(MAX_IDENTIFIER_BYTES),
        workspace_id = decoder.readString(MAX_IDENTIFIER_BYTES),
        revision = decoder.readULong(),
        format = decodeWorkspaceExportFormat(decoder),
        content = decoder.readString(MAX_EXPORT_CONTENT_BYTES),
    )
    validateWorkspaceExportView(value)
    return value
}

private fun validateLibrarySourceView(value: LibrarySourceView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeLibrarySourceView(encoder: CoreStatusPayloadEncoder, value: LibrarySourceView) {
    validateLibrarySourceView(value)
    encoder.putString(value.source_id, MAX_IDENTIFIER_BYTES)
    encoder.putString(value.title, MAX_WORKSPACE_TITLE_BYTES)
    encoder.putString(value.host, MAX_SOURCE_HOST_BYTES)
    encoder.putULong(value.observed_at_epoch_ms)
}

private fun decodeLibrarySourceView(decoder: CoreStatusPayloadDecoder): LibrarySourceView {
    val value = LibrarySourceView(
        source_id = decoder.readString(MAX_IDENTIFIER_BYTES),
        title = decoder.readString(MAX_WORKSPACE_TITLE_BYTES),
        host = decoder.readString(MAX_SOURCE_HOST_BYTES),
        observed_at_epoch_ms = decoder.readULong(),
    )
    validateLibrarySourceView(value)
    return value
}

private fun validateLibraryEntryView(value: LibraryEntryView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeLibraryEntryView(encoder: CoreStatusPayloadEncoder, value: LibraryEntryView) {
    validateLibraryEntryView(value)
    encoder.putString(value.entry_id, MAX_IDENTIFIER_BYTES)
    encoder.putULong(value.revision)
    encoder.putString(value.collection_id, MAX_IDENTIFIER_BYTES)
    encoder.putString(value.collection_name, MAX_WORKSPACE_DISPLAY_NAME_BYTES)
    encoder.putString(value.source_workspace_id, MAX_IDENTIFIER_BYTES)
    encoder.putULong(value.source_workspace_revision)
    encoder.putString(value.source_fact_id, MAX_IDENTIFIER_BYTES)
    encoder.putString(value.field, MAX_FACT_FIELD_BYTES)
    encoder.putString(value.original_value, MAX_FACT_VALUE_BYTES)
    run {
        val optional = value.correction
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encoder.putString(optional, MAX_FACT_VALUE_BYTES)
        }
    }
    encodeWorkspaceFactKind(encoder, value.kind)
    encoder.putLength(value.sources.size, MAX_LIBRARY_SOURCES)
    for (item in value.sources) {
        encodeLibrarySourceView(encoder, item)
    }
    encoder.putULong(value.captured_at_epoch_ms)
    encoder.putULong(value.last_checked_epoch_ms)
    encoder.putBoolean(value.has_conflict)
}

private fun decodeLibraryEntryView(decoder: CoreStatusPayloadDecoder): LibraryEntryView {
    val value = LibraryEntryView(
        entry_id = decoder.readString(MAX_IDENTIFIER_BYTES),
        revision = decoder.readULong(),
        collection_id = decoder.readString(MAX_IDENTIFIER_BYTES),
        collection_name = decoder.readString(MAX_WORKSPACE_DISPLAY_NAME_BYTES),
        source_workspace_id = decoder.readString(MAX_IDENTIFIER_BYTES),
        source_workspace_revision = decoder.readULong(),
        source_fact_id = decoder.readString(MAX_IDENTIFIER_BYTES),
        field = decoder.readString(MAX_FACT_FIELD_BYTES),
        original_value = decoder.readString(MAX_FACT_VALUE_BYTES),
        correction = if (decoder.readBoolean()) decoder.readString(MAX_FACT_VALUE_BYTES) else null,
        kind = decodeWorkspaceFactKind(decoder),
        sources = decoder.readList(MAX_LIBRARY_SOURCES) { decodeLibrarySourceView(decoder) },
        captured_at_epoch_ms = decoder.readULong(),
        last_checked_epoch_ms = decoder.readULong(),
        has_conflict = decoder.readBoolean(),
    )
    validateLibraryEntryView(value)
    return value
}

private fun validateLibrarySearchHitView(value: LibrarySearchHitView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeLibrarySearchHitView(encoder: CoreStatusPayloadEncoder, value: LibrarySearchHitView) {
    validateLibrarySearchHitView(value)
    encoder.putString(value.entry_id, MAX_IDENTIFIER_BYTES)
    encoder.putULong(value.age_ms)
}

private fun decodeLibrarySearchHitView(decoder: CoreStatusPayloadDecoder): LibrarySearchHitView {
    val value = LibrarySearchHitView(
        entry_id = decoder.readString(MAX_IDENTIFIER_BYTES),
        age_ms = decoder.readULong(),
    )
    validateLibrarySearchHitView(value)
    return value
}

private fun validateLibrarySearchView(value: LibrarySearchView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeLibrarySearchView(encoder: CoreStatusPayloadEncoder, value: LibrarySearchView) {
    validateLibrarySearchView(value)
    encoder.putString(value.request_id, MAX_IDENTIFIER_BYTES)
    encoder.putString(value.query, MAX_LIBRARY_QUERY_BYTES)
    encoder.putULong(value.library_revision)
    encoder.putLength(value.hits.size, MAX_LIBRARY_SEARCH_RESULTS)
    for (item in value.hits) {
        encodeLibrarySearchHitView(encoder, item)
    }
}

private fun decodeLibrarySearchView(decoder: CoreStatusPayloadDecoder): LibrarySearchView {
    val value = LibrarySearchView(
        request_id = decoder.readString(MAX_IDENTIFIER_BYTES),
        query = decoder.readString(MAX_LIBRARY_QUERY_BYTES),
        library_revision = decoder.readULong(),
        hits = decoder.readList(MAX_LIBRARY_SEARCH_RESULTS) { decodeLibrarySearchHitView(decoder) },
    )
    validateLibrarySearchView(value)
    return value
}

private fun validateLibraryRefreshSourceView(value: LibraryRefreshSourceView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeLibraryRefreshSourceView(encoder: CoreStatusPayloadEncoder, value: LibraryRefreshSourceView) {
    validateLibraryRefreshSourceView(value)
    encoder.putString(value.source_id, MAX_IDENTIFIER_BYTES)
    encoder.putString(value.title, MAX_WORKSPACE_TITLE_BYTES)
    encoder.putString(value.host, MAX_SOURCE_HOST_BYTES)
}

private fun decodeLibraryRefreshSourceView(decoder: CoreStatusPayloadDecoder): LibraryRefreshSourceView {
    val value = LibraryRefreshSourceView(
        source_id = decoder.readString(MAX_IDENTIFIER_BYTES),
        title = decoder.readString(MAX_WORKSPACE_TITLE_BYTES),
        host = decoder.readString(MAX_SOURCE_HOST_BYTES),
    )
    validateLibraryRefreshSourceView(value)
    return value
}

private fun validateLibraryRefreshPreviewView(value: LibraryRefreshPreviewView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeLibraryRefreshPreviewView(encoder: CoreStatusPayloadEncoder, value: LibraryRefreshPreviewView) {
    validateLibraryRefreshPreviewView(value)
    encoder.putString(value.preview_id, MAX_LIBRARY_REFRESH_PREVIEW_ID_BYTES)
    encoder.putString(value.collection_id, MAX_IDENTIFIER_BYTES)
    encoder.putULong(value.library_revision)
    encoder.putULong(value.source_workspace_revision)
    encodeTaskProviderRoute(encoder, value.provider_route)
    encoder.putUInt(value.navigation_count)
    encoder.putUInt(value.observation_count)
    encoder.putLength(value.sources.size, MAX_LIBRARY_REFRESH_SOURCES)
    for (item in value.sources) {
        encodeLibraryRefreshSourceView(encoder, item)
    }
}

private fun decodeLibraryRefreshPreviewView(decoder: CoreStatusPayloadDecoder): LibraryRefreshPreviewView {
    val value = LibraryRefreshPreviewView(
        preview_id = decoder.readString(MAX_LIBRARY_REFRESH_PREVIEW_ID_BYTES),
        collection_id = decoder.readString(MAX_IDENTIFIER_BYTES),
        library_revision = decoder.readULong(),
        source_workspace_revision = decoder.readULong(),
        provider_route = decodeTaskProviderRoute(decoder),
        navigation_count = decoder.readUInt(),
        observation_count = decoder.readUInt(),
        sources = decoder.readList(MAX_LIBRARY_REFRESH_SOURCES) { decodeLibraryRefreshSourceView(decoder) },
    )
    validateLibraryRefreshPreviewView(value)
    return value
}

private fun validateLibraryRefreshResultItemView(value: LibraryRefreshResultItemView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeLibraryRefreshResultItemView(encoder: CoreStatusPayloadEncoder, value: LibraryRefreshResultItemView) {
    validateLibraryRefreshResultItemView(value)
    encoder.putString(value.source_id, MAX_IDENTIFIER_BYTES)
    encodeLibraryRefreshDisposition(encoder, value.disposition)
}

private fun decodeLibraryRefreshResultItemView(decoder: CoreStatusPayloadDecoder): LibraryRefreshResultItemView {
    val value = LibraryRefreshResultItemView(
        source_id = decoder.readString(MAX_IDENTIFIER_BYTES),
        disposition = decodeLibraryRefreshDisposition(decoder),
    )
    validateLibraryRefreshResultItemView(value)
    return value
}

private fun validateLibraryRefreshResultView(value: LibraryRefreshResultView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeLibraryRefreshResultView(encoder: CoreStatusPayloadEncoder, value: LibraryRefreshResultView) {
    validateLibraryRefreshResultView(value)
    encoder.putString(value.preview_id, MAX_LIBRARY_REFRESH_PREVIEW_ID_BYTES)
    encoder.putString(value.collection_id, MAX_IDENTIFIER_BYTES)
    encoder.putLength(value.items.size, MAX_LIBRARY_REFRESH_SOURCES)
    for (item in value.items) {
        encodeLibraryRefreshResultItemView(encoder, item)
    }
}

private fun decodeLibraryRefreshResultView(decoder: CoreStatusPayloadDecoder): LibraryRefreshResultView {
    val value = LibraryRefreshResultView(
        preview_id = decoder.readString(MAX_LIBRARY_REFRESH_PREVIEW_ID_BYTES),
        collection_id = decoder.readString(MAX_IDENTIFIER_BYTES),
        items = decoder.readList(MAX_LIBRARY_REFRESH_SOURCES) { decodeLibraryRefreshResultItemView(decoder) },
    )
    validateLibraryRefreshResultView(value)
    return value
}

private fun validateLibraryViewState(value: LibraryViewState) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeLibraryViewState(encoder: CoreStatusPayloadEncoder, value: LibraryViewState) {
    validateLibraryViewState(value)
    encodeLibraryAvailability(encoder, value.availability)
    encoder.putULong(value.revision)
    encoder.putLength(value.entries.size, MAX_LIBRARY_ENTRIES)
    for (item in value.entries) {
        encodeLibraryEntryView(encoder, item)
    }
    run {
        val optional = value.search
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encodeLibrarySearchView(encoder, optional)
        }
    }
    encoder.putLength(value.refresh_previews.size, MAX_WORKSPACES)
    for (item in value.refresh_previews) {
        encodeLibraryRefreshPreviewView(encoder, item)
    }
    encoder.putLength(value.refresh_results.size, MAX_LIBRARY_REFRESH_RESULTS)
    for (item in value.refresh_results) {
        encodeLibraryRefreshResultView(encoder, item)
    }
}

private fun decodeLibraryViewState(decoder: CoreStatusPayloadDecoder): LibraryViewState {
    val value = LibraryViewState(
        availability = decodeLibraryAvailability(decoder),
        revision = decoder.readULong(),
        entries = decoder.readList(MAX_LIBRARY_ENTRIES) { decodeLibraryEntryView(decoder) },
        search = if (decoder.readBoolean()) decodeLibrarySearchView(decoder) else null,
        refresh_previews = decoder.readList(MAX_WORKSPACES) { decodeLibraryRefreshPreviewView(decoder) },
        refresh_results = decoder.readList(MAX_LIBRARY_REFRESH_RESULTS) { decodeLibraryRefreshResultView(decoder) },
    )
    validateLibraryViewState(value)
    return value
}

private fun validateLibraryExportView(value: LibraryExportView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeLibraryExportView(encoder: CoreStatusPayloadEncoder, value: LibraryExportView) {
    validateLibraryExportView(value)
    encoder.putString(value.request_id, MAX_IDENTIFIER_BYTES)
    encoder.putULong(value.library_revision)
    run {
        val optional = value.collection_id
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encoder.putString(optional, MAX_IDENTIFIER_BYTES)
        }
    }
    encodeWorkspaceExportFormat(encoder, value.format)
    encoder.putString(value.content, MAX_EXPORT_CONTENT_BYTES)
}

private fun decodeLibraryExportView(decoder: CoreStatusPayloadDecoder): LibraryExportView {
    val value = LibraryExportView(
        request_id = decoder.readString(MAX_IDENTIFIER_BYTES),
        library_revision = decoder.readULong(),
        collection_id = if (decoder.readBoolean()) decoder.readString(MAX_IDENTIFIER_BYTES) else null,
        format = decodeWorkspaceExportFormat(decoder),
        content = decoder.readString(MAX_EXPORT_CONTENT_BYTES),
    )
    validateLibraryExportView(value)
    return value
}

private fun validateMemoryWorkspaceView(value: MemoryWorkspaceView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeMemoryWorkspaceView(encoder: CoreStatusPayloadEncoder, value: MemoryWorkspaceView) {
    validateMemoryWorkspaceView(value)
    encoder.putString(value.workspace_id, MAX_IDENTIFIER_BYTES)
    encoder.putString(value.display_name, MAX_WORKSPACE_DISPLAY_NAME_BYTES)
}

private fun decodeMemoryWorkspaceView(decoder: CoreStatusPayloadDecoder): MemoryWorkspaceView {
    val value = MemoryWorkspaceView(
        workspace_id = decoder.readString(MAX_IDENTIFIER_BYTES),
        display_name = decoder.readString(MAX_WORKSPACE_DISPLAY_NAME_BYTES),
    )
    validateMemoryWorkspaceView(value)
    return value
}

private fun validateMemoryRecordView(value: MemoryRecordView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeMemoryRecordView(encoder: CoreStatusPayloadEncoder, value: MemoryRecordView) {
    validateMemoryRecordView(value)
    encoder.putString(value.memory_id, MAX_IDENTIFIER_BYTES)
    encoder.putULong(value.revision)
    encoder.putString(value.statement, MAX_MEMORY_STATEMENT_BYTES)
    encodeMemorySourceKind(encoder, value.source_kind)
    run {
        val optional = value.source_task_id
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encoder.putString(optional, MAX_IDENTIFIER_BYTES)
        }
    }
    run {
        val optional = value.source_workspace
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encodeMemoryWorkspaceView(encoder, optional)
        }
    }
    encodeMemoryScopeKind(encoder, value.scope_kind)
    run {
        val optional = value.scope_workspace
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encodeMemoryWorkspaceView(encoder, optional)
        }
    }
    encodeMemorySensitivity(encoder, value.sensitivity)
    encoder.putULong(value.created_at_epoch_ms)
    encoder.putULong(value.updated_at_epoch_ms)
    encoder.putULong(value.reviewed_at_epoch_ms)
    encoder.putULong(value.expires_at_epoch_ms)
}

private fun decodeMemoryRecordView(decoder: CoreStatusPayloadDecoder): MemoryRecordView {
    val value = MemoryRecordView(
        memory_id = decoder.readString(MAX_IDENTIFIER_BYTES),
        revision = decoder.readULong(),
        statement = decoder.readString(MAX_MEMORY_STATEMENT_BYTES),
        source_kind = decodeMemorySourceKind(decoder),
        source_task_id = if (decoder.readBoolean()) decoder.readString(MAX_IDENTIFIER_BYTES) else null,
        source_workspace = if (decoder.readBoolean()) decodeMemoryWorkspaceView(decoder) else null,
        scope_kind = decodeMemoryScopeKind(decoder),
        scope_workspace = if (decoder.readBoolean()) decodeMemoryWorkspaceView(decoder) else null,
        sensitivity = decodeMemorySensitivity(decoder),
        created_at_epoch_ms = decoder.readULong(),
        updated_at_epoch_ms = decoder.readULong(),
        reviewed_at_epoch_ms = decoder.readULong(),
        expires_at_epoch_ms = decoder.readULong(),
    )
    validateMemoryRecordView(value)
    return value
}

private fun validateMemorySearchHitView(value: MemorySearchHitView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeMemorySearchHitView(encoder: CoreStatusPayloadEncoder, value: MemorySearchHitView) {
    validateMemorySearchHitView(value)
    encoder.putString(value.memory_id, MAX_IDENTIFIER_BYTES)
}

private fun decodeMemorySearchHitView(decoder: CoreStatusPayloadDecoder): MemorySearchHitView {
    val value = MemorySearchHitView(
        memory_id = decoder.readString(MAX_IDENTIFIER_BYTES),
    )
    validateMemorySearchHitView(value)
    return value
}

private fun validateMemorySearchView(value: MemorySearchView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeMemorySearchView(encoder: CoreStatusPayloadEncoder, value: MemorySearchView) {
    validateMemorySearchView(value)
    encoder.putString(value.request_id, MAX_IDENTIFIER_BYTES)
    encoder.putString(value.query, MAX_MEMORY_QUERY_BYTES)
    encoder.putULong(value.memory_revision)
    encoder.putLength(value.hits.size, MAX_MEMORY_SEARCH_RESULTS)
    for (item in value.hits) {
        encodeMemorySearchHitView(encoder, item)
    }
}

private fun decodeMemorySearchView(decoder: CoreStatusPayloadDecoder): MemorySearchView {
    val value = MemorySearchView(
        request_id = decoder.readString(MAX_IDENTIFIER_BYTES),
        query = decoder.readString(MAX_MEMORY_QUERY_BYTES),
        memory_revision = decoder.readULong(),
        hits = decoder.readList(MAX_MEMORY_SEARCH_RESULTS) { decodeMemorySearchHitView(decoder) },
    )
    validateMemorySearchView(value)
    return value
}

private fun validateMemoryViewState(value: MemoryViewState) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeMemoryViewState(encoder: CoreStatusPayloadEncoder, value: MemoryViewState) {
    validateMemoryViewState(value)
    encodeMemoryAvailability(encoder, value.availability)
    encoder.putULong(value.revision)
    encoder.putLength(value.records.size, MAX_MEMORY_RECORDS)
    for (item in value.records) {
        encodeMemoryRecordView(encoder, item)
    }
    run {
        val optional = value.search
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encodeMemorySearchView(encoder, optional)
        }
    }
}

private fun decodeMemoryViewState(decoder: CoreStatusPayloadDecoder): MemoryViewState {
    val value = MemoryViewState(
        availability = decodeMemoryAvailability(decoder),
        revision = decoder.readULong(),
        records = decoder.readList(MAX_MEMORY_RECORDS) { decodeMemoryRecordView(decoder) },
        search = if (decoder.readBoolean()) decodeMemorySearchView(decoder) else null,
    )
    validateMemoryViewState(value)
    return value
}

private fun validateAuthAccountView(value: AuthAccountView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeAuthAccountView(encoder: CoreStatusPayloadEncoder, value: AuthAccountView) {
    validateAuthAccountView(value)
    encoder.putString(value.account_id, MAX_IDENTIFIER_BYTES)
    run {
        val optional = value.display_name
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encoder.putString(optional, MAX_AUTH_DISPLAY_NAME_BYTES)
        }
    }
    run {
        val optional = value.email
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encoder.putString(optional, MAX_AUTH_EMAIL_BYTES)
        }
    }
    encodeAuthProvider(encoder, value.method)
}

private fun decodeAuthAccountView(decoder: CoreStatusPayloadDecoder): AuthAccountView {
    val value = AuthAccountView(
        account_id = decoder.readString(MAX_IDENTIFIER_BYTES),
        display_name = if (decoder.readBoolean()) decoder.readString(MAX_AUTH_DISPLAY_NAME_BYTES) else null,
        email = if (decoder.readBoolean()) decoder.readString(MAX_AUTH_EMAIL_BYTES) else null,
        method = decodeAuthProvider(decoder),
    )
    validateAuthAccountView(value)
    return value
}

private fun validateAuthFailure(value: AuthFailure) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeAuthFailure(encoder: CoreStatusPayloadEncoder, value: AuthFailure) {
    validateAuthFailure(value)
    encodeAuthFailureCode(encoder, value.code)
    encoder.putBoolean(value.retryable)
}

private fun decodeAuthFailure(decoder: CoreStatusPayloadDecoder): AuthFailure {
    val value = AuthFailure(
        code = decodeAuthFailureCode(decoder),
        retryable = decoder.readBoolean(),
    )
    validateAuthFailure(value)
    return value
}

private fun validateAuthMethodView(value: AuthMethodView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeAuthMethodView(encoder: CoreStatusPayloadEncoder, value: AuthMethodView) {
    validateAuthMethodView(value)
    encodeAuthProvider(encoder, value.provider)
    encodeAuthMethodAvailability(encoder, value.availability)
}

private fun decodeAuthMethodView(decoder: CoreStatusPayloadDecoder): AuthMethodView {
    val value = AuthMethodView(
        provider = decodeAuthProvider(decoder),
        availability = decodeAuthMethodAvailability(decoder),
    )
    validateAuthMethodView(value)
    return value
}

private fun validateEntitlementView(value: EntitlementView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeEntitlementView(encoder: CoreStatusPayloadEncoder, value: EntitlementView) {
    validateEntitlementView(value)
    encoder.putString(value.plan_id, MAX_IDENTIFIER_BYTES)
    encoder.putULong(value.credits_granted)
    encoder.putULong(value.credits_remaining)
    encoder.putULong(value.next_renewal_epoch_seconds)
    encoder.putULong(value.valid_until_epoch_seconds)
}

private fun decodeEntitlementView(decoder: CoreStatusPayloadDecoder): EntitlementView {
    val value = EntitlementView(
        plan_id = decoder.readString(MAX_IDENTIFIER_BYTES),
        credits_granted = decoder.readULong(),
        credits_remaining = decoder.readULong(),
        next_renewal_epoch_seconds = decoder.readULong(),
        valid_until_epoch_seconds = decoder.readULong(),
    )
    validateEntitlementView(value)
    return value
}

private fun validateAuthViewState(value: AuthViewState) {
    if ((value.phase == AuthPhase.SIGNED_IN) != (value.account != null)) {
        fail(CoreStatusPayloadCodecError.MALFORMED)
    }
    if ((value.phase == AuthPhase.LINK_SENT) != (value.pending_email != null)) {
        fail(CoreStatusPayloadCodecError.MALFORMED)
    }
    if ((value.phase == AuthPhase.FAILED) != (value.failure != null)) {
        fail(CoreStatusPayloadCodecError.MALFORMED)
    }
}

private fun encodeAuthViewState(encoder: CoreStatusPayloadEncoder, value: AuthViewState) {
    validateAuthViewState(value)
    encodeAuthPhase(encoder, value.phase)
    run {
        val optional = value.account
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encodeAuthAccountView(encoder, optional)
        }
    }
    run {
        val optional = value.pending_email
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encoder.putString(optional, MAX_AUTH_EMAIL_BYTES)
        }
    }
    run {
        val optional = value.failure
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encodeAuthFailure(encoder, optional)
        }
    }
    encoder.putLength(value.methods.size, MAX_AUTH_METHODS)
    for (item in value.methods) {
        encodeAuthMethodView(encoder, item)
    }
    run {
        val optional = value.entitlement
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encodeEntitlementView(encoder, optional)
        }
    }
}

private fun decodeAuthViewState(decoder: CoreStatusPayloadDecoder): AuthViewState {
    val value = AuthViewState(
        phase = decodeAuthPhase(decoder),
        account = if (decoder.readBoolean()) decodeAuthAccountView(decoder) else null,
        pending_email = if (decoder.readBoolean()) decoder.readString(MAX_AUTH_EMAIL_BYTES) else null,
        failure = if (decoder.readBoolean()) decodeAuthFailure(decoder) else null,
        methods = decoder.readList(MAX_AUTH_METHODS) { decodeAuthMethodView(decoder) },
        entitlement = if (decoder.readBoolean()) decodeEntitlementView(decoder) else null,
    )
    validateAuthViewState(value)
    return value
}

private fun validateAssetRefusal(value: AssetRefusal) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeAssetRefusal(encoder: CoreStatusPayloadEncoder, value: AssetRefusal) {
    validateAssetRefusal(value)
    encodeAssetRefusalView(encoder, value.reason)
    encoder.putBoolean(value.retryable)
}

private fun decodeAssetRefusal(decoder: CoreStatusPayloadDecoder): AssetRefusal {
    val value = AssetRefusal(
        reason = decodeAssetRefusalView(decoder),
        retryable = decoder.readBoolean(),
    )
    validateAssetRefusal(value)
    return value
}

private fun validateAssetViewState(value: AssetViewState) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeAssetViewState(encoder: CoreStatusPayloadEncoder, value: AssetViewState) {
    validateAssetViewState(value)
    encoder.putString(value.asset_id, MAX_IDENTIFIER_BYTES)
    encoder.putString(value.asset_revision, MAX_IDENTIFIER_BYTES)
    encodeAssetKindView(encoder, value.kind)
    encodeAssetPresenceView(encoder, value.presence)
    encoder.putULong(value.written_bytes)
    encoder.putULong(value.total_bytes)
    encoder.putUInt(value.attempts)
    run {
        val optional = value.refusal
        if (optional == null) {
            encoder.putBoolean(false)
        } else {
            encoder.putBoolean(true)
            encodeAssetRefusal(encoder, optional)
        }
    }
    encoder.putULong(value.waiting_until_monotonic_ms)
}

private fun decodeAssetViewState(decoder: CoreStatusPayloadDecoder): AssetViewState {
    val value = AssetViewState(
        asset_id = decoder.readString(MAX_IDENTIFIER_BYTES),
        asset_revision = decoder.readString(MAX_IDENTIFIER_BYTES),
        kind = decodeAssetKindView(decoder),
        presence = decodeAssetPresenceView(decoder),
        written_bytes = decoder.readULong(),
        total_bytes = decoder.readULong(),
        attempts = decoder.readUInt(),
        refusal = if (decoder.readBoolean()) decodeAssetRefusal(decoder) else null,
        waiting_until_monotonic_ms = decoder.readULong(),
    )
    validateAssetViewState(value)
    return value
}

private fun validateAssetDeliveryView(value: AssetDeliveryView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeAssetDeliveryView(encoder: CoreStatusPayloadEncoder, value: AssetDeliveryView) {
    validateAssetDeliveryView(value)
    encoder.putBoolean(value.platform_supported)
    encodeAssetNetworkCostView(encoder, value.network_cost)
    encoder.putBoolean(value.metered_permitted)
    encoder.putLength(value.assets.size, MAX_ASSETS)
    for (item in value.assets) {
        encodeAssetViewState(encoder, item)
    }
}

private fun decodeAssetDeliveryView(decoder: CoreStatusPayloadDecoder): AssetDeliveryView {
    val value = AssetDeliveryView(
        platform_supported = decoder.readBoolean(),
        network_cost = decodeAssetNetworkCostView(decoder),
        metered_permitted = decoder.readBoolean(),
        assets = decoder.readList(MAX_ASSETS) { decodeAssetViewState(decoder) },
    )
    validateAssetDeliveryView(value)
    return value
}

private fun validateCustomModelSpecView(value: CustomModelSpecView) {
    @Suppress("UNUSED_VARIABLE") val checked = value
}

private fun encodeCustomModelSpecView(encoder: CoreStatusPayloadEncoder, value: CustomModelSpecView) {
    validateCustomModelSpecView(value)
    encoder.putString(value.model_id, MAX_MODEL_ID_BYTES)
    encoder.putString(value.display_name, MAX_MODEL_DISPLAY_NAME_BYTES)
    encoder.putUInt(value.context_window)
    encoder.putUInt(value.max_output_tokens)
    encoder.putBoolean(value.reasoning)
    encoder.putBoolean(value.tool_calling)
}

private fun decodeCustomModelSpecView(decoder: CoreStatusPayloadDecoder): CustomModelSpecView {
    val value = CustomModelSpecView(
        model_id = decoder.readString(MAX_MODEL_ID_BYTES),
        display_name = decoder.readString(MAX_MODEL_DISPLAY_NAME_BYTES),
        context_window = decoder.readUInt(),
        max_output_tokens = decoder.readUInt(),
        reasoning = decoder.readBoolean(),
        tool_calling = decoder.readBoolean(),
    )
    validateCustomModelSpecView(value)
    return value
}

fun encodeCoreStatusPayload(value: CoreStatus): CoreStatusPayloadEncodeResult = try {
    val encoder = CoreStatusPayloadEncoder()
    encoder.putRaw(CORE_STATUS_PAYLOAD_MAGIC)
    encoder.putUInt(CORE_STATUS_PAYLOAD_SCHEMA_VERSION)
    encodeCoreStatus(encoder, value)
    CoreStatusPayloadEncodeResult.Success(encoder.finish())
} catch (failure: CoreStatusCodecFailure) {
    CoreStatusPayloadEncodeResult.Failure(failure.reason)
}

fun decodeCoreStatusPayload(bytes: ByteArray): CoreStatusPayloadDecodeResult {
    if (bytes.size > MAX_EVENT_PAYLOAD_BYTES) {
        return CoreStatusPayloadDecodeResult.Failure(CoreStatusPayloadCodecError.SIZE_LIMIT)
    }
    return try {
        val decoder = CoreStatusPayloadDecoder(bytes)
        if (!decoder.take(CORE_STATUS_PAYLOAD_MAGIC.size).contentEquals(CORE_STATUS_PAYLOAD_MAGIC)) {
            fail(CoreStatusPayloadCodecError.INVALID_MAGIC)
        }
        if (decoder.readUInt() != CORE_STATUS_PAYLOAD_SCHEMA_VERSION) {
            fail(CoreStatusPayloadCodecError.UNSUPPORTED_VERSION)
        }
        val value = decodeCoreStatus(decoder)
        if (decoder.offset != bytes.size) fail(CoreStatusPayloadCodecError.TRAILING_BYTES)
        CoreStatusPayloadDecodeResult.Success(value)
    } catch (failure: CoreStatusCodecFailure) {
        CoreStatusPayloadDecodeResult.Failure(failure.reason)
    }
}
