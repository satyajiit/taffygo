// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Generated from schema/contract.json. Do not edit.
// Contract core_api 3.49 state payload.

use crate::{
    ActionApprovalView,
    AssetDeliveryView,
    AssetKindView,
    AssetNetworkCostView,
    AssetPresenceView,
    AssetRefusal,
    AssetRefusalView,
    AssetViewState,
    AssistantAbilityView,
    AssistantConfigurationView,
    AuthAccountView,
    AuthFailure,
    AuthFailureCode,
    AuthMethodAvailability,
    AuthMethodView,
    AuthPhase,
    AuthProvider,
    AuthViewState,
    BuiltinSkillAvailabilityView,
    BuiltinSkillIdView,
    BuiltinSkillReferenceView,
    BuiltinSkillView,
    CatalogLayerView,
    CoreAvailability,
    CoreFailure,
    CoreFailureCode,
    CoreStatus,
    CoreStatusProjectionFamily,
    CoreStatusProjectionMode,
    CoreStatusProjectionOmission,
    CustomModelSpecView,
    EntitlementView,
    InputModalityView,
    LibraryAvailability,
    LibraryEntryView,
    LibraryExportView,
    LibraryRefreshDisposition,
    LibraryRefreshPreviewView,
    LibraryRefreshResultItemView,
    LibraryRefreshResultView,
    LibraryRefreshSourceView,
    LibrarySearchHitView,
    LibrarySearchView,
    LibrarySourceView,
    LibraryViewState,
    MemoryAvailability,
    MemoryRecordView,
    MemoryScopeKind,
    MemorySearchHitView,
    MemorySearchView,
    MemorySensitivity,
    MemorySourceKind,
    MemoryViewState,
    MemoryWorkspaceView,
    ModelRoleView,
    PersonalityPresetView,
    ProbeEndpointView,
    ProviderAuthMethodView,
    ProviderCredentialStateView,
    ProviderModelView,
    ProviderOriginView,
    ProviderPresentationView,
    ProviderProbeVerdictView,
    ProviderProbeView,
    ProviderRefusalStateView,
    ProviderRefusalView,
    ProviderRosterEntry,
    SavedDataAvailability,
    SavedDetailView,
    SavedDetailsView,
    SavedSignInView,
    SavedSignInsView,
    ServerKindView,
    SiteSkillArgumentKind,
    SiteSkillObservedArgument,
    SiteSkillObservedStep,
    SiteSkillProvenanceView,
    SiteSkillSemanticTarget,
    SiteSkillStatusView,
    SiteSkillView,
    StoredCredentialView,
    TaskActivityKind,
    TaskActivityView,
    TaskArtifactKind,
    TaskArtifactView,
    TaskControlKind,
    TaskPhase,
    TaskProviderRoute,
    TaskTemplateId,
    TaskViewState,
    ThinkingLevelView,
    ThinkingPreferenceView,
    WorkspaceDeletionPreviewView,
    WorkspaceExportFormat,
    WorkspaceExportView,
    WorkspaceFactKind,
    WorkspaceFactView,
    WorkspacePhase,
    WorkspaceSourceView,
    WorkspaceViewState,
    MAX_ACTIVE_TASKS,
    MAX_ASSETS,
    MAX_ASSISTANT_ABILITIES,
    MAX_AUTH_DISPLAY_NAME_BYTES,
    MAX_AUTH_EMAIL_BYTES,
    MAX_AUTH_METHODS,
    MAX_BUILTIN_SKILLS,
    MAX_CORE_STATUS_PROJECTION_OMISSIONS,
    MAX_CUSTOM_MODEL_ENTRIES,
    MAX_EVENT_PAYLOAD_BYTES,
    MAX_EXPORT_CONTENT_BYTES,
    MAX_FACT_FIELD_BYTES,
    MAX_FACT_SOURCES,
    MAX_FACT_VALUE_BYTES,
    MAX_IDENTIFIER_BYTES,
    MAX_LIBRARY_ENTRIES,
    MAX_LIBRARY_QUERY_BYTES,
    MAX_LIBRARY_REFRESH_PREVIEW_ID_BYTES,
    MAX_LIBRARY_REFRESH_RESULTS,
    MAX_LIBRARY_REFRESH_SOURCES,
    MAX_LIBRARY_SEARCH_RESULTS,
    MAX_LIBRARY_SOURCES,
    MAX_MEMORY_QUERY_BYTES,
    MAX_MEMORY_RECORDS,
    MAX_MEMORY_SEARCH_RESULTS,
    MAX_MEMORY_STATEMENT_BYTES,
    MAX_MESSAGE_KEY_BYTES,
    MAX_MODEL_DISPLAY_NAME_BYTES,
    MAX_MODEL_ID_BYTES,
    MAX_MODEL_INPUT_MODALITIES,
    MAX_MODEL_ROLES,
    MAX_MODEL_THINKING_LEVELS,
    MAX_PERSONALITY_SCALE,
    MAX_PROGRESS_BASIS_POINTS,
    MAX_PROVIDER_DISPLAY_NAME_BYTES,
    MAX_PROVIDER_ENDPOINT_BYTES,
    MAX_PROVIDER_ID_BYTES,
    MAX_PROVIDER_MODEL_ENTRIES,
    MAX_PROVIDER_PRESENTATION_BYTES,
    MAX_PROVIDER_ROSTER_ENTRIES,
    MAX_SAVED_DETAILS,
    MAX_SAVED_DETAIL_ADDRESS_BYTES,
    MAX_SAVED_DETAIL_COUNTRY_BYTES,
    MAX_SAVED_DETAIL_EMAIL_BYTES,
    MAX_SAVED_DETAIL_NAME_BYTES,
    MAX_SAVED_DETAIL_PHONE_BYTES,
    MAX_SAVED_DETAIL_POSTCODE_BYTES,
    MAX_SAVED_SIGN_INS,
    MAX_SAVED_SIGN_IN_SITE_BYTES,
    MAX_SAVED_SIGN_IN_USERNAME_BYTES,
    MAX_SITE_SKILLS,
    MAX_SKILL_ARGUMENTS_PER_STEP,
    MAX_SKILL_ID_BYTES,
    MAX_SKILL_ORIGIN_BYTES,
    MAX_SKILL_STEPS,
    MAX_SKILL_TOOL_NAME_BYTES,
    MAX_SOURCE_HOST_BYTES,
    MAX_TASK_ACTIVITY,
    MAX_TASK_ARTIFACTS,
    MAX_TASK_CONTROLS,
    MAX_TASK_GOAL_BYTES,
    MAX_USER_INPUT_ANSWER_BYTES,
    MAX_WORKSPACES,
    MAX_WORKSPACE_CONFIRMATION_TOKEN_BYTES,
    MAX_WORKSPACE_DISPLAY_NAME_BYTES,
    MAX_WORKSPACE_FACTS,
    MAX_WORKSPACE_SOURCES,
    MAX_WORKSPACE_TITLE_BYTES,
};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CoreStatusPayloadCodecError {
    SizeLimit,
    CollectionLimit,
    StringLimit,
    ValueLimit,
    LengthOverflow,
    Truncated,
    InvalidMagic,
    UnsupportedVersion,
    InvalidBoolean,
    InvalidEnum,
    InvalidUtf8,
    Malformed,
    TrailingBytes,
}

pub const CORE_STATUS_PAYLOAD_MAGIC: &[u8] = &[84, 65, 70, 70, 89, 83, 84, 65];
pub const CORE_STATUS_PAYLOAD_SCHEMA_VERSION: u32 = 33;

struct CoreStatusPayloadEncoder {
    bytes: Option<Vec<u8>>,
    length: usize,
}

impl CoreStatusPayloadEncoder {
    fn put_raw(&mut self, value: &[u8]) -> Result<(), CoreStatusPayloadCodecError> {
        let length = self.length.checked_add(value.len())
            .ok_or(CoreStatusPayloadCodecError::LengthOverflow)?;
        if length > MAX_EVENT_PAYLOAD_BYTES {
            return Err(CoreStatusPayloadCodecError::SizeLimit);
        }
        if let Some(bytes) = self.bytes.as_mut() {
            bytes.extend_from_slice(value);
        }
        self.length = length;
        Ok(())
    }

    fn put_bool(&mut self, value: bool) -> Result<(), CoreStatusPayloadCodecError> {
        self.put_raw(&[u8::from(value)])
    }

    fn put_u32(&mut self, value: u32) -> Result<(), CoreStatusPayloadCodecError> {
        self.put_raw(&value.to_le_bytes())
    }

    fn put_u64(&mut self, value: u64) -> Result<(), CoreStatusPayloadCodecError> {
        self.put_raw(&value.to_le_bytes())
    }

    fn put_length(
        &mut self,
        value: usize,
        limit: usize,
    ) -> Result<(), CoreStatusPayloadCodecError> {
        if value > limit {
            return Err(CoreStatusPayloadCodecError::CollectionLimit);
        }
        let value = u32::try_from(value)
            .map_err(|_| CoreStatusPayloadCodecError::LengthOverflow)?;
        self.put_u32(value)
    }

    fn put_string(
        &mut self,
        value: &str,
        limit: usize,
    ) -> Result<(), CoreStatusPayloadCodecError> {
        if value.len() > limit {
            return Err(CoreStatusPayloadCodecError::StringLimit);
        }
        let length = u32::try_from(value.len())
            .map_err(|_| CoreStatusPayloadCodecError::LengthOverflow)?;
        self.put_u32(length)?;
        self.put_raw(value.as_bytes())
    }
}

struct CoreStatusPayloadDecoder<'a> {
    bytes: &'a [u8],
    offset: usize,
}

impl<'a> CoreStatusPayloadDecoder<'a> {
    fn take(&mut self, length: usize) -> Result<&'a [u8], CoreStatusPayloadCodecError> {
        let end = self.offset.checked_add(length)
            .ok_or(CoreStatusPayloadCodecError::LengthOverflow)?;
        let value = self.bytes.get(self.offset..end)
            .ok_or(CoreStatusPayloadCodecError::Truncated)?;
        self.offset = end;
        Ok(value)
    }

    fn read_bool(&mut self) -> Result<bool, CoreStatusPayloadCodecError> {
        match self.take(1)?.first().copied() {
            Some(0) => Ok(false),
            Some(1) => Ok(true),
            Some(_) => Err(CoreStatusPayloadCodecError::InvalidBoolean),
            None => Err(CoreStatusPayloadCodecError::Truncated),
        }
    }

    fn read_u32(&mut self) -> Result<u32, CoreStatusPayloadCodecError> {
        let value: [u8; 4] = self.take(4)?.try_into()
            .map_err(|_| CoreStatusPayloadCodecError::Truncated)?;
        Ok(u32::from_le_bytes(value))
    }

    fn read_bounded_u32(
        &mut self,
        limit: usize,
    ) -> Result<u32, CoreStatusPayloadCodecError> {
        let value = self.read_u32()?;
        if !usize::try_from(value).is_ok_and(|value| value <= limit) {
            return Err(CoreStatusPayloadCodecError::ValueLimit);
        }
        Ok(value)
    }

    fn read_u64(&mut self) -> Result<u64, CoreStatusPayloadCodecError> {
        let value: [u8; 8] = self.take(8)?.try_into()
            .map_err(|_| CoreStatusPayloadCodecError::Truncated)?;
        Ok(u64::from_le_bytes(value))
    }

    fn read_length(&mut self, limit: usize) -> Result<usize, CoreStatusPayloadCodecError> {
        let value = usize::try_from(self.read_u32()?)
            .map_err(|_| CoreStatusPayloadCodecError::LengthOverflow)?;
        if value > limit {
            return Err(CoreStatusPayloadCodecError::CollectionLimit);
        }
        Ok(value)
    }

    fn read_string(&mut self, limit: usize) -> Result<String, CoreStatusPayloadCodecError> {
        let length = usize::try_from(self.read_u32()?)
            .map_err(|_| CoreStatusPayloadCodecError::LengthOverflow)?;
        if length > limit {
            return Err(CoreStatusPayloadCodecError::StringLimit);
        }
        let value = core::str::from_utf8(self.take(length)?)
            .map_err(|_| CoreStatusPayloadCodecError::InvalidUtf8)?;
        Ok(value.to_owned())
    }

    fn read_optional<T>(
        &mut self,
        read: impl FnOnce(&mut Self) -> Result<T, CoreStatusPayloadCodecError>,
    ) -> Result<Option<T>, CoreStatusPayloadCodecError> {
        if self.read_bool()? { read(self).map(Some) } else { Ok(None) }
    }

    fn read_list<T>(
        &mut self,
        limit: usize,
        mut read: impl FnMut(&mut Self) -> Result<T, CoreStatusPayloadCodecError>,
    ) -> Result<Vec<T>, CoreStatusPayloadCodecError> {
        let length = self.read_length(limit)?;
        let mut values = Vec::with_capacity(length);
        for _ in 0..length {
            values.push(read(self)?);
        }
        Ok(values)
    }
}

fn encode_site_skill_semantic_target(encoder: &mut CoreStatusPayloadEncoder, value: &SiteSkillSemanticTarget) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_u32(value.role)?;
    encoder.put_u32(value.phrase)?;
    Ok(())
}

fn decode_site_skill_semantic_target(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<SiteSkillSemanticTarget, CoreStatusPayloadCodecError>
{
    let value = SiteSkillSemanticTarget {
        role: decoder.read_u32()?,
        phrase: decoder.read_u32()?,
    };
    Ok(value)
}

fn encode_site_skill_observed_argument(encoder: &mut CoreStatusPayloadEncoder, value: &SiteSkillObservedArgument) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_u32(value.parameter)?;
    encoder.put_u32(value.kind as u32)?;
    encoder.put_u64(value.value)?;
    encoder.put_u32(value.purpose)?;
    match &value.public_address {
        Some(value) => {
            encoder.put_bool(true)?;
            encoder.put_string(value, MAX_TASK_GOAL_BYTES)?;
        }
        None => encoder.put_bool(false)?,
    }
    match &value.semantic_target {
        Some(value) => {
            encoder.put_bool(true)?;
            encode_site_skill_semantic_target(encoder, value)?;
        }
        None => encoder.put_bool(false)?,
    }
    Ok(())
}

fn decode_site_skill_observed_argument(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<SiteSkillObservedArgument, CoreStatusPayloadCodecError>
{
    let value = SiteSkillObservedArgument {
        parameter: decoder.read_u32()?,
        kind: decoder.read_u32().and_then(|value| SiteSkillArgumentKind::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        value: decoder.read_u64()?,
        purpose: decoder.read_u32()?,
        public_address: decoder.read_optional(|decoder| decoder.read_string(MAX_TASK_GOAL_BYTES))?,
        semantic_target: decoder.read_optional(|decoder| decode_site_skill_semantic_target(decoder))?,
    };
    Ok(value)
}

fn encode_site_skill_observed_step(encoder: &mut CoreStatusPayloadEncoder, value: &SiteSkillObservedStep) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_string(&value.verb, MAX_SKILL_TOOL_NAME_BYTES)?;
    encoder.put_length(value.arguments.len(), MAX_SKILL_ARGUMENTS_PER_STEP)?;
    for item in &value.arguments {
        encode_site_skill_observed_argument(encoder, item)?;
    }
    encoder.put_u32(value.postcondition)?;
    encoder.put_bool(value.has_fill)?;
    encoder.put_u32(value.fill_purpose)?;
    Ok(())
}

fn decode_site_skill_observed_step(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<SiteSkillObservedStep, CoreStatusPayloadCodecError>
{
    let value = SiteSkillObservedStep {
        verb: decoder.read_string(MAX_SKILL_TOOL_NAME_BYTES)?,
        arguments: decoder.read_list(MAX_SKILL_ARGUMENTS_PER_STEP, |decoder| decode_site_skill_observed_argument(decoder))?,
        postcondition: decoder.read_u32()?,
        has_fill: decoder.read_bool()?,
        fill_purpose: decoder.read_u32()?,
    };
    Ok(value)
}

fn encode_site_skill_view(encoder: &mut CoreStatusPayloadEncoder, value: &SiteSkillView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_string(&value.skill_id, MAX_SKILL_ID_BYTES)?;
    encoder.put_string(&value.origin, MAX_SKILL_ORIGIN_BYTES)?;
    encoder.put_u32(value.provenance as u32)?;
    encoder.put_u32(value.status as u32)?;
    encoder.put_u32(value.active_version)?;
    encoder.put_u32(value.step_count)?;
    encoder.put_u64(value.installed_at_epoch_ms)?;
    encoder.put_u64(value.updated_at_epoch_ms)?;
    match &value.recorded_from_task_id {
        Some(value) => {
            encoder.put_bool(true)?;
            encoder.put_string(value, MAX_IDENTIFIER_BYTES)?;
        }
        None => encoder.put_bool(false)?,
    }
    encoder.put_length(value.reviewed_steps.len(), MAX_SKILL_STEPS)?;
    for item in &value.reviewed_steps {
        encode_site_skill_observed_step(encoder, item)?;
    }
    Ok(())
}

fn decode_site_skill_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<SiteSkillView, CoreStatusPayloadCodecError>
{
    let value = SiteSkillView {
        skill_id: decoder.read_string(MAX_SKILL_ID_BYTES)?,
        origin: decoder.read_string(MAX_SKILL_ORIGIN_BYTES)?,
        provenance: decoder.read_u32().and_then(|value| SiteSkillProvenanceView::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        status: decoder.read_u32().and_then(|value| SiteSkillStatusView::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        active_version: decoder.read_u32()?,
        step_count: decoder.read_u32()?,
        installed_at_epoch_ms: decoder.read_u64()?,
        updated_at_epoch_ms: decoder.read_u64()?,
        recorded_from_task_id: decoder.read_optional(|decoder| decoder.read_string(MAX_IDENTIFIER_BYTES))?,
        reviewed_steps: decoder.read_list(MAX_SKILL_STEPS, |decoder| decode_site_skill_observed_step(decoder))?,
    };
    Ok(value)
}

fn encode_assistant_configuration_view(encoder: &mut CoreStatusPayloadEncoder, value: &AssistantConfigurationView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_u64(value.revision)?;
    encoder.put_length(value.disabled_abilities.len(), MAX_ASSISTANT_ABILITIES)?;
    for item in &value.disabled_abilities {
        encoder.put_u32(*item as u32)?;
    }
    encoder.put_u32(value.preset as u32)?;
    if !usize::try_from(value.pace)
        .is_ok_and(|value| value <= MAX_PERSONALITY_SCALE)
    {
        return Err(CoreStatusPayloadCodecError::ValueLimit);
    }
    encoder.put_u32(value.pace)?;
    if !usize::try_from(value.length)
        .is_ok_and(|value| value <= MAX_PERSONALITY_SCALE)
    {
        return Err(CoreStatusPayloadCodecError::ValueLimit);
    }
    encoder.put_u32(value.length)?;
    if !usize::try_from(value.check_in)
        .is_ok_and(|value| value <= MAX_PERSONALITY_SCALE)
    {
        return Err(CoreStatusPayloadCodecError::ValueLimit);
    }
    encoder.put_u32(value.check_in)?;
    Ok(())
}

fn decode_assistant_configuration_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<AssistantConfigurationView, CoreStatusPayloadCodecError>
{
    let value = AssistantConfigurationView {
        revision: decoder.read_u64()?,
        disabled_abilities: decoder.read_list(MAX_ASSISTANT_ABILITIES, |decoder| decoder.read_u32().and_then(|value| AssistantAbilityView::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum)))?,
        preset: decoder.read_u32().and_then(|value| PersonalityPresetView::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        pace: decoder.read_bounded_u32(MAX_PERSONALITY_SCALE)?,
        length: decoder.read_bounded_u32(MAX_PERSONALITY_SCALE)?,
        check_in: decoder.read_bounded_u32(MAX_PERSONALITY_SCALE)?,
    };
    Ok(value)
}

fn encode_builtin_skill_reference_view(encoder: &mut CoreStatusPayloadEncoder, value: &BuiltinSkillReferenceView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_u32(value.skill_id as u32)?;
    encoder.put_u32(value.version)?;
    Ok(())
}

fn decode_builtin_skill_reference_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<BuiltinSkillReferenceView, CoreStatusPayloadCodecError>
{
    let value = BuiltinSkillReferenceView {
        skill_id: decoder.read_u32().and_then(|value| BuiltinSkillIdView::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        version: decoder.read_u32()?,
    };
    Ok(value)
}

fn encode_builtin_skill_view(encoder: &mut CoreStatusPayloadEncoder, value: &BuiltinSkillView) -> Result<(), CoreStatusPayloadCodecError> {
    encode_builtin_skill_reference_view(encoder, &value.reference)?;
    encoder.put_u32(value.required_ability as u32)?;
    encoder.put_bool(value.enabled)?;
    encoder.put_u32(value.availability as u32)?;
    encoder.put_u32(value.required_tool_count)?;
    encoder.put_u32(value.available_tool_count)?;
    encoder.put_u32(value.required_part_count)?;
    encoder.put_u32(value.installed_part_count)?;
    Ok(())
}

fn decode_builtin_skill_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<BuiltinSkillView, CoreStatusPayloadCodecError>
{
    let value = BuiltinSkillView {
        reference: decode_builtin_skill_reference_view(decoder)?,
        required_ability: decoder.read_u32().and_then(|value| AssistantAbilityView::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        enabled: decoder.read_bool()?,
        availability: decoder.read_u32().and_then(|value| BuiltinSkillAvailabilityView::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        required_tool_count: decoder.read_u32()?,
        available_tool_count: decoder.read_u32()?,
        required_part_count: decoder.read_u32()?,
        installed_part_count: decoder.read_u32()?,
    };
    Ok(value)
}

fn encode_core_status_projection_omission(encoder: &mut CoreStatusPayloadEncoder, value: &CoreStatusProjectionOmission) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_u32(value.family as u32)?;
    encoder.put_u64(value.revision)?;
    encoder.put_u32(value.item_count)?;
    Ok(())
}

fn decode_core_status_projection_omission(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<CoreStatusProjectionOmission, CoreStatusPayloadCodecError>
{
    let value = CoreStatusProjectionOmission {
        family: decoder.read_u32().and_then(|value| CoreStatusProjectionFamily::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        revision: decoder.read_u64()?,
        item_count: decoder.read_u32()?,
    };
    Ok(value)
}

fn encode_core_failure(encoder: &mut CoreStatusPayloadEncoder, value: &CoreFailure) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_u32(value.code as u32)?;
    encoder.put_bool(value.retryable)?;
    match &value.message_key {
        Some(value) => {
            encoder.put_bool(true)?;
            encoder.put_string(value, MAX_MESSAGE_KEY_BYTES)?;
        }
        None => encoder.put_bool(false)?,
    }
    Ok(())
}

fn decode_core_failure(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<CoreFailure, CoreStatusPayloadCodecError>
{
    let value = CoreFailure {
        code: decoder.read_u32().and_then(|value| CoreFailureCode::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        retryable: decoder.read_bool()?,
        message_key: decoder.read_optional(|decoder| decoder.read_string(MAX_MESSAGE_KEY_BYTES))?,
    };
    Ok(value)
}

fn encode_task_activity_view(encoder: &mut CoreStatusPayloadEncoder, value: &TaskActivityView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_u64(value.sequence)?;
    encoder.put_u32(value.kind as u32)?;
    match &value.host {
        Some(value) => {
            encoder.put_bool(true)?;
            encoder.put_string(value, MAX_SOURCE_HOST_BYTES)?;
        }
        None => encoder.put_bool(false)?,
    }
    encoder.put_u32(value.count)?;
    encoder.put_u64(value.at_epoch_ms)?;
    Ok(())
}

fn decode_task_activity_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<TaskActivityView, CoreStatusPayloadCodecError>
{
    let value = TaskActivityView {
        sequence: decoder.read_u64()?,
        kind: decoder.read_u32().and_then(|value| TaskActivityKind::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        host: decoder.read_optional(|decoder| decoder.read_string(MAX_SOURCE_HOST_BYTES))?,
        count: decoder.read_u32()?,
        at_epoch_ms: decoder.read_u64()?,
    };
    Ok(value)
}

fn encode_task_artifact_view(encoder: &mut CoreStatusPayloadEncoder, value: &TaskArtifactView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_string(&value.artifact_id, MAX_IDENTIFIER_BYTES)?;
    encoder.put_u32(value.kind as u32)?;
    encoder.put_u64(value.workspace_revision)?;
    encoder.put_bool(value.accepted)?;
    Ok(())
}

fn decode_task_artifact_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<TaskArtifactView, CoreStatusPayloadCodecError>
{
    let value = TaskArtifactView {
        artifact_id: decoder.read_string(MAX_IDENTIFIER_BYTES)?,
        kind: decoder.read_u32().and_then(|value| TaskArtifactKind::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        workspace_revision: decoder.read_u64()?,
        accepted: decoder.read_bool()?,
    };
    Ok(value)
}

fn encode_task_view_state(encoder: &mut CoreStatusPayloadEncoder, value: &TaskViewState) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_string(&value.task_id, MAX_IDENTIFIER_BYTES)?;
    encoder.put_u64(value.revision)?;
    encoder.put_u32(value.phase as u32)?;
    if !usize::try_from(value.progress_basis_points)
        .is_ok_and(|value| value <= MAX_PROGRESS_BASIS_POINTS)
    {
        return Err(CoreStatusPayloadCodecError::ValueLimit);
    }
    encoder.put_u32(value.progress_basis_points)?;
    match &value.status_message_key {
        Some(value) => {
            encoder.put_bool(true)?;
            encoder.put_string(value, MAX_MESSAGE_KEY_BYTES)?;
        }
        None => encoder.put_bool(false)?,
    }
    match &value.failure {
        Some(value) => {
            encoder.put_bool(true)?;
            encode_core_failure(encoder, value)?;
        }
        None => encoder.put_bool(false)?,
    }
    encoder.put_string(&value.goal, MAX_TASK_GOAL_BYTES)?;
    encoder.put_u32(value.template_id as u32)?;
    match &value.pending_action {
        Some(value) => {
            encoder.put_bool(true)?;
            encode_action_approval_view(encoder, value)?;
        }
        None => encoder.put_bool(false)?,
    }
    match &value.workspace_id {
        Some(value) => {
            encoder.put_bool(true)?;
            encoder.put_string(value, MAX_IDENTIFIER_BYTES)?;
        }
        None => encoder.put_bool(false)?,
    }
    match &value.pending_ask_prompt {
        Some(value) => {
            encoder.put_bool(true)?;
            encoder.put_string(value, MAX_USER_INPUT_ANSWER_BYTES)?;
        }
        None => encoder.put_bool(false)?,
    }
    match &value.pending_field_value_request {
        Some(value) => {
            encoder.put_bool(true)?;
            encoder.put_string(value, MAX_IDENTIFIER_BYTES)?;
        }
        None => encoder.put_bool(false)?,
    }
    encoder.put_length(value.allowed_controls.len(), MAX_TASK_CONTROLS)?;
    for item in &value.allowed_controls {
        encoder.put_u32(*item as u32)?;
    }
    encoder.put_length(value.artifacts.len(), MAX_TASK_ARTIFACTS)?;
    for item in &value.artifacts {
        encode_task_artifact_view(encoder, item)?;
    }
    encoder.put_length(value.activity.len(), MAX_TASK_ACTIVITY)?;
    for item in &value.activity {
        encode_task_activity_view(encoder, item)?;
    }
    Ok(())
}

fn decode_task_view_state(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<TaskViewState, CoreStatusPayloadCodecError>
{
    let value = TaskViewState {
        task_id: decoder.read_string(MAX_IDENTIFIER_BYTES)?,
        revision: decoder.read_u64()?,
        phase: decoder.read_u32().and_then(|value| TaskPhase::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        progress_basis_points: decoder.read_bounded_u32(MAX_PROGRESS_BASIS_POINTS)?,
        status_message_key: decoder.read_optional(|decoder| decoder.read_string(MAX_MESSAGE_KEY_BYTES))?,
        failure: decoder.read_optional(|decoder| decode_core_failure(decoder))?,
        goal: decoder.read_string(MAX_TASK_GOAL_BYTES)?,
        template_id: decoder.read_u32().and_then(|value| TaskTemplateId::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        pending_action: decoder.read_optional(|decoder| decode_action_approval_view(decoder))?,
        workspace_id: decoder.read_optional(|decoder| decoder.read_string(MAX_IDENTIFIER_BYTES))?,
        pending_ask_prompt: decoder.read_optional(|decoder| decoder.read_string(MAX_USER_INPUT_ANSWER_BYTES))?,
        pending_field_value_request: decoder.read_optional(|decoder| decoder.read_string(MAX_IDENTIFIER_BYTES))?,
        allowed_controls: decoder.read_list(MAX_TASK_CONTROLS, |decoder| decoder.read_u32().and_then(|value| TaskControlKind::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum)))?,
        artifacts: decoder.read_list(MAX_TASK_ARTIFACTS, |decoder| decode_task_artifact_view(decoder))?,
        activity: decoder.read_list(MAX_TASK_ACTIVITY, |decoder| decode_task_activity_view(decoder))?,
    };
    Ok(value)
}

fn encode_action_approval_view(encoder: &mut CoreStatusPayloadEncoder, value: &ActionApprovalView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_string(&value.action_id, MAX_IDENTIFIER_BYTES)?;
    match &value.host {
        Some(value) => {
            encoder.put_bool(true)?;
            encoder.put_string(value, MAX_IDENTIFIER_BYTES)?;
        }
        None => encoder.put_bool(false)?,
    }
    encoder.put_u32(value.item_count)?;
    encoder.put_string(&value.summary_message_key, MAX_MESSAGE_KEY_BYTES)?;
    Ok(())
}

fn decode_action_approval_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<ActionApprovalView, CoreStatusPayloadCodecError>
{
    let value = ActionApprovalView {
        action_id: decoder.read_string(MAX_IDENTIFIER_BYTES)?,
        host: decoder.read_optional(|decoder| decoder.read_string(MAX_IDENTIFIER_BYTES))?,
        item_count: decoder.read_u32()?,
        summary_message_key: decoder.read_string(MAX_MESSAGE_KEY_BYTES)?,
    };
    Ok(value)
}

fn encode_stored_credential_view(encoder: &mut CoreStatusPayloadEncoder, value: &StoredCredentialView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_u32(value.auth_method as u32)?;
    encoder.put_u32(value.state as u32)?;
    encoder.put_bool(value.subscription_backed)?;
    match &value.account_label {
        Some(value) => {
            encoder.put_bool(true)?;
            encoder.put_string(value, MAX_AUTH_DISPLAY_NAME_BYTES)?;
        }
        None => encoder.put_bool(false)?,
    }
    match &value.plan_label {
        Some(value) => {
            encoder.put_bool(true)?;
            encoder.put_string(value, MAX_PROVIDER_DISPLAY_NAME_BYTES)?;
        }
        None => encoder.put_bool(false)?,
    }
    Ok(())
}

fn decode_stored_credential_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<StoredCredentialView, CoreStatusPayloadCodecError>
{
    let value = StoredCredentialView {
        auth_method: decoder.read_u32().and_then(|value| ProviderAuthMethodView::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        state: decoder.read_u32().and_then(|value| ProviderCredentialStateView::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        subscription_backed: decoder.read_bool()?,
        account_label: decoder.read_optional(|decoder| decoder.read_string(MAX_AUTH_DISPLAY_NAME_BYTES))?,
        plan_label: decoder.read_optional(|decoder| decoder.read_string(MAX_PROVIDER_DISPLAY_NAME_BYTES))?,
    };
    Ok(value)
}

fn encode_thinking_preference_view(encoder: &mut CoreStatusPayloadEncoder, value: &ThinkingPreferenceView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_u32(value.level as u32)?;
    Ok(())
}

fn decode_thinking_preference_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<ThinkingPreferenceView, CoreStatusPayloadCodecError>
{
    let value = ThinkingPreferenceView {
        level: decoder.read_u32().and_then(|value| ThinkingLevelView::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
    };
    Ok(value)
}

fn encode_provider_refusal_state_view(encoder: &mut CoreStatusPayloadEncoder, value: &ProviderRefusalStateView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_u32(value.refusal as u32)?;
    encoder.put_u64(value.at_monotonic_ms)?;
    Ok(())
}

fn decode_provider_refusal_state_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<ProviderRefusalStateView, CoreStatusPayloadCodecError>
{
    let value = ProviderRefusalStateView {
        refusal: decoder.read_u32().and_then(|value| ProviderRefusalView::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        at_monotonic_ms: decoder.read_u64()?,
    };
    Ok(value)
}

fn encode_provider_presentation_view(encoder: &mut CoreStatusPayloadEncoder, value: &ProviderPresentationView) -> Result<(), CoreStatusPayloadCodecError> {
    match &value.key_prefix {
        Some(value) => {
            encoder.put_bool(true)?;
            encoder.put_string(value, MAX_PROVIDER_PRESENTATION_BYTES)?;
        }
        None => encoder.put_bool(false)?,
    }
    match &value.get_key_url {
        Some(value) => {
            encoder.put_bool(true)?;
            encoder.put_string(value, MAX_PROVIDER_PRESENTATION_BYTES)?;
        }
        None => encoder.put_bool(false)?,
    }
    match &value.docs_url {
        Some(value) => {
            encoder.put_bool(true)?;
            encoder.put_string(value, MAX_PROVIDER_PRESENTATION_BYTES)?;
        }
        None => encoder.put_bool(false)?,
    }
    Ok(())
}

fn decode_provider_presentation_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<ProviderPresentationView, CoreStatusPayloadCodecError>
{
    let value = ProviderPresentationView {
        key_prefix: decoder.read_optional(|decoder| decoder.read_string(MAX_PROVIDER_PRESENTATION_BYTES))?,
        get_key_url: decoder.read_optional(|decoder| decoder.read_string(MAX_PROVIDER_PRESENTATION_BYTES))?,
        docs_url: decoder.read_optional(|decoder| decoder.read_string(MAX_PROVIDER_PRESENTATION_BYTES))?,
    };
    Ok(value)
}

fn encode_provider_model_view(encoder: &mut CoreStatusPayloadEncoder, value: &ProviderModelView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_string(&value.provider_id, MAX_PROVIDER_ID_BYTES)?;
    encoder.put_string(&value.model_id, MAX_MODEL_ID_BYTES)?;
    encoder.put_string(&value.display_name, MAX_MODEL_DISPLAY_NAME_BYTES)?;
    encoder.put_u64(value.context_window)?;
    encoder.put_u64(value.max_output_tokens)?;
    encoder.put_bool(value.reasoning)?;
    encoder.put_bool(value.tool_calling)?;
    encoder.put_length(value.roles.len(), MAX_MODEL_ROLES)?;
    for item in &value.roles {
        encoder.put_u32(*item as u32)?;
    }
    encoder.put_length(value.input_modalities.len(), MAX_MODEL_INPUT_MODALITIES)?;
    for item in &value.input_modalities {
        encoder.put_u32(*item as u32)?;
    }
    encoder.put_length(value.thinking_levels.len(), MAX_MODEL_THINKING_LEVELS)?;
    for item in &value.thinking_levels {
        encoder.put_u32(*item as u32)?;
    }
    Ok(())
}

fn decode_provider_model_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<ProviderModelView, CoreStatusPayloadCodecError>
{
    let value = ProviderModelView {
        provider_id: decoder.read_string(MAX_PROVIDER_ID_BYTES)?,
        model_id: decoder.read_string(MAX_MODEL_ID_BYTES)?,
        display_name: decoder.read_string(MAX_MODEL_DISPLAY_NAME_BYTES)?,
        context_window: decoder.read_u64()?,
        max_output_tokens: decoder.read_u64()?,
        reasoning: decoder.read_bool()?,
        tool_calling: decoder.read_bool()?,
        roles: decoder.read_list(MAX_MODEL_ROLES, |decoder| decoder.read_u32().and_then(|value| ModelRoleView::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum)))?,
        input_modalities: decoder.read_list(MAX_MODEL_INPUT_MODALITIES, |decoder| decoder.read_u32().and_then(|value| InputModalityView::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum)))?,
        thinking_levels: decoder.read_list(MAX_MODEL_THINKING_LEVELS, |decoder| decoder.read_u32().and_then(|value| ThinkingLevelView::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum)))?,
    };
    Ok(value)
}

fn encode_provider_roster_entry(encoder: &mut CoreStatusPayloadEncoder, value: &ProviderRosterEntry) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_string(&value.provider_id, MAX_PROVIDER_ID_BYTES)?;
    encoder.put_string(&value.display_name, MAX_PROVIDER_DISPLAY_NAME_BYTES)?;
    encoder.put_u32(value.origin as u32)?;
    encoder.put_length(value.auth_methods.len(), MAX_AUTH_METHODS)?;
    for item in &value.auth_methods {
        encoder.put_u32(*item as u32)?;
    }
    match &value.stored {
        Some(value) => {
            encoder.put_bool(true)?;
            encode_stored_credential_view(encoder, value)?;
        }
        None => encoder.put_bool(false)?,
    }
    encoder.put_bool(value.signing_in)?;
    encoder.put_bool(value.enabled)?;
    match &value.endpoint_host {
        Some(value) => {
            encoder.put_bool(true)?;
            encoder.put_string(value, MAX_SOURCE_HOST_BYTES)?;
        }
        None => encoder.put_bool(false)?,
    }
    encoder.put_bool(value.configurable)?;
    encoder.put_bool(value.endpoint_changed)?;
    encoder.put_u32(value.catalog_layer as u32)?;
    match &value.selected_model_id {
        Some(value) => {
            encoder.put_bool(true)?;
            encoder.put_string(value, MAX_MODEL_ID_BYTES)?;
        }
        None => encoder.put_bool(false)?,
    }
    match &value.thinking {
        Some(value) => {
            encoder.put_bool(true)?;
            encode_thinking_preference_view(encoder, value)?;
        }
        None => encoder.put_bool(false)?,
    }
    match &value.presentation {
        Some(value) => {
            encoder.put_bool(true)?;
            encode_provider_presentation_view(encoder, value)?;
        }
        None => encoder.put_bool(false)?,
    }
    match &value.endpoint_base {
        Some(value) => {
            encoder.put_bool(true)?;
            encoder.put_string(value, MAX_PROVIDER_ENDPOINT_BYTES)?;
        }
        None => encoder.put_bool(false)?,
    }
    match &value.last_refusal {
        Some(value) => {
            encoder.put_bool(true)?;
            encode_provider_refusal_state_view(encoder, value)?;
        }
        None => encoder.put_bool(false)?,
    }
    encoder.put_u32(value.model_count)?;
    encoder.put_bool(value.subscription)?;
    match &value.refused_endpoint_host {
        Some(value) => {
            encoder.put_bool(true)?;
            encoder.put_string(value, MAX_SOURCE_HOST_BYTES)?;
        }
        None => encoder.put_bool(false)?,
    }
    Ok(())
}

fn decode_provider_roster_entry(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<ProviderRosterEntry, CoreStatusPayloadCodecError>
{
    let value = ProviderRosterEntry {
        provider_id: decoder.read_string(MAX_PROVIDER_ID_BYTES)?,
        display_name: decoder.read_string(MAX_PROVIDER_DISPLAY_NAME_BYTES)?,
        origin: decoder.read_u32().and_then(|value| ProviderOriginView::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        auth_methods: decoder.read_list(MAX_AUTH_METHODS, |decoder| decoder.read_u32().and_then(|value| ProviderAuthMethodView::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum)))?,
        stored: decoder.read_optional(|decoder| decode_stored_credential_view(decoder))?,
        signing_in: decoder.read_bool()?,
        enabled: decoder.read_bool()?,
        endpoint_host: decoder.read_optional(|decoder| decoder.read_string(MAX_SOURCE_HOST_BYTES))?,
        configurable: decoder.read_bool()?,
        endpoint_changed: decoder.read_bool()?,
        catalog_layer: decoder.read_u32().and_then(|value| CatalogLayerView::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        selected_model_id: decoder.read_optional(|decoder| decoder.read_string(MAX_MODEL_ID_BYTES))?,
        thinking: decoder.read_optional(|decoder| decode_thinking_preference_view(decoder))?,
        presentation: decoder.read_optional(|decoder| decode_provider_presentation_view(decoder))?,
        endpoint_base: decoder.read_optional(|decoder| decoder.read_string(MAX_PROVIDER_ENDPOINT_BYTES))?,
        last_refusal: decoder.read_optional(|decoder| decode_provider_refusal_state_view(decoder))?,
        model_count: decoder.read_u32()?,
        subscription: decoder.read_bool()?,
        refused_endpoint_host: decoder.read_optional(|decoder| decoder.read_string(MAX_SOURCE_HOST_BYTES))?,
    };
    Ok(value)
}

fn encode_provider_probe_view(encoder: &mut CoreStatusPayloadEncoder, value: &ProviderProbeView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_string(&value.provider_id, MAX_PROVIDER_ID_BYTES)?;
    encoder.put_u32(value.verdict as u32)?;
    encoder.put_u64(value.at_monotonic_ms)?;
    match &value.endpoint {
        Some(value) => {
            encoder.put_bool(true)?;
            encode_probe_endpoint_view(encoder, value)?;
        }
        None => encoder.put_bool(false)?,
    }
    Ok(())
}

fn decode_provider_probe_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<ProviderProbeView, CoreStatusPayloadCodecError>
{
    let value = ProviderProbeView {
        provider_id: decoder.read_string(MAX_PROVIDER_ID_BYTES)?,
        verdict: decoder.read_u32().and_then(|value| ProviderProbeVerdictView::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        at_monotonic_ms: decoder.read_u64()?,
        endpoint: decoder.read_optional(|decoder| decode_probe_endpoint_view(decoder))?,
    };
    Ok(value)
}

fn encode_probe_endpoint_view(encoder: &mut CoreStatusPayloadEncoder, value: &ProbeEndpointView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_u32(value.server_kind as u32)?;
    encoder.put_u32(value.model_count)?;
    encoder.put_length(value.models.len(), MAX_CUSTOM_MODEL_ENTRIES)?;
    for item in &value.models {
        encode_custom_model_spec_view(encoder, item)?;
    }
    match &value.proved_base {
        Some(value) => {
            encoder.put_bool(true)?;
            encoder.put_string(value, MAX_PROVIDER_ENDPOINT_BYTES)?;
        }
        None => encoder.put_bool(false)?,
    }
    Ok(())
}

fn decode_probe_endpoint_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<ProbeEndpointView, CoreStatusPayloadCodecError>
{
    let value = ProbeEndpointView {
        server_kind: decoder.read_u32().and_then(|value| ServerKindView::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        model_count: decoder.read_u32()?,
        models: decoder.read_list(MAX_CUSTOM_MODEL_ENTRIES, |decoder| decode_custom_model_spec_view(decoder))?,
        proved_base: decoder.read_optional(|decoder| decoder.read_string(MAX_PROVIDER_ENDPOINT_BYTES))?,
    };
    Ok(value)
}

fn encode_saved_sign_in_view(encoder: &mut CoreStatusPayloadEncoder, value: &SavedSignInView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_string(&value.id, MAX_IDENTIFIER_BYTES)?;
    encoder.put_string(&value.site, MAX_SAVED_SIGN_IN_SITE_BYTES)?;
    encoder.put_string(&value.username, MAX_SAVED_SIGN_IN_USERNAME_BYTES)?;
    encoder.put_u64(value.last_used_epoch_ms)?;
    Ok(())
}

fn decode_saved_sign_in_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<SavedSignInView, CoreStatusPayloadCodecError>
{
    let value = SavedSignInView {
        id: decoder.read_string(MAX_IDENTIFIER_BYTES)?,
        site: decoder.read_string(MAX_SAVED_SIGN_IN_SITE_BYTES)?,
        username: decoder.read_string(MAX_SAVED_SIGN_IN_USERNAME_BYTES)?,
        last_used_epoch_ms: decoder.read_u64()?,
    };
    Ok(value)
}

fn encode_saved_sign_ins_view(encoder: &mut CoreStatusPayloadEncoder, value: &SavedSignInsView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_u32(value.availability as u32)?;
    encoder.put_u64(value.revision)?;
    encoder.put_length(value.records.len(), MAX_SAVED_SIGN_INS)?;
    for item in &value.records {
        encode_saved_sign_in_view(encoder, item)?;
    }
    Ok(())
}

fn decode_saved_sign_ins_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<SavedSignInsView, CoreStatusPayloadCodecError>
{
    let value = SavedSignInsView {
        availability: decoder.read_u32().and_then(|value| SavedDataAvailability::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        revision: decoder.read_u64()?,
        records: decoder.read_list(MAX_SAVED_SIGN_INS, |decoder| decode_saved_sign_in_view(decoder))?,
    };
    Ok(value)
}

fn encode_saved_detail_view(encoder: &mut CoreStatusPayloadEncoder, value: &SavedDetailView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_string(&value.id, MAX_IDENTIFIER_BYTES)?;
    encoder.put_string(&value.given_name, MAX_SAVED_DETAIL_NAME_BYTES)?;
    encoder.put_string(&value.family_name, MAX_SAVED_DETAIL_NAME_BYTES)?;
    encoder.put_string(&value.email, MAX_SAVED_DETAIL_EMAIL_BYTES)?;
    encoder.put_string(&value.phone, MAX_SAVED_DETAIL_PHONE_BYTES)?;
    encoder.put_string(&value.address, MAX_SAVED_DETAIL_ADDRESS_BYTES)?;
    encoder.put_string(&value.postcode, MAX_SAVED_DETAIL_POSTCODE_BYTES)?;
    encoder.put_string(&value.country, MAX_SAVED_DETAIL_COUNTRY_BYTES)?;
    Ok(())
}

fn decode_saved_detail_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<SavedDetailView, CoreStatusPayloadCodecError>
{
    let value = SavedDetailView {
        id: decoder.read_string(MAX_IDENTIFIER_BYTES)?,
        given_name: decoder.read_string(MAX_SAVED_DETAIL_NAME_BYTES)?,
        family_name: decoder.read_string(MAX_SAVED_DETAIL_NAME_BYTES)?,
        email: decoder.read_string(MAX_SAVED_DETAIL_EMAIL_BYTES)?,
        phone: decoder.read_string(MAX_SAVED_DETAIL_PHONE_BYTES)?,
        address: decoder.read_string(MAX_SAVED_DETAIL_ADDRESS_BYTES)?,
        postcode: decoder.read_string(MAX_SAVED_DETAIL_POSTCODE_BYTES)?,
        country: decoder.read_string(MAX_SAVED_DETAIL_COUNTRY_BYTES)?,
    };
    Ok(value)
}

fn encode_saved_details_view(encoder: &mut CoreStatusPayloadEncoder, value: &SavedDetailsView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_u32(value.availability as u32)?;
    encoder.put_u64(value.revision)?;
    encoder.put_length(value.people.len(), MAX_SAVED_DETAILS)?;
    for item in &value.people {
        encode_saved_detail_view(encoder, item)?;
    }
    Ok(())
}

fn decode_saved_details_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<SavedDetailsView, CoreStatusPayloadCodecError>
{
    let value = SavedDetailsView {
        availability: decoder.read_u32().and_then(|value| SavedDataAvailability::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        revision: decoder.read_u64()?,
        people: decoder.read_list(MAX_SAVED_DETAILS, |decoder| decode_saved_detail_view(decoder))?,
    };
    Ok(value)
}

fn validate_core_status(value: &CoreStatus) -> Result<(), CoreStatusPayloadCodecError> {
    if matches!(value.projection_mode, CoreStatusProjectionMode::Complete | CoreStatusProjectionMode::RecoveryRequired) {
        let expected = [BuiltinSkillIdView::GeneralWebResearch, BuiltinSkillIdView::DeepResearch, BuiltinSkillIdView::ProductComparison, BuiltinSkillIdView::MultiTabComparison, BuiltinSkillIdView::WebsiteSummarizer, BuiltinSkillIdView::PdfAnalysis, BuiltinSkillIdView::DataExtraction, BuiltinSkillIdView::FormAssistant, BuiltinSkillIdView::Shopping, BuiltinSkillIdView::DownloadOrganizer, BuiltinSkillIdView::TravelResearch, BuiltinSkillIdView::VideoTranscriptAnalyzer, BuiltinSkillIdView::ImageUnderstanding, BuiltinSkillIdView::LibraryBuilder, BuiltinSkillIdView::SpreadsheetBuilder, BuiltinSkillIdView::DocumentGenerator];
        if value.builtin_skills.len() != expected.len()
            || value.builtin_skills
                .iter()
                .zip(expected)
                .any(|(item, expected)| item.reference.skill_id != expected)
        {
            return Err(CoreStatusPayloadCodecError::Malformed);
        }
    }
    if matches!(value.projection_mode, CoreStatusProjectionMode::RecoveryRequired) {
        let expected = [CoreStatusProjectionFamily::ActiveTasks, CoreStatusProjectionFamily::Workspaces, CoreStatusProjectionFamily::WorkspaceExport, CoreStatusProjectionFamily::AssetDelivery, CoreStatusProjectionFamily::ProviderRoster, CoreStatusProjectionFamily::ProviderProbes, CoreStatusProjectionFamily::ProviderModels, CoreStatusProjectionFamily::Library, CoreStatusProjectionFamily::LibraryExport, CoreStatusProjectionFamily::Memory, CoreStatusProjectionFamily::SavedSignIns, CoreStatusProjectionFamily::SavedDetails, CoreStatusProjectionFamily::SiteSkills];
        if value.projection_omissions.len() != expected.len()
            || value.projection_omissions
                .iter()
                .zip(expected)
                .any(|(item, expected)| item.family != expected)
        {
            return Err(CoreStatusPayloadCodecError::Malformed);
        }
    }
    if matches!(value.projection_mode, CoreStatusProjectionMode::Complete)
        && !value.projection_omissions.is_empty()
    {
        return Err(CoreStatusPayloadCodecError::Malformed);
    }
    Ok(())
}

fn encode_core_status(encoder: &mut CoreStatusPayloadEncoder, value: &CoreStatus) -> Result<(), CoreStatusPayloadCodecError> {
    validate_core_status(value)?;
    encoder.put_u32(value.availability as u32)?;
    encoder.put_u64(value.generation)?;
    encoder.put_length(value.active_tasks.len(), MAX_ACTIVE_TASKS)?;
    for item in &value.active_tasks {
        encode_task_view_state(encoder, item)?;
    }
    match &value.auth_state {
        Some(value) => {
            encoder.put_bool(true)?;
            encode_auth_view_state(encoder, value)?;
        }
        None => encoder.put_bool(false)?,
    }
    encoder.put_length(value.workspaces.len(), MAX_WORKSPACES)?;
    for item in &value.workspaces {
        encode_workspace_view_state(encoder, item)?;
    }
    match &value.workspace_export {
        Some(value) => {
            encoder.put_bool(true)?;
            encode_workspace_export_view(encoder, value)?;
        }
        None => encoder.put_bool(false)?,
    }
    match &value.asset_delivery {
        Some(value) => {
            encoder.put_bool(true)?;
            encode_asset_delivery_view(encoder, value)?;
        }
        None => encoder.put_bool(false)?,
    }
    encoder.put_length(value.provider_roster.len(), MAX_PROVIDER_ROSTER_ENTRIES)?;
    for item in &value.provider_roster {
        encode_provider_roster_entry(encoder, item)?;
    }
    encoder.put_length(value.provider_probes.len(), MAX_PROVIDER_ROSTER_ENTRIES)?;
    for item in &value.provider_probes {
        encode_provider_probe_view(encoder, item)?;
    }
    encoder.put_length(value.provider_models.len(), MAX_PROVIDER_MODEL_ENTRIES)?;
    for item in &value.provider_models {
        encode_provider_model_view(encoder, item)?;
    }
    encode_assistant_configuration_view(encoder, &value.assistant_configuration)?;
    encode_library_view_state(encoder, &value.library)?;
    match &value.library_export {
        Some(value) => {
            encoder.put_bool(true)?;
            encode_library_export_view(encoder, value)?;
        }
        None => encoder.put_bool(false)?,
    }
    encode_memory_view_state(encoder, &value.memory)?;
    encode_saved_sign_ins_view(encoder, &value.saved_sign_ins)?;
    encode_saved_details_view(encoder, &value.saved_details)?;
    encoder.put_length(value.site_skills.len(), MAX_SITE_SKILLS)?;
    for item in &value.site_skills {
        encode_site_skill_view(encoder, item)?;
    }
    encoder.put_length(value.builtin_skills.len(), MAX_BUILTIN_SKILLS)?;
    for item in &value.builtin_skills {
        encode_builtin_skill_view(encoder, item)?;
    }
    encoder.put_u32(value.projection_mode as u32)?;
    encoder.put_length(value.projection_omissions.len(), MAX_CORE_STATUS_PROJECTION_OMISSIONS)?;
    for item in &value.projection_omissions {
        encode_core_status_projection_omission(encoder, item)?;
    }
    Ok(())
}

fn decode_core_status(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<CoreStatus, CoreStatusPayloadCodecError>
{
    let value = CoreStatus {
        availability: decoder.read_u32().and_then(|value| CoreAvailability::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        generation: decoder.read_u64()?,
        active_tasks: decoder.read_list(MAX_ACTIVE_TASKS, |decoder| decode_task_view_state(decoder))?,
        auth_state: decoder.read_optional(|decoder| decode_auth_view_state(decoder))?,
        workspaces: decoder.read_list(MAX_WORKSPACES, |decoder| decode_workspace_view_state(decoder))?,
        workspace_export: decoder.read_optional(|decoder| decode_workspace_export_view(decoder))?,
        asset_delivery: decoder.read_optional(|decoder| decode_asset_delivery_view(decoder))?,
        provider_roster: decoder.read_list(MAX_PROVIDER_ROSTER_ENTRIES, |decoder| decode_provider_roster_entry(decoder))?,
        provider_probes: decoder.read_list(MAX_PROVIDER_ROSTER_ENTRIES, |decoder| decode_provider_probe_view(decoder))?,
        provider_models: decoder.read_list(MAX_PROVIDER_MODEL_ENTRIES, |decoder| decode_provider_model_view(decoder))?,
        assistant_configuration: decode_assistant_configuration_view(decoder)?,
        library: decode_library_view_state(decoder)?,
        library_export: decoder.read_optional(|decoder| decode_library_export_view(decoder))?,
        memory: decode_memory_view_state(decoder)?,
        saved_sign_ins: decode_saved_sign_ins_view(decoder)?,
        saved_details: decode_saved_details_view(decoder)?,
        site_skills: decoder.read_list(MAX_SITE_SKILLS, |decoder| decode_site_skill_view(decoder))?,
        builtin_skills: decoder.read_list(MAX_BUILTIN_SKILLS, |decoder| decode_builtin_skill_view(decoder))?,
        projection_mode: decoder.read_u32().and_then(|value| CoreStatusProjectionMode::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        projection_omissions: decoder.read_list(MAX_CORE_STATUS_PROJECTION_OMISSIONS, |decoder| decode_core_status_projection_omission(decoder))?,
    };
    validate_core_status(&value)?;
    Ok(value)
}

fn encode_workspace_view_state(encoder: &mut CoreStatusPayloadEncoder, value: &WorkspaceViewState) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_string(&value.workspace_id, MAX_IDENTIFIER_BYTES)?;
    encoder.put_u64(value.revision)?;
    encoder.put_string(&value.goal, MAX_TASK_GOAL_BYTES)?;
    encoder.put_u32(value.phase as u32)?;
    encoder.put_u64(value.last_updated_epoch_ms)?;
    encoder.put_u32(value.template_id as u32)?;
    encoder.put_length(value.sources.len(), MAX_WORKSPACE_SOURCES)?;
    for item in &value.sources {
        encode_workspace_source_view(encoder, item)?;
    }
    encoder.put_length(value.facts.len(), MAX_WORKSPACE_FACTS)?;
    for item in &value.facts {
        encode_workspace_fact_view(encoder, item)?;
    }
    encoder.put_bool(value.saved)?;
    encoder.put_string(&value.display_name, MAX_WORKSPACE_DISPLAY_NAME_BYTES)?;
    match &value.deletion_preview {
        Some(value) => {
            encoder.put_bool(true)?;
            encode_workspace_deletion_preview_view(encoder, value)?;
        }
        None => encoder.put_bool(false)?,
    }
    Ok(())
}

fn decode_workspace_view_state(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<WorkspaceViewState, CoreStatusPayloadCodecError>
{
    let value = WorkspaceViewState {
        workspace_id: decoder.read_string(MAX_IDENTIFIER_BYTES)?,
        revision: decoder.read_u64()?,
        goal: decoder.read_string(MAX_TASK_GOAL_BYTES)?,
        phase: decoder.read_u32().and_then(|value| WorkspacePhase::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        last_updated_epoch_ms: decoder.read_u64()?,
        template_id: decoder.read_u32().and_then(|value| TaskTemplateId::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        sources: decoder.read_list(MAX_WORKSPACE_SOURCES, |decoder| decode_workspace_source_view(decoder))?,
        facts: decoder.read_list(MAX_WORKSPACE_FACTS, |decoder| decode_workspace_fact_view(decoder))?,
        saved: decoder.read_bool()?,
        display_name: decoder.read_string(MAX_WORKSPACE_DISPLAY_NAME_BYTES)?,
        deletion_preview: decoder.read_optional(|decoder| decode_workspace_deletion_preview_view(decoder))?,
    };
    Ok(value)
}

fn encode_workspace_deletion_preview_view(encoder: &mut CoreStatusPayloadEncoder, value: &WorkspaceDeletionPreviewView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_u32(value.sources)?;
    encoder.put_u32(value.facts)?;
    encoder.put_u32(value.artifact_metadata)?;
    encoder.put_u32(value.derived_indexes)?;
    encoder.put_string(&value.confirmation_token, MAX_WORKSPACE_CONFIRMATION_TOKEN_BYTES)?;
    Ok(())
}

fn decode_workspace_deletion_preview_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<WorkspaceDeletionPreviewView, CoreStatusPayloadCodecError>
{
    let value = WorkspaceDeletionPreviewView {
        sources: decoder.read_u32()?,
        facts: decoder.read_u32()?,
        artifact_metadata: decoder.read_u32()?,
        derived_indexes: decoder.read_u32()?,
        confirmation_token: decoder.read_string(MAX_WORKSPACE_CONFIRMATION_TOKEN_BYTES)?,
    };
    Ok(value)
}

fn encode_workspace_source_view(encoder: &mut CoreStatusPayloadEncoder, value: &WorkspaceSourceView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_string(&value.source_id, MAX_IDENTIFIER_BYTES)?;
    encoder.put_string(&value.title, MAX_WORKSPACE_TITLE_BYTES)?;
    encoder.put_string(&value.host, MAX_SOURCE_HOST_BYTES)?;
    encoder.put_u64(value.read_at_epoch_ms)?;
    encoder.put_u32(value.fact_count)?;
    encoder.put_bool(value.excluded)?;
    Ok(())
}

fn decode_workspace_source_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<WorkspaceSourceView, CoreStatusPayloadCodecError>
{
    let value = WorkspaceSourceView {
        source_id: decoder.read_string(MAX_IDENTIFIER_BYTES)?,
        title: decoder.read_string(MAX_WORKSPACE_TITLE_BYTES)?,
        host: decoder.read_string(MAX_SOURCE_HOST_BYTES)?,
        read_at_epoch_ms: decoder.read_u64()?,
        fact_count: decoder.read_u32()?,
        excluded: decoder.read_bool()?,
    };
    Ok(value)
}

fn encode_workspace_fact_view(encoder: &mut CoreStatusPayloadEncoder, value: &WorkspaceFactView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_string(&value.fact_id, MAX_IDENTIFIER_BYTES)?;
    encoder.put_string(&value.field, MAX_FACT_FIELD_BYTES)?;
    encoder.put_string(&value.value, MAX_FACT_VALUE_BYTES)?;
    encoder.put_u32(value.kind as u32)?;
    encoder.put_length(value.sources.len(), MAX_FACT_SOURCES)?;
    for item in &value.sources {
        encoder.put_string(item, MAX_IDENTIFIER_BYTES)?;
    }
    match &value.correction {
        Some(value) => {
            encoder.put_bool(true)?;
            encoder.put_string(value, MAX_FACT_VALUE_BYTES)?;
        }
        None => encoder.put_bool(false)?,
    }
    encoder.put_bool(value.has_conflict)?;
    encoder.put_bool(value.needs_new_source)?;
    Ok(())
}

fn decode_workspace_fact_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<WorkspaceFactView, CoreStatusPayloadCodecError>
{
    let value = WorkspaceFactView {
        fact_id: decoder.read_string(MAX_IDENTIFIER_BYTES)?,
        field: decoder.read_string(MAX_FACT_FIELD_BYTES)?,
        value: decoder.read_string(MAX_FACT_VALUE_BYTES)?,
        kind: decoder.read_u32().and_then(|value| WorkspaceFactKind::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        sources: decoder.read_list(MAX_FACT_SOURCES, |decoder| decoder.read_string(MAX_IDENTIFIER_BYTES))?,
        correction: decoder.read_optional(|decoder| decoder.read_string(MAX_FACT_VALUE_BYTES))?,
        has_conflict: decoder.read_bool()?,
        needs_new_source: decoder.read_bool()?,
    };
    Ok(value)
}

fn encode_workspace_export_view(encoder: &mut CoreStatusPayloadEncoder, value: &WorkspaceExportView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_string(&value.request_id, MAX_IDENTIFIER_BYTES)?;
    encoder.put_string(&value.workspace_id, MAX_IDENTIFIER_BYTES)?;
    encoder.put_u64(value.revision)?;
    encoder.put_u32(value.format as u32)?;
    encoder.put_string(&value.content, MAX_EXPORT_CONTENT_BYTES)?;
    Ok(())
}

fn decode_workspace_export_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<WorkspaceExportView, CoreStatusPayloadCodecError>
{
    let value = WorkspaceExportView {
        request_id: decoder.read_string(MAX_IDENTIFIER_BYTES)?,
        workspace_id: decoder.read_string(MAX_IDENTIFIER_BYTES)?,
        revision: decoder.read_u64()?,
        format: decoder.read_u32().and_then(|value| WorkspaceExportFormat::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        content: decoder.read_string(MAX_EXPORT_CONTENT_BYTES)?,
    };
    Ok(value)
}

fn encode_library_source_view(encoder: &mut CoreStatusPayloadEncoder, value: &LibrarySourceView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_string(&value.source_id, MAX_IDENTIFIER_BYTES)?;
    encoder.put_string(&value.title, MAX_WORKSPACE_TITLE_BYTES)?;
    encoder.put_string(&value.host, MAX_SOURCE_HOST_BYTES)?;
    encoder.put_u64(value.observed_at_epoch_ms)?;
    Ok(())
}

fn decode_library_source_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<LibrarySourceView, CoreStatusPayloadCodecError>
{
    let value = LibrarySourceView {
        source_id: decoder.read_string(MAX_IDENTIFIER_BYTES)?,
        title: decoder.read_string(MAX_WORKSPACE_TITLE_BYTES)?,
        host: decoder.read_string(MAX_SOURCE_HOST_BYTES)?,
        observed_at_epoch_ms: decoder.read_u64()?,
    };
    Ok(value)
}

fn encode_library_entry_view(encoder: &mut CoreStatusPayloadEncoder, value: &LibraryEntryView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_string(&value.entry_id, MAX_IDENTIFIER_BYTES)?;
    encoder.put_u64(value.revision)?;
    encoder.put_string(&value.collection_id, MAX_IDENTIFIER_BYTES)?;
    encoder.put_string(&value.collection_name, MAX_WORKSPACE_DISPLAY_NAME_BYTES)?;
    encoder.put_string(&value.source_workspace_id, MAX_IDENTIFIER_BYTES)?;
    encoder.put_u64(value.source_workspace_revision)?;
    encoder.put_string(&value.source_fact_id, MAX_IDENTIFIER_BYTES)?;
    encoder.put_string(&value.field, MAX_FACT_FIELD_BYTES)?;
    encoder.put_string(&value.original_value, MAX_FACT_VALUE_BYTES)?;
    match &value.correction {
        Some(value) => {
            encoder.put_bool(true)?;
            encoder.put_string(value, MAX_FACT_VALUE_BYTES)?;
        }
        None => encoder.put_bool(false)?,
    }
    encoder.put_u32(value.kind as u32)?;
    encoder.put_length(value.sources.len(), MAX_LIBRARY_SOURCES)?;
    for item in &value.sources {
        encode_library_source_view(encoder, item)?;
    }
    encoder.put_u64(value.captured_at_epoch_ms)?;
    encoder.put_u64(value.last_checked_epoch_ms)?;
    encoder.put_bool(value.has_conflict)?;
    Ok(())
}

fn decode_library_entry_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<LibraryEntryView, CoreStatusPayloadCodecError>
{
    let value = LibraryEntryView {
        entry_id: decoder.read_string(MAX_IDENTIFIER_BYTES)?,
        revision: decoder.read_u64()?,
        collection_id: decoder.read_string(MAX_IDENTIFIER_BYTES)?,
        collection_name: decoder.read_string(MAX_WORKSPACE_DISPLAY_NAME_BYTES)?,
        source_workspace_id: decoder.read_string(MAX_IDENTIFIER_BYTES)?,
        source_workspace_revision: decoder.read_u64()?,
        source_fact_id: decoder.read_string(MAX_IDENTIFIER_BYTES)?,
        field: decoder.read_string(MAX_FACT_FIELD_BYTES)?,
        original_value: decoder.read_string(MAX_FACT_VALUE_BYTES)?,
        correction: decoder.read_optional(|decoder| decoder.read_string(MAX_FACT_VALUE_BYTES))?,
        kind: decoder.read_u32().and_then(|value| WorkspaceFactKind::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        sources: decoder.read_list(MAX_LIBRARY_SOURCES, |decoder| decode_library_source_view(decoder))?,
        captured_at_epoch_ms: decoder.read_u64()?,
        last_checked_epoch_ms: decoder.read_u64()?,
        has_conflict: decoder.read_bool()?,
    };
    Ok(value)
}

fn encode_library_search_hit_view(encoder: &mut CoreStatusPayloadEncoder, value: &LibrarySearchHitView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_string(&value.entry_id, MAX_IDENTIFIER_BYTES)?;
    encoder.put_u64(value.age_ms)?;
    Ok(())
}

fn decode_library_search_hit_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<LibrarySearchHitView, CoreStatusPayloadCodecError>
{
    let value = LibrarySearchHitView {
        entry_id: decoder.read_string(MAX_IDENTIFIER_BYTES)?,
        age_ms: decoder.read_u64()?,
    };
    Ok(value)
}

fn encode_library_search_view(encoder: &mut CoreStatusPayloadEncoder, value: &LibrarySearchView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_string(&value.request_id, MAX_IDENTIFIER_BYTES)?;
    encoder.put_string(&value.query, MAX_LIBRARY_QUERY_BYTES)?;
    encoder.put_u64(value.library_revision)?;
    encoder.put_length(value.hits.len(), MAX_LIBRARY_SEARCH_RESULTS)?;
    for item in &value.hits {
        encode_library_search_hit_view(encoder, item)?;
    }
    Ok(())
}

fn decode_library_search_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<LibrarySearchView, CoreStatusPayloadCodecError>
{
    let value = LibrarySearchView {
        request_id: decoder.read_string(MAX_IDENTIFIER_BYTES)?,
        query: decoder.read_string(MAX_LIBRARY_QUERY_BYTES)?,
        library_revision: decoder.read_u64()?,
        hits: decoder.read_list(MAX_LIBRARY_SEARCH_RESULTS, |decoder| decode_library_search_hit_view(decoder))?,
    };
    Ok(value)
}

fn encode_library_refresh_source_view(encoder: &mut CoreStatusPayloadEncoder, value: &LibraryRefreshSourceView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_string(&value.source_id, MAX_IDENTIFIER_BYTES)?;
    encoder.put_string(&value.title, MAX_WORKSPACE_TITLE_BYTES)?;
    encoder.put_string(&value.host, MAX_SOURCE_HOST_BYTES)?;
    Ok(())
}

fn decode_library_refresh_source_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<LibraryRefreshSourceView, CoreStatusPayloadCodecError>
{
    let value = LibraryRefreshSourceView {
        source_id: decoder.read_string(MAX_IDENTIFIER_BYTES)?,
        title: decoder.read_string(MAX_WORKSPACE_TITLE_BYTES)?,
        host: decoder.read_string(MAX_SOURCE_HOST_BYTES)?,
    };
    Ok(value)
}

fn encode_library_refresh_preview_view(encoder: &mut CoreStatusPayloadEncoder, value: &LibraryRefreshPreviewView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_string(&value.preview_id, MAX_LIBRARY_REFRESH_PREVIEW_ID_BYTES)?;
    encoder.put_string(&value.collection_id, MAX_IDENTIFIER_BYTES)?;
    encoder.put_u64(value.library_revision)?;
    encoder.put_u64(value.source_workspace_revision)?;
    encoder.put_u32(value.provider_route as u32)?;
    encoder.put_u32(value.navigation_count)?;
    encoder.put_u32(value.observation_count)?;
    encoder.put_length(value.sources.len(), MAX_LIBRARY_REFRESH_SOURCES)?;
    for item in &value.sources {
        encode_library_refresh_source_view(encoder, item)?;
    }
    Ok(())
}

fn decode_library_refresh_preview_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<LibraryRefreshPreviewView, CoreStatusPayloadCodecError>
{
    let value = LibraryRefreshPreviewView {
        preview_id: decoder.read_string(MAX_LIBRARY_REFRESH_PREVIEW_ID_BYTES)?,
        collection_id: decoder.read_string(MAX_IDENTIFIER_BYTES)?,
        library_revision: decoder.read_u64()?,
        source_workspace_revision: decoder.read_u64()?,
        provider_route: decoder.read_u32().and_then(|value| TaskProviderRoute::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        navigation_count: decoder.read_u32()?,
        observation_count: decoder.read_u32()?,
        sources: decoder.read_list(MAX_LIBRARY_REFRESH_SOURCES, |decoder| decode_library_refresh_source_view(decoder))?,
    };
    Ok(value)
}

fn encode_library_refresh_result_item_view(encoder: &mut CoreStatusPayloadEncoder, value: &LibraryRefreshResultItemView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_string(&value.source_id, MAX_IDENTIFIER_BYTES)?;
    encoder.put_u32(value.disposition as u32)?;
    Ok(())
}

fn decode_library_refresh_result_item_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<LibraryRefreshResultItemView, CoreStatusPayloadCodecError>
{
    let value = LibraryRefreshResultItemView {
        source_id: decoder.read_string(MAX_IDENTIFIER_BYTES)?,
        disposition: decoder.read_u32().and_then(|value| LibraryRefreshDisposition::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
    };
    Ok(value)
}

fn encode_library_refresh_result_view(encoder: &mut CoreStatusPayloadEncoder, value: &LibraryRefreshResultView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_string(&value.preview_id, MAX_LIBRARY_REFRESH_PREVIEW_ID_BYTES)?;
    encoder.put_string(&value.collection_id, MAX_IDENTIFIER_BYTES)?;
    encoder.put_length(value.items.len(), MAX_LIBRARY_REFRESH_SOURCES)?;
    for item in &value.items {
        encode_library_refresh_result_item_view(encoder, item)?;
    }
    Ok(())
}

fn decode_library_refresh_result_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<LibraryRefreshResultView, CoreStatusPayloadCodecError>
{
    let value = LibraryRefreshResultView {
        preview_id: decoder.read_string(MAX_LIBRARY_REFRESH_PREVIEW_ID_BYTES)?,
        collection_id: decoder.read_string(MAX_IDENTIFIER_BYTES)?,
        items: decoder.read_list(MAX_LIBRARY_REFRESH_SOURCES, |decoder| decode_library_refresh_result_item_view(decoder))?,
    };
    Ok(value)
}

fn encode_library_view_state(encoder: &mut CoreStatusPayloadEncoder, value: &LibraryViewState) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_u32(value.availability as u32)?;
    encoder.put_u64(value.revision)?;
    encoder.put_length(value.entries.len(), MAX_LIBRARY_ENTRIES)?;
    for item in &value.entries {
        encode_library_entry_view(encoder, item)?;
    }
    match &value.search {
        Some(value) => {
            encoder.put_bool(true)?;
            encode_library_search_view(encoder, value)?;
        }
        None => encoder.put_bool(false)?,
    }
    encoder.put_length(value.refresh_previews.len(), MAX_WORKSPACES)?;
    for item in &value.refresh_previews {
        encode_library_refresh_preview_view(encoder, item)?;
    }
    encoder.put_length(value.refresh_results.len(), MAX_LIBRARY_REFRESH_RESULTS)?;
    for item in &value.refresh_results {
        encode_library_refresh_result_view(encoder, item)?;
    }
    Ok(())
}

fn decode_library_view_state(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<LibraryViewState, CoreStatusPayloadCodecError>
{
    let value = LibraryViewState {
        availability: decoder.read_u32().and_then(|value| LibraryAvailability::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        revision: decoder.read_u64()?,
        entries: decoder.read_list(MAX_LIBRARY_ENTRIES, |decoder| decode_library_entry_view(decoder))?,
        search: decoder.read_optional(|decoder| decode_library_search_view(decoder))?,
        refresh_previews: decoder.read_list(MAX_WORKSPACES, |decoder| decode_library_refresh_preview_view(decoder))?,
        refresh_results: decoder.read_list(MAX_LIBRARY_REFRESH_RESULTS, |decoder| decode_library_refresh_result_view(decoder))?,
    };
    Ok(value)
}

fn encode_library_export_view(encoder: &mut CoreStatusPayloadEncoder, value: &LibraryExportView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_string(&value.request_id, MAX_IDENTIFIER_BYTES)?;
    encoder.put_u64(value.library_revision)?;
    match &value.collection_id {
        Some(value) => {
            encoder.put_bool(true)?;
            encoder.put_string(value, MAX_IDENTIFIER_BYTES)?;
        }
        None => encoder.put_bool(false)?,
    }
    encoder.put_u32(value.format as u32)?;
    encoder.put_string(&value.content, MAX_EXPORT_CONTENT_BYTES)?;
    Ok(())
}

fn decode_library_export_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<LibraryExportView, CoreStatusPayloadCodecError>
{
    let value = LibraryExportView {
        request_id: decoder.read_string(MAX_IDENTIFIER_BYTES)?,
        library_revision: decoder.read_u64()?,
        collection_id: decoder.read_optional(|decoder| decoder.read_string(MAX_IDENTIFIER_BYTES))?,
        format: decoder.read_u32().and_then(|value| WorkspaceExportFormat::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        content: decoder.read_string(MAX_EXPORT_CONTENT_BYTES)?,
    };
    Ok(value)
}

fn encode_memory_workspace_view(encoder: &mut CoreStatusPayloadEncoder, value: &MemoryWorkspaceView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_string(&value.workspace_id, MAX_IDENTIFIER_BYTES)?;
    encoder.put_string(&value.display_name, MAX_WORKSPACE_DISPLAY_NAME_BYTES)?;
    Ok(())
}

fn decode_memory_workspace_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<MemoryWorkspaceView, CoreStatusPayloadCodecError>
{
    let value = MemoryWorkspaceView {
        workspace_id: decoder.read_string(MAX_IDENTIFIER_BYTES)?,
        display_name: decoder.read_string(MAX_WORKSPACE_DISPLAY_NAME_BYTES)?,
    };
    Ok(value)
}

fn encode_memory_record_view(encoder: &mut CoreStatusPayloadEncoder, value: &MemoryRecordView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_string(&value.memory_id, MAX_IDENTIFIER_BYTES)?;
    encoder.put_u64(value.revision)?;
    encoder.put_string(&value.statement, MAX_MEMORY_STATEMENT_BYTES)?;
    encoder.put_u32(value.source_kind as u32)?;
    match &value.source_task_id {
        Some(value) => {
            encoder.put_bool(true)?;
            encoder.put_string(value, MAX_IDENTIFIER_BYTES)?;
        }
        None => encoder.put_bool(false)?,
    }
    match &value.source_workspace {
        Some(value) => {
            encoder.put_bool(true)?;
            encode_memory_workspace_view(encoder, value)?;
        }
        None => encoder.put_bool(false)?,
    }
    encoder.put_u32(value.scope_kind as u32)?;
    match &value.scope_workspace {
        Some(value) => {
            encoder.put_bool(true)?;
            encode_memory_workspace_view(encoder, value)?;
        }
        None => encoder.put_bool(false)?,
    }
    encoder.put_u32(value.sensitivity as u32)?;
    encoder.put_u64(value.created_at_epoch_ms)?;
    encoder.put_u64(value.updated_at_epoch_ms)?;
    encoder.put_u64(value.reviewed_at_epoch_ms)?;
    encoder.put_u64(value.expires_at_epoch_ms)?;
    Ok(())
}

fn decode_memory_record_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<MemoryRecordView, CoreStatusPayloadCodecError>
{
    let value = MemoryRecordView {
        memory_id: decoder.read_string(MAX_IDENTIFIER_BYTES)?,
        revision: decoder.read_u64()?,
        statement: decoder.read_string(MAX_MEMORY_STATEMENT_BYTES)?,
        source_kind: decoder.read_u32().and_then(|value| MemorySourceKind::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        source_task_id: decoder.read_optional(|decoder| decoder.read_string(MAX_IDENTIFIER_BYTES))?,
        source_workspace: decoder.read_optional(|decoder| decode_memory_workspace_view(decoder))?,
        scope_kind: decoder.read_u32().and_then(|value| MemoryScopeKind::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        scope_workspace: decoder.read_optional(|decoder| decode_memory_workspace_view(decoder))?,
        sensitivity: decoder.read_u32().and_then(|value| MemorySensitivity::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        created_at_epoch_ms: decoder.read_u64()?,
        updated_at_epoch_ms: decoder.read_u64()?,
        reviewed_at_epoch_ms: decoder.read_u64()?,
        expires_at_epoch_ms: decoder.read_u64()?,
    };
    Ok(value)
}

fn encode_memory_search_hit_view(encoder: &mut CoreStatusPayloadEncoder, value: &MemorySearchHitView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_string(&value.memory_id, MAX_IDENTIFIER_BYTES)?;
    Ok(())
}

fn decode_memory_search_hit_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<MemorySearchHitView, CoreStatusPayloadCodecError>
{
    let value = MemorySearchHitView {
        memory_id: decoder.read_string(MAX_IDENTIFIER_BYTES)?,
    };
    Ok(value)
}

fn encode_memory_search_view(encoder: &mut CoreStatusPayloadEncoder, value: &MemorySearchView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_string(&value.request_id, MAX_IDENTIFIER_BYTES)?;
    encoder.put_string(&value.query, MAX_MEMORY_QUERY_BYTES)?;
    encoder.put_u64(value.memory_revision)?;
    encoder.put_length(value.hits.len(), MAX_MEMORY_SEARCH_RESULTS)?;
    for item in &value.hits {
        encode_memory_search_hit_view(encoder, item)?;
    }
    Ok(())
}

fn decode_memory_search_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<MemorySearchView, CoreStatusPayloadCodecError>
{
    let value = MemorySearchView {
        request_id: decoder.read_string(MAX_IDENTIFIER_BYTES)?,
        query: decoder.read_string(MAX_MEMORY_QUERY_BYTES)?,
        memory_revision: decoder.read_u64()?,
        hits: decoder.read_list(MAX_MEMORY_SEARCH_RESULTS, |decoder| decode_memory_search_hit_view(decoder))?,
    };
    Ok(value)
}

fn encode_memory_view_state(encoder: &mut CoreStatusPayloadEncoder, value: &MemoryViewState) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_u32(value.availability as u32)?;
    encoder.put_u64(value.revision)?;
    encoder.put_length(value.records.len(), MAX_MEMORY_RECORDS)?;
    for item in &value.records {
        encode_memory_record_view(encoder, item)?;
    }
    match &value.search {
        Some(value) => {
            encoder.put_bool(true)?;
            encode_memory_search_view(encoder, value)?;
        }
        None => encoder.put_bool(false)?,
    }
    Ok(())
}

fn decode_memory_view_state(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<MemoryViewState, CoreStatusPayloadCodecError>
{
    let value = MemoryViewState {
        availability: decoder.read_u32().and_then(|value| MemoryAvailability::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        revision: decoder.read_u64()?,
        records: decoder.read_list(MAX_MEMORY_RECORDS, |decoder| decode_memory_record_view(decoder))?,
        search: decoder.read_optional(|decoder| decode_memory_search_view(decoder))?,
    };
    Ok(value)
}

fn encode_auth_account_view(encoder: &mut CoreStatusPayloadEncoder, value: &AuthAccountView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_string(&value.account_id, MAX_IDENTIFIER_BYTES)?;
    match &value.display_name {
        Some(value) => {
            encoder.put_bool(true)?;
            encoder.put_string(value, MAX_AUTH_DISPLAY_NAME_BYTES)?;
        }
        None => encoder.put_bool(false)?,
    }
    match &value.email {
        Some(value) => {
            encoder.put_bool(true)?;
            encoder.put_string(value, MAX_AUTH_EMAIL_BYTES)?;
        }
        None => encoder.put_bool(false)?,
    }
    encoder.put_u32(value.method as u32)?;
    Ok(())
}

fn decode_auth_account_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<AuthAccountView, CoreStatusPayloadCodecError>
{
    let value = AuthAccountView {
        account_id: decoder.read_string(MAX_IDENTIFIER_BYTES)?,
        display_name: decoder.read_optional(|decoder| decoder.read_string(MAX_AUTH_DISPLAY_NAME_BYTES))?,
        email: decoder.read_optional(|decoder| decoder.read_string(MAX_AUTH_EMAIL_BYTES))?,
        method: decoder.read_u32().and_then(|value| AuthProvider::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
    };
    Ok(value)
}

fn encode_auth_failure(encoder: &mut CoreStatusPayloadEncoder, value: &AuthFailure) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_u32(value.code as u32)?;
    encoder.put_bool(value.retryable)?;
    Ok(())
}

fn decode_auth_failure(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<AuthFailure, CoreStatusPayloadCodecError>
{
    let value = AuthFailure {
        code: decoder.read_u32().and_then(|value| AuthFailureCode::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        retryable: decoder.read_bool()?,
    };
    Ok(value)
}

fn encode_auth_method_view(encoder: &mut CoreStatusPayloadEncoder, value: &AuthMethodView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_u32(value.provider as u32)?;
    encoder.put_u32(value.availability as u32)?;
    Ok(())
}

fn decode_auth_method_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<AuthMethodView, CoreStatusPayloadCodecError>
{
    let value = AuthMethodView {
        provider: decoder.read_u32().and_then(|value| AuthProvider::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        availability: decoder.read_u32().and_then(|value| AuthMethodAvailability::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
    };
    Ok(value)
}

fn encode_entitlement_view(encoder: &mut CoreStatusPayloadEncoder, value: &EntitlementView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_string(&value.plan_id, MAX_IDENTIFIER_BYTES)?;
    encoder.put_u64(value.credits_granted)?;
    encoder.put_u64(value.credits_remaining)?;
    encoder.put_u64(value.next_renewal_epoch_seconds)?;
    encoder.put_u64(value.valid_until_epoch_seconds)?;
    Ok(())
}

fn decode_entitlement_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<EntitlementView, CoreStatusPayloadCodecError>
{
    let value = EntitlementView {
        plan_id: decoder.read_string(MAX_IDENTIFIER_BYTES)?,
        credits_granted: decoder.read_u64()?,
        credits_remaining: decoder.read_u64()?,
        next_renewal_epoch_seconds: decoder.read_u64()?,
        valid_until_epoch_seconds: decoder.read_u64()?,
    };
    Ok(value)
}

fn validate_auth_view_state(value: &AuthViewState) -> Result<(), CoreStatusPayloadCodecError> {
    if matches!(value.phase, AuthPhase::SignedIn)
        != value.account.is_some()
    {
        return Err(CoreStatusPayloadCodecError::Malformed);
    }
    if matches!(value.phase, AuthPhase::LinkSent)
        != value.pending_email.is_some()
    {
        return Err(CoreStatusPayloadCodecError::Malformed);
    }
    if matches!(value.phase, AuthPhase::Failed)
        != value.failure.is_some()
    {
        return Err(CoreStatusPayloadCodecError::Malformed);
    }
    Ok(())
}

fn encode_auth_view_state(encoder: &mut CoreStatusPayloadEncoder, value: &AuthViewState) -> Result<(), CoreStatusPayloadCodecError> {
    validate_auth_view_state(value)?;
    encoder.put_u32(value.phase as u32)?;
    match &value.account {
        Some(value) => {
            encoder.put_bool(true)?;
            encode_auth_account_view(encoder, value)?;
        }
        None => encoder.put_bool(false)?,
    }
    match &value.pending_email {
        Some(value) => {
            encoder.put_bool(true)?;
            encoder.put_string(value, MAX_AUTH_EMAIL_BYTES)?;
        }
        None => encoder.put_bool(false)?,
    }
    match &value.failure {
        Some(value) => {
            encoder.put_bool(true)?;
            encode_auth_failure(encoder, value)?;
        }
        None => encoder.put_bool(false)?,
    }
    encoder.put_length(value.methods.len(), MAX_AUTH_METHODS)?;
    for item in &value.methods {
        encode_auth_method_view(encoder, item)?;
    }
    match &value.entitlement {
        Some(value) => {
            encoder.put_bool(true)?;
            encode_entitlement_view(encoder, value)?;
        }
        None => encoder.put_bool(false)?,
    }
    Ok(())
}

fn decode_auth_view_state(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<AuthViewState, CoreStatusPayloadCodecError>
{
    let value = AuthViewState {
        phase: decoder.read_u32().and_then(|value| AuthPhase::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        account: decoder.read_optional(|decoder| decode_auth_account_view(decoder))?,
        pending_email: decoder.read_optional(|decoder| decoder.read_string(MAX_AUTH_EMAIL_BYTES))?,
        failure: decoder.read_optional(|decoder| decode_auth_failure(decoder))?,
        methods: decoder.read_list(MAX_AUTH_METHODS, |decoder| decode_auth_method_view(decoder))?,
        entitlement: decoder.read_optional(|decoder| decode_entitlement_view(decoder))?,
    };
    validate_auth_view_state(&value)?;
    Ok(value)
}

fn encode_asset_refusal(encoder: &mut CoreStatusPayloadEncoder, value: &AssetRefusal) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_u32(value.reason as u32)?;
    encoder.put_bool(value.retryable)?;
    Ok(())
}

fn decode_asset_refusal(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<AssetRefusal, CoreStatusPayloadCodecError>
{
    let value = AssetRefusal {
        reason: decoder.read_u32().and_then(|value| AssetRefusalView::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        retryable: decoder.read_bool()?,
    };
    Ok(value)
}

fn encode_asset_view_state(encoder: &mut CoreStatusPayloadEncoder, value: &AssetViewState) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_string(&value.asset_id, MAX_IDENTIFIER_BYTES)?;
    encoder.put_string(&value.asset_revision, MAX_IDENTIFIER_BYTES)?;
    encoder.put_u32(value.kind as u32)?;
    encoder.put_u32(value.presence as u32)?;
    encoder.put_u64(value.written_bytes)?;
    encoder.put_u64(value.total_bytes)?;
    encoder.put_u32(value.attempts)?;
    match &value.refusal {
        Some(value) => {
            encoder.put_bool(true)?;
            encode_asset_refusal(encoder, value)?;
        }
        None => encoder.put_bool(false)?,
    }
    encoder.put_u64(value.waiting_until_monotonic_ms)?;
    Ok(())
}

fn decode_asset_view_state(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<AssetViewState, CoreStatusPayloadCodecError>
{
    let value = AssetViewState {
        asset_id: decoder.read_string(MAX_IDENTIFIER_BYTES)?,
        asset_revision: decoder.read_string(MAX_IDENTIFIER_BYTES)?,
        kind: decoder.read_u32().and_then(|value| AssetKindView::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        presence: decoder.read_u32().and_then(|value| AssetPresenceView::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        written_bytes: decoder.read_u64()?,
        total_bytes: decoder.read_u64()?,
        attempts: decoder.read_u32()?,
        refusal: decoder.read_optional(|decoder| decode_asset_refusal(decoder))?,
        waiting_until_monotonic_ms: decoder.read_u64()?,
    };
    Ok(value)
}

fn encode_asset_delivery_view(encoder: &mut CoreStatusPayloadEncoder, value: &AssetDeliveryView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_bool(value.platform_supported)?;
    encoder.put_u32(value.network_cost as u32)?;
    encoder.put_bool(value.metered_permitted)?;
    encoder.put_length(value.assets.len(), MAX_ASSETS)?;
    for item in &value.assets {
        encode_asset_view_state(encoder, item)?;
    }
    Ok(())
}

fn decode_asset_delivery_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<AssetDeliveryView, CoreStatusPayloadCodecError>
{
    let value = AssetDeliveryView {
        platform_supported: decoder.read_bool()?,
        network_cost: decoder.read_u32().and_then(|value| AssetNetworkCostView::from_wire(value).ok_or(CoreStatusPayloadCodecError::InvalidEnum))?,
        metered_permitted: decoder.read_bool()?,
        assets: decoder.read_list(MAX_ASSETS, |decoder| decode_asset_view_state(decoder))?,
    };
    Ok(value)
}

fn encode_custom_model_spec_view(encoder: &mut CoreStatusPayloadEncoder, value: &CustomModelSpecView) -> Result<(), CoreStatusPayloadCodecError> {
    encoder.put_string(&value.model_id, MAX_MODEL_ID_BYTES)?;
    encoder.put_string(&value.display_name, MAX_MODEL_DISPLAY_NAME_BYTES)?;
    encoder.put_u32(value.context_window)?;
    encoder.put_u32(value.max_output_tokens)?;
    encoder.put_bool(value.reasoning)?;
    encoder.put_bool(value.tool_calling)?;
    Ok(())
}

fn decode_custom_model_spec_view(decoder: &mut CoreStatusPayloadDecoder<'_>)
    -> Result<CustomModelSpecView, CoreStatusPayloadCodecError>
{
    let value = CustomModelSpecView {
        model_id: decoder.read_string(MAX_MODEL_ID_BYTES)?,
        display_name: decoder.read_string(MAX_MODEL_DISPLAY_NAME_BYTES)?,
        context_window: decoder.read_u32()?,
        max_output_tokens: decoder.read_u32()?,
        reasoning: decoder.read_bool()?,
        tool_calling: decoder.read_bool()?,
    };
    Ok(value)
}

pub fn encode_core_status_payload(
    value: &CoreStatus,
) -> Result<Vec<u8>, CoreStatusPayloadCodecError> {
    let mut encoder = CoreStatusPayloadEncoder { bytes: Some(Vec::new()), length: 0 };
    encoder.put_raw(CORE_STATUS_PAYLOAD_MAGIC)?;
    encoder.put_u32(CORE_STATUS_PAYLOAD_SCHEMA_VERSION)?;
    encode_core_status(&mut encoder, value)?;
    encoder.bytes.ok_or(CoreStatusPayloadCodecError::LengthOverflow)
}

/// Measures the exact encoded payload path without allocating its byte buffer.
pub fn measure_core_status_payload(
    value: &CoreStatus,
) -> Result<usize, CoreStatusPayloadCodecError> {
    let mut encoder = CoreStatusPayloadEncoder { bytes: None, length: 0 };
    encoder.put_raw(CORE_STATUS_PAYLOAD_MAGIC)?;
    encoder.put_u32(CORE_STATUS_PAYLOAD_SCHEMA_VERSION)?;
    encode_core_status(&mut encoder, value)?;
    Ok(encoder.length)
}

pub fn decode_core_status_payload(
    bytes: &[u8],
) -> Result<CoreStatus, CoreStatusPayloadCodecError> {
    if bytes.len() > MAX_EVENT_PAYLOAD_BYTES {
        return Err(CoreStatusPayloadCodecError::SizeLimit);
    }
    let mut decoder = CoreStatusPayloadDecoder { bytes, offset: 0 };
    if decoder.take(CORE_STATUS_PAYLOAD_MAGIC.len())? != CORE_STATUS_PAYLOAD_MAGIC {
        return Err(CoreStatusPayloadCodecError::InvalidMagic);
    }
    if decoder.read_u32()? != CORE_STATUS_PAYLOAD_SCHEMA_VERSION {
        return Err(CoreStatusPayloadCodecError::UnsupportedVersion);
    }
    let value = decode_core_status(&mut decoder)?;
    if decoder.offset != bytes.len() {
        return Err(CoreStatusPayloadCodecError::TrailingBytes);
    }
    Ok(value)
}
