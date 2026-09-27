// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Generated from schema/contract.json. Do not edit.
// Contract core_api 3.49 state payload.

import {
  AssistantAbilityView,
  BuiltinSkillIdView,
  BuiltinSkillAvailabilityView,
  CoreStatusProjectionMode,
  CoreStatusProjectionFamily,
  SiteSkillArgumentKind,
  SiteSkillProvenanceView,
  SiteSkillStatusView,
  PersonalityPresetView,
  TaskTemplateId,
  TaskProviderRoute,
  CoreAvailability,
  SavedDataAvailability,
  TaskPhase,
  TaskControlKind,
  CoreFailureCode,
  AuthProvider,
  AuthMethodAvailability,
  AuthPhase,
  AuthFailureCode,
  WorkspacePhase,
  WorkspaceFactKind,
  WorkspaceExportFormat,
  TaskActivityKind,
  TaskArtifactKind,
  LibraryAvailability,
  LibraryRefreshDisposition,
  MemoryAvailability,
  MemorySourceKind,
  MemoryScopeKind,
  MemorySensitivity,
  AssetKindView,
  AssetPresenceView,
  AssetNetworkCostView,
  AssetRefusalView,
  ProviderAuthMethodView,
  ProviderProbeVerdictView,
  ProviderCredentialStateView,
  ProviderRefusalView,
  ProviderOriginView,
  CatalogLayerView,
  ThinkingLevelView,
  ModelRoleView,
  InputModalityView,
  ServerKindView,
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
} from "./core_api";
import type {
  SiteSkillSemanticTarget,
  SiteSkillObservedArgument,
  SiteSkillObservedStep,
  SiteSkillView,
  AssistantConfigurationView,
  BuiltinSkillReferenceView,
  BuiltinSkillView,
  CoreStatusProjectionOmission,
  CoreFailure,
  TaskActivityView,
  TaskArtifactView,
  TaskViewState,
  ActionApprovalView,
  StoredCredentialView,
  ThinkingPreferenceView,
  ProviderRefusalStateView,
  ProviderPresentationView,
  ProviderModelView,
  ProviderRosterEntry,
  ProviderProbeView,
  ProbeEndpointView,
  SavedSignInView,
  SavedSignInsView,
  SavedDetailView,
  SavedDetailsView,
  CoreStatus,
  WorkspaceViewState,
  WorkspaceDeletionPreviewView,
  WorkspaceSourceView,
  WorkspaceFactView,
  WorkspaceExportView,
  LibrarySourceView,
  LibraryEntryView,
  LibrarySearchHitView,
  LibrarySearchView,
  LibraryRefreshSourceView,
  LibraryRefreshPreviewView,
  LibraryRefreshResultItemView,
  LibraryRefreshResultView,
  LibraryViewState,
  LibraryExportView,
  MemoryWorkspaceView,
  MemoryRecordView,
  MemorySearchHitView,
  MemorySearchView,
  MemoryViewState,
  AuthAccountView,
  AuthFailure,
  AuthMethodView,
  EntitlementView,
  AuthViewState,
  AssetRefusal,
  AssetViewState,
  AssetDeliveryView,
  CustomModelSpecView,
} from "./core_api";

export enum CoreStatusPayloadCodecError {
  SizeLimit, CollectionLimit, StringLimit, ValueLimit, LengthOverflow,
  Truncated, InvalidMagic, UnsupportedVersion, InvalidBoolean, InvalidEnum,
  InvalidUtf8, Malformed, TrailingBytes,
}

export type CoreStatusPayloadEncodeResult =
  | { readonly ok: true; readonly bytes: Uint8Array }
  | { readonly ok: false; readonly error: CoreStatusPayloadCodecError };

export type CoreStatusPayloadDecodeResult =
  | { readonly ok: true; readonly value: CoreStatus }
  | { readonly ok: false; readonly error: CoreStatusPayloadCodecError };

const CORE_STATUS_PAYLOAD_MAGIC = new Uint8Array([84, 65, 70, 70, 89, 83, 84, 65]);
export const CORE_STATUS_PAYLOAD_SCHEMA_VERSION = 33 as const;

class CoreStatusCodecFailure extends Error {
  constructor(readonly reason: CoreStatusPayloadCodecError) { super(); }
}

function fail(reason: CoreStatusPayloadCodecError): never {
  throw new CoreStatusCodecFailure(reason);
}

class CoreStatusPayloadEncoder {
  private readonly bytes = new Uint8Array(MAX_EVENT_PAYLOAD_BYTES);
  private offset = 0;

  finish(): Uint8Array { return this.bytes.slice(0, this.offset); }

  putRaw(value: Uint8Array): void {
    if (value.length > this.bytes.length - this.offset) fail(CoreStatusPayloadCodecError.SizeLimit);
    this.bytes.set(value, this.offset);
    this.offset += value.length;
  }

  private putByte(value: number): void {
    if (this.offset >= this.bytes.length) fail(CoreStatusPayloadCodecError.SizeLimit);
    this.bytes[this.offset++] = value;
  }

  putBoolean(value: boolean): void { this.putByte(value ? 1 : 0); }

  putU32(value: number): void {
    if (!Number.isInteger(value) || value < 0 || value > 0xffff_ffff) fail(CoreStatusPayloadCodecError.ValueLimit);
    for (let shift = 0; shift < 4; shift++) {
      this.putByte(Math.floor(value / 2 ** (shift * 8)) & 0xff);
    }
  }

  putBoundedU32(value: number, limit: number): void {
    if (value > limit) fail(CoreStatusPayloadCodecError.ValueLimit);
    this.putU32(value);
  }

  putU64(value: bigint): void {
    if (value < 0n || value > 0xffff_ffff_ffff_ffffn) fail(CoreStatusPayloadCodecError.ValueLimit);
    for (let shift = 0n; shift < 64n; shift += 8n) this.putByte(Number((value >> shift) & 0xffn));
  }

  putLength(value: number, limit: number): void {
    if (!Number.isInteger(value) || value < 0 || value > limit) fail(CoreStatusPayloadCodecError.CollectionLimit);
    this.putU32(value);
  }

  putString(value: string, limit: number): void {
    const encoded = new TextEncoder().encode(value);
    if (encoded.length > limit) fail(CoreStatusPayloadCodecError.StringLimit);
    this.putU32(encoded.length);
    this.putRaw(encoded);
  }
}

class CoreStatusPayloadDecoder {
  offset = 0;
  constructor(private readonly bytes: Uint8Array) {}

  take(length: number): Uint8Array {
    if (!Number.isInteger(length) || length < 0 || length > this.bytes.length - this.offset) {
      fail(CoreStatusPayloadCodecError.Truncated);
    }
    const value = this.bytes.slice(this.offset, this.offset + length);
    this.offset += length;
    return value;
  }

  private readByte(): number {
    if (this.offset >= this.bytes.length) fail(CoreStatusPayloadCodecError.Truncated);
    // The bound above already proves this index is in range, but under
    // noUncheckedIndexedAccess the compiler types it as possibly
    // undefined and cannot see that. Answer it with the same refusal
    // rather than a non-null assertion: a decoder whose truncation
    // check is a claim rather than a branch is exactly what fails open.
    const byte = this.bytes[this.offset++];
    if (byte === undefined) fail(CoreStatusPayloadCodecError.Truncated);
    return byte;
  }

  readBoolean(): boolean {
    const value = this.readByte();
    if (value === 0) return false;
    if (value === 1) return true;
    return fail(CoreStatusPayloadCodecError.InvalidBoolean);
  }

  readU32(): number {
    let value = 0;
    for (let shift = 0; shift < 4; shift++) value += this.readByte() * 2 ** (shift * 8);
    return value;
  }

  readBoundedU32(limit: number): number {
    const value = this.readU32();
    if (value > limit) fail(CoreStatusPayloadCodecError.ValueLimit);
    return value;
  }

  readU64(): bigint {
    let value = 0n;
    for (let shift = 0n; shift < 64n; shift += 8n) value |= BigInt(this.readByte()) << shift;
    return value;
  }

  private readLength(limit: number): number {
    const value = this.readU32();
    if (value > limit) fail(CoreStatusPayloadCodecError.CollectionLimit);
    return value;
  }

  readString(limit: number): string {
    const length = this.readU32();
    if (length > limit) fail(CoreStatusPayloadCodecError.StringLimit);
    const value = this.take(length);
    try { return new TextDecoder("utf-8", { fatal: true }).decode(value); }
    catch { return fail(CoreStatusPayloadCodecError.InvalidUtf8); }
  }

  readList<T>(limit: number, read: () => T): ReadonlyArray<T> {
    const length = this.readLength(limit);
    const values: T[] = [];
    for (let index = 0; index < length; index++) values.push(read());
    return values;
  }
}

function encodeAssistantAbilityView(encoder: CoreStatusPayloadEncoder, value: AssistantAbilityView): void {
  switch (value) {
    case AssistantAbilityView.PagesLookup:
    case AssistantAbilityView.PagesCompare:
    case AssistantAbilityView.PagesSummarize:
    case AssistantAbilityView.PagesTable:
    case AssistantAbilityView.Products:
    case AssistantAbilityView.Offers:
    case AssistantAbilityView.Form:
    case AssistantAbilityView.Downloads:
    case AssistantAbilityView.Pdf:
    case AssistantAbilityView.Sheet:
    case AssistantAbilityView.Document:
    case AssistantAbilityView.Depth:
    case AssistantAbilityView.Trip:
    case AssistantAbilityView.Pictures:
    case AssistantAbilityView.Video:
    case AssistantAbilityView.Keep:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeAssistantAbilityView(decoder: CoreStatusPayloadDecoder): AssistantAbilityView {
  const value = decoder.readU32();
  switch (value) {
    case AssistantAbilityView.PagesLookup:
    case AssistantAbilityView.PagesCompare:
    case AssistantAbilityView.PagesSummarize:
    case AssistantAbilityView.PagesTable:
    case AssistantAbilityView.Products:
    case AssistantAbilityView.Offers:
    case AssistantAbilityView.Form:
    case AssistantAbilityView.Downloads:
    case AssistantAbilityView.Pdf:
    case AssistantAbilityView.Sheet:
    case AssistantAbilityView.Document:
    case AssistantAbilityView.Depth:
    case AssistantAbilityView.Trip:
    case AssistantAbilityView.Pictures:
    case AssistantAbilityView.Video:
    case AssistantAbilityView.Keep:
      return value as AssistantAbilityView;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeBuiltinSkillIdView(encoder: CoreStatusPayloadEncoder, value: BuiltinSkillIdView): void {
  switch (value) {
    case BuiltinSkillIdView.GeneralWebResearch:
    case BuiltinSkillIdView.DeepResearch:
    case BuiltinSkillIdView.ProductComparison:
    case BuiltinSkillIdView.MultiTabComparison:
    case BuiltinSkillIdView.WebsiteSummarizer:
    case BuiltinSkillIdView.PdfAnalysis:
    case BuiltinSkillIdView.DataExtraction:
    case BuiltinSkillIdView.FormAssistant:
    case BuiltinSkillIdView.Shopping:
    case BuiltinSkillIdView.DownloadOrganizer:
    case BuiltinSkillIdView.TravelResearch:
    case BuiltinSkillIdView.VideoTranscriptAnalyzer:
    case BuiltinSkillIdView.ImageUnderstanding:
    case BuiltinSkillIdView.LibraryBuilder:
    case BuiltinSkillIdView.SpreadsheetBuilder:
    case BuiltinSkillIdView.DocumentGenerator:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeBuiltinSkillIdView(decoder: CoreStatusPayloadDecoder): BuiltinSkillIdView {
  const value = decoder.readU32();
  switch (value) {
    case BuiltinSkillIdView.GeneralWebResearch:
    case BuiltinSkillIdView.DeepResearch:
    case BuiltinSkillIdView.ProductComparison:
    case BuiltinSkillIdView.MultiTabComparison:
    case BuiltinSkillIdView.WebsiteSummarizer:
    case BuiltinSkillIdView.PdfAnalysis:
    case BuiltinSkillIdView.DataExtraction:
    case BuiltinSkillIdView.FormAssistant:
    case BuiltinSkillIdView.Shopping:
    case BuiltinSkillIdView.DownloadOrganizer:
    case BuiltinSkillIdView.TravelResearch:
    case BuiltinSkillIdView.VideoTranscriptAnalyzer:
    case BuiltinSkillIdView.ImageUnderstanding:
    case BuiltinSkillIdView.LibraryBuilder:
    case BuiltinSkillIdView.SpreadsheetBuilder:
    case BuiltinSkillIdView.DocumentGenerator:
      return value as BuiltinSkillIdView;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeBuiltinSkillAvailabilityView(encoder: CoreStatusPayloadEncoder, value: BuiltinSkillAvailabilityView): void {
  switch (value) {
    case BuiltinSkillAvailabilityView.Available:
    case BuiltinSkillAvailabilityView.RequiredToolUnavailable:
    case BuiltinSkillAvailabilityView.RequiredPartMissing:
    case BuiltinSkillAvailabilityView.ProfileUnavailable:
    case BuiltinSkillAvailabilityView.PolicyUnavailable:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeBuiltinSkillAvailabilityView(decoder: CoreStatusPayloadDecoder): BuiltinSkillAvailabilityView {
  const value = decoder.readU32();
  switch (value) {
    case BuiltinSkillAvailabilityView.Available:
    case BuiltinSkillAvailabilityView.RequiredToolUnavailable:
    case BuiltinSkillAvailabilityView.RequiredPartMissing:
    case BuiltinSkillAvailabilityView.ProfileUnavailable:
    case BuiltinSkillAvailabilityView.PolicyUnavailable:
      return value as BuiltinSkillAvailabilityView;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeCoreStatusProjectionMode(encoder: CoreStatusPayloadEncoder, value: CoreStatusProjectionMode): void {
  switch (value) {
    case CoreStatusProjectionMode.Complete:
    case CoreStatusProjectionMode.RecoveryRequired:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeCoreStatusProjectionMode(decoder: CoreStatusPayloadDecoder): CoreStatusProjectionMode {
  const value = decoder.readU32();
  switch (value) {
    case CoreStatusProjectionMode.Complete:
    case CoreStatusProjectionMode.RecoveryRequired:
      return value as CoreStatusProjectionMode;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeCoreStatusProjectionFamily(encoder: CoreStatusPayloadEncoder, value: CoreStatusProjectionFamily): void {
  switch (value) {
    case CoreStatusProjectionFamily.ActiveTasks:
    case CoreStatusProjectionFamily.Workspaces:
    case CoreStatusProjectionFamily.WorkspaceExport:
    case CoreStatusProjectionFamily.AssetDelivery:
    case CoreStatusProjectionFamily.ProviderRoster:
    case CoreStatusProjectionFamily.ProviderProbes:
    case CoreStatusProjectionFamily.ProviderModels:
    case CoreStatusProjectionFamily.Library:
    case CoreStatusProjectionFamily.LibraryExport:
    case CoreStatusProjectionFamily.Memory:
    case CoreStatusProjectionFamily.SavedSignIns:
    case CoreStatusProjectionFamily.SavedDetails:
    case CoreStatusProjectionFamily.SiteSkills:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeCoreStatusProjectionFamily(decoder: CoreStatusPayloadDecoder): CoreStatusProjectionFamily {
  const value = decoder.readU32();
  switch (value) {
    case CoreStatusProjectionFamily.ActiveTasks:
    case CoreStatusProjectionFamily.Workspaces:
    case CoreStatusProjectionFamily.WorkspaceExport:
    case CoreStatusProjectionFamily.AssetDelivery:
    case CoreStatusProjectionFamily.ProviderRoster:
    case CoreStatusProjectionFamily.ProviderProbes:
    case CoreStatusProjectionFamily.ProviderModels:
    case CoreStatusProjectionFamily.Library:
    case CoreStatusProjectionFamily.LibraryExport:
    case CoreStatusProjectionFamily.Memory:
    case CoreStatusProjectionFamily.SavedSignIns:
    case CoreStatusProjectionFamily.SavedDetails:
    case CoreStatusProjectionFamily.SiteSkills:
      return value as CoreStatusProjectionFamily;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeSiteSkillArgumentKind(encoder: CoreStatusPayloadEncoder, value: SiteSkillArgumentKind): void {
  switch (value) {
    case SiteSkillArgumentKind.FromEarlierStep:
    case SiteSkillArgumentKind.FromPerson:
    case SiteSkillArgumentKind.Choice:
    case SiteSkillArgumentKind.Count:
    case SiteSkillArgumentKind.Flag:
    case SiteSkillArgumentKind.PublicAddress:
    case SiteSkillArgumentKind.SemanticTarget:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeSiteSkillArgumentKind(decoder: CoreStatusPayloadDecoder): SiteSkillArgumentKind {
  const value = decoder.readU32();
  switch (value) {
    case SiteSkillArgumentKind.FromEarlierStep:
    case SiteSkillArgumentKind.FromPerson:
    case SiteSkillArgumentKind.Choice:
    case SiteSkillArgumentKind.Count:
    case SiteSkillArgumentKind.Flag:
    case SiteSkillArgumentKind.PublicAddress:
    case SiteSkillArgumentKind.SemanticTarget:
      return value as SiteSkillArgumentKind;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeSiteSkillProvenanceView(encoder: CoreStatusPayloadEncoder, value: SiteSkillProvenanceView): void {
  switch (value) {
    case SiteSkillProvenanceView.Authored:
    case SiteSkillProvenanceView.RecordedFromTask:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeSiteSkillProvenanceView(decoder: CoreStatusPayloadDecoder): SiteSkillProvenanceView {
  const value = decoder.readU32();
  switch (value) {
    case SiteSkillProvenanceView.Authored:
    case SiteSkillProvenanceView.RecordedFromTask:
      return value as SiteSkillProvenanceView;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeSiteSkillStatusView(encoder: CoreStatusPayloadEncoder, value: SiteSkillStatusView): void {
  switch (value) {
    case SiteSkillStatusView.Draft:
    case SiteSkillStatusView.Active:
    case SiteSkillStatusView.Superseded:
    case SiteSkillStatusView.Retired:
    case SiteSkillStatusView.Disabled:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeSiteSkillStatusView(decoder: CoreStatusPayloadDecoder): SiteSkillStatusView {
  const value = decoder.readU32();
  switch (value) {
    case SiteSkillStatusView.Draft:
    case SiteSkillStatusView.Active:
    case SiteSkillStatusView.Superseded:
    case SiteSkillStatusView.Retired:
    case SiteSkillStatusView.Disabled:
      return value as SiteSkillStatusView;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodePersonalityPresetView(encoder: CoreStatusPayloadEncoder, value: PersonalityPresetView): void {
  switch (value) {
    case PersonalityPresetView.CarefulResearcher:
    case PersonalityPresetView.QuickShopper:
    case PersonalityPresetView.TripPlanner:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodePersonalityPresetView(decoder: CoreStatusPayloadDecoder): PersonalityPresetView {
  const value = decoder.readU32();
  switch (value) {
    case PersonalityPresetView.CarefulResearcher:
    case PersonalityPresetView.QuickShopper:
    case PersonalityPresetView.TripPlanner:
      return value as PersonalityPresetView;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeTaskTemplateId(encoder: CoreStatusPayloadEncoder, value: TaskTemplateId): void {
  switch (value) {
    case TaskTemplateId.CompareProducts:
    case TaskTemplateId.SummarizeEvidence:
    case TaskTemplateId.BuildSourceTable:
    case TaskTemplateId.WebErrand:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeTaskTemplateId(decoder: CoreStatusPayloadDecoder): TaskTemplateId {
  const value = decoder.readU32();
  switch (value) {
    case TaskTemplateId.CompareProducts:
    case TaskTemplateId.SummarizeEvidence:
    case TaskTemplateId.BuildSourceTable:
    case TaskTemplateId.WebErrand:
      return value as TaskTemplateId;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeTaskProviderRoute(encoder: CoreStatusPayloadEncoder, value: TaskProviderRoute): void {
  switch (value) {
    case TaskProviderRoute.NotConfigured:
    case TaskProviderRoute.DirectUserKey:
    case TaskProviderRoute.ManagedService:
    case TaskProviderRoute.NoModelRequired:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeTaskProviderRoute(decoder: CoreStatusPayloadDecoder): TaskProviderRoute {
  const value = decoder.readU32();
  switch (value) {
    case TaskProviderRoute.NotConfigured:
    case TaskProviderRoute.DirectUserKey:
    case TaskProviderRoute.ManagedService:
    case TaskProviderRoute.NoModelRequired:
      return value as TaskProviderRoute;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeCoreAvailability(encoder: CoreStatusPayloadEncoder, value: CoreAvailability): void {
  switch (value) {
    case CoreAvailability.Starting:
    case CoreAvailability.Ready:
    case CoreAvailability.Unavailable:
    case CoreAvailability.CircuitOpen:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeCoreAvailability(decoder: CoreStatusPayloadDecoder): CoreAvailability {
  const value = decoder.readU32();
  switch (value) {
    case CoreAvailability.Starting:
    case CoreAvailability.Ready:
    case CoreAvailability.Unavailable:
    case CoreAvailability.CircuitOpen:
      return value as CoreAvailability;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeSavedDataAvailability(encoder: CoreStatusPayloadEncoder, value: SavedDataAvailability): void {
  switch (value) {
    case SavedDataAvailability.Loading:
    case SavedDataAvailability.Ready:
    case SavedDataAvailability.Unavailable:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeSavedDataAvailability(decoder: CoreStatusPayloadDecoder): SavedDataAvailability {
  const value = decoder.readU32();
  switch (value) {
    case SavedDataAvailability.Loading:
    case SavedDataAvailability.Ready:
    case SavedDataAvailability.Unavailable:
      return value as SavedDataAvailability;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeTaskPhase(encoder: CoreStatusPayloadEncoder, value: TaskPhase): void {
  switch (value) {
    case TaskPhase.Idle:
    case TaskPhase.Planning:
    case TaskPhase.WaitingForUser:
    case TaskPhase.Running:
    case TaskPhase.Completed:
    case TaskPhase.Failed:
    case TaskPhase.Cancelled:
    case TaskPhase.OutcomeUnknown:
    case TaskPhase.Paused:
    case TaskPhase.Partial:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeTaskPhase(decoder: CoreStatusPayloadDecoder): TaskPhase {
  const value = decoder.readU32();
  switch (value) {
    case TaskPhase.Idle:
    case TaskPhase.Planning:
    case TaskPhase.WaitingForUser:
    case TaskPhase.Running:
    case TaskPhase.Completed:
    case TaskPhase.Failed:
    case TaskPhase.Cancelled:
    case TaskPhase.OutcomeUnknown:
    case TaskPhase.Paused:
    case TaskPhase.Partial:
      return value as TaskPhase;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeTaskControlKind(encoder: CoreStatusPayloadEncoder, value: TaskControlKind): void {
  switch (value) {
    case TaskControlKind.Pause:
    case TaskControlKind.Resume:
    case TaskControlKind.TakeOver:
    case TaskControlKind.Stop:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeTaskControlKind(decoder: CoreStatusPayloadDecoder): TaskControlKind {
  const value = decoder.readU32();
  switch (value) {
    case TaskControlKind.Pause:
    case TaskControlKind.Resume:
    case TaskControlKind.TakeOver:
    case TaskControlKind.Stop:
      return value as TaskControlKind;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeCoreFailureCode(encoder: CoreStatusPayloadEncoder, value: CoreFailureCode): void {
  switch (value) {
    case CoreFailureCode.Cancelled:
    case CoreFailureCode.DeadlineExceeded:
    case CoreFailureCode.CoreUnavailable:
    case CoreFailureCode.PolicyDenied:
    case CoreFailureCode.InvalidRequest:
    case CoreFailureCode.Backpressure:
    case CoreFailureCode.OutcomeUnknown:
    case CoreFailureCode.Internal:
    case CoreFailureCode.BudgetExceeded:
    case CoreFailureCode.ProviderUnavailable:
    case CoreFailureCode.SourcesUnavailable:
    case CoreFailureCode.JournalUnusable:
    case CoreFailureCode.UnverifiableAction:
    case CoreFailureCode.ProviderRefused:
    case CoreFailureCode.ProviderLimit:
    case CoreFailureCode.Offline:
    case CoreFailureCode.PolicyRefused:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeCoreFailureCode(decoder: CoreStatusPayloadDecoder): CoreFailureCode {
  const value = decoder.readU32();
  switch (value) {
    case CoreFailureCode.Cancelled:
    case CoreFailureCode.DeadlineExceeded:
    case CoreFailureCode.CoreUnavailable:
    case CoreFailureCode.PolicyDenied:
    case CoreFailureCode.InvalidRequest:
    case CoreFailureCode.Backpressure:
    case CoreFailureCode.OutcomeUnknown:
    case CoreFailureCode.Internal:
    case CoreFailureCode.BudgetExceeded:
    case CoreFailureCode.ProviderUnavailable:
    case CoreFailureCode.SourcesUnavailable:
    case CoreFailureCode.JournalUnusable:
    case CoreFailureCode.UnverifiableAction:
    case CoreFailureCode.ProviderRefused:
    case CoreFailureCode.ProviderLimit:
    case CoreFailureCode.Offline:
    case CoreFailureCode.PolicyRefused:
      return value as CoreFailureCode;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeAuthProvider(encoder: CoreStatusPayloadEncoder, value: AuthProvider): void {
  switch (value) {
    case AuthProvider.Google:
    case AuthProvider.EmailLink:
    case AuthProvider.Github:
    case AuthProvider.Facebook:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeAuthProvider(decoder: CoreStatusPayloadDecoder): AuthProvider {
  const value = decoder.readU32();
  switch (value) {
    case AuthProvider.Google:
    case AuthProvider.EmailLink:
    case AuthProvider.Github:
    case AuthProvider.Facebook:
      return value as AuthProvider;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeAuthMethodAvailability(encoder: CoreStatusPayloadEncoder, value: AuthMethodAvailability): void {
  switch (value) {
    case AuthMethodAvailability.Available:
    case AuthMethodAvailability.NotConfigured:
    case AuthMethodAvailability.PlatformUnavailable:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeAuthMethodAvailability(decoder: CoreStatusPayloadDecoder): AuthMethodAvailability {
  const value = decoder.readU32();
  switch (value) {
    case AuthMethodAvailability.Available:
    case AuthMethodAvailability.NotConfigured:
    case AuthMethodAvailability.PlatformUnavailable:
      return value as AuthMethodAvailability;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeAuthPhase(encoder: CoreStatusPayloadEncoder, value: AuthPhase): void {
  switch (value) {
    case AuthPhase.Initializing:
    case AuthPhase.SignedOut:
    case AuthPhase.InFlight:
    case AuthPhase.LinkSent:
    case AuthPhase.SignedIn:
    case AuthPhase.Failed:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeAuthPhase(decoder: CoreStatusPayloadDecoder): AuthPhase {
  const value = decoder.readU32();
  switch (value) {
    case AuthPhase.Initializing:
    case AuthPhase.SignedOut:
    case AuthPhase.InFlight:
    case AuthPhase.LinkSent:
    case AuthPhase.SignedIn:
    case AuthPhase.Failed:
      return value as AuthPhase;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeAuthFailureCode(encoder: CoreStatusPayloadEncoder, value: AuthFailureCode): void {
  switch (value) {
    case AuthFailureCode.NotConfigured:
    case AuthFailureCode.Cancelled:
    case AuthFailureCode.NoCredential:
    case AuthFailureCode.Network:
    case AuthFailureCode.Rejected:
    case AuthFailureCode.InvalidRedirect:
    case AuthFailureCode.CoreUnavailable:
    case AuthFailureCode.Unknown:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeAuthFailureCode(decoder: CoreStatusPayloadDecoder): AuthFailureCode {
  const value = decoder.readU32();
  switch (value) {
    case AuthFailureCode.NotConfigured:
    case AuthFailureCode.Cancelled:
    case AuthFailureCode.NoCredential:
    case AuthFailureCode.Network:
    case AuthFailureCode.Rejected:
    case AuthFailureCode.InvalidRedirect:
    case AuthFailureCode.CoreUnavailable:
    case AuthFailureCode.Unknown:
      return value as AuthFailureCode;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeWorkspacePhase(encoder: CoreStatusPayloadEncoder, value: WorkspacePhase): void {
  switch (value) {
    case WorkspacePhase.Running:
    case WorkspacePhase.WaitingForUser:
    case WorkspacePhase.Paused:
    case WorkspacePhase.Done:
    case WorkspacePhase.PartlyDone:
    case WorkspacePhase.Stopped:
    case WorkspacePhase.Failed:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeWorkspacePhase(decoder: CoreStatusPayloadDecoder): WorkspacePhase {
  const value = decoder.readU32();
  switch (value) {
    case WorkspacePhase.Running:
    case WorkspacePhase.WaitingForUser:
    case WorkspacePhase.Paused:
    case WorkspacePhase.Done:
    case WorkspacePhase.PartlyDone:
    case WorkspacePhase.Stopped:
    case WorkspacePhase.Failed:
      return value as WorkspacePhase;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeWorkspaceFactKind(encoder: CoreStatusPayloadEncoder, value: WorkspaceFactKind): void {
  switch (value) {
    case WorkspaceFactKind.FromPage:
    case WorkspaceFactKind.Summarized:
    case WorkspaceFactKind.TaffyInference:
    case WorkspaceFactKind.UserEntered:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeWorkspaceFactKind(decoder: CoreStatusPayloadDecoder): WorkspaceFactKind {
  const value = decoder.readU32();
  switch (value) {
    case WorkspaceFactKind.FromPage:
    case WorkspaceFactKind.Summarized:
    case WorkspaceFactKind.TaffyInference:
    case WorkspaceFactKind.UserEntered:
      return value as WorkspaceFactKind;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeWorkspaceExportFormat(encoder: CoreStatusPayloadEncoder, value: WorkspaceExportFormat): void {
  switch (value) {
    case WorkspaceExportFormat.Markdown:
    case WorkspaceExportFormat.Csv:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeWorkspaceExportFormat(decoder: CoreStatusPayloadDecoder): WorkspaceExportFormat {
  const value = decoder.readU32();
  switch (value) {
    case WorkspaceExportFormat.Markdown:
    case WorkspaceExportFormat.Csv:
      return value as WorkspaceExportFormat;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeTaskActivityKind(encoder: CoreStatusPayloadEncoder, value: TaskActivityKind): void {
  switch (value) {
    case TaskActivityKind.OpenedPage:
    case TaskActivityKind.ReadPage:
    case TaskActivityKind.PageUnavailable:
    case TaskActivityKind.MoveRefused:
    case TaskActivityKind.AskedYou:
    case TaskActivityKind.YouAnswered:
    case TaskActivityKind.HandedBack:
    case TaskActivityKind.YouTookOver:
    case TaskActivityKind.BuiltOutput:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeTaskActivityKind(decoder: CoreStatusPayloadDecoder): TaskActivityKind {
  const value = decoder.readU32();
  switch (value) {
    case TaskActivityKind.OpenedPage:
    case TaskActivityKind.ReadPage:
    case TaskActivityKind.PageUnavailable:
    case TaskActivityKind.MoveRefused:
    case TaskActivityKind.AskedYou:
    case TaskActivityKind.YouAnswered:
    case TaskActivityKind.HandedBack:
    case TaskActivityKind.YouTookOver:
    case TaskActivityKind.BuiltOutput:
      return value as TaskActivityKind;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeTaskArtifactKind(encoder: CoreStatusPayloadEncoder, value: TaskArtifactKind): void {
  switch (value) {
    case TaskArtifactKind.Markdown:
    case TaskArtifactKind.Csv:
    case TaskArtifactKind.Xlsx:
    case TaskArtifactKind.Pdf:
    case TaskArtifactKind.Docx:
    case TaskArtifactKind.Pptx:
    case TaskArtifactKind.WaveAudio:
    case TaskArtifactKind.FrameArchive:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeTaskArtifactKind(decoder: CoreStatusPayloadDecoder): TaskArtifactKind {
  const value = decoder.readU32();
  switch (value) {
    case TaskArtifactKind.Markdown:
    case TaskArtifactKind.Csv:
    case TaskArtifactKind.Xlsx:
    case TaskArtifactKind.Pdf:
    case TaskArtifactKind.Docx:
    case TaskArtifactKind.Pptx:
    case TaskArtifactKind.WaveAudio:
    case TaskArtifactKind.FrameArchive:
      return value as TaskArtifactKind;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeLibraryAvailability(encoder: CoreStatusPayloadEncoder, value: LibraryAvailability): void {
  switch (value) {
    case LibraryAvailability.Available:
    case LibraryAvailability.PrivateProfile:
    case LibraryAvailability.Unavailable:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeLibraryAvailability(decoder: CoreStatusPayloadDecoder): LibraryAvailability {
  const value = decoder.readU32();
  switch (value) {
    case LibraryAvailability.Available:
    case LibraryAvailability.PrivateProfile:
    case LibraryAvailability.Unavailable:
      return value as LibraryAvailability;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeLibraryRefreshDisposition(encoder: CoreStatusPayloadEncoder, value: LibraryRefreshDisposition): void {
  switch (value) {
    case LibraryRefreshDisposition.Unchanged:
    case LibraryRefreshDisposition.Changed:
    case LibraryRefreshDisposition.Missing:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeLibraryRefreshDisposition(decoder: CoreStatusPayloadDecoder): LibraryRefreshDisposition {
  const value = decoder.readU32();
  switch (value) {
    case LibraryRefreshDisposition.Unchanged:
    case LibraryRefreshDisposition.Changed:
    case LibraryRefreshDisposition.Missing:
      return value as LibraryRefreshDisposition;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeMemoryAvailability(encoder: CoreStatusPayloadEncoder, value: MemoryAvailability): void {
  switch (value) {
    case MemoryAvailability.Available:
    case MemoryAvailability.PrivateProfile:
    case MemoryAvailability.Unavailable:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeMemoryAvailability(decoder: CoreStatusPayloadDecoder): MemoryAvailability {
  const value = decoder.readU32();
  switch (value) {
    case MemoryAvailability.Available:
    case MemoryAvailability.PrivateProfile:
    case MemoryAvailability.Unavailable:
      return value as MemoryAvailability;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeMemorySourceKind(encoder: CoreStatusPayloadEncoder, value: MemorySourceKind): void {
  switch (value) {
    case MemorySourceKind.YouWrote:
    case MemorySourceKind.TaffySuggested:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeMemorySourceKind(decoder: CoreStatusPayloadDecoder): MemorySourceKind {
  const value = decoder.readU32();
  switch (value) {
    case MemorySourceKind.YouWrote:
    case MemorySourceKind.TaffySuggested:
      return value as MemorySourceKind;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeMemoryScopeKind(encoder: CoreStatusPayloadEncoder, value: MemoryScopeKind): void {
  switch (value) {
    case MemoryScopeKind.AllTasks:
    case MemoryScopeKind.Workspace:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeMemoryScopeKind(decoder: CoreStatusPayloadDecoder): MemoryScopeKind {
  const value = decoder.readU32();
  switch (value) {
    case MemoryScopeKind.AllTasks:
    case MemoryScopeKind.Workspace:
      return value as MemoryScopeKind;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeMemorySensitivity(encoder: CoreStatusPayloadEncoder, value: MemorySensitivity): void {
  switch (value) {
    case MemorySensitivity.Standard:
    case MemorySensitivity.Sensitive:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeMemorySensitivity(decoder: CoreStatusPayloadDecoder): MemorySensitivity {
  const value = decoder.readU32();
  switch (value) {
    case MemorySensitivity.Standard:
    case MemorySensitivity.Sensitive:
      return value as MemorySensitivity;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeAssetKindView(encoder: CoreStatusPayloadEncoder, value: AssetKindView): void {
  switch (value) {
    case AssetKindView.PythonStdlib:
    case AssetKindView.PythonPackages:
    case AssetKindView.ModelWeights:
    case AssetKindView.ModelTokenizer:
    case AssetKindView.FilterList:
    case AssetKindView.CountryFlags:
    case AssetKindView.StartScenes:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeAssetKindView(decoder: CoreStatusPayloadDecoder): AssetKindView {
  const value = decoder.readU32();
  switch (value) {
    case AssetKindView.PythonStdlib:
    case AssetKindView.PythonPackages:
    case AssetKindView.ModelWeights:
    case AssetKindView.ModelTokenizer:
    case AssetKindView.FilterList:
    case AssetKindView.CountryFlags:
    case AssetKindView.StartScenes:
      return value as AssetKindView;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeAssetPresenceView(encoder: CoreStatusPayloadEncoder, value: AssetPresenceView): void {
  switch (value) {
    case AssetPresenceView.Absent:
    case AssetPresenceView.Partial:
    case AssetPresenceView.Complete:
    case AssetPresenceView.Installed:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeAssetPresenceView(decoder: CoreStatusPayloadDecoder): AssetPresenceView {
  const value = decoder.readU32();
  switch (value) {
    case AssetPresenceView.Absent:
    case AssetPresenceView.Partial:
    case AssetPresenceView.Complete:
    case AssetPresenceView.Installed:
      return value as AssetPresenceView;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeAssetNetworkCostView(encoder: CoreStatusPayloadEncoder, value: AssetNetworkCostView): void {
  switch (value) {
    case AssetNetworkCostView.Offline:
    case AssetNetworkCostView.Metered:
    case AssetNetworkCostView.Unmetered:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeAssetNetworkCostView(decoder: CoreStatusPayloadDecoder): AssetNetworkCostView {
  const value = decoder.readU32();
  switch (value) {
    case AssetNetworkCostView.Offline:
    case AssetNetworkCostView.Metered:
    case AssetNetworkCostView.Unmetered:
      return value as AssetNetworkCostView;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeAssetRefusalView(encoder: CoreStatusPayloadEncoder, value: AssetRefusalView): void {
  switch (value) {
    case AssetRefusalView.UnknownAsset:
    case AssetRefusalView.NoVariantForPlatform:
    case AssetRefusalView.NotPublishedYet:
    case AssetRefusalView.CatalogRowIncomplete:
    case AssetRefusalView.VariantTooLarge:
    case AssetRefusalView.AttemptsExhausted:
    case AssetRefusalView.IntegrityFailed:
    case AssetRefusalView.NetworkNotPermitted:
    case AssetRefusalView.DeclinedByPerson:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeAssetRefusalView(decoder: CoreStatusPayloadDecoder): AssetRefusalView {
  const value = decoder.readU32();
  switch (value) {
    case AssetRefusalView.UnknownAsset:
    case AssetRefusalView.NoVariantForPlatform:
    case AssetRefusalView.NotPublishedYet:
    case AssetRefusalView.CatalogRowIncomplete:
    case AssetRefusalView.VariantTooLarge:
    case AssetRefusalView.AttemptsExhausted:
    case AssetRefusalView.IntegrityFailed:
    case AssetRefusalView.NetworkNotPermitted:
    case AssetRefusalView.DeclinedByPerson:
      return value as AssetRefusalView;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeProviderAuthMethodView(encoder: CoreStatusPayloadEncoder, value: ProviderAuthMethodView): void {
  switch (value) {
    case ProviderAuthMethodView.ApiKey:
    case ProviderAuthMethodView.Oauth:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeProviderAuthMethodView(decoder: CoreStatusPayloadDecoder): ProviderAuthMethodView {
  const value = decoder.readU32();
  switch (value) {
    case ProviderAuthMethodView.ApiKey:
    case ProviderAuthMethodView.Oauth:
      return value as ProviderAuthMethodView;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeProviderProbeVerdictView(encoder: CoreStatusPayloadEncoder, value: ProviderProbeVerdictView): void {
  switch (value) {
    case ProviderProbeVerdictView.Usable:
    case ProviderProbeVerdictView.Auth:
    case ProviderProbeVerdictView.Billing:
    case ProviderProbeVerdictView.RateLimit:
    case ProviderProbeVerdictView.Overloaded:
    case ProviderProbeVerdictView.Timeout:
    case ProviderProbeVerdictView.Network:
    case ProviderProbeVerdictView.ModelNotFound:
    case ProviderProbeVerdictView.Unknown:
    case ProviderProbeVerdictView.EndpointReached:
    case ProviderProbeVerdictView.NoModelListed:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeProviderProbeVerdictView(decoder: CoreStatusPayloadDecoder): ProviderProbeVerdictView {
  const value = decoder.readU32();
  switch (value) {
    case ProviderProbeVerdictView.Usable:
    case ProviderProbeVerdictView.Auth:
    case ProviderProbeVerdictView.Billing:
    case ProviderProbeVerdictView.RateLimit:
    case ProviderProbeVerdictView.Overloaded:
    case ProviderProbeVerdictView.Timeout:
    case ProviderProbeVerdictView.Network:
    case ProviderProbeVerdictView.ModelNotFound:
    case ProviderProbeVerdictView.Unknown:
    case ProviderProbeVerdictView.EndpointReached:
    case ProviderProbeVerdictView.NoModelListed:
      return value as ProviderProbeVerdictView;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeProviderCredentialStateView(encoder: CoreStatusPayloadEncoder, value: ProviderCredentialStateView): void {
  switch (value) {
    case ProviderCredentialStateView.Usable:
    case ProviderCredentialStateView.NeedsSignIn:
    case ProviderCredentialStateView.RefreshFailed:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeProviderCredentialStateView(decoder: CoreStatusPayloadDecoder): ProviderCredentialStateView {
  const value = decoder.readU32();
  switch (value) {
    case ProviderCredentialStateView.Usable:
    case ProviderCredentialStateView.NeedsSignIn:
    case ProviderCredentialStateView.RefreshFailed:
      return value as ProviderCredentialStateView;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeProviderRefusalView(encoder: CoreStatusPayloadEncoder, value: ProviderRefusalView): void {
  switch (value) {
    case ProviderRefusalView.RateLimit:
    case ProviderRefusalView.Billing:
    case ProviderRefusalView.Overloaded:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeProviderRefusalView(decoder: CoreStatusPayloadDecoder): ProviderRefusalView {
  const value = decoder.readU32();
  switch (value) {
    case ProviderRefusalView.RateLimit:
    case ProviderRefusalView.Billing:
    case ProviderRefusalView.Overloaded:
      return value as ProviderRefusalView;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeProviderOriginView(encoder: CoreStatusPayloadEncoder, value: ProviderOriginView): void {
  switch (value) {
    case ProviderOriginView.Catalog:
    case ProviderOriginView.Custom:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeProviderOriginView(decoder: CoreStatusPayloadDecoder): ProviderOriginView {
  const value = decoder.readU32();
  switch (value) {
    case ProviderOriginView.Catalog:
    case ProviderOriginView.Custom:
      return value as ProviderOriginView;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeCatalogLayerView(encoder: CoreStatusPayloadEncoder, value: CatalogLayerView): void {
  switch (value) {
    case CatalogLayerView.EmbeddedBaseline:
    case CatalogLayerView.RemoteOverlay:
    case CatalogLayerView.UserOverride:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeCatalogLayerView(decoder: CoreStatusPayloadDecoder): CatalogLayerView {
  const value = decoder.readU32();
  switch (value) {
    case CatalogLayerView.EmbeddedBaseline:
    case CatalogLayerView.RemoteOverlay:
    case CatalogLayerView.UserOverride:
      return value as CatalogLayerView;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeThinkingLevelView(encoder: CoreStatusPayloadEncoder, value: ThinkingLevelView): void {
  switch (value) {
    case ThinkingLevelView.Off:
    case ThinkingLevelView.Minimal:
    case ThinkingLevelView.Low:
    case ThinkingLevelView.Medium:
    case ThinkingLevelView.High:
    case ThinkingLevelView.Xhigh:
    case ThinkingLevelView.Max:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeThinkingLevelView(decoder: CoreStatusPayloadDecoder): ThinkingLevelView {
  const value = decoder.readU32();
  switch (value) {
    case ThinkingLevelView.Off:
    case ThinkingLevelView.Minimal:
    case ThinkingLevelView.Low:
    case ThinkingLevelView.Medium:
    case ThinkingLevelView.High:
    case ThinkingLevelView.Xhigh:
    case ThinkingLevelView.Max:
      return value as ThinkingLevelView;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeModelRoleView(encoder: CoreStatusPayloadEncoder, value: ModelRoleView): void {
  switch (value) {
    case ModelRoleView.PrimaryReasoning:
    case ModelRoleView.FastBrowsing:
    case ModelRoleView.Vision:
    case ModelRoleView.Embedding:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeModelRoleView(decoder: CoreStatusPayloadDecoder): ModelRoleView {
  const value = decoder.readU32();
  switch (value) {
    case ModelRoleView.PrimaryReasoning:
    case ModelRoleView.FastBrowsing:
    case ModelRoleView.Vision:
    case ModelRoleView.Embedding:
      return value as ModelRoleView;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeInputModalityView(encoder: CoreStatusPayloadEncoder, value: InputModalityView): void {
  switch (value) {
    case InputModalityView.Text:
    case InputModalityView.Image:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeInputModalityView(decoder: CoreStatusPayloadDecoder): InputModalityView {
  const value = decoder.readU32();
  switch (value) {
    case InputModalityView.Text:
    case InputModalityView.Image:
      return value as InputModalityView;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function encodeServerKindView(encoder: CoreStatusPayloadEncoder, value: ServerKindView): void {
  switch (value) {
    case ServerKindView.OpenaiCompatible:
    case ServerKindView.Ollama:
    case ServerKindView.LmStudio:
    case ServerKindView.Vllm:
    case ServerKindView.LlamaCpp:
      encoder.putU32(value); return;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function decodeServerKindView(decoder: CoreStatusPayloadDecoder): ServerKindView {
  const value = decoder.readU32();
  switch (value) {
    case ServerKindView.OpenaiCompatible:
    case ServerKindView.Ollama:
    case ServerKindView.LmStudio:
    case ServerKindView.Vllm:
    case ServerKindView.LlamaCpp:
      return value as ServerKindView;
    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);
  }
}

function validateSiteSkillSemanticTarget(value: SiteSkillSemanticTarget): void {
  void value;
}

function encodeSiteSkillSemanticTarget(encoder: CoreStatusPayloadEncoder, value: SiteSkillSemanticTarget): void {
  validateSiteSkillSemanticTarget(value);
  encoder.putU32(value.role);
  encoder.putU32(value.phrase);
}

function decodeSiteSkillSemanticTarget(decoder: CoreStatusPayloadDecoder): SiteSkillSemanticTarget {
  const value = {
    role: decoder.readU32(),
    phrase: decoder.readU32(),
  } satisfies SiteSkillSemanticTarget;
  validateSiteSkillSemanticTarget(value);
  return value;
}

function validateSiteSkillObservedArgument(value: SiteSkillObservedArgument): void {
  void value;
}

function encodeSiteSkillObservedArgument(encoder: CoreStatusPayloadEncoder, value: SiteSkillObservedArgument): void {
  validateSiteSkillObservedArgument(value);
  encoder.putU32(value.parameter);
  encodeSiteSkillArgumentKind(encoder, value.kind);
  encoder.putU64(value.value);
  encoder.putU32(value.purpose);
  if (value.public_address === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encoder.putString(value.public_address, MAX_TASK_GOAL_BYTES);
  }
  if (value.semantic_target === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encodeSiteSkillSemanticTarget(encoder, value.semantic_target);
  }
}

function decodeSiteSkillObservedArgument(decoder: CoreStatusPayloadDecoder): SiteSkillObservedArgument {
  const value = {
    parameter: decoder.readU32(),
    kind: decodeSiteSkillArgumentKind(decoder),
    value: decoder.readU64(),
    purpose: decoder.readU32(),
    public_address: decoder.readBoolean() ? decoder.readString(MAX_TASK_GOAL_BYTES) : null,
    semantic_target: decoder.readBoolean() ? decodeSiteSkillSemanticTarget(decoder) : null,
  } satisfies SiteSkillObservedArgument;
  validateSiteSkillObservedArgument(value);
  return value;
}

function validateSiteSkillObservedStep(value: SiteSkillObservedStep): void {
  void value;
}

function encodeSiteSkillObservedStep(encoder: CoreStatusPayloadEncoder, value: SiteSkillObservedStep): void {
  validateSiteSkillObservedStep(value);
  encoder.putString(value.verb, MAX_SKILL_TOOL_NAME_BYTES);
  encoder.putLength(value.arguments.length, MAX_SKILL_ARGUMENTS_PER_STEP);
  for (const item of value.arguments) {
    encodeSiteSkillObservedArgument(encoder, item);
  }
  encoder.putU32(value.postcondition);
  encoder.putBoolean(value.has_fill);
  encoder.putU32(value.fill_purpose);
}

function decodeSiteSkillObservedStep(decoder: CoreStatusPayloadDecoder): SiteSkillObservedStep {
  const value = {
    verb: decoder.readString(MAX_SKILL_TOOL_NAME_BYTES),
    arguments: decoder.readList(MAX_SKILL_ARGUMENTS_PER_STEP, () => decodeSiteSkillObservedArgument(decoder)),
    postcondition: decoder.readU32(),
    has_fill: decoder.readBoolean(),
    fill_purpose: decoder.readU32(),
  } satisfies SiteSkillObservedStep;
  validateSiteSkillObservedStep(value);
  return value;
}

function validateSiteSkillView(value: SiteSkillView): void {
  void value;
}

function encodeSiteSkillView(encoder: CoreStatusPayloadEncoder, value: SiteSkillView): void {
  validateSiteSkillView(value);
  encoder.putString(value.skill_id, MAX_SKILL_ID_BYTES);
  encoder.putString(value.origin, MAX_SKILL_ORIGIN_BYTES);
  encodeSiteSkillProvenanceView(encoder, value.provenance);
  encodeSiteSkillStatusView(encoder, value.status);
  encoder.putU32(value.active_version);
  encoder.putU32(value.step_count);
  encoder.putU64(value.installed_at_epoch_ms);
  encoder.putU64(value.updated_at_epoch_ms);
  if (value.recorded_from_task_id === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encoder.putString(value.recorded_from_task_id, MAX_IDENTIFIER_BYTES);
  }
  encoder.putLength(value.reviewed_steps.length, MAX_SKILL_STEPS);
  for (const item of value.reviewed_steps) {
    encodeSiteSkillObservedStep(encoder, item);
  }
}

function decodeSiteSkillView(decoder: CoreStatusPayloadDecoder): SiteSkillView {
  const value = {
    skill_id: decoder.readString(MAX_SKILL_ID_BYTES),
    origin: decoder.readString(MAX_SKILL_ORIGIN_BYTES),
    provenance: decodeSiteSkillProvenanceView(decoder),
    status: decodeSiteSkillStatusView(decoder),
    active_version: decoder.readU32(),
    step_count: decoder.readU32(),
    installed_at_epoch_ms: decoder.readU64(),
    updated_at_epoch_ms: decoder.readU64(),
    recorded_from_task_id: decoder.readBoolean() ? decoder.readString(MAX_IDENTIFIER_BYTES) : null,
    reviewed_steps: decoder.readList(MAX_SKILL_STEPS, () => decodeSiteSkillObservedStep(decoder)),
  } satisfies SiteSkillView;
  validateSiteSkillView(value);
  return value;
}

function validateAssistantConfigurationView(value: AssistantConfigurationView): void {
  void value;
}

function encodeAssistantConfigurationView(encoder: CoreStatusPayloadEncoder, value: AssistantConfigurationView): void {
  validateAssistantConfigurationView(value);
  encoder.putU64(value.revision);
  encoder.putLength(value.disabled_abilities.length, MAX_ASSISTANT_ABILITIES);
  for (const item of value.disabled_abilities) {
    encodeAssistantAbilityView(encoder, item);
  }
  encodePersonalityPresetView(encoder, value.preset);
  encoder.putBoundedU32(value.pace, MAX_PERSONALITY_SCALE);
  encoder.putBoundedU32(value.length, MAX_PERSONALITY_SCALE);
  encoder.putBoundedU32(value.check_in, MAX_PERSONALITY_SCALE);
}

function decodeAssistantConfigurationView(decoder: CoreStatusPayloadDecoder): AssistantConfigurationView {
  const value = {
    revision: decoder.readU64(),
    disabled_abilities: decoder.readList(MAX_ASSISTANT_ABILITIES, () => decodeAssistantAbilityView(decoder)),
    preset: decodePersonalityPresetView(decoder),
    pace: decoder.readBoundedU32(MAX_PERSONALITY_SCALE),
    length: decoder.readBoundedU32(MAX_PERSONALITY_SCALE),
    check_in: decoder.readBoundedU32(MAX_PERSONALITY_SCALE),
  } satisfies AssistantConfigurationView;
  validateAssistantConfigurationView(value);
  return value;
}

function validateBuiltinSkillReferenceView(value: BuiltinSkillReferenceView): void {
  void value;
}

function encodeBuiltinSkillReferenceView(encoder: CoreStatusPayloadEncoder, value: BuiltinSkillReferenceView): void {
  validateBuiltinSkillReferenceView(value);
  encodeBuiltinSkillIdView(encoder, value.skill_id);
  encoder.putU32(value.version);
}

function decodeBuiltinSkillReferenceView(decoder: CoreStatusPayloadDecoder): BuiltinSkillReferenceView {
  const value = {
    skill_id: decodeBuiltinSkillIdView(decoder),
    version: decoder.readU32(),
  } satisfies BuiltinSkillReferenceView;
  validateBuiltinSkillReferenceView(value);
  return value;
}

function validateBuiltinSkillView(value: BuiltinSkillView): void {
  void value;
}

function encodeBuiltinSkillView(encoder: CoreStatusPayloadEncoder, value: BuiltinSkillView): void {
  validateBuiltinSkillView(value);
  encodeBuiltinSkillReferenceView(encoder, value.reference);
  encodeAssistantAbilityView(encoder, value.required_ability);
  encoder.putBoolean(value.enabled);
  encodeBuiltinSkillAvailabilityView(encoder, value.availability);
  encoder.putU32(value.required_tool_count);
  encoder.putU32(value.available_tool_count);
  encoder.putU32(value.required_part_count);
  encoder.putU32(value.installed_part_count);
}

function decodeBuiltinSkillView(decoder: CoreStatusPayloadDecoder): BuiltinSkillView {
  const value = {
    reference: decodeBuiltinSkillReferenceView(decoder),
    required_ability: decodeAssistantAbilityView(decoder),
    enabled: decoder.readBoolean(),
    availability: decodeBuiltinSkillAvailabilityView(decoder),
    required_tool_count: decoder.readU32(),
    available_tool_count: decoder.readU32(),
    required_part_count: decoder.readU32(),
    installed_part_count: decoder.readU32(),
  } satisfies BuiltinSkillView;
  validateBuiltinSkillView(value);
  return value;
}

function validateCoreStatusProjectionOmission(value: CoreStatusProjectionOmission): void {
  void value;
}

function encodeCoreStatusProjectionOmission(encoder: CoreStatusPayloadEncoder, value: CoreStatusProjectionOmission): void {
  validateCoreStatusProjectionOmission(value);
  encodeCoreStatusProjectionFamily(encoder, value.family);
  encoder.putU64(value.revision);
  encoder.putU32(value.item_count);
}

function decodeCoreStatusProjectionOmission(decoder: CoreStatusPayloadDecoder): CoreStatusProjectionOmission {
  const value = {
    family: decodeCoreStatusProjectionFamily(decoder),
    revision: decoder.readU64(),
    item_count: decoder.readU32(),
  } satisfies CoreStatusProjectionOmission;
  validateCoreStatusProjectionOmission(value);
  return value;
}

function validateCoreFailure(value: CoreFailure): void {
  void value;
}

function encodeCoreFailure(encoder: CoreStatusPayloadEncoder, value: CoreFailure): void {
  validateCoreFailure(value);
  encodeCoreFailureCode(encoder, value.code);
  encoder.putBoolean(value.retryable);
  if (value.message_key === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encoder.putString(value.message_key, MAX_MESSAGE_KEY_BYTES);
  }
}

function decodeCoreFailure(decoder: CoreStatusPayloadDecoder): CoreFailure {
  const value = {
    code: decodeCoreFailureCode(decoder),
    retryable: decoder.readBoolean(),
    message_key: decoder.readBoolean() ? decoder.readString(MAX_MESSAGE_KEY_BYTES) : null,
  } satisfies CoreFailure;
  validateCoreFailure(value);
  return value;
}

function validateTaskActivityView(value: TaskActivityView): void {
  void value;
}

function encodeTaskActivityView(encoder: CoreStatusPayloadEncoder, value: TaskActivityView): void {
  validateTaskActivityView(value);
  encoder.putU64(value.sequence);
  encodeTaskActivityKind(encoder, value.kind);
  if (value.host === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encoder.putString(value.host, MAX_SOURCE_HOST_BYTES);
  }
  encoder.putU32(value.count);
  encoder.putU64(value.at_epoch_ms);
}

function decodeTaskActivityView(decoder: CoreStatusPayloadDecoder): TaskActivityView {
  const value = {
    sequence: decoder.readU64(),
    kind: decodeTaskActivityKind(decoder),
    host: decoder.readBoolean() ? decoder.readString(MAX_SOURCE_HOST_BYTES) : null,
    count: decoder.readU32(),
    at_epoch_ms: decoder.readU64(),
  } satisfies TaskActivityView;
  validateTaskActivityView(value);
  return value;
}

function validateTaskArtifactView(value: TaskArtifactView): void {
  void value;
}

function encodeTaskArtifactView(encoder: CoreStatusPayloadEncoder, value: TaskArtifactView): void {
  validateTaskArtifactView(value);
  encoder.putString(value.artifact_id, MAX_IDENTIFIER_BYTES);
  encodeTaskArtifactKind(encoder, value.kind);
  encoder.putU64(value.workspace_revision);
  encoder.putBoolean(value.accepted);
}

function decodeTaskArtifactView(decoder: CoreStatusPayloadDecoder): TaskArtifactView {
  const value = {
    artifact_id: decoder.readString(MAX_IDENTIFIER_BYTES),
    kind: decodeTaskArtifactKind(decoder),
    workspace_revision: decoder.readU64(),
    accepted: decoder.readBoolean(),
  } satisfies TaskArtifactView;
  validateTaskArtifactView(value);
  return value;
}

function validateTaskViewState(value: TaskViewState): void {
  void value;
}

function encodeTaskViewState(encoder: CoreStatusPayloadEncoder, value: TaskViewState): void {
  validateTaskViewState(value);
  encoder.putString(value.task_id, MAX_IDENTIFIER_BYTES);
  encoder.putU64(value.revision);
  encodeTaskPhase(encoder, value.phase);
  encoder.putBoundedU32(value.progress_basis_points, MAX_PROGRESS_BASIS_POINTS);
  if (value.status_message_key === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encoder.putString(value.status_message_key, MAX_MESSAGE_KEY_BYTES);
  }
  if (value.failure === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encodeCoreFailure(encoder, value.failure);
  }
  encoder.putString(value.goal, MAX_TASK_GOAL_BYTES);
  encodeTaskTemplateId(encoder, value.template_id);
  if (value.pending_action === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encodeActionApprovalView(encoder, value.pending_action);
  }
  if (value.workspace_id === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encoder.putString(value.workspace_id, MAX_IDENTIFIER_BYTES);
  }
  if (value.pending_ask_prompt === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encoder.putString(value.pending_ask_prompt, MAX_USER_INPUT_ANSWER_BYTES);
  }
  if (value.pending_field_value_request === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encoder.putString(value.pending_field_value_request, MAX_IDENTIFIER_BYTES);
  }
  encoder.putLength(value.allowed_controls.length, MAX_TASK_CONTROLS);
  for (const item of value.allowed_controls) {
    encodeTaskControlKind(encoder, item);
  }
  encoder.putLength(value.artifacts.length, MAX_TASK_ARTIFACTS);
  for (const item of value.artifacts) {
    encodeTaskArtifactView(encoder, item);
  }
  encoder.putLength(value.activity.length, MAX_TASK_ACTIVITY);
  for (const item of value.activity) {
    encodeTaskActivityView(encoder, item);
  }
}

function decodeTaskViewState(decoder: CoreStatusPayloadDecoder): TaskViewState {
  const value = {
    task_id: decoder.readString(MAX_IDENTIFIER_BYTES),
    revision: decoder.readU64(),
    phase: decodeTaskPhase(decoder),
    progress_basis_points: decoder.readBoundedU32(MAX_PROGRESS_BASIS_POINTS),
    status_message_key: decoder.readBoolean() ? decoder.readString(MAX_MESSAGE_KEY_BYTES) : null,
    failure: decoder.readBoolean() ? decodeCoreFailure(decoder) : null,
    goal: decoder.readString(MAX_TASK_GOAL_BYTES),
    template_id: decodeTaskTemplateId(decoder),
    pending_action: decoder.readBoolean() ? decodeActionApprovalView(decoder) : null,
    workspace_id: decoder.readBoolean() ? decoder.readString(MAX_IDENTIFIER_BYTES) : null,
    pending_ask_prompt: decoder.readBoolean() ? decoder.readString(MAX_USER_INPUT_ANSWER_BYTES) : null,
    pending_field_value_request: decoder.readBoolean() ? decoder.readString(MAX_IDENTIFIER_BYTES) : null,
    allowed_controls: decoder.readList(MAX_TASK_CONTROLS, () => decodeTaskControlKind(decoder)),
    artifacts: decoder.readList(MAX_TASK_ARTIFACTS, () => decodeTaskArtifactView(decoder)),
    activity: decoder.readList(MAX_TASK_ACTIVITY, () => decodeTaskActivityView(decoder)),
  } satisfies TaskViewState;
  validateTaskViewState(value);
  return value;
}

function validateActionApprovalView(value: ActionApprovalView): void {
  void value;
}

function encodeActionApprovalView(encoder: CoreStatusPayloadEncoder, value: ActionApprovalView): void {
  validateActionApprovalView(value);
  encoder.putString(value.action_id, MAX_IDENTIFIER_BYTES);
  if (value.host === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encoder.putString(value.host, MAX_IDENTIFIER_BYTES);
  }
  encoder.putU32(value.item_count);
  encoder.putString(value.summary_message_key, MAX_MESSAGE_KEY_BYTES);
}

function decodeActionApprovalView(decoder: CoreStatusPayloadDecoder): ActionApprovalView {
  const value = {
    action_id: decoder.readString(MAX_IDENTIFIER_BYTES),
    host: decoder.readBoolean() ? decoder.readString(MAX_IDENTIFIER_BYTES) : null,
    item_count: decoder.readU32(),
    summary_message_key: decoder.readString(MAX_MESSAGE_KEY_BYTES),
  } satisfies ActionApprovalView;
  validateActionApprovalView(value);
  return value;
}

function validateStoredCredentialView(value: StoredCredentialView): void {
  void value;
}

function encodeStoredCredentialView(encoder: CoreStatusPayloadEncoder, value: StoredCredentialView): void {
  validateStoredCredentialView(value);
  encodeProviderAuthMethodView(encoder, value.auth_method);
  encodeProviderCredentialStateView(encoder, value.state);
  encoder.putBoolean(value.subscription_backed);
  if (value.account_label === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encoder.putString(value.account_label, MAX_AUTH_DISPLAY_NAME_BYTES);
  }
  if (value.plan_label === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encoder.putString(value.plan_label, MAX_PROVIDER_DISPLAY_NAME_BYTES);
  }
}

function decodeStoredCredentialView(decoder: CoreStatusPayloadDecoder): StoredCredentialView {
  const value = {
    auth_method: decodeProviderAuthMethodView(decoder),
    state: decodeProviderCredentialStateView(decoder),
    subscription_backed: decoder.readBoolean(),
    account_label: decoder.readBoolean() ? decoder.readString(MAX_AUTH_DISPLAY_NAME_BYTES) : null,
    plan_label: decoder.readBoolean() ? decoder.readString(MAX_PROVIDER_DISPLAY_NAME_BYTES) : null,
  } satisfies StoredCredentialView;
  validateStoredCredentialView(value);
  return value;
}

function validateThinkingPreferenceView(value: ThinkingPreferenceView): void {
  void value;
}

function encodeThinkingPreferenceView(encoder: CoreStatusPayloadEncoder, value: ThinkingPreferenceView): void {
  validateThinkingPreferenceView(value);
  encodeThinkingLevelView(encoder, value.level);
}

function decodeThinkingPreferenceView(decoder: CoreStatusPayloadDecoder): ThinkingPreferenceView {
  const value = {
    level: decodeThinkingLevelView(decoder),
  } satisfies ThinkingPreferenceView;
  validateThinkingPreferenceView(value);
  return value;
}

function validateProviderRefusalStateView(value: ProviderRefusalStateView): void {
  void value;
}

function encodeProviderRefusalStateView(encoder: CoreStatusPayloadEncoder, value: ProviderRefusalStateView): void {
  validateProviderRefusalStateView(value);
  encodeProviderRefusalView(encoder, value.refusal);
  encoder.putU64(value.at_monotonic_ms);
}

function decodeProviderRefusalStateView(decoder: CoreStatusPayloadDecoder): ProviderRefusalStateView {
  const value = {
    refusal: decodeProviderRefusalView(decoder),
    at_monotonic_ms: decoder.readU64(),
  } satisfies ProviderRefusalStateView;
  validateProviderRefusalStateView(value);
  return value;
}

function validateProviderPresentationView(value: ProviderPresentationView): void {
  void value;
}

function encodeProviderPresentationView(encoder: CoreStatusPayloadEncoder, value: ProviderPresentationView): void {
  validateProviderPresentationView(value);
  if (value.key_prefix === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encoder.putString(value.key_prefix, MAX_PROVIDER_PRESENTATION_BYTES);
  }
  if (value.get_key_url === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encoder.putString(value.get_key_url, MAX_PROVIDER_PRESENTATION_BYTES);
  }
  if (value.docs_url === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encoder.putString(value.docs_url, MAX_PROVIDER_PRESENTATION_BYTES);
  }
}

function decodeProviderPresentationView(decoder: CoreStatusPayloadDecoder): ProviderPresentationView {
  const value = {
    key_prefix: decoder.readBoolean() ? decoder.readString(MAX_PROVIDER_PRESENTATION_BYTES) : null,
    get_key_url: decoder.readBoolean() ? decoder.readString(MAX_PROVIDER_PRESENTATION_BYTES) : null,
    docs_url: decoder.readBoolean() ? decoder.readString(MAX_PROVIDER_PRESENTATION_BYTES) : null,
  } satisfies ProviderPresentationView;
  validateProviderPresentationView(value);
  return value;
}

function validateProviderModelView(value: ProviderModelView): void {
  void value;
}

function encodeProviderModelView(encoder: CoreStatusPayloadEncoder, value: ProviderModelView): void {
  validateProviderModelView(value);
  encoder.putString(value.provider_id, MAX_PROVIDER_ID_BYTES);
  encoder.putString(value.model_id, MAX_MODEL_ID_BYTES);
  encoder.putString(value.display_name, MAX_MODEL_DISPLAY_NAME_BYTES);
  encoder.putU64(value.context_window);
  encoder.putU64(value.max_output_tokens);
  encoder.putBoolean(value.reasoning);
  encoder.putBoolean(value.tool_calling);
  encoder.putLength(value.roles.length, MAX_MODEL_ROLES);
  for (const item of value.roles) {
    encodeModelRoleView(encoder, item);
  }
  encoder.putLength(value.input_modalities.length, MAX_MODEL_INPUT_MODALITIES);
  for (const item of value.input_modalities) {
    encodeInputModalityView(encoder, item);
  }
  encoder.putLength(value.thinking_levels.length, MAX_MODEL_THINKING_LEVELS);
  for (const item of value.thinking_levels) {
    encodeThinkingLevelView(encoder, item);
  }
}

function decodeProviderModelView(decoder: CoreStatusPayloadDecoder): ProviderModelView {
  const value = {
    provider_id: decoder.readString(MAX_PROVIDER_ID_BYTES),
    model_id: decoder.readString(MAX_MODEL_ID_BYTES),
    display_name: decoder.readString(MAX_MODEL_DISPLAY_NAME_BYTES),
    context_window: decoder.readU64(),
    max_output_tokens: decoder.readU64(),
    reasoning: decoder.readBoolean(),
    tool_calling: decoder.readBoolean(),
    roles: decoder.readList(MAX_MODEL_ROLES, () => decodeModelRoleView(decoder)),
    input_modalities: decoder.readList(MAX_MODEL_INPUT_MODALITIES, () => decodeInputModalityView(decoder)),
    thinking_levels: decoder.readList(MAX_MODEL_THINKING_LEVELS, () => decodeThinkingLevelView(decoder)),
  } satisfies ProviderModelView;
  validateProviderModelView(value);
  return value;
}

function validateProviderRosterEntry(value: ProviderRosterEntry): void {
  void value;
}

function encodeProviderRosterEntry(encoder: CoreStatusPayloadEncoder, value: ProviderRosterEntry): void {
  validateProviderRosterEntry(value);
  encoder.putString(value.provider_id, MAX_PROVIDER_ID_BYTES);
  encoder.putString(value.display_name, MAX_PROVIDER_DISPLAY_NAME_BYTES);
  encodeProviderOriginView(encoder, value.origin);
  encoder.putLength(value.auth_methods.length, MAX_AUTH_METHODS);
  for (const item of value.auth_methods) {
    encodeProviderAuthMethodView(encoder, item);
  }
  if (value.stored === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encodeStoredCredentialView(encoder, value.stored);
  }
  encoder.putBoolean(value.signing_in);
  encoder.putBoolean(value.enabled);
  if (value.endpoint_host === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encoder.putString(value.endpoint_host, MAX_SOURCE_HOST_BYTES);
  }
  encoder.putBoolean(value.configurable);
  encoder.putBoolean(value.endpoint_changed);
  encodeCatalogLayerView(encoder, value.catalog_layer);
  if (value.selected_model_id === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encoder.putString(value.selected_model_id, MAX_MODEL_ID_BYTES);
  }
  if (value.thinking === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encodeThinkingPreferenceView(encoder, value.thinking);
  }
  if (value.presentation === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encodeProviderPresentationView(encoder, value.presentation);
  }
  if (value.endpoint_base === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encoder.putString(value.endpoint_base, MAX_PROVIDER_ENDPOINT_BYTES);
  }
  if (value.last_refusal === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encodeProviderRefusalStateView(encoder, value.last_refusal);
  }
  encoder.putU32(value.model_count);
  encoder.putBoolean(value.subscription);
  if (value.refused_endpoint_host === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encoder.putString(value.refused_endpoint_host, MAX_SOURCE_HOST_BYTES);
  }
}

function decodeProviderRosterEntry(decoder: CoreStatusPayloadDecoder): ProviderRosterEntry {
  const value = {
    provider_id: decoder.readString(MAX_PROVIDER_ID_BYTES),
    display_name: decoder.readString(MAX_PROVIDER_DISPLAY_NAME_BYTES),
    origin: decodeProviderOriginView(decoder),
    auth_methods: decoder.readList(MAX_AUTH_METHODS, () => decodeProviderAuthMethodView(decoder)),
    stored: decoder.readBoolean() ? decodeStoredCredentialView(decoder) : null,
    signing_in: decoder.readBoolean(),
    enabled: decoder.readBoolean(),
    endpoint_host: decoder.readBoolean() ? decoder.readString(MAX_SOURCE_HOST_BYTES) : null,
    configurable: decoder.readBoolean(),
    endpoint_changed: decoder.readBoolean(),
    catalog_layer: decodeCatalogLayerView(decoder),
    selected_model_id: decoder.readBoolean() ? decoder.readString(MAX_MODEL_ID_BYTES) : null,
    thinking: decoder.readBoolean() ? decodeThinkingPreferenceView(decoder) : null,
    presentation: decoder.readBoolean() ? decodeProviderPresentationView(decoder) : null,
    endpoint_base: decoder.readBoolean() ? decoder.readString(MAX_PROVIDER_ENDPOINT_BYTES) : null,
    last_refusal: decoder.readBoolean() ? decodeProviderRefusalStateView(decoder) : null,
    model_count: decoder.readU32(),
    subscription: decoder.readBoolean(),
    refused_endpoint_host: decoder.readBoolean() ? decoder.readString(MAX_SOURCE_HOST_BYTES) : null,
  } satisfies ProviderRosterEntry;
  validateProviderRosterEntry(value);
  return value;
}

function validateProviderProbeView(value: ProviderProbeView): void {
  void value;
}

function encodeProviderProbeView(encoder: CoreStatusPayloadEncoder, value: ProviderProbeView): void {
  validateProviderProbeView(value);
  encoder.putString(value.provider_id, MAX_PROVIDER_ID_BYTES);
  encodeProviderProbeVerdictView(encoder, value.verdict);
  encoder.putU64(value.at_monotonic_ms);
  if (value.endpoint === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encodeProbeEndpointView(encoder, value.endpoint);
  }
}

function decodeProviderProbeView(decoder: CoreStatusPayloadDecoder): ProviderProbeView {
  const value = {
    provider_id: decoder.readString(MAX_PROVIDER_ID_BYTES),
    verdict: decodeProviderProbeVerdictView(decoder),
    at_monotonic_ms: decoder.readU64(),
    endpoint: decoder.readBoolean() ? decodeProbeEndpointView(decoder) : null,
  } satisfies ProviderProbeView;
  validateProviderProbeView(value);
  return value;
}

function validateProbeEndpointView(value: ProbeEndpointView): void {
  void value;
}

function encodeProbeEndpointView(encoder: CoreStatusPayloadEncoder, value: ProbeEndpointView): void {
  validateProbeEndpointView(value);
  encodeServerKindView(encoder, value.server_kind);
  encoder.putU32(value.model_count);
  encoder.putLength(value.models.length, MAX_CUSTOM_MODEL_ENTRIES);
  for (const item of value.models) {
    encodeCustomModelSpecView(encoder, item);
  }
  if (value.proved_base === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encoder.putString(value.proved_base, MAX_PROVIDER_ENDPOINT_BYTES);
  }
}

function decodeProbeEndpointView(decoder: CoreStatusPayloadDecoder): ProbeEndpointView {
  const value = {
    server_kind: decodeServerKindView(decoder),
    model_count: decoder.readU32(),
    models: decoder.readList(MAX_CUSTOM_MODEL_ENTRIES, () => decodeCustomModelSpecView(decoder)),
    proved_base: decoder.readBoolean() ? decoder.readString(MAX_PROVIDER_ENDPOINT_BYTES) : null,
  } satisfies ProbeEndpointView;
  validateProbeEndpointView(value);
  return value;
}

function validateSavedSignInView(value: SavedSignInView): void {
  void value;
}

function encodeSavedSignInView(encoder: CoreStatusPayloadEncoder, value: SavedSignInView): void {
  validateSavedSignInView(value);
  encoder.putString(value.id, MAX_IDENTIFIER_BYTES);
  encoder.putString(value.site, MAX_SAVED_SIGN_IN_SITE_BYTES);
  encoder.putString(value.username, MAX_SAVED_SIGN_IN_USERNAME_BYTES);
  encoder.putU64(value.last_used_epoch_ms);
}

function decodeSavedSignInView(decoder: CoreStatusPayloadDecoder): SavedSignInView {
  const value = {
    id: decoder.readString(MAX_IDENTIFIER_BYTES),
    site: decoder.readString(MAX_SAVED_SIGN_IN_SITE_BYTES),
    username: decoder.readString(MAX_SAVED_SIGN_IN_USERNAME_BYTES),
    last_used_epoch_ms: decoder.readU64(),
  } satisfies SavedSignInView;
  validateSavedSignInView(value);
  return value;
}

function validateSavedSignInsView(value: SavedSignInsView): void {
  void value;
}

function encodeSavedSignInsView(encoder: CoreStatusPayloadEncoder, value: SavedSignInsView): void {
  validateSavedSignInsView(value);
  encodeSavedDataAvailability(encoder, value.availability);
  encoder.putU64(value.revision);
  encoder.putLength(value.records.length, MAX_SAVED_SIGN_INS);
  for (const item of value.records) {
    encodeSavedSignInView(encoder, item);
  }
}

function decodeSavedSignInsView(decoder: CoreStatusPayloadDecoder): SavedSignInsView {
  const value = {
    availability: decodeSavedDataAvailability(decoder),
    revision: decoder.readU64(),
    records: decoder.readList(MAX_SAVED_SIGN_INS, () => decodeSavedSignInView(decoder)),
  } satisfies SavedSignInsView;
  validateSavedSignInsView(value);
  return value;
}

function validateSavedDetailView(value: SavedDetailView): void {
  void value;
}

function encodeSavedDetailView(encoder: CoreStatusPayloadEncoder, value: SavedDetailView): void {
  validateSavedDetailView(value);
  encoder.putString(value.id, MAX_IDENTIFIER_BYTES);
  encoder.putString(value.given_name, MAX_SAVED_DETAIL_NAME_BYTES);
  encoder.putString(value.family_name, MAX_SAVED_DETAIL_NAME_BYTES);
  encoder.putString(value.email, MAX_SAVED_DETAIL_EMAIL_BYTES);
  encoder.putString(value.phone, MAX_SAVED_DETAIL_PHONE_BYTES);
  encoder.putString(value.address, MAX_SAVED_DETAIL_ADDRESS_BYTES);
  encoder.putString(value.postcode, MAX_SAVED_DETAIL_POSTCODE_BYTES);
  encoder.putString(value.country, MAX_SAVED_DETAIL_COUNTRY_BYTES);
}

function decodeSavedDetailView(decoder: CoreStatusPayloadDecoder): SavedDetailView {
  const value = {
    id: decoder.readString(MAX_IDENTIFIER_BYTES),
    given_name: decoder.readString(MAX_SAVED_DETAIL_NAME_BYTES),
    family_name: decoder.readString(MAX_SAVED_DETAIL_NAME_BYTES),
    email: decoder.readString(MAX_SAVED_DETAIL_EMAIL_BYTES),
    phone: decoder.readString(MAX_SAVED_DETAIL_PHONE_BYTES),
    address: decoder.readString(MAX_SAVED_DETAIL_ADDRESS_BYTES),
    postcode: decoder.readString(MAX_SAVED_DETAIL_POSTCODE_BYTES),
    country: decoder.readString(MAX_SAVED_DETAIL_COUNTRY_BYTES),
  } satisfies SavedDetailView;
  validateSavedDetailView(value);
  return value;
}

function validateSavedDetailsView(value: SavedDetailsView): void {
  void value;
}

function encodeSavedDetailsView(encoder: CoreStatusPayloadEncoder, value: SavedDetailsView): void {
  validateSavedDetailsView(value);
  encodeSavedDataAvailability(encoder, value.availability);
  encoder.putU64(value.revision);
  encoder.putLength(value.people.length, MAX_SAVED_DETAILS);
  for (const item of value.people) {
    encodeSavedDetailView(encoder, item);
  }
}

function decodeSavedDetailsView(decoder: CoreStatusPayloadDecoder): SavedDetailsView {
  const value = {
    availability: decodeSavedDataAvailability(decoder),
    revision: decoder.readU64(),
    people: decoder.readList(MAX_SAVED_DETAILS, () => decodeSavedDetailView(decoder)),
  } satisfies SavedDetailsView;
  validateSavedDetailsView(value);
  return value;
}

function validateCoreStatus(value: CoreStatus): void {
  if (value.projection_mode === CoreStatusProjectionMode.Complete || value.projection_mode === CoreStatusProjectionMode.RecoveryRequired) {
    const expected = [BuiltinSkillIdView.GeneralWebResearch, BuiltinSkillIdView.DeepResearch, BuiltinSkillIdView.ProductComparison, BuiltinSkillIdView.MultiTabComparison, BuiltinSkillIdView.WebsiteSummarizer, BuiltinSkillIdView.PdfAnalysis, BuiltinSkillIdView.DataExtraction, BuiltinSkillIdView.FormAssistant, BuiltinSkillIdView.Shopping, BuiltinSkillIdView.DownloadOrganizer, BuiltinSkillIdView.TravelResearch, BuiltinSkillIdView.VideoTranscriptAnalyzer, BuiltinSkillIdView.ImageUnderstanding, BuiltinSkillIdView.LibraryBuilder, BuiltinSkillIdView.SpreadsheetBuilder, BuiltinSkillIdView.DocumentGenerator] as const;
    if (value.builtin_skills.length !== expected.length ||
        value.builtin_skills.some((item, index) => item.reference.skill_id !== expected[index])) {
      fail(CoreStatusPayloadCodecError.Malformed);
    }
  }
  if (value.projection_mode === CoreStatusProjectionMode.RecoveryRequired) {
    const expected = [CoreStatusProjectionFamily.ActiveTasks, CoreStatusProjectionFamily.Workspaces, CoreStatusProjectionFamily.WorkspaceExport, CoreStatusProjectionFamily.AssetDelivery, CoreStatusProjectionFamily.ProviderRoster, CoreStatusProjectionFamily.ProviderProbes, CoreStatusProjectionFamily.ProviderModels, CoreStatusProjectionFamily.Library, CoreStatusProjectionFamily.LibraryExport, CoreStatusProjectionFamily.Memory, CoreStatusProjectionFamily.SavedSignIns, CoreStatusProjectionFamily.SavedDetails, CoreStatusProjectionFamily.SiteSkills] as const;
    if (value.projection_omissions.length !== expected.length ||
        value.projection_omissions.some((item, index) => item.family !== expected[index])) {
      fail(CoreStatusPayloadCodecError.Malformed);
    }
  }
  if ((value.projection_mode === CoreStatusProjectionMode.Complete) && value.projection_omissions.length !== 0) {
    fail(CoreStatusPayloadCodecError.Malformed);
  }
}

function encodeCoreStatus(encoder: CoreStatusPayloadEncoder, value: CoreStatus): void {
  validateCoreStatus(value);
  encodeCoreAvailability(encoder, value.availability);
  encoder.putU64(value.generation);
  encoder.putLength(value.active_tasks.length, MAX_ACTIVE_TASKS);
  for (const item of value.active_tasks) {
    encodeTaskViewState(encoder, item);
  }
  if (value.auth_state === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encodeAuthViewState(encoder, value.auth_state);
  }
  encoder.putLength(value.workspaces.length, MAX_WORKSPACES);
  for (const item of value.workspaces) {
    encodeWorkspaceViewState(encoder, item);
  }
  if (value.workspace_export === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encodeWorkspaceExportView(encoder, value.workspace_export);
  }
  if (value.asset_delivery === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encodeAssetDeliveryView(encoder, value.asset_delivery);
  }
  encoder.putLength(value.provider_roster.length, MAX_PROVIDER_ROSTER_ENTRIES);
  for (const item of value.provider_roster) {
    encodeProviderRosterEntry(encoder, item);
  }
  encoder.putLength(value.provider_probes.length, MAX_PROVIDER_ROSTER_ENTRIES);
  for (const item of value.provider_probes) {
    encodeProviderProbeView(encoder, item);
  }
  encoder.putLength(value.provider_models.length, MAX_PROVIDER_MODEL_ENTRIES);
  for (const item of value.provider_models) {
    encodeProviderModelView(encoder, item);
  }
  encodeAssistantConfigurationView(encoder, value.assistant_configuration);
  encodeLibraryViewState(encoder, value.library);
  if (value.library_export === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encodeLibraryExportView(encoder, value.library_export);
  }
  encodeMemoryViewState(encoder, value.memory);
  encodeSavedSignInsView(encoder, value.saved_sign_ins);
  encodeSavedDetailsView(encoder, value.saved_details);
  encoder.putLength(value.site_skills.length, MAX_SITE_SKILLS);
  for (const item of value.site_skills) {
    encodeSiteSkillView(encoder, item);
  }
  encoder.putLength(value.builtin_skills.length, MAX_BUILTIN_SKILLS);
  for (const item of value.builtin_skills) {
    encodeBuiltinSkillView(encoder, item);
  }
  encodeCoreStatusProjectionMode(encoder, value.projection_mode);
  encoder.putLength(value.projection_omissions.length, MAX_CORE_STATUS_PROJECTION_OMISSIONS);
  for (const item of value.projection_omissions) {
    encodeCoreStatusProjectionOmission(encoder, item);
  }
}

function decodeCoreStatus(decoder: CoreStatusPayloadDecoder): CoreStatus {
  const value = {
    availability: decodeCoreAvailability(decoder),
    generation: decoder.readU64(),
    active_tasks: decoder.readList(MAX_ACTIVE_TASKS, () => decodeTaskViewState(decoder)),
    auth_state: decoder.readBoolean() ? decodeAuthViewState(decoder) : null,
    workspaces: decoder.readList(MAX_WORKSPACES, () => decodeWorkspaceViewState(decoder)),
    workspace_export: decoder.readBoolean() ? decodeWorkspaceExportView(decoder) : null,
    asset_delivery: decoder.readBoolean() ? decodeAssetDeliveryView(decoder) : null,
    provider_roster: decoder.readList(MAX_PROVIDER_ROSTER_ENTRIES, () => decodeProviderRosterEntry(decoder)),
    provider_probes: decoder.readList(MAX_PROVIDER_ROSTER_ENTRIES, () => decodeProviderProbeView(decoder)),
    provider_models: decoder.readList(MAX_PROVIDER_MODEL_ENTRIES, () => decodeProviderModelView(decoder)),
    assistant_configuration: decodeAssistantConfigurationView(decoder),
    library: decodeLibraryViewState(decoder),
    library_export: decoder.readBoolean() ? decodeLibraryExportView(decoder) : null,
    memory: decodeMemoryViewState(decoder),
    saved_sign_ins: decodeSavedSignInsView(decoder),
    saved_details: decodeSavedDetailsView(decoder),
    site_skills: decoder.readList(MAX_SITE_SKILLS, () => decodeSiteSkillView(decoder)),
    builtin_skills: decoder.readList(MAX_BUILTIN_SKILLS, () => decodeBuiltinSkillView(decoder)),
    projection_mode: decodeCoreStatusProjectionMode(decoder),
    projection_omissions: decoder.readList(MAX_CORE_STATUS_PROJECTION_OMISSIONS, () => decodeCoreStatusProjectionOmission(decoder)),
  } satisfies CoreStatus;
  validateCoreStatus(value);
  return value;
}

function validateWorkspaceViewState(value: WorkspaceViewState): void {
  void value;
}

function encodeWorkspaceViewState(encoder: CoreStatusPayloadEncoder, value: WorkspaceViewState): void {
  validateWorkspaceViewState(value);
  encoder.putString(value.workspace_id, MAX_IDENTIFIER_BYTES);
  encoder.putU64(value.revision);
  encoder.putString(value.goal, MAX_TASK_GOAL_BYTES);
  encodeWorkspacePhase(encoder, value.phase);
  encoder.putU64(value.last_updated_epoch_ms);
  encodeTaskTemplateId(encoder, value.template_id);
  encoder.putLength(value.sources.length, MAX_WORKSPACE_SOURCES);
  for (const item of value.sources) {
    encodeWorkspaceSourceView(encoder, item);
  }
  encoder.putLength(value.facts.length, MAX_WORKSPACE_FACTS);
  for (const item of value.facts) {
    encodeWorkspaceFactView(encoder, item);
  }
  encoder.putBoolean(value.saved);
  encoder.putString(value.display_name, MAX_WORKSPACE_DISPLAY_NAME_BYTES);
  if (value.deletion_preview === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encodeWorkspaceDeletionPreviewView(encoder, value.deletion_preview);
  }
}

function decodeWorkspaceViewState(decoder: CoreStatusPayloadDecoder): WorkspaceViewState {
  const value = {
    workspace_id: decoder.readString(MAX_IDENTIFIER_BYTES),
    revision: decoder.readU64(),
    goal: decoder.readString(MAX_TASK_GOAL_BYTES),
    phase: decodeWorkspacePhase(decoder),
    last_updated_epoch_ms: decoder.readU64(),
    template_id: decodeTaskTemplateId(decoder),
    sources: decoder.readList(MAX_WORKSPACE_SOURCES, () => decodeWorkspaceSourceView(decoder)),
    facts: decoder.readList(MAX_WORKSPACE_FACTS, () => decodeWorkspaceFactView(decoder)),
    saved: decoder.readBoolean(),
    display_name: decoder.readString(MAX_WORKSPACE_DISPLAY_NAME_BYTES),
    deletion_preview: decoder.readBoolean() ? decodeWorkspaceDeletionPreviewView(decoder) : null,
  } satisfies WorkspaceViewState;
  validateWorkspaceViewState(value);
  return value;
}

function validateWorkspaceDeletionPreviewView(value: WorkspaceDeletionPreviewView): void {
  void value;
}

function encodeWorkspaceDeletionPreviewView(encoder: CoreStatusPayloadEncoder, value: WorkspaceDeletionPreviewView): void {
  validateWorkspaceDeletionPreviewView(value);
  encoder.putU32(value.sources);
  encoder.putU32(value.facts);
  encoder.putU32(value.artifact_metadata);
  encoder.putU32(value.derived_indexes);
  encoder.putString(value.confirmation_token, MAX_WORKSPACE_CONFIRMATION_TOKEN_BYTES);
}

function decodeWorkspaceDeletionPreviewView(decoder: CoreStatusPayloadDecoder): WorkspaceDeletionPreviewView {
  const value = {
    sources: decoder.readU32(),
    facts: decoder.readU32(),
    artifact_metadata: decoder.readU32(),
    derived_indexes: decoder.readU32(),
    confirmation_token: decoder.readString(MAX_WORKSPACE_CONFIRMATION_TOKEN_BYTES),
  } satisfies WorkspaceDeletionPreviewView;
  validateWorkspaceDeletionPreviewView(value);
  return value;
}

function validateWorkspaceSourceView(value: WorkspaceSourceView): void {
  void value;
}

function encodeWorkspaceSourceView(encoder: CoreStatusPayloadEncoder, value: WorkspaceSourceView): void {
  validateWorkspaceSourceView(value);
  encoder.putString(value.source_id, MAX_IDENTIFIER_BYTES);
  encoder.putString(value.title, MAX_WORKSPACE_TITLE_BYTES);
  encoder.putString(value.host, MAX_SOURCE_HOST_BYTES);
  encoder.putU64(value.read_at_epoch_ms);
  encoder.putU32(value.fact_count);
  encoder.putBoolean(value.excluded);
}

function decodeWorkspaceSourceView(decoder: CoreStatusPayloadDecoder): WorkspaceSourceView {
  const value = {
    source_id: decoder.readString(MAX_IDENTIFIER_BYTES),
    title: decoder.readString(MAX_WORKSPACE_TITLE_BYTES),
    host: decoder.readString(MAX_SOURCE_HOST_BYTES),
    read_at_epoch_ms: decoder.readU64(),
    fact_count: decoder.readU32(),
    excluded: decoder.readBoolean(),
  } satisfies WorkspaceSourceView;
  validateWorkspaceSourceView(value);
  return value;
}

function validateWorkspaceFactView(value: WorkspaceFactView): void {
  void value;
}

function encodeWorkspaceFactView(encoder: CoreStatusPayloadEncoder, value: WorkspaceFactView): void {
  validateWorkspaceFactView(value);
  encoder.putString(value.fact_id, MAX_IDENTIFIER_BYTES);
  encoder.putString(value.field, MAX_FACT_FIELD_BYTES);
  encoder.putString(value.value, MAX_FACT_VALUE_BYTES);
  encodeWorkspaceFactKind(encoder, value.kind);
  encoder.putLength(value.sources.length, MAX_FACT_SOURCES);
  for (const item of value.sources) {
    encoder.putString(item, MAX_IDENTIFIER_BYTES);
  }
  if (value.correction === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encoder.putString(value.correction, MAX_FACT_VALUE_BYTES);
  }
  encoder.putBoolean(value.has_conflict);
  encoder.putBoolean(value.needs_new_source);
}

function decodeWorkspaceFactView(decoder: CoreStatusPayloadDecoder): WorkspaceFactView {
  const value = {
    fact_id: decoder.readString(MAX_IDENTIFIER_BYTES),
    field: decoder.readString(MAX_FACT_FIELD_BYTES),
    value: decoder.readString(MAX_FACT_VALUE_BYTES),
    kind: decodeWorkspaceFactKind(decoder),
    sources: decoder.readList(MAX_FACT_SOURCES, () => decoder.readString(MAX_IDENTIFIER_BYTES)),
    correction: decoder.readBoolean() ? decoder.readString(MAX_FACT_VALUE_BYTES) : null,
    has_conflict: decoder.readBoolean(),
    needs_new_source: decoder.readBoolean(),
  } satisfies WorkspaceFactView;
  validateWorkspaceFactView(value);
  return value;
}

function validateWorkspaceExportView(value: WorkspaceExportView): void {
  void value;
}

function encodeWorkspaceExportView(encoder: CoreStatusPayloadEncoder, value: WorkspaceExportView): void {
  validateWorkspaceExportView(value);
  encoder.putString(value.request_id, MAX_IDENTIFIER_BYTES);
  encoder.putString(value.workspace_id, MAX_IDENTIFIER_BYTES);
  encoder.putU64(value.revision);
  encodeWorkspaceExportFormat(encoder, value.format);
  encoder.putString(value.content, MAX_EXPORT_CONTENT_BYTES);
}

function decodeWorkspaceExportView(decoder: CoreStatusPayloadDecoder): WorkspaceExportView {
  const value = {
    request_id: decoder.readString(MAX_IDENTIFIER_BYTES),
    workspace_id: decoder.readString(MAX_IDENTIFIER_BYTES),
    revision: decoder.readU64(),
    format: decodeWorkspaceExportFormat(decoder),
    content: decoder.readString(MAX_EXPORT_CONTENT_BYTES),
  } satisfies WorkspaceExportView;
  validateWorkspaceExportView(value);
  return value;
}

function validateLibrarySourceView(value: LibrarySourceView): void {
  void value;
}

function encodeLibrarySourceView(encoder: CoreStatusPayloadEncoder, value: LibrarySourceView): void {
  validateLibrarySourceView(value);
  encoder.putString(value.source_id, MAX_IDENTIFIER_BYTES);
  encoder.putString(value.title, MAX_WORKSPACE_TITLE_BYTES);
  encoder.putString(value.host, MAX_SOURCE_HOST_BYTES);
  encoder.putU64(value.observed_at_epoch_ms);
}

function decodeLibrarySourceView(decoder: CoreStatusPayloadDecoder): LibrarySourceView {
  const value = {
    source_id: decoder.readString(MAX_IDENTIFIER_BYTES),
    title: decoder.readString(MAX_WORKSPACE_TITLE_BYTES),
    host: decoder.readString(MAX_SOURCE_HOST_BYTES),
    observed_at_epoch_ms: decoder.readU64(),
  } satisfies LibrarySourceView;
  validateLibrarySourceView(value);
  return value;
}

function validateLibraryEntryView(value: LibraryEntryView): void {
  void value;
}

function encodeLibraryEntryView(encoder: CoreStatusPayloadEncoder, value: LibraryEntryView): void {
  validateLibraryEntryView(value);
  encoder.putString(value.entry_id, MAX_IDENTIFIER_BYTES);
  encoder.putU64(value.revision);
  encoder.putString(value.collection_id, MAX_IDENTIFIER_BYTES);
  encoder.putString(value.collection_name, MAX_WORKSPACE_DISPLAY_NAME_BYTES);
  encoder.putString(value.source_workspace_id, MAX_IDENTIFIER_BYTES);
  encoder.putU64(value.source_workspace_revision);
  encoder.putString(value.source_fact_id, MAX_IDENTIFIER_BYTES);
  encoder.putString(value.field, MAX_FACT_FIELD_BYTES);
  encoder.putString(value.original_value, MAX_FACT_VALUE_BYTES);
  if (value.correction === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encoder.putString(value.correction, MAX_FACT_VALUE_BYTES);
  }
  encodeWorkspaceFactKind(encoder, value.kind);
  encoder.putLength(value.sources.length, MAX_LIBRARY_SOURCES);
  for (const item of value.sources) {
    encodeLibrarySourceView(encoder, item);
  }
  encoder.putU64(value.captured_at_epoch_ms);
  encoder.putU64(value.last_checked_epoch_ms);
  encoder.putBoolean(value.has_conflict);
}

function decodeLibraryEntryView(decoder: CoreStatusPayloadDecoder): LibraryEntryView {
  const value = {
    entry_id: decoder.readString(MAX_IDENTIFIER_BYTES),
    revision: decoder.readU64(),
    collection_id: decoder.readString(MAX_IDENTIFIER_BYTES),
    collection_name: decoder.readString(MAX_WORKSPACE_DISPLAY_NAME_BYTES),
    source_workspace_id: decoder.readString(MAX_IDENTIFIER_BYTES),
    source_workspace_revision: decoder.readU64(),
    source_fact_id: decoder.readString(MAX_IDENTIFIER_BYTES),
    field: decoder.readString(MAX_FACT_FIELD_BYTES),
    original_value: decoder.readString(MAX_FACT_VALUE_BYTES),
    correction: decoder.readBoolean() ? decoder.readString(MAX_FACT_VALUE_BYTES) : null,
    kind: decodeWorkspaceFactKind(decoder),
    sources: decoder.readList(MAX_LIBRARY_SOURCES, () => decodeLibrarySourceView(decoder)),
    captured_at_epoch_ms: decoder.readU64(),
    last_checked_epoch_ms: decoder.readU64(),
    has_conflict: decoder.readBoolean(),
  } satisfies LibraryEntryView;
  validateLibraryEntryView(value);
  return value;
}

function validateLibrarySearchHitView(value: LibrarySearchHitView): void {
  void value;
}

function encodeLibrarySearchHitView(encoder: CoreStatusPayloadEncoder, value: LibrarySearchHitView): void {
  validateLibrarySearchHitView(value);
  encoder.putString(value.entry_id, MAX_IDENTIFIER_BYTES);
  encoder.putU64(value.age_ms);
}

function decodeLibrarySearchHitView(decoder: CoreStatusPayloadDecoder): LibrarySearchHitView {
  const value = {
    entry_id: decoder.readString(MAX_IDENTIFIER_BYTES),
    age_ms: decoder.readU64(),
  } satisfies LibrarySearchHitView;
  validateLibrarySearchHitView(value);
  return value;
}

function validateLibrarySearchView(value: LibrarySearchView): void {
  void value;
}

function encodeLibrarySearchView(encoder: CoreStatusPayloadEncoder, value: LibrarySearchView): void {
  validateLibrarySearchView(value);
  encoder.putString(value.request_id, MAX_IDENTIFIER_BYTES);
  encoder.putString(value.query, MAX_LIBRARY_QUERY_BYTES);
  encoder.putU64(value.library_revision);
  encoder.putLength(value.hits.length, MAX_LIBRARY_SEARCH_RESULTS);
  for (const item of value.hits) {
    encodeLibrarySearchHitView(encoder, item);
  }
}

function decodeLibrarySearchView(decoder: CoreStatusPayloadDecoder): LibrarySearchView {
  const value = {
    request_id: decoder.readString(MAX_IDENTIFIER_BYTES),
    query: decoder.readString(MAX_LIBRARY_QUERY_BYTES),
    library_revision: decoder.readU64(),
    hits: decoder.readList(MAX_LIBRARY_SEARCH_RESULTS, () => decodeLibrarySearchHitView(decoder)),
  } satisfies LibrarySearchView;
  validateLibrarySearchView(value);
  return value;
}

function validateLibraryRefreshSourceView(value: LibraryRefreshSourceView): void {
  void value;
}

function encodeLibraryRefreshSourceView(encoder: CoreStatusPayloadEncoder, value: LibraryRefreshSourceView): void {
  validateLibraryRefreshSourceView(value);
  encoder.putString(value.source_id, MAX_IDENTIFIER_BYTES);
  encoder.putString(value.title, MAX_WORKSPACE_TITLE_BYTES);
  encoder.putString(value.host, MAX_SOURCE_HOST_BYTES);
}

function decodeLibraryRefreshSourceView(decoder: CoreStatusPayloadDecoder): LibraryRefreshSourceView {
  const value = {
    source_id: decoder.readString(MAX_IDENTIFIER_BYTES),
    title: decoder.readString(MAX_WORKSPACE_TITLE_BYTES),
    host: decoder.readString(MAX_SOURCE_HOST_BYTES),
  } satisfies LibraryRefreshSourceView;
  validateLibraryRefreshSourceView(value);
  return value;
}

function validateLibraryRefreshPreviewView(value: LibraryRefreshPreviewView): void {
  void value;
}

function encodeLibraryRefreshPreviewView(encoder: CoreStatusPayloadEncoder, value: LibraryRefreshPreviewView): void {
  validateLibraryRefreshPreviewView(value);
  encoder.putString(value.preview_id, MAX_LIBRARY_REFRESH_PREVIEW_ID_BYTES);
  encoder.putString(value.collection_id, MAX_IDENTIFIER_BYTES);
  encoder.putU64(value.library_revision);
  encoder.putU64(value.source_workspace_revision);
  encodeTaskProviderRoute(encoder, value.provider_route);
  encoder.putU32(value.navigation_count);
  encoder.putU32(value.observation_count);
  encoder.putLength(value.sources.length, MAX_LIBRARY_REFRESH_SOURCES);
  for (const item of value.sources) {
    encodeLibraryRefreshSourceView(encoder, item);
  }
}

function decodeLibraryRefreshPreviewView(decoder: CoreStatusPayloadDecoder): LibraryRefreshPreviewView {
  const value = {
    preview_id: decoder.readString(MAX_LIBRARY_REFRESH_PREVIEW_ID_BYTES),
    collection_id: decoder.readString(MAX_IDENTIFIER_BYTES),
    library_revision: decoder.readU64(),
    source_workspace_revision: decoder.readU64(),
    provider_route: decodeTaskProviderRoute(decoder),
    navigation_count: decoder.readU32(),
    observation_count: decoder.readU32(),
    sources: decoder.readList(MAX_LIBRARY_REFRESH_SOURCES, () => decodeLibraryRefreshSourceView(decoder)),
  } satisfies LibraryRefreshPreviewView;
  validateLibraryRefreshPreviewView(value);
  return value;
}

function validateLibraryRefreshResultItemView(value: LibraryRefreshResultItemView): void {
  void value;
}

function encodeLibraryRefreshResultItemView(encoder: CoreStatusPayloadEncoder, value: LibraryRefreshResultItemView): void {
  validateLibraryRefreshResultItemView(value);
  encoder.putString(value.source_id, MAX_IDENTIFIER_BYTES);
  encodeLibraryRefreshDisposition(encoder, value.disposition);
}

function decodeLibraryRefreshResultItemView(decoder: CoreStatusPayloadDecoder): LibraryRefreshResultItemView {
  const value = {
    source_id: decoder.readString(MAX_IDENTIFIER_BYTES),
    disposition: decodeLibraryRefreshDisposition(decoder),
  } satisfies LibraryRefreshResultItemView;
  validateLibraryRefreshResultItemView(value);
  return value;
}

function validateLibraryRefreshResultView(value: LibraryRefreshResultView): void {
  void value;
}

function encodeLibraryRefreshResultView(encoder: CoreStatusPayloadEncoder, value: LibraryRefreshResultView): void {
  validateLibraryRefreshResultView(value);
  encoder.putString(value.preview_id, MAX_LIBRARY_REFRESH_PREVIEW_ID_BYTES);
  encoder.putString(value.collection_id, MAX_IDENTIFIER_BYTES);
  encoder.putLength(value.items.length, MAX_LIBRARY_REFRESH_SOURCES);
  for (const item of value.items) {
    encodeLibraryRefreshResultItemView(encoder, item);
  }
}

function decodeLibraryRefreshResultView(decoder: CoreStatusPayloadDecoder): LibraryRefreshResultView {
  const value = {
    preview_id: decoder.readString(MAX_LIBRARY_REFRESH_PREVIEW_ID_BYTES),
    collection_id: decoder.readString(MAX_IDENTIFIER_BYTES),
    items: decoder.readList(MAX_LIBRARY_REFRESH_SOURCES, () => decodeLibraryRefreshResultItemView(decoder)),
  } satisfies LibraryRefreshResultView;
  validateLibraryRefreshResultView(value);
  return value;
}

function validateLibraryViewState(value: LibraryViewState): void {
  void value;
}

function encodeLibraryViewState(encoder: CoreStatusPayloadEncoder, value: LibraryViewState): void {
  validateLibraryViewState(value);
  encodeLibraryAvailability(encoder, value.availability);
  encoder.putU64(value.revision);
  encoder.putLength(value.entries.length, MAX_LIBRARY_ENTRIES);
  for (const item of value.entries) {
    encodeLibraryEntryView(encoder, item);
  }
  if (value.search === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encodeLibrarySearchView(encoder, value.search);
  }
  encoder.putLength(value.refresh_previews.length, MAX_WORKSPACES);
  for (const item of value.refresh_previews) {
    encodeLibraryRefreshPreviewView(encoder, item);
  }
  encoder.putLength(value.refresh_results.length, MAX_LIBRARY_REFRESH_RESULTS);
  for (const item of value.refresh_results) {
    encodeLibraryRefreshResultView(encoder, item);
  }
}

function decodeLibraryViewState(decoder: CoreStatusPayloadDecoder): LibraryViewState {
  const value = {
    availability: decodeLibraryAvailability(decoder),
    revision: decoder.readU64(),
    entries: decoder.readList(MAX_LIBRARY_ENTRIES, () => decodeLibraryEntryView(decoder)),
    search: decoder.readBoolean() ? decodeLibrarySearchView(decoder) : null,
    refresh_previews: decoder.readList(MAX_WORKSPACES, () => decodeLibraryRefreshPreviewView(decoder)),
    refresh_results: decoder.readList(MAX_LIBRARY_REFRESH_RESULTS, () => decodeLibraryRefreshResultView(decoder)),
  } satisfies LibraryViewState;
  validateLibraryViewState(value);
  return value;
}

function validateLibraryExportView(value: LibraryExportView): void {
  void value;
}

function encodeLibraryExportView(encoder: CoreStatusPayloadEncoder, value: LibraryExportView): void {
  validateLibraryExportView(value);
  encoder.putString(value.request_id, MAX_IDENTIFIER_BYTES);
  encoder.putU64(value.library_revision);
  if (value.collection_id === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encoder.putString(value.collection_id, MAX_IDENTIFIER_BYTES);
  }
  encodeWorkspaceExportFormat(encoder, value.format);
  encoder.putString(value.content, MAX_EXPORT_CONTENT_BYTES);
}

function decodeLibraryExportView(decoder: CoreStatusPayloadDecoder): LibraryExportView {
  const value = {
    request_id: decoder.readString(MAX_IDENTIFIER_BYTES),
    library_revision: decoder.readU64(),
    collection_id: decoder.readBoolean() ? decoder.readString(MAX_IDENTIFIER_BYTES) : null,
    format: decodeWorkspaceExportFormat(decoder),
    content: decoder.readString(MAX_EXPORT_CONTENT_BYTES),
  } satisfies LibraryExportView;
  validateLibraryExportView(value);
  return value;
}

function validateMemoryWorkspaceView(value: MemoryWorkspaceView): void {
  void value;
}

function encodeMemoryWorkspaceView(encoder: CoreStatusPayloadEncoder, value: MemoryWorkspaceView): void {
  validateMemoryWorkspaceView(value);
  encoder.putString(value.workspace_id, MAX_IDENTIFIER_BYTES);
  encoder.putString(value.display_name, MAX_WORKSPACE_DISPLAY_NAME_BYTES);
}

function decodeMemoryWorkspaceView(decoder: CoreStatusPayloadDecoder): MemoryWorkspaceView {
  const value = {
    workspace_id: decoder.readString(MAX_IDENTIFIER_BYTES),
    display_name: decoder.readString(MAX_WORKSPACE_DISPLAY_NAME_BYTES),
  } satisfies MemoryWorkspaceView;
  validateMemoryWorkspaceView(value);
  return value;
}

function validateMemoryRecordView(value: MemoryRecordView): void {
  void value;
}

function encodeMemoryRecordView(encoder: CoreStatusPayloadEncoder, value: MemoryRecordView): void {
  validateMemoryRecordView(value);
  encoder.putString(value.memory_id, MAX_IDENTIFIER_BYTES);
  encoder.putU64(value.revision);
  encoder.putString(value.statement, MAX_MEMORY_STATEMENT_BYTES);
  encodeMemorySourceKind(encoder, value.source_kind);
  if (value.source_task_id === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encoder.putString(value.source_task_id, MAX_IDENTIFIER_BYTES);
  }
  if (value.source_workspace === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encodeMemoryWorkspaceView(encoder, value.source_workspace);
  }
  encodeMemoryScopeKind(encoder, value.scope_kind);
  if (value.scope_workspace === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encodeMemoryWorkspaceView(encoder, value.scope_workspace);
  }
  encodeMemorySensitivity(encoder, value.sensitivity);
  encoder.putU64(value.created_at_epoch_ms);
  encoder.putU64(value.updated_at_epoch_ms);
  encoder.putU64(value.reviewed_at_epoch_ms);
  encoder.putU64(value.expires_at_epoch_ms);
}

function decodeMemoryRecordView(decoder: CoreStatusPayloadDecoder): MemoryRecordView {
  const value = {
    memory_id: decoder.readString(MAX_IDENTIFIER_BYTES),
    revision: decoder.readU64(),
    statement: decoder.readString(MAX_MEMORY_STATEMENT_BYTES),
    source_kind: decodeMemorySourceKind(decoder),
    source_task_id: decoder.readBoolean() ? decoder.readString(MAX_IDENTIFIER_BYTES) : null,
    source_workspace: decoder.readBoolean() ? decodeMemoryWorkspaceView(decoder) : null,
    scope_kind: decodeMemoryScopeKind(decoder),
    scope_workspace: decoder.readBoolean() ? decodeMemoryWorkspaceView(decoder) : null,
    sensitivity: decodeMemorySensitivity(decoder),
    created_at_epoch_ms: decoder.readU64(),
    updated_at_epoch_ms: decoder.readU64(),
    reviewed_at_epoch_ms: decoder.readU64(),
    expires_at_epoch_ms: decoder.readU64(),
  } satisfies MemoryRecordView;
  validateMemoryRecordView(value);
  return value;
}

function validateMemorySearchHitView(value: MemorySearchHitView): void {
  void value;
}

function encodeMemorySearchHitView(encoder: CoreStatusPayloadEncoder, value: MemorySearchHitView): void {
  validateMemorySearchHitView(value);
  encoder.putString(value.memory_id, MAX_IDENTIFIER_BYTES);
}

function decodeMemorySearchHitView(decoder: CoreStatusPayloadDecoder): MemorySearchHitView {
  const value = {
    memory_id: decoder.readString(MAX_IDENTIFIER_BYTES),
  } satisfies MemorySearchHitView;
  validateMemorySearchHitView(value);
  return value;
}

function validateMemorySearchView(value: MemorySearchView): void {
  void value;
}

function encodeMemorySearchView(encoder: CoreStatusPayloadEncoder, value: MemorySearchView): void {
  validateMemorySearchView(value);
  encoder.putString(value.request_id, MAX_IDENTIFIER_BYTES);
  encoder.putString(value.query, MAX_MEMORY_QUERY_BYTES);
  encoder.putU64(value.memory_revision);
  encoder.putLength(value.hits.length, MAX_MEMORY_SEARCH_RESULTS);
  for (const item of value.hits) {
    encodeMemorySearchHitView(encoder, item);
  }
}

function decodeMemorySearchView(decoder: CoreStatusPayloadDecoder): MemorySearchView {
  const value = {
    request_id: decoder.readString(MAX_IDENTIFIER_BYTES),
    query: decoder.readString(MAX_MEMORY_QUERY_BYTES),
    memory_revision: decoder.readU64(),
    hits: decoder.readList(MAX_MEMORY_SEARCH_RESULTS, () => decodeMemorySearchHitView(decoder)),
  } satisfies MemorySearchView;
  validateMemorySearchView(value);
  return value;
}

function validateMemoryViewState(value: MemoryViewState): void {
  void value;
}

function encodeMemoryViewState(encoder: CoreStatusPayloadEncoder, value: MemoryViewState): void {
  validateMemoryViewState(value);
  encodeMemoryAvailability(encoder, value.availability);
  encoder.putU64(value.revision);
  encoder.putLength(value.records.length, MAX_MEMORY_RECORDS);
  for (const item of value.records) {
    encodeMemoryRecordView(encoder, item);
  }
  if (value.search === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encodeMemorySearchView(encoder, value.search);
  }
}

function decodeMemoryViewState(decoder: CoreStatusPayloadDecoder): MemoryViewState {
  const value = {
    availability: decodeMemoryAvailability(decoder),
    revision: decoder.readU64(),
    records: decoder.readList(MAX_MEMORY_RECORDS, () => decodeMemoryRecordView(decoder)),
    search: decoder.readBoolean() ? decodeMemorySearchView(decoder) : null,
  } satisfies MemoryViewState;
  validateMemoryViewState(value);
  return value;
}

function validateAuthAccountView(value: AuthAccountView): void {
  void value;
}

function encodeAuthAccountView(encoder: CoreStatusPayloadEncoder, value: AuthAccountView): void {
  validateAuthAccountView(value);
  encoder.putString(value.account_id, MAX_IDENTIFIER_BYTES);
  if (value.display_name === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encoder.putString(value.display_name, MAX_AUTH_DISPLAY_NAME_BYTES);
  }
  if (value.email === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encoder.putString(value.email, MAX_AUTH_EMAIL_BYTES);
  }
  encodeAuthProvider(encoder, value.method);
}

function decodeAuthAccountView(decoder: CoreStatusPayloadDecoder): AuthAccountView {
  const value = {
    account_id: decoder.readString(MAX_IDENTIFIER_BYTES),
    display_name: decoder.readBoolean() ? decoder.readString(MAX_AUTH_DISPLAY_NAME_BYTES) : null,
    email: decoder.readBoolean() ? decoder.readString(MAX_AUTH_EMAIL_BYTES) : null,
    method: decodeAuthProvider(decoder),
  } satisfies AuthAccountView;
  validateAuthAccountView(value);
  return value;
}

function validateAuthFailure(value: AuthFailure): void {
  void value;
}

function encodeAuthFailure(encoder: CoreStatusPayloadEncoder, value: AuthFailure): void {
  validateAuthFailure(value);
  encodeAuthFailureCode(encoder, value.code);
  encoder.putBoolean(value.retryable);
}

function decodeAuthFailure(decoder: CoreStatusPayloadDecoder): AuthFailure {
  const value = {
    code: decodeAuthFailureCode(decoder),
    retryable: decoder.readBoolean(),
  } satisfies AuthFailure;
  validateAuthFailure(value);
  return value;
}

function validateAuthMethodView(value: AuthMethodView): void {
  void value;
}

function encodeAuthMethodView(encoder: CoreStatusPayloadEncoder, value: AuthMethodView): void {
  validateAuthMethodView(value);
  encodeAuthProvider(encoder, value.provider);
  encodeAuthMethodAvailability(encoder, value.availability);
}

function decodeAuthMethodView(decoder: CoreStatusPayloadDecoder): AuthMethodView {
  const value = {
    provider: decodeAuthProvider(decoder),
    availability: decodeAuthMethodAvailability(decoder),
  } satisfies AuthMethodView;
  validateAuthMethodView(value);
  return value;
}

function validateEntitlementView(value: EntitlementView): void {
  void value;
}

function encodeEntitlementView(encoder: CoreStatusPayloadEncoder, value: EntitlementView): void {
  validateEntitlementView(value);
  encoder.putString(value.plan_id, MAX_IDENTIFIER_BYTES);
  encoder.putU64(value.credits_granted);
  encoder.putU64(value.credits_remaining);
  encoder.putU64(value.next_renewal_epoch_seconds);
  encoder.putU64(value.valid_until_epoch_seconds);
}

function decodeEntitlementView(decoder: CoreStatusPayloadDecoder): EntitlementView {
  const value = {
    plan_id: decoder.readString(MAX_IDENTIFIER_BYTES),
    credits_granted: decoder.readU64(),
    credits_remaining: decoder.readU64(),
    next_renewal_epoch_seconds: decoder.readU64(),
    valid_until_epoch_seconds: decoder.readU64(),
  } satisfies EntitlementView;
  validateEntitlementView(value);
  return value;
}

function validateAuthViewState(value: AuthViewState): void {
  if ((value.phase === AuthPhase.SignedIn) !== (value.account !== null)) {
    fail(CoreStatusPayloadCodecError.Malformed);
  }
  if ((value.phase === AuthPhase.LinkSent) !== (value.pending_email !== null)) {
    fail(CoreStatusPayloadCodecError.Malformed);
  }
  if ((value.phase === AuthPhase.Failed) !== (value.failure !== null)) {
    fail(CoreStatusPayloadCodecError.Malformed);
  }
}

function encodeAuthViewState(encoder: CoreStatusPayloadEncoder, value: AuthViewState): void {
  validateAuthViewState(value);
  encodeAuthPhase(encoder, value.phase);
  if (value.account === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encodeAuthAccountView(encoder, value.account);
  }
  if (value.pending_email === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encoder.putString(value.pending_email, MAX_AUTH_EMAIL_BYTES);
  }
  if (value.failure === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encodeAuthFailure(encoder, value.failure);
  }
  encoder.putLength(value.methods.length, MAX_AUTH_METHODS);
  for (const item of value.methods) {
    encodeAuthMethodView(encoder, item);
  }
  if (value.entitlement === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encodeEntitlementView(encoder, value.entitlement);
  }
}

function decodeAuthViewState(decoder: CoreStatusPayloadDecoder): AuthViewState {
  const value = {
    phase: decodeAuthPhase(decoder),
    account: decoder.readBoolean() ? decodeAuthAccountView(decoder) : null,
    pending_email: decoder.readBoolean() ? decoder.readString(MAX_AUTH_EMAIL_BYTES) : null,
    failure: decoder.readBoolean() ? decodeAuthFailure(decoder) : null,
    methods: decoder.readList(MAX_AUTH_METHODS, () => decodeAuthMethodView(decoder)),
    entitlement: decoder.readBoolean() ? decodeEntitlementView(decoder) : null,
  } satisfies AuthViewState;
  validateAuthViewState(value);
  return value;
}

function validateAssetRefusal(value: AssetRefusal): void {
  void value;
}

function encodeAssetRefusal(encoder: CoreStatusPayloadEncoder, value: AssetRefusal): void {
  validateAssetRefusal(value);
  encodeAssetRefusalView(encoder, value.reason);
  encoder.putBoolean(value.retryable);
}

function decodeAssetRefusal(decoder: CoreStatusPayloadDecoder): AssetRefusal {
  const value = {
    reason: decodeAssetRefusalView(decoder),
    retryable: decoder.readBoolean(),
  } satisfies AssetRefusal;
  validateAssetRefusal(value);
  return value;
}

function validateAssetViewState(value: AssetViewState): void {
  void value;
}

function encodeAssetViewState(encoder: CoreStatusPayloadEncoder, value: AssetViewState): void {
  validateAssetViewState(value);
  encoder.putString(value.asset_id, MAX_IDENTIFIER_BYTES);
  encoder.putString(value.asset_revision, MAX_IDENTIFIER_BYTES);
  encodeAssetKindView(encoder, value.kind);
  encodeAssetPresenceView(encoder, value.presence);
  encoder.putU64(value.written_bytes);
  encoder.putU64(value.total_bytes);
  encoder.putU32(value.attempts);
  if (value.refusal === null) {
    encoder.putBoolean(false);
  } else {
    encoder.putBoolean(true);
    encodeAssetRefusal(encoder, value.refusal);
  }
  encoder.putU64(value.waiting_until_monotonic_ms);
}

function decodeAssetViewState(decoder: CoreStatusPayloadDecoder): AssetViewState {
  const value = {
    asset_id: decoder.readString(MAX_IDENTIFIER_BYTES),
    asset_revision: decoder.readString(MAX_IDENTIFIER_BYTES),
    kind: decodeAssetKindView(decoder),
    presence: decodeAssetPresenceView(decoder),
    written_bytes: decoder.readU64(),
    total_bytes: decoder.readU64(),
    attempts: decoder.readU32(),
    refusal: decoder.readBoolean() ? decodeAssetRefusal(decoder) : null,
    waiting_until_monotonic_ms: decoder.readU64(),
  } satisfies AssetViewState;
  validateAssetViewState(value);
  return value;
}

function validateAssetDeliveryView(value: AssetDeliveryView): void {
  void value;
}

function encodeAssetDeliveryView(encoder: CoreStatusPayloadEncoder, value: AssetDeliveryView): void {
  validateAssetDeliveryView(value);
  encoder.putBoolean(value.platform_supported);
  encodeAssetNetworkCostView(encoder, value.network_cost);
  encoder.putBoolean(value.metered_permitted);
  encoder.putLength(value.assets.length, MAX_ASSETS);
  for (const item of value.assets) {
    encodeAssetViewState(encoder, item);
  }
}

function decodeAssetDeliveryView(decoder: CoreStatusPayloadDecoder): AssetDeliveryView {
  const value = {
    platform_supported: decoder.readBoolean(),
    network_cost: decodeAssetNetworkCostView(decoder),
    metered_permitted: decoder.readBoolean(),
    assets: decoder.readList(MAX_ASSETS, () => decodeAssetViewState(decoder)),
  } satisfies AssetDeliveryView;
  validateAssetDeliveryView(value);
  return value;
}

function validateCustomModelSpecView(value: CustomModelSpecView): void {
  void value;
}

function encodeCustomModelSpecView(encoder: CoreStatusPayloadEncoder, value: CustomModelSpecView): void {
  validateCustomModelSpecView(value);
  encoder.putString(value.model_id, MAX_MODEL_ID_BYTES);
  encoder.putString(value.display_name, MAX_MODEL_DISPLAY_NAME_BYTES);
  encoder.putU32(value.context_window);
  encoder.putU32(value.max_output_tokens);
  encoder.putBoolean(value.reasoning);
  encoder.putBoolean(value.tool_calling);
}

function decodeCustomModelSpecView(decoder: CoreStatusPayloadDecoder): CustomModelSpecView {
  const value = {
    model_id: decoder.readString(MAX_MODEL_ID_BYTES),
    display_name: decoder.readString(MAX_MODEL_DISPLAY_NAME_BYTES),
    context_window: decoder.readU32(),
    max_output_tokens: decoder.readU32(),
    reasoning: decoder.readBoolean(),
    tool_calling: decoder.readBoolean(),
  } satisfies CustomModelSpecView;
  validateCustomModelSpecView(value);
  return value;
}

export function encodeCoreStatusPayload(value: CoreStatus): CoreStatusPayloadEncodeResult {
  try {
    const encoder = new CoreStatusPayloadEncoder();
    encoder.putRaw(CORE_STATUS_PAYLOAD_MAGIC);
    encoder.putU32(CORE_STATUS_PAYLOAD_SCHEMA_VERSION);
    encodeCoreStatus(encoder, value);
    return { ok: true, bytes: encoder.finish() };
  } catch (failure) {
    if (failure instanceof CoreStatusCodecFailure) return { ok: false, error: failure.reason };
    throw failure;
  }
}

export function decodeCoreStatusPayload(bytes: Uint8Array): CoreStatusPayloadDecodeResult {
  if (bytes.length > MAX_EVENT_PAYLOAD_BYTES) return { ok: false, error: CoreStatusPayloadCodecError.SizeLimit };
  try {
    const decoder = new CoreStatusPayloadDecoder(bytes);
    const magic = decoder.take(CORE_STATUS_PAYLOAD_MAGIC.length);
    if (!magic.every((byte, index) => byte === CORE_STATUS_PAYLOAD_MAGIC[index])) {
      fail(CoreStatusPayloadCodecError.InvalidMagic);
    }
    if (decoder.readU32() !== CORE_STATUS_PAYLOAD_SCHEMA_VERSION) {
      fail(CoreStatusPayloadCodecError.UnsupportedVersion);
    }
    const value = decodeCoreStatus(decoder);
    if (decoder.offset !== bytes.length) fail(CoreStatusPayloadCodecError.TrailingBytes);
    return { ok: true, value };
  } catch (failure) {
    if (failure instanceof CoreStatusCodecFailure) return { ok: false, error: failure.reason };
    throw failure;
  }
}
