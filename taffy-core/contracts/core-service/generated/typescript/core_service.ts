// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Generated from schema/contract.json. Do not edit.
// Contract core_service 2.113.

export const MAX_SAVED_FLOW_QUERY_RESULTS = 4 as const;
export const MAX_COMMAND_BYTES = 262144 as const;
export const MAX_EFFECT_BYTES = 1048576 as const;
export const MAX_IN_FLIGHT_PER_PROFILE = 64 as const;
export const MAX_QUEUED_BYTES_PER_PROFILE = 4194304 as const;
export const MAX_OPERATION_ID_BYTES = 128 as const;
export const MAX_IDEMPOTENCY_KEY_BYTES = 256 as const;
export const MAX_IDENTIFIER_BYTES = 256 as const;
export const MAX_USER_INPUT_ANSWER_BYTES = 512 as const;
export const MAX_AUTHORITY_SUBJECT_ID_BYTES = 128 as const;
export const MAX_DIRECT_OBSERVATION_NODES = 1500 as const;
export const MAX_DIRECT_OBSERVATION_TEXT_BYTES = 65536 as const;
export const MAX_DIRECT_OBSERVATION_TOTAL_BYTES = 1048576 as const;
export const MAX_DIRECT_OBSERVATION_FRAMES = 1 as const;
export const MAX_DIRECT_OBSERVATION_DEADLINE_MS = 1500 as const;
export const MAX_DIRECT_OBSERVATION_LEASE_MS = 2000 as const;
export const MAX_PAGE_SNAPSHOT_EXPORT_BYTES = 262144 as const;
export const MAX_TASK_ARTIFACT_EXPORT_BYTES = 262144 as const;
export const MAX_ACCOUNT_EMAIL_BYTES = 320 as const;
export const MAX_ACCOUNT_DISPLAY_NAME_BYTES = 128 as const;
export const MAX_ACCOUNT_SCOPES = 3 as const;
export const MAX_ACCOUNT_RESPONSE_BYTES = 65536 as const;
export const MAX_ACCOUNT_ACCESS_TOKEN_BYTES = 16384 as const;
export const MAX_ACCOUNT_REFRESH_TOKEN_BYTES = 16384 as const;
export const MAX_ACCOUNT_ID_TOKEN_BYTES = 16384 as const;
export const MAX_ACCOUNT_LINKED_PROVIDERS = 4 as const;
export const MAX_ENTITLED_MODELS = 64 as const;
export const MAX_ACCOUNT_SESSION_LIFETIME_SECONDS = 31536000 as const;
export const MAX_PENDING_ACCOUNT_FLOWS = 8 as const;
export const GOOGLE_NONCE_ENTROPY_BYTES = 32 as const;
export const GOOGLE_NONCE_HASH_HEX_BYTES = 64 as const;
export const MAX_GOOGLE_RAW_NONCE_BYTES = 43 as const;
export const MAX_GOAL_BYTES = 65536 as const;
export const MAX_TASK_CONSENT_SOURCES = 64 as const;
export const MAX_NORMALIZED_ORIGIN_BYTES = 2048 as const;
export const MAX_LIBRARY_REFRESH_SOURCES = 64 as const;
export const MAX_SOURCE_LOCATOR_BYTES = 4096 as const;
export const MAX_SOURCE_TITLE_BYTES = 1024 as const;
export const MAX_SOURCE_HOST_BYTES = 253 as const;
export const MAX_LIBRARY_REFRESH_PREVIEW_ID_BYTES = 64 as const;
export const MAX_DESTINATION_ADDRESS_BYTES = 4096 as const;
export const MAX_CANONICAL_ACTION_INTENT_BYTES = 16384 as const;
export const MAX_TRANSIENT_SEARCH_QUERY_BYTES = 4096 as const;
export const MAX_NEW_SOURCE_CAP = 64 as const;
export const MAX_WORKSPACES_PER_PROFILE = 32 as const;
export const MAX_WORKSPACE_SNAPSHOT_BYTES = 262144 as const;
export const MAX_WORKSPACE_BOOTSTRAP_BYTES = 4194304 as const;
export const MAX_WORKSPACE_VALUE_BYTES = 16384 as const;
export const MAX_WORKSPACE_DISPLAY_NAME_BYTES = 256 as const;
export const MAX_WORKSPACE_CONFIRMATION_TOKEN_BYTES = 64 as const;
export const MAX_LIBRARY_ENTRIES = 1024 as const;
export const MAX_LIBRARY_SOURCES = 16 as const;
export const MAX_LIBRARY_QUERY_BYTES = 512 as const;
export const MAX_LIBRARY_SEARCH_TERMS = 16 as const;
export const MAX_LIBRARY_SEARCH_RESULTS = 32 as const;
export const MAX_MEMORY_RECORDS = 512 as const;
export const MAX_MEMORY_STATEMENT_BYTES = 2048 as const;
export const MAX_MEMORY_QUERY_BYTES = 512 as const;
export const MAX_MEMORY_SEARCH_TERMS = 16 as const;
export const MAX_MEMORY_SEARCH_RESULTS = 32 as const;
export const MAX_ASSET_PATH_BYTES = 256 as const;
export const MAX_ASSET_TRANSFER_BYTES = 4294967296 as const;
export const MAX_TOOL_ALLOWLIST_ENTRIES = 128 as const;
export const MAX_TASK_BUDGET_ENTRIES = 10 as const;
export const MAX_PENDING_APPROVALS_PER_PROFILE = 64 as const;
export const MAX_PENDING_PERMISSIONS_PER_PROFILE = 64 as const;
export const MAX_PENDING_TASK_POLICY_PER_PROFILE = 64 as const;
export const MAX_TASK_EFFECTS_PER_STATE = 64 as const;
export const MAX_TASK_TAB_RESULTS = 16 as const;
export const MAX_TASK_DOWNLOAD_RESULTS = 16 as const;
export const MAX_TASK_STORE_RESULTS = 32 as const;
export const MAX_TASK_STORE_ROW_FIELD_BYTES = 512 as const;
export const MAX_TASK_STORE_QUERY_BYTES = 512 as const;
export const MAX_TASK_SUPPLIED_VALUES = 8 as const;
export const MAX_TASK_ACTION_PRECONDITIONS = 4 as const;
export const MAX_TASK_OBSERVATION_NODES = 1500 as const;
export const MAX_TASK_OBSERVATION_TEXT_BYTES = 65536 as const;
export const MAX_TASK_OBSERVATION_TOTAL_BYTES = 1048576 as const;
export const MAX_TASK_OBSERVATION_FRAMES = 1 as const;
export const MAX_TASK_OBSERVATION_DEADLINE_MS = 1500 as const;
export const MAX_MEDIA_FACTS = 256 as const;
export const MAX_MEDIA_FACT_TEXT_BYTES = 8192 as const;
export const MAX_MEDIA_FACT_LOCATOR_BYTES = 512 as const;
export const MAX_MEDIA_FACT_TOTAL_BYTES = 131072 as const;
export const MAX_MEDIA_ATTACHMENT_HANDLE_BYTES = 128 as const;
export const MAX_MEDIA_ATTACHMENT_BYTES = 4194304 as const;
export const MAX_MEDIA_DIMENSION_PX = 4096 as const;
export const MAX_MEDIA_PDF_PAGES = 256 as const;
export const MAX_TASK_REVISIONS_PER_PROFILE = 64 as const;
export const MAX_TASK_CONTROLS = 4 as const;
export const MAX_TOOL_JOB_INPUT_BYTES = 16777216 as const;
export const MAX_TOOL_JOB_OUTPUT_BYTES = 16777216 as const;
export const MAX_TOOL_OUTPUT_CHUNK_BYTES = 65536 as const;
export const MAX_TOOL_OUTPUT_CHUNKS = 256 as const;
export const MAX_TOOL_PROGRESS_EVENTS = 256 as const;
export const MAX_TOOL_JOB_ID_BYTES = 128 as const;
export const MAX_TOOL_OPERATION_ID_BYTES = 128 as const;
export const MAX_TOOL_IDEMPOTENCY_KEY_BYTES = 128 as const;
export const MAX_TOOL_ID_BYTES = 128 as const;
export const MAX_TOOL_VERSION_BYTES = 64 as const;
export const MAX_TOOL_CONVERSATION_ID_BYTES = 128 as const;
export const MAX_TOOL_ENTRYPOINT_ID_BYTES = 128 as const;
export const MAX_TOOL_HANDLE_ID_BYTES = 256 as const;
export const MAX_TOOL_PRESET_ID_BYTES = 128 as const;
export const MAX_TOOL_JOB_MEMORY_BYTES = 4294967296 as const;
export const MAX_TOOL_JOB_CPU_MS = 600000 as const;
export const MAX_TOOL_JOB_TEMPORARY_BYTES = 1073741824 as const;
export const MAX_TOOL_JOB_WALL_TIME_MS = 600000 as const;
export const MAX_TOOL_INLINE_PAYLOAD_BYTES = 262144 as const;
export const MAX_TOOL_STREAM_OUTPUT_CHUNKS = 65536 as const;
export const MAX_TOOL_STREAM_CHUNK_BYTES = 4096 as const;
export const MAX_TOOL_MODEL_ID_BYTES = 128 as const;
export const MAX_TOOL_MODEL_REVISION_BYTES = 64 as const;
export const MAX_TOOL_MODEL_ARTIFACT_BYTES = 2147483648 as const;
export const MAX_REGISTERED_MODEL_ARTIFACTS = 64 as const;
export const MAX_TOOL_EMBEDDING_DIMENSIONS = 4096 as const;
export const MAX_TOOL_EMBEDDING_VALUE_BYTES = 16384 as const;
export const MAX_PROVIDER_ID_BYTES = 64 as const;
export const MAX_PROVIDER_DISPLAY_NAME_BYTES = 128 as const;
export const MAX_PROVIDER_ENDPOINT_BYTES = 512 as const;
export const MAX_CUSTOM_PROVIDERS = 32 as const;
export const MAX_MODEL_ID_BYTES = 128 as const;
export const MAX_AVAILABLE_MODEL_IDS = 64 as const;
export const MAX_MODEL_STATIC_HEADERS = 8 as const;
export const MAX_MODEL_HEADER_NAME_BYTES = 64 as const;
export const MAX_MODEL_HEADER_VALUE_BYTES = 1024 as const;
export const MAX_MODEL_STREAM_CHUNK_BYTES = 16384 as const;
export const MAX_TASK_ANSWER_DELTA_BYTES = 16384 as const;
export const MAX_TASK_ANSWER_EVENTS_PER_BATCH = 64 as const;
export const MAX_SKILLS_PER_PROFILE = 64 as const;
export const MAX_SKILL_VERSIONS_PER_SKILL = 16 as const;
export const MAX_SKILL_ID_BYTES = 128 as const;
export const MAX_SKILL_DEFINITION_BYTES = 65536 as const;
export const MAX_SKILL_STEPS = 32 as const;
export const MAX_SKILL_MATCH_CLAUSES = 8 as const;
export const MAX_SKILL_ARGUMENTS_PER_STEP = 16 as const;
export const MAX_SKILL_RECALL_ENTRIES = 32 as const;
export const MAX_CUSTOM_MODEL_ENTRIES = 32 as const;
export const MAX_MODEL_DISPLAY_NAME_BYTES = 128 as const;
export const MAX_COMPOSER_PREFIX_BYTES = 4096 as const;
export const MAX_COMPOSER_SUFFIX_BYTES = 1024 as const;
export const MAX_COMPOSER_COMPLETION_BYTES = 512 as const;
export const MAX_PROVIDER_LISTING_BYTES = 262144 as const;
export const MAX_ASSISTANT_ABILITIES = 16 as const;
export const MAX_PERSONALITY_SCALE = 2 as const;
export const MAX_SAVED_SIGN_INS = 256 as const;
export const MAX_SAVED_DETAILS = 64 as const;
export const MAX_SAVED_SIGN_IN_SITE_BYTES = 253 as const;
export const MAX_SAVED_SIGN_IN_USERNAME_BYTES = 320 as const;
export const MAX_SAVED_DETAIL_NAME_BYTES = 256 as const;
export const MAX_SAVED_DETAIL_EMAIL_BYTES = 320 as const;
export const MAX_SAVED_DETAIL_PHONE_BYTES = 128 as const;
export const MAX_SAVED_DETAIL_ADDRESS_BYTES = 2048 as const;
export const MAX_SAVED_DETAIL_POSTCODE_BYTES = 64 as const;
export const MAX_SAVED_DETAIL_COUNTRY_BYTES = 128 as const;
export const MAX_BACKUP_RECORDS = 100000 as const;
export const MAX_BACKUP_RECORD_BYTES = 67108864 as const;
export const MAX_BACKUP_PLAINTEXT_BYTES = 8589934592 as const;
export const MAX_BACKUP_MANIFEST_BYTES = 67108864 as const;
export const MAX_BACKUP_ID_BYTES = 160 as const;
export const MAX_BACKUP_TIMESTAMP_BYTES = 40 as const;
export const MAX_BACKUP_SELECTION_KINDS = 8 as const;
export const MAX_BACKUP_RESTORE_RECOVERY_RECORDS = 128 as const;
export const MAX_BACKUP_SEALED_CHUNKS = 8193 as const;
export const BACKUP_PAYLOAD_CHUNK_BYTES = 1048576 as const;

export enum CoreServiceCommandKind {
  StartTask = 0,
  CancelTask = 1,
  UserDecision = 2,
  AuthCallback = 3,
  PermissionResult = 4,
  StartAuth = 5,
  RequestEmailLink = 6,
  SignOut = 7,
  AuthCredentialResult = 8,
  CorrectWorkspaceFact = 9,
  ExcludeWorkspaceSource = 10,
  RequestWorkspaceExport = 11,
  SetAssetDeliveryPolicy = 12,
  RequestAsset = 13,
  RemoveAsset = 14,
  SaveProviderCredential = 15,
  ForgetProviderCredential = 16,
  StartProviderAuth = 17,
  ProviderAuthCallback = 18,
  SaveCustomProvider = 19,
  RemoveCustomProvider = 20,
  CompleteHandover = 21,
  ExpireHandover = 22,
  SupplyUserInput = 23,
  SetProviderCredentialState = 24,
  ProbeProviderCredential = 25,
  SupplyFieldValues = 26,
  SetProviderModelPreference = 27,
  ProbeCustomEndpoint = 28,
  RequestComposerCompletion = 29,
  CancelComposerCompletion = 30,
  PauseTask = 31,
  ResumeTask = 32,
  TakeOver = 33,
  SetAssistantConfiguration = 34,
  SaveWorkspace = 35,
  RenameWorkspace = 36,
  DeleteWorkspace = 37,
  DiscardWorkspace = 38,
  SearchLibrary = 39,
  SaveLibraryFact = 40,
  RemoveLibraryEntry = 41,
  RequestLibraryExport = 42,
  SearchMemory = 43,
  UpsertMemory = 44,
  DeleteMemory = 45,
  AcceptTaskArtifact = 46,
  ExportTaskArtifact = 47,
  ReplaceSavedDataSnapshot = 48,
  MutateSkill = 49,
  CancelProviderAuth = 50,
  FollowUp = 51,
}

export enum SavedDataAvailability {
  Loading = 0,
  Ready = 1,
  Unavailable = 2,
}

export enum TaskControlKind {
  Pause = 0,
  Resume = 1,
  TakeOver = 2,
  Stop = 3,
}

export enum EffectKind {
  StorageCommit = 0,
  PageObservation = 1,
  ModelRequest = 2,
  NetworkRequest = 3,
  BrowserAction = 4,
  ToolJob = 5,
  SecureStore = 6,
  OpenAuthSurface = 7,
  RequestPermission = 8,
  DeliverAsset = 9,
  FetchCatalog = 10,
  FetchProviderListing = 11,
  DeliverComposerCompletion = 12,
  ProbeCustomEndpoint = 13,
}

export enum CatalogNetworkOperation {
  FetchPublishedCatalog = 0,
}

export enum CatalogFetchDisposition {
  Success = 0,
  NotModified = 1,
  Unavailable = 2,
  Oversized = 3,
  MalformedTransport = 4,
}

export enum RetryClass {
  Idempotent = 0,
  Consequential = 1,
  Never = 2,
}

export enum EffectStatus {
  Completed = 0,
  Denied = 1,
  Cancelled = 2,
  DeadlineExceeded = 3,
  ResourceLimit = 4,
  Unavailable = 5,
  OutcomeUnknown = 6,
  InvalidResult = 7,
}

export enum DisclosureClass {
  ContentFree = 0,
  AccountMetadata = 1,
  UserSelectedContent = 2,
  PageContent = 3,
}

export enum InitializationStatus {
  Ready = 0,
  InvalidBootstrap = 1,
  IncompatibleVersion = 2,
  ResourceLimit = 3,
}

export enum AdmissionStatus {
  Accepted = 0,
  StaleGeneration = 1,
  StaleRevision = 2,
  DeadlineExceeded = 3,
  Backpressure = 4,
  InvalidCommand = 5,
  CoreUnavailable = 6,
  Duplicate = 7,
}

export enum TaskKind {
  Research = 0,
  Errand = 1,
}

export enum TaskTemplateId {
  CompareProducts = 0,
  SummarizeEvidence = 1,
  BuildSourceTable = 2,
  WebErrand = 3,
}

export enum BuiltinSkillId {
  GeneralWebResearch = 0,
  DeepResearch = 1,
  ProductComparison = 2,
  MultiTabComparison = 3,
  WebsiteSummarizer = 4,
  PdfAnalysis = 5,
  DataExtraction = 6,
  FormAssistant = 7,
  Shopping = 8,
  DownloadOrganizer = 9,
  TravelResearch = 10,
  VideoTranscriptAnalyzer = 11,
  ImageUnderstanding = 12,
  LibraryBuilder = 13,
  SpreadsheetBuilder = 14,
  DocumentGenerator = 15,
}

export enum TaskProviderRoute {
  NotConfigured = 0,
  DirectUserKey = 1,
  ManagedService = 2,
  NoModelRequired = 3,
}

export enum TaskControlMode {
  User = 0,
  Shared = 1,
  Assistant = 2,
}

export enum TaskMilestone {
  M0 = 0,
  M1 = 1,
  M2 = 2,
  M3 = 3,
  M4 = 4,
  M5 = 5,
  M6 = 6,
  M7 = 7,
  M8 = 8,
}

export enum TaskBudgetKind {
  MaxSources = 0,
  MaxWorkingTabs = 1,
  MaxWallTimeMs = 2,
  MaxModelRequests = 3,
  MaxInputUnits = 4,
  MaxOutputUnits = 5,
  MaxCostUnits = 6,
  MaxNavigationDepth = 7,
  MaxRetriesPerStep = 8,
  MaxArtifactBytes = 9,
}

export enum UserDecisionKind {
  Accept = 0,
  Deny = 1,
  Dismiss = 2,
}

export enum CancelReason {
  User = 0,
  ProfileShutdown = 1,
  Deadline = 2,
}

export enum TaskSettlementKind {
  Pause = 0,
  Cancel = 1,
}

export enum TerminalTaskKind {
  Completed = 0,
  Partial = 1,
  Failed = 2,
  Cancelled = 3,
}

export enum TaskReducerEffectKind {
  RevokeAuthority = 0,
  AskPolicy = 1,
  RequestApproval = 2,
  RequestPermission = 3,
  DispatchAction = 4,
  AwaitInFlightWork = 5,
  ReconcileAction = 6,
  ReleaseTaskTabs = 7,
  GenerateArtifact = 8,
  ExportArtifact = 9,
  CallModel = 10,
  AwaitHandover = 11,
  RunToolJob = 12,
  RequestFieldValues = 13,
  PrepareDiscoveryTab = 14,
  RunLibraryTool = 15,
  RunMemoryTool = 16,
}

export enum TaskRevocationReason {
  UserTookOver = 0,
  DirectUserInput = 1,
  TaskCancelled = 2,
  PolicyRevoked = 3,
  TabClosed = 4,
}

export enum TaskRecoveryRule {
  RetryWithinEpochAndBudget = 0,
  RetryAfterStateCheck = 1,
  ReconcileFirst = 2,
  NeverAutomatically = 3,
}

export enum TaskArtifactKind {
  Markdown = 0,
  Csv = 1,
  Xlsx = 2,
  Pdf = 3,
  Docx = 4,
  Pptx = 5,
  WaveAudio = 6,
  FrameArchive = 7,
}

export enum TaskActionPrecondition {
  DocumentUnchanged = 0,
  GraphRevisionAtLeast = 1,
  NodePresent = 2,
  DestinationUnchanged = 3,
}

export enum TaskActionPostcondition {
  ObservationCaptured = 0,
  DocumentNavigated = 1,
  NodeStateChanged = 2,
  DownloadStarted = 3,
  PlatformAcknowledged = 4,
  TaskTabsListed = 5,
  TaskTabActive = 6,
  TaskTabAbsent = 7,
  DownloadCancelled = 8,
  PageReloaded = 9,
  LoadingStopped = 10,
  StoreRowsListed = 11,
}

export enum TaskTabPostcondition {
  Listed = 0,
  Active = 1,
  Absent = 2,
}

export enum TaskDownloadPostcondition {
  Started = 0,
  Listed = 1,
  Cancelled = 2,
}

export enum TaskDownloadState {
  Created = 0,
  InProgress = 1,
  Paused = 2,
  Complete = 3,
  Interrupted = 4,
  Cancelled = 5,
}

export enum TaskDownloadMediaType {
  Unknown = 0,
  Application = 1,
  Audio = 2,
  Font = 3,
  Image = 4,
  Message = 5,
  Model = 6,
  Multipart = 7,
  Text = 8,
  Video = 9,
}

export enum TaskDownloadDirectoryClass {
  Undecided = 0,
  PersonChosen = 1,
  DefaultDownloads = 2,
  ApplicationPrivate = 3,
}

export enum TaskEffectCompletionStatus {
  Succeeded = 0,
  Refused = 1,
  Unavailable = 2,
  Cancelled = 3,
  OutcomeUnknown = 4,
  ValueReferenceUnknown = 5,
}

export enum AuthCallbackStatus {
  AuthorizationCode = 0,
  Denied = 1,
  ProviderError = 2,
  DeadlineExceeded = 3,
  PlatformUnavailable = 4,
}

export enum AccountAuthMethod {
  Google = 0,
  EmailLink = 1,
  Github = 2,
  Facebook = 3,
}

export enum AccountTokenValidationStatus {
  Validated = 0,
  InvalidResponse = 1,
  StaleGeneration = 2,
  DeadlineExceeded = 3,
  ResourceLimit = 4,
}

export enum AccountScope {
  OpenId = 0,
  Email = 1,
  Profile = 2,
}

export enum AuthCredentialStatus {
  Success = 0,
  Cancelled = 1,
  NoCredential = 2,
  Unavailable = 3,
}

export enum PlatformPermission {
  Notifications = 0,
  Microphone = 1,
  Camera = 2,
  Location = 3,
}

export enum PermissionDecision {
  Granted = 0,
  Denied = 1,
  Dismissed = 2,
  Unavailable = 3,
}

export enum StorageOperation {
  AppendTaskCommit = 0,
  QueryWorkspace = 1,
  DeleteSource = 2,
  UpsertWorkspace = 3,
  InstallSkill = 4,
  SetSkillStatus = 5,
  RecordSkillRun = 6,
  ForgetSkill = 7,
  SetAssistantConfiguration = 8,
  DeleteWorkspace = 9,
  UpsertLibraryEntry = 10,
  RemoveLibraryEntry = 11,
  UpsertMemory = 12,
  DeleteMemory = 13,
}

export enum LibraryFactKind {
  FromPage = 0,
  Summarized = 1,
  TaffyInference = 2,
  UserEntered = 3,
}

export enum MemorySourceKind {
  UserEntered = 0,
  AcceptedTaskSuggestion = 1,
}

export enum MemoryScopeKind {
  AllTasks = 0,
  Workspace = 1,
}

export enum MemorySensitivity {
  Standard = 0,
  Sensitive = 1,
}

export enum AssistantAbility {
  PagesLookup = 0,
  PagesCompare = 1,
  PagesSummarize = 2,
  PagesTable = 3,
  Products = 4,
  Offers = 5,
  Form = 6,
  Downloads = 7,
  Pdf = 8,
  Sheet = 9,
  Document = 10,
  Depth = 11,
  Trip = 12,
  Pictures = 13,
  Video = 14,
  Keep = 15,
}

export enum PersonalityPreset {
  CarefulResearcher = 0,
  QuickShopper = 1,
  TripPlanner = 2,
}

export enum SkillProvenance {
  Authored = 0,
  RecordedFromTask = 1,
  InstalledFromPack = 2,
}

export enum SkillMutationKind {
  Teach = 0,
  Update = 1,
  SetEnabled = 2,
  Remove = 3,
}

export enum SkillClauseKind {
  RolePresent = 0,
  PhraseAt = 1,
  StateAt = 2,
}

export enum SkillArgumentKind {
  FromEarlierStep = 0,
  FromPerson = 1,
  Choice = 2,
  Count = 3,
  Flag = 4,
  PublicAddress = 5,
  SemanticTarget = 6,
}

export enum SkillStatus {
  Draft = 0,
  Active = 1,
  Superseded = 2,
  Retired = 3,
  Disabled = 4,
}

export enum SkillRunOutcome {
  Completed = 0,
  Refused = 1,
  Abandoned = 2,
  Unavailable = 3,
}

export enum WorkspaceExportFormat {
  Markdown = 0,
  Csv = 1,
}

export enum PageSnapshotExportFormat {
  Markdown = 0,
  CanonicalJson = 1,
}

export enum PageSnapshotExportStatus {
  Exported = 0,
  StalePage = 1,
  PrivateProfile = 2,
  Incomplete = 3,
  Oversize = 4,
  Malformed = 5,
  Cancelled = 6,
  ReplayConflict = 7,
  Unavailable = 8,
}

export enum SiteSkillMatchStatus {
  Available = 0,
  StalePage = 1,
  PrivateProfile = 2,
  Incomplete = 3,
  Malformed = 4,
  Unavailable = 5,
}

export enum ObservationScope {
  CurrentDocument = 0,
  SelectedSources = 1,
}

export enum BipObservationStatus {
  Ok = 0,
  Unsupported = 1,
  Incomplete = 2,
  Conflicted = 3,
  StalePageEpoch = 4,
  DocumentInactive = 5,
  BudgetExceeded = 6,
  DeadlineExceeded = 7,
  Cancelled = 8,
  ResourcePressure = 9,
  InternalError = 10,
}

export enum BipGraphEncoding {
  None = 0,
  BipContract = 1,
}

export enum MediaObservationKind {
  Image = 0,
  Video = 1,
  Pdf = 2,
  PageScreenshot = 3,
}

export enum MediaFactKind {
  Description = 0,
  OcrText = 1,
  Transcript = 2,
  PdfText = 3,
  PdfTableRow = 4,
  Metadata = 5,
}

export enum MediaEvidenceKind {
  Dom = 0,
  Accessibility = 1,
  CaptionTrack = 2,
  PdfTextLayer = 3,
  VisualInference = 4,
  TableHeuristic = 5,
}

export enum BipSensitivity {
  NotSensitive = 0,
  Personal = 1,
  Account = 2,
  Payment = 3,
  Identity = 4,
  Health = 5,
  Financial = 6,
  Legal = 7,
  PrivateCommunication = 8,
  Administration = 9,
  Credential = 10,
  UnknownSensitive = 11,
  OneTimeCode = 12,
  ChallengeResponse = 13,
}

export enum AccountNetworkOperation {
  ExchangeAuthorizationCode = 0,
  ExchangeNativeCredential = 1,
  RequestEmailLink = 2,
  RefreshSession = 3,
  RevokeSession = 4,
  FetchEntitlement = 5,
}

export enum EntitlementFetchReason {
  Bootstrap = 0,
  SignIn = 1,
  Cadence = 2,
  QuotaRefused = 3,
}

export enum BrowserActionOperation {
  Dispatch = 0,
  Reconcile = 1,
  ReleaseTaskTabs = 2,
  ExportArtifact = 3,
}

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

export enum SecureStoreOperation {
  GenerateEntropy = 0,
  WriteTransient = 1,
  DeleteHandle = 2,
}

export enum SecretMaterialPurpose {
  PkceVerifier = 0,
  GoogleRawNonce = 1,
}

export enum AuthSurfaceOperation {
  OpenOauth = 0,
  RequestNativeCredential = 1,
}

export enum BrowserActionOutcome {
  Completed = 0,
  Refused = 1,
  OutcomeUnknown = 2,
}

export enum CapabilityRegistrationStatus {
  Registered = 0,
  Duplicate = 1,
  StaleGeneration = 2,
  LeaseMissing = 3,
  InvalidGrant = 4,
}

export enum PendingApprovalRegistrationStatus {
  Registered = 0,
  StaleGeneration = 1,
  StaleSequence = 2,
  InvalidBinding = 3,
  TooManyBindings = 4,
}

export enum PolicyActionClass {
  ObservePage = 0,
  ScrollIntoView = 1,
  OpenLink = 2,
  CreateTaskTab = 3,
  SyntheticClick = 4,
  MoveFocus = 5,
  FillField = 6,
  SelectOption = 7,
  ToggleControl = 8,
  SubmitForm = 9,
  StartDownload = 10,
  UploadFile = 11,
  SendMessage = 12,
  Purchase = 13,
  ExtractCredential = 14,
  BypassAccessControl = 15,
  ExecuteToolJob = 16,
  LibraryRead = 17,
  LibraryWrite = 18,
  MemoryRead = 19,
  MemoryWrite = 20,
  ControlTab = 21,
  ProfileStoreRead = 22,
}

export enum TaskActionOperationKind {
  Navigate = 0,
  Search = 1,
  HistoryBack = 2,
  HistoryForward = 3,
  TabsOpen = 4,
  TabsList = 5,
  TabsActivate = 6,
  TabsClose = 7,
  DomQuery = 8,
  DomRead = 9,
  DomClick = 10,
  DomScroll = 11,
  FormInspect = 12,
  FormFill = 13,
  FormSubmit = 14,
  DownloadStart = 15,
  DownloadList = 16,
  SelectionRead = 17,
  ImageDescribe = 18,
  ImageReadText = 19,
  VideoInspect = 20,
  PdfInspect = 21,
  ToolJob = 22,
  LinkOpen = 23,
  FormSelect = 24,
  FormToggle = 25,
  LibrarySearch = 26,
  LibrarySave = 27,
  LibraryRemove = 28,
  MemorySearch = 29,
  MemorySave = 30,
  MemoryUpdate = 31,
  MemoryDelete = 32,
  DomFocus = 33,
  PageScreenshotInspect = 34,
  DownloadCancel = 35,
  Reload = 36,
  StopLoading = 37,
  HistorySearch = 38,
  HistoryRecent = 39,
  BookmarksSearch = 40,
  BookmarksList = 41,
  OpenTabsList = 42,
}

export enum TaskActionInputKind {
  None = 0,
  SuppliedValue = 1,
  ToggleState = 2,
}

export enum PolicyPrincipalKind {
  Assistant = 0,
  Skill = 1,
}

export enum PolicyEvaluationContext {
  Task = 0,
  DirectUserObservation = 1,
  TaskDiscovery = 2,
}

export enum AuthoritySubjectKind {
  Task = 0,
  DirectUserIntent = 1,
}

export enum PolicyOriginKind {
  Tuple = 0,
  Opaque = 1,
}

export enum PolicyRiskClass {
  LocalRead = 0,
  ReversibleDisclosure = 1,
  SensitiveDisclosure = 2,
  ExcludedCommitment = 3,
  ProhibitedAbuse = 4,
}

export enum PolicyEvaluationStatus {
  Granted = 0,
  ApprovalRequired = 1,
  Denied = 2,
  InvalidRequest = 3,
  CoreUnavailable = 4,
}

export enum TaskActionResultCode {
  Verified = 0,
  DeniedByPolicy = 1,
  ApprovalRequired = 2,
  ApprovalDenied = 3,
  ActorLeaseMissing = 4,
  CapabilityExpired = 5,
  TabGone = 6,
  FrameGone = 7,
  DocumentInactive = 8,
  StalePageEpoch = 9,
  StaleGraph = 10,
  NodeGone = 11,
  OriginChanged = 12,
  RoleOrActionChanged = 13,
  NotVisible = 14,
  Occluded = 15,
  NotEnabled = 16,
  NotEditable = 17,
  SensitiveField = 18,
  DestinationChanged = 19,
  Unsupported = 20,
  BudgetExceeded = 21,
  DispatchFailed = 22,
  NavigationStarted = 23,
  PostconditionTimeout = 24,
  PostconditionFailed = 25,
  CancelledByUser = 26,
  CancelledByNavigation = 27,
  RendererCrashed = 28,
  OutcomeUnknown = 29,
  InternalError = 30,
  EgressNotAuthorized = 31,
  DestinationClassRestricted = 32,
  UntrustedContentOrigin = 33,
  PreparedEffectChanged = 34,
  CommitWithoutPrepare = 35,
  GraphMovedDuringPreflight = 36,
  ValueReferenceUnknown = 37,
}

export enum AssetDeliveryOperation {
  FetchAsset = 0,
  RemoveAsset = 1,
}

export enum AssetPlatform {
  AndroidArm64 = 0,
  AndroidX64 = 1,
  MacosArm64 = 2,
  MacosX64 = 3,
  WindowsX64 = 4,
  WindowsArm64 = 5,
  Unsupported = 6,
}

export enum AssetKind {
  PythonStdlib = 0,
  PythonPackages = 1,
  ModelWeights = 2,
  ModelTokenizer = 3,
  FilterList = 4,
  CountryFlags = 5,
  StartScenes = 6,
}

export enum AssetContainer {
  Raw = 0,
  Zip = 1,
}

export enum AssetPresence {
  Absent = 0,
  Partial = 1,
  Complete = 2,
  Installed = 3,
}

export enum AssetRefusalReason {
  UnknownAsset = 0,
  NoVariantForPlatform = 1,
  NotPublishedYet = 2,
  CatalogRowIncomplete = 3,
  VariantTooLarge = 4,
  AttemptsExhausted = 5,
  IntegrityFailed = 6,
  NetworkNotPermitted = 7,
  DeclinedByPerson = 8,
}

export enum AssetTransferOutcome {
  Interrupted = 0,
  OriginRefusedTemporary = 1,
  OriginRefusedPermanent = 2,
  IntegritySound = 3,
  IntegrityWrongLength = 4,
  IntegrityWrongDigest = 5,
  Installed = 6,
  Declined = 7,
}

export enum AssetNetworkCost {
  Offline = 0,
  Metered = 1,
  Unmetered = 2,
}

export enum ProviderAuthMethod {
  ApiKey = 0,
  Oauth = 1,
}

export enum ThinkingLevel {
  Off = 0,
  Minimal = 1,
  Low = 2,
  Medium = 3,
  High = 4,
  Xhigh = 5,
  Max = 6,
}

export enum ProviderCredentialState {
  Usable = 0,
  NeedsSignIn = 1,
  RefreshFailed = 2,
}

export enum ProviderWireApi {
  AnthropicMessages = 0,
  OpenAiResponses = 1,
  OpenAiCompletions = 2,
  GoogleGenerativeLanguage = 3,
  Managed = 4,
  OpenAiCodexResponses = 5,
  GoogleCloudCodeAssist = 6,
}

export enum ModelErrorClass {
  Auth = 0,
  Quota = 1,
  Overloaded = 2,
  InvalidRequest = 3,
  Network = 4,
  Overflow = 5,
  Canceled = 6,
  Unknown = 7,
}

export enum ModelEndpointKind {
  CatalogOrigin = 0,
  UserBaseUrl = 1,
}

export enum ServerKind {
  OpenaiCompatible = 0,
  Ollama = 1,
  LmStudio = 2,
  Vllm = 3,
  LlamaCpp = 4,
}

export enum ModelStreamChunkStatus {
  Accepted = 0,
  Invalid = 1,
  Stale = 2,
  Unavailable = 3,
}

export enum BackupRecordKind {
  AssistantConfiguration = 0,
  SavedWorkspace = 1,
  LibraryEntry = 2,
  MemoryRecord = 3,
  UserAuthoredSkill = 4,
  LearnedProcedure = 5,
  Bookmark = 6,
  BrowserPreference = 7,
}

export enum BackupRecordState {
  Active = 0,
  Tombstone = 1,
}

export enum BackupPlanningStatus {
  Succeeded = 0,
  InvalidRequest = 1,
  InvalidManifest = 2,
  SnapshotMismatch = 3,
  StagedPayloadMismatch = 4,
  RestoreConflict = 5,
  DigestUnavailable = 6,
  Unavailable = 7,
}

export enum BackupRestoreTargetKind {
  NewRegularProfile = 0,
  ExistingRegularProfile = 1,
}

export enum BackupRestoreAction {
  StageCreate = 0,
  StageDeletion = 1,
  AlreadyPresent = 2,
  KeepNewerCurrent = 3,
  BlockedByDeletion = 4,
  NeedsExplicitConflictChoice = 5,
}

export enum BackupRestoreProtocolStatus {
  Succeeded = 0,
  InvalidOperation = 1,
  Unavailable = 2,
  BindingMismatch = 3,
  WrongPhase = 4,
  ConfirmationMismatch = 5,
  SnapshotMismatch = 6,
  ReconcileRequired = 7,
}

export enum BackupRestoreCommitOutcome {
  Committed = 0,
  DefinitelyNotCommitted = 1,
  OutcomeUnknown = 2,
}

export enum BackupRestoreResolutionChoice {
  AcceptCandidate = 0,
  DiscardCandidate = 1,
}

export enum BackupRestoreResolutionOutcome {
  Completed = 0,
  DefinitelyNotCompleted = 1,
  OutcomeUnknown = 2,
}

export enum BackupRestoreRecoveryFactKind {
  IntentRecorded = 0,
  OutcomeObserved = 1,
}

export enum BackupRestorePhysicalIntent {
  CommitCandidate = 0,
  AcceptCandidate = 1,
  DiscardCandidate = 2,
}

export enum BackupRestoreObservedOutcome {
  Completed = 0,
  DefinitelyNotCompleted = 1,
  OutcomeUnknown = 2,
}

export enum BackupRestoreRecoveryClassificationKind {
  ReconcileRequired = 0,
  RollbackAvailable = 1,
  CleanupRequired = 2,
  Published = 3,
  VerifiedDeleted = 4,
}

export enum BackupRestoreRecoveryError {
  MissingCommitIntent = 0,
  TooManyRecords = 1,
  UnsupportedVersion = 2,
  InvalidSequence = 3,
  InvalidBinding = 4,
  BindingChanged = 5,
  InvalidIntentId = 6,
  IntentIdReused = 7,
  UnexpectedIntent = 8,
  UnresolvedIntent = 9,
  OutcomeWithoutIntent = 10,
  OutcomeIntentMismatch = 11,
  DuplicateUnknownOutcome = 12,
  TerminalHistoryExtended = 13,
}

export enum BackupRestoreRecoveryInspectionStatus {
  Succeeded = 0,
  InvalidOperation = 1,
  InvalidRecord = 2,
  InvalidHistory = 3,
  Unavailable = 4,
}

export enum SavedFlowQueryStatus {
  Available = 0,
  PrivateProfile = 1,
  Unavailable = 2,
  InvalidRequest = 3,
  StaleRequest = 4,
}

export enum SavedFlowQueryKind {
  ExactGoal = 0,
  Review = 1,
  PublicStart = 2,
}

export enum FieldValueAskOutcome {
  Answered = 0,
  Dismissed = 1,
  NotAField = 2,
  ChallengeOffScreen = 3,
  CannotBeShown = 4,
  PageMoved = 5,
  NoSurface = 6,
}

export interface OperationEnvelope {
  readonly operation_id: string;
  readonly service_generation: bigint;
  readonly task_revision: bigint;
  readonly deadline_monotonic_ms: bigint;
  readonly idempotency_key: string;
}

export interface PolicyPrincipal {
  readonly kind: PolicyPrincipalKind;
  readonly skill_version_id: string | null;
}

export interface AuthoritySubject {
  readonly kind: AuthoritySubjectKind;
  readonly authority_subject_id: string;
}

export interface PolicyOrigin {
  readonly kind: PolicyOriginKind;
  readonly serialization: string | null;
  readonly opaque_id: string | null;
}

export interface PolicyCapabilityScope {
  readonly profile_id: string;
  readonly tab_id: string;
  readonly frame_id: string;
  readonly page_epoch: string;
  readonly origin: PolicyOrigin;
  readonly node_id: string | null;
  readonly destination_scope: PolicyOrigin | null;
  readonly required_graph_revision: bigint;
  readonly allowed_redirects: ReadonlyArray<PolicyOrigin>;
  readonly destination_address: string | null;
}

export interface PolicyApprovalFact {
  readonly receipt_reference: string;
  readonly proposal_digest: string;
  readonly service_generation: bigint;
  readonly expires_at_monotonic_ms: bigint;
  readonly expires_at_utc_ms: bigint;
  readonly browser_session_id: string;
}

export interface ActorLeaseFact {
  readonly lease_id: string;
  readonly service_generation: bigint;
  readonly task_id: string;
  readonly profile_id: string;
  readonly tab_id: string;
  readonly control_mode: TaskControlMode;
  readonly expires_at_monotonic_ms: bigint;
  readonly authority_subject: AuthoritySubject;
}

export interface TaskDiscoveryAuthorityFact {
  readonly discovery_tab_id: string;
  readonly browser_session_id: string;
  readonly remaining_new_source_cap: number;
}

export interface PolicyEvaluationRequest {
  readonly operation: OperationEnvelope;
  readonly now_monotonic_ms: bigint;
  readonly action_id: string;
  readonly task_id: string;
  readonly principal: PolicyPrincipal;
  readonly action_class: PolicyActionClass;
  readonly proposal_digest: string;
  readonly scope: PolicyCapabilityScope;
  readonly data_classes: ReadonlyArray<BipSensitivity>;
  readonly context_risk: PolicyRiskClass;
  readonly expires_at_monotonic_ms: bigint;
  readonly actor_lease: ActorLeaseFact;
  readonly approval: PolicyApprovalFact | null;
  readonly context: PolicyEvaluationContext;
  readonly authority_subject: AuthoritySubject;
  readonly policy_version: number;
  readonly now_utc_ms: bigint;
  readonly operation_kind: TaskActionOperationKind;
  readonly canonical_intent_digest: Uint8Array;
  readonly discovery: TaskDiscoveryAuthorityFact | null;
}

export interface PolicyEvaluationResult {
  readonly operation_id: string;
  readonly status: PolicyEvaluationStatus;
  readonly minted_grant: MintedCapabilityGrant | null;
  readonly direct_observation_effect: EffectEnvelope | null;
  readonly denial: PolicyDenial | null;
}

export function isValidPolicyEvaluationResultPresence(value: PolicyEvaluationResult): boolean {
  return ((value.status === PolicyEvaluationStatus.Granted) ? value.minted_grant !== null : value.minted_grant === null) &&
      ((value.status === PolicyEvaluationStatus.Denied) ? value.denial !== null : value.denial === null);
}

export interface PolicyDenial {
  readonly code: TaskActionResultCode;
}

export interface MintedCapabilityGrant {
  readonly capability_id: string;
  readonly service_generation: bigint;
  readonly policy_version: number;
  readonly actor_lease_id: string;
  readonly task_id: string;
  readonly action_id: string;
  readonly action_class: PolicyActionClass;
  readonly principal: PolicyPrincipal;
  readonly proposal_digest: string;
  readonly idempotency_key: string;
  readonly scope: PolicyCapabilityScope;
  readonly data_classes: ReadonlyArray<BipSensitivity>;
  readonly effective_risk: PolicyRiskClass;
  readonly approval: PolicyApprovalFact | null;
  readonly issued_at_monotonic_ms: bigint;
  readonly expires_at_monotonic_ms: bigint;
  readonly authority_subject: AuthoritySubject;
  readonly operation_kind: TaskActionOperationKind;
  readonly canonical_intent_digest: Uint8Array;
  readonly discovery: TaskDiscoveryAuthorityFact | null;
}

export interface CommittedTaskBatch {
  readonly effect_id: string;
  readonly expected_revision: bigint;
  readonly resulting_revision: bigint;
  readonly transaction_batch: Uint8Array;
}

export interface TaskRestoreRecord {
  readonly task_id: string;
  readonly batches: ReadonlyArray<CommittedTaskBatch>;
  readonly task_id_seed: Uint8Array;
}

export interface AccountSessionHandle {
  readonly session_handle: string;
  readonly account_subject: string;
  readonly expires_at_monotonic_ms: bigint;
  readonly rotation: bigint;
  readonly auth_method: AccountAuthMethod;
  readonly email: string | null;
  readonly display_name: string | null;
}

export interface WorkspaceRestoreRecord {
  readonly workspace_id: string;
  readonly revision: bigint;
  readonly snapshot: Uint8Array;
}

export interface LibrarySourceRecord {
  readonly source_id: string;
  readonly title: string;
  readonly host: string;
  readonly observed_at_epoch_ms: bigint;
}

export interface LibraryEntryRecord {
  readonly entry_id: string;
  readonly revision: bigint;
  readonly collection_id: string;
  readonly collection_name: string;
  readonly source_workspace_id: string;
  readonly source_workspace_revision: bigint;
  readonly source_fact_id: string;
  readonly field: string;
  readonly original_value: string;
  readonly correction: string | null;
  readonly kind: LibraryFactKind;
  readonly sources: ReadonlyArray<LibrarySourceRecord>;
  readonly captured_at_epoch_ms: bigint;
  readonly last_checked_epoch_ms: bigint;
  readonly has_conflict: boolean;
}

export interface MemoryWorkspaceRecord {
  readonly workspace_id: string;
  readonly display_name: string;
}

export interface MemoryRecord {
  readonly memory_id: string;
  readonly revision: bigint;
  readonly statement: string;
  readonly source_kind: MemorySourceKind;
  readonly source_task_id: string | null;
  readonly source_workspace: MemoryWorkspaceRecord | null;
  readonly scope_kind: MemoryScopeKind;
  readonly scope_workspace: MemoryWorkspaceRecord | null;
  readonly sensitivity: MemorySensitivity;
  readonly created_at_epoch_ms: bigint;
  readonly updated_at_epoch_ms: bigint;
  readonly reviewed_at_epoch_ms: bigint;
  readonly expires_at_epoch_ms: bigint;
}

export interface AssetFetchRequest {
  readonly asset_id: string;
  readonly asset_revision: string;
  readonly origin_path: string;
  readonly offset_bytes: bigint;
  readonly total_bytes: bigint;
  readonly expected_digest: Uint8Array;
  readonly container: AssetContainer;
}

export interface AssetRemoveRequest {
  readonly asset_id: string;
  readonly asset_revision: string;
}

export interface AssetDeliveryEffect {
  readonly operation_kind: AssetDeliveryOperation;
  readonly fetch: AssetFetchRequest | null;
  readonly remove: AssetRemoveRequest | null;
}

export function isValidAssetDeliveryEffect(value: AssetDeliveryEffect): boolean {
  const bodyCount = [
    value.fetch !== null,
    value.remove !== null,
  ].filter(Boolean).length;
  if (bodyCount !== 1) return false;
  switch (value.operation_kind) {
    case AssetDeliveryOperation.FetchAsset:
      return value.fetch !== null;
    case AssetDeliveryOperation.RemoveAsset:
      return value.remove !== null;
  }
}

export interface AssetTransferReport {
  readonly asset_id: string;
  readonly asset_revision: string;
  readonly outcome: AssetTransferOutcome;
  readonly written_bytes: bigint;
  readonly observed_bytes: bigint;
  readonly observed_digest: Uint8Array;
}

export interface AssetRemovalReport {
  readonly asset_id: string;
  readonly asset_revision: string;
  readonly reclaimed_bytes: bigint;
}

export interface AssetDeliveryEffectResult {
  readonly operation_kind: AssetDeliveryOperation;
  readonly transfer: AssetTransferReport | null;
  readonly removal: AssetRemovalReport | null;
}

export function isValidAssetDeliveryEffectResult(value: AssetDeliveryEffectResult): boolean {
  const bodyCount = [
    value.transfer !== null,
    value.removal !== null,
  ].filter(Boolean).length;
  if (bodyCount !== 1) return false;
  switch (value.operation_kind) {
    case AssetDeliveryOperation.FetchAsset:
      return value.transfer !== null;
    case AssetDeliveryOperation.RemoveAsset:
      return value.removal !== null;
  }
}

export interface CatalogFetchEffect {
  readonly operation_kind: CatalogNetworkOperation;
  readonly known_catalog_version: string | null;
  readonly max_response_bytes: number;
}

export interface CatalogFetchEffectResult {
  readonly operation_kind: CatalogNetworkOperation;
  readonly disposition: CatalogFetchDisposition;
  readonly body: Uint8Array;
}

export interface CachedCatalogOverlay {
  readonly fetched_at_utc_ms: bigint;
  readonly document: Uint8Array;
}

export interface AssetOnDisk {
  readonly asset_id: string;
  readonly asset_revision: string;
  readonly presence: AssetPresence;
  readonly written_bytes: bigint;
}

export interface SetAssetDeliveryPolicyCommand {
  readonly network_cost: AssetNetworkCost;
  readonly metered_permitted: boolean;
}

export interface RequestAssetCommand {
  readonly asset_id: string;
  readonly asset_revision: string;
}

export interface RemoveAssetCommand {
  readonly asset_id: string;
  readonly asset_revision: string;
}

export interface SkillRecord {
  readonly skill_id: string;
  readonly origin: string;
  readonly provenance: SkillProvenance;
  readonly status: SkillStatus;
  readonly active_version: number;
  readonly definition: Uint8Array;
  readonly step_count: number;
  readonly installed_at_utc_ms: bigint;
  readonly updated_at_utc_ms: bigint;
}

export interface SkillRunRecord {
  readonly skill_id: string;
  readonly version: number;
  readonly task_id: string;
  readonly outcome: SkillRunOutcome;
  readonly ran_at_utc_ms: bigint;
}

export interface AssistantConfiguration {
  readonly revision: bigint;
  readonly disabled_abilities: ReadonlyArray<AssistantAbility>;
  readonly preset: PersonalityPreset;
  readonly pace: number;
  readonly length: number;
  readonly check_in: number;
}

export interface CoreBootstrap {
  readonly service_generation: bigint;
  readonly private_profile: boolean;
  readonly core_journal_schema_version: number;
  readonly core_journal_schema_checksum: string;
  readonly tasks: ReadonlyArray<TaskRestoreRecord>;
  readonly generation_capability_entropy: Uint8Array;
  readonly account_session: AccountSessionHandle | null;
  readonly browser_profile_id: string;
  readonly workspaces: ReadonlyArray<WorkspaceRestoreRecord>;
  readonly browser_session_id: string;
  readonly available_account_methods: ReadonlyArray<AccountAuthMethod>;
  readonly asset_platform: AssetPlatform;
  readonly assets: ReadonlyArray<AssetOnDisk>;
  readonly skills: ReadonlyArray<SkillRecord>;
  readonly recall: ReadonlyArray<SkillRunRecord>;
  readonly cached_catalog_overlay: CachedCatalogOverlay | null;
  readonly assistant_configuration: AssistantConfiguration | null;
  readonly library_revision: bigint;
  readonly library_entries: ReadonlyArray<LibraryEntryRecord>;
  readonly memory_revision: bigint;
  readonly memory_records: ReadonlyArray<MemoryRecord>;
}

export interface TaskBudget {
  readonly kind: TaskBudgetKind;
  readonly limit: bigint;
}

export interface TaskConsentSource {
  readonly source_id: string;
  readonly tab_id: string;
  readonly normalized_origin: string;
  readonly canonical_locator: string | null;
}

export interface LibraryRefreshSource {
  readonly source_id: string;
  readonly title: string;
  readonly host: string;
  readonly canonical_locator: string;
  readonly original_content_digest: Uint8Array;
}

export interface LibraryRefreshRequest {
  readonly preview_id: string;
  readonly library_revision: bigint;
  readonly collection_id: string;
  readonly source_workspace_revision: bigint;
  readonly sources: ReadonlyArray<LibraryRefreshSource>;
}

export interface TaskConsentPreview {
  readonly sources: ReadonlyArray<TaskConsentSource>;
  readonly source_discovery_enabled: boolean;
  readonly new_source_cap: number;
  readonly provider_route: TaskProviderRoute;
}

export interface TaskSuppliedValuePosition {
  readonly index: number;
  readonly request_id: string;
}

export interface TaskToggleState {
  readonly checked: boolean;
}

export interface TaskActionInput {
  readonly kind: TaskActionInputKind;
  readonly supplied_value: TaskSuppliedValuePosition | null;
  readonly toggle_state: TaskToggleState | null;
}

export interface TaskPolicyEffect {
  readonly operation: OperationEnvelope;
  readonly effect_id: string;
  readonly task_id: string;
  readonly action_id: string;
  readonly action_class: PolicyActionClass;
  readonly proposal_digest: string;
  readonly idempotency_key: string;
  readonly tab_id: string;
  readonly node_id: string | null;
  readonly principal: PolicyPrincipal;
  readonly data_classes: ReadonlyArray<BipSensitivity>;
  readonly context_risk: PolicyRiskClass;
  readonly approval: PolicyApprovalFact | null;
  readonly control_mode: TaskControlMode;
  readonly policy_version: number;
  readonly operation_kind: TaskActionOperationKind;
  readonly tool_name: string;
  readonly destination_address: string | null;
  readonly canonical_intent: Uint8Array;
  readonly transient_search_query: string | null;
  readonly input: TaskActionInput;
  readonly discovery: TaskDiscoveryAuthorityFact | null;
}

export interface TaskFrozenDocument {
  readonly frame_id: string;
  readonly page_epoch: string;
  readonly graph_revision: bigint;
  readonly normalized_origin: string;
  readonly opaque_origin_id: string | null;
}

export interface TaskTabDocumentTarget {
  readonly tab_id: string;
  readonly frame_id: string;
  readonly page_epoch: string;
  readonly graph_revision: bigint;
}

export interface TaskTabActionBinding {
  readonly browser_session_id: string;
  readonly target: TaskTabDocumentTarget | null;
}

export interface TaskDownloadActionBinding {
  readonly browser_session_id: string;
  readonly download_id: string | null;
}

export interface TaskStoreActionBinding {
  readonly query: string | null;
  readonly limit: number;
}

export interface TaskExecutableAction {
  readonly action_class: PolicyActionClass;
  readonly tool_name: string;
  readonly tab_id: string;
  readonly node_id: string | null;
  readonly destination_origin: string | null;
  readonly operand_handle: string | null;
  readonly destination_address: string | null;
  readonly operation_kind: TaskActionOperationKind;
  readonly canonical_intent: Uint8Array;
  readonly transient_search_query: string | null;
  readonly input: TaskActionInput;
  readonly task_tab: TaskTabActionBinding | null;
  readonly task_download: TaskDownloadActionBinding | null;
  readonly task_store: TaskStoreActionBinding | null;
}

export interface TaskObservationBounds {
  readonly scope: ObservationScope;
  readonly max_bytes: number;
  readonly max_nodes: number;
  readonly max_text_bytes: number;
  readonly max_frames: number;
  readonly deadline_ms: number;
}

export interface TaskActionEffect {
  readonly action_id: string;
  readonly proposal_digest: string;
  readonly idempotency_key: string;
  readonly capability_id: string;
  readonly dispatch_id: string;
  readonly document: TaskFrozenDocument;
  readonly executable: TaskExecutableAction;
  readonly preconditions: ReadonlyArray<TaskActionPrecondition>;
  readonly postcondition: TaskActionPostcondition;
  readonly observation: TaskObservationBounds | null;
}

export interface TaskRevocationEffect {
  readonly reason: TaskRevocationReason;
}

export interface TaskApprovalEffect {
  readonly action_id: string;
  readonly proposal_digest: string;
  readonly form_action: TaskExecutableAction | null;
}

export interface TaskPermissionEffect {
  readonly request_id: string;
  readonly permission: PlatformPermission;
  readonly deadline_monotonic_ms: bigint;
  readonly deadline_utc_ms: bigint;
  readonly browser_session_id: string;
}

export interface TaskSettlementEffect {
  readonly kind: TaskSettlementKind;
}

export interface TaskReconcileEffect {
  readonly action_id: string;
  readonly rule: TaskRecoveryRule;
  readonly dispatch_id: string;
  readonly operation: TaskActionOperationKind;
}

export interface TaskReleaseTabsEffect {
  readonly terminal_revision: bigint;
}

export interface TaskArtifactEffect {
  readonly kind: TaskArtifactKind;
  readonly artifact_id: string;
  readonly workspace_revision: bigint;
  readonly content: Uint8Array;
}

export interface TaskModelEffect {
  readonly call_id: string;
  readonly request: ModelRequestEffect;
}

export interface TaskHandoverEffect {
  readonly handover_id: string;
  readonly window_ms: number;
}

export interface TaskToolJobEffect {
  readonly action_id: string;
  readonly job_id: string;
  readonly runtime: ToolRuntimeKind;
  readonly job: ToolJobEffect | null;
}

export interface TaskFieldValuesEffect {
  readonly request_id: string;
  readonly tab_id: string;
  readonly node_id: string;
  readonly companion_node_ids: ReadonlyArray<string>;
}

export interface TaskDiscoveryBootstrapEffect {
  readonly browser_session_id: string;
  readonly remaining_new_source_cap: number;
}

export interface TaskLibraryToolEffect {
  readonly action_id: string;
  readonly operation_kind: TaskActionOperationKind;
}

export interface TaskMemoryToolEffect {
  readonly action_id: string;
  readonly operation_kind: TaskActionOperationKind;
}

export interface TaskEffectBinding {
  readonly operation: OperationEnvelope;
  readonly effect_id: string;
  readonly task_id: string;
  readonly ordinal: number;
  readonly kind: TaskReducerEffectKind;
  readonly revocation: TaskRevocationEffect | null;
  readonly policy: TaskPolicyEffect | null;
  readonly approval: TaskApprovalEffect | null;
  readonly permission: TaskPermissionEffect | null;
  readonly action: TaskActionEffect | null;
  readonly settlement: TaskSettlementEffect | null;
  readonly reconcile: TaskReconcileEffect | null;
  readonly release_tabs: TaskReleaseTabsEffect | null;
  readonly generate_artifact: TaskArtifactEffect | null;
  readonly export_artifact: TaskArtifactEffect | null;
  readonly model: TaskModelEffect | null;
  readonly handover: TaskHandoverEffect | null;
  readonly tool_job: TaskToolJobEffect | null;
  readonly field_values: TaskFieldValuesEffect | null;
  readonly discovery_bootstrap: TaskDiscoveryBootstrapEffect | null;
  readonly library_tool: TaskLibraryToolEffect | null;
  readonly memory_tool: TaskMemoryToolEffect | null;
}

export function isValidTaskEffectBinding(value: TaskEffectBinding): boolean {
  const bodyCount = [
    value.revocation !== null,
    value.policy !== null,
    value.approval !== null,
    value.permission !== null,
    value.action !== null,
    value.settlement !== null,
    value.reconcile !== null,
    value.release_tabs !== null,
    value.generate_artifact !== null,
    value.export_artifact !== null,
    value.model !== null,
    value.handover !== null,
    value.tool_job !== null,
    value.field_values !== null,
    value.discovery_bootstrap !== null,
    value.library_tool !== null,
    value.memory_tool !== null,
  ].filter(Boolean).length;
  if (bodyCount !== 1) return false;
  switch (value.kind) {
    case TaskReducerEffectKind.RevokeAuthority:
      return value.revocation !== null;
    case TaskReducerEffectKind.AskPolicy:
      return value.policy !== null;
    case TaskReducerEffectKind.RequestApproval:
      return value.approval !== null;
    case TaskReducerEffectKind.RequestPermission:
      return value.permission !== null;
    case TaskReducerEffectKind.DispatchAction:
      return value.action !== null;
    case TaskReducerEffectKind.AwaitInFlightWork:
      return value.settlement !== null;
    case TaskReducerEffectKind.ReconcileAction:
      return value.reconcile !== null;
    case TaskReducerEffectKind.ReleaseTaskTabs:
      return value.release_tabs !== null;
    case TaskReducerEffectKind.GenerateArtifact:
      return value.generate_artifact !== null;
    case TaskReducerEffectKind.ExportArtifact:
      return value.export_artifact !== null;
    case TaskReducerEffectKind.CallModel:
      return value.model !== null;
    case TaskReducerEffectKind.AwaitHandover:
      return value.handover !== null;
    case TaskReducerEffectKind.RunToolJob:
      return value.tool_job !== null;
    case TaskReducerEffectKind.RequestFieldValues:
      return value.field_values !== null;
    case TaskReducerEffectKind.PrepareDiscoveryTab:
      return value.discovery_bootstrap !== null;
    case TaskReducerEffectKind.RunLibraryTool:
      return value.library_tool !== null;
    case TaskReducerEffectKind.RunMemoryTool:
      return value.memory_tool !== null;
  }
}

export interface TaskProducedArtifactReceipt {
  readonly artifact_id: string;
  readonly kind: TaskArtifactKind;
}

export interface TaskToolOutputReceipt {
  readonly digest: Uint8Array;
  readonly byte_count: bigint;
  readonly chunk_count: number;
  readonly artifact: TaskProducedArtifactReceipt | null;
}

export interface TaskReconciledActionResult {
  readonly result_code: number;
}

export interface TaskEffectCompletion {
  readonly operation: OperationEnvelope;
  readonly effect_id: string;
  readonly task_id: string;
  readonly kind: TaskReducerEffectKind;
  readonly status: TaskEffectCompletionStatus;
  readonly effect_result: EffectResult | null;
  readonly tool_output: TaskToolOutputReceipt | null;
  readonly reconciled_action_result: TaskReconciledActionResult | null;
}

export interface BuiltinSkillReference {
  readonly skill_id: BuiltinSkillId;
  readonly version: number;
}

export interface StartTaskCommand {
  readonly task_id: string;
  readonly workspace_id: string | null;
  readonly browser_profile_id: string;
  readonly kind: TaskKind;
  readonly goal: string;
  readonly control_mode: TaskControlMode;
  readonly provider_route_id: string | null;
  readonly assistant_config_version: number;
  readonly policy_version: number;
  readonly skill_version_id: string | null;
  readonly tool_allowlist: ReadonlyArray<string>;
  readonly milestone: TaskMilestone;
  readonly budgets: ReadonlyArray<TaskBudget>;
  readonly has_task_deadline: boolean;
  readonly task_deadline_monotonic_ms: bigint;
  readonly predecessor_task_id: string | null;
  readonly trace_id: string;
  readonly task_id_seed: Uint8Array;
  readonly template_id: TaskTemplateId;
  readonly consent_preview: TaskConsentPreview;
  readonly initial_consent_receipt_id: string;
  readonly browser_session_id: string;
  readonly task_deadline_utc_ms: bigint;
  readonly library_refresh: LibraryRefreshRequest | null;
  readonly builtin_skill: BuiltinSkillReference | null;
}

export interface CancelTaskCommand {
  readonly task_id: string;
  readonly reason: CancelReason;
  readonly trace_id: string;
}

export interface PauseTaskCommand {
  readonly task_id: string;
  readonly trace_id: string;
}

export interface ResumeTaskCommand {
  readonly task_id: string;
  readonly trace_id: string;
}

export interface TakeOverCommand {
  readonly task_id: string;
  readonly trace_id: string;
}

export interface UserDecisionCommand {
  readonly task_id: string;
  readonly action_id: string;
  readonly decision: UserDecisionKind;
  readonly approval_digest: string;
  readonly trace_id: string;
  readonly approval_receipt_id: string;
  readonly approval_expires_at_monotonic_ms: bigint;
  readonly approval_expires_at_utc_ms: bigint;
  readonly browser_session_id: string;
}

export interface AuthCallbackCommand {
  readonly flow_id: string;
  readonly redirect_binding_id: string;
  readonly returned_state: string;
  readonly status: AuthCallbackStatus;
  readonly authorization_code_handle: string | null;
}

export function isValidAuthCallbackCommandPresence(value: AuthCallbackCommand): boolean {
  return ((value.status === AuthCallbackStatus.AuthorizationCode) ? value.authorization_code_handle !== null : value.authorization_code_handle === null);
}

export interface PermissionResultCommand {
  readonly task_id: string;
  readonly request_id: string;
  readonly permission: PlatformPermission;
  readonly decision: PermissionDecision;
  readonly trace_id: string;
}

export interface CompleteHandoverCommand {
  readonly task_id: string;
  readonly handover_id: string;
  readonly lease_before: string;
  readonly resumed_with: string;
  readonly person_input: number;
  readonly trace_id: string;
}

export interface ExpireHandoverCommand {
  readonly task_id: string;
  readonly handover_id: string;
  readonly trace_id: string;
}

export interface SupplyUserInputCommand {
  readonly task_id: string;
  readonly answer: string;
  readonly trace_id: string;
}

export interface FollowUpCommand {
  readonly task_id: string;
  readonly question: string;
  readonly trace_id: string;
}

export interface SupplyFieldValuesCommand {
  readonly task_id: string;
  readonly request_id: string;
  readonly supplied: number;
  readonly trace_id: string;
  readonly outcome: FieldValueAskOutcome;
  readonly field_node_ids: ReadonlyArray<string>;
}

export interface StartAuthCommand {
  readonly flow_id: string;
  readonly method: AccountAuthMethod;
  readonly redirect_binding_id: string;
  readonly scopes: ReadonlyArray<AccountScope>;
  readonly issued_at_monotonic_ms: bigint;
}

export interface RequestEmailLinkCommand {
  readonly flow_id: string;
  readonly email: string;
  readonly redirect_binding_id: string;
  readonly scopes: ReadonlyArray<AccountScope>;
  readonly issued_at_monotonic_ms: bigint;
}

export interface SignOutCommand {
  readonly account_subject: string | null;
}

export interface AuthCredentialResultCommand {
  readonly flow_id: string;
  readonly method: AccountAuthMethod;
  readonly credential_handle: string | null;
  readonly status: AuthCredentialStatus;
}

export interface CorrectWorkspaceFactCommand {
  readonly workspace_id: string;
  readonly expected_revision: bigint;
  readonly fact_id: string;
  readonly value: string;
}

export interface ExcludeWorkspaceSourceCommand {
  readonly workspace_id: string;
  readonly expected_revision: bigint;
  readonly source_id: string;
}

export interface RequestWorkspaceExportCommand {
  readonly request_id: string;
  readonly workspace_id: string;
  readonly expected_revision: bigint;
  readonly format: WorkspaceExportFormat;
}

export interface SaveWorkspaceCommand {
  readonly workspace_id: string;
  readonly expected_revision: bigint;
}

export interface RenameWorkspaceCommand {
  readonly workspace_id: string;
  readonly expected_revision: bigint;
  readonly display_name: string;
}

export interface DeleteWorkspaceCommand {
  readonly workspace_id: string;
  readonly expected_revision: bigint;
  readonly confirmation_token: string;
}

export interface DiscardWorkspaceCommand {
  readonly workspace_id: string;
  readonly expected_revision: bigint;
}

export interface SearchLibraryCommand {
  readonly request_id: string;
  readonly query: string;
  readonly limit: number;
  readonly requested_at_epoch_ms: bigint;
}

export interface SaveLibraryFactCommand {
  readonly workspace_id: string;
  readonly expected_workspace_revision: bigint;
  readonly fact_id: string;
  readonly expected_library_revision: bigint;
  readonly expected_entry_revision: bigint;
  readonly approved_at_epoch_ms: bigint;
}

export interface RemoveLibraryEntryCommand {
  readonly entry_id: string;
  readonly expected_library_revision: bigint;
  readonly expected_entry_revision: bigint;
  readonly removed_at_epoch_ms: bigint;
}

export interface RequestLibraryExportCommand {
  readonly request_id: string;
  readonly expected_library_revision: bigint;
  readonly collection_id: string | null;
  readonly format: WorkspaceExportFormat;
}

export interface SearchMemoryCommand {
  readonly request_id: string;
  readonly query: string;
  readonly limit: number;
  readonly requested_at_epoch_ms: bigint;
}

export interface UpsertMemoryCommand {
  readonly memory_id: string | null;
  readonly statement: string;
  readonly scope_kind: MemoryScopeKind;
  readonly scope_workspace: MemoryWorkspaceRecord | null;
  readonly sensitivity: MemorySensitivity;
  readonly expected_memory_revision: bigint;
  readonly expected_record_revision: bigint;
  readonly expires_at_epoch_ms: bigint;
  readonly approved_at_epoch_ms: bigint;
}

export interface DeleteMemoryCommand {
  readonly memory_id: string;
  readonly expected_memory_revision: bigint;
  readonly expected_record_revision: bigint;
  readonly deleted_at_epoch_ms: bigint;
}

export interface SaveProviderCredentialCommand {
  readonly provider_id: string;
  readonly auth_method: ProviderAuthMethod;
  readonly credential_handle: string;
}

export interface SetProviderCredentialStateCommand {
  readonly provider_id: string;
  readonly state: ProviderCredentialState;
  readonly available_model_ids: ReadonlyArray<string>;
}

export interface ThinkingPreference {
  readonly level: ThinkingLevel;
}

export interface SetProviderModelPreferenceCommand {
  readonly provider_id: string;
  readonly model_id: string | null;
  readonly thinking: ThinkingPreference | null;
}

export interface ProbeProviderCredentialCommand {
  readonly provider_id: string;
  readonly credential_handle: string;
}

export interface ForgetProviderCredentialCommand {
  readonly provider_id: string;
}

export interface StartProviderAuthCommand {
  readonly flow_id: string;
  readonly provider_id: string;
  readonly redirect_binding_id: string;
  readonly issued_at_monotonic_ms: bigint;
}

export interface CancelProviderAuthCommand {
  readonly flow_id: string;
}

export interface ProviderAuthCallbackCommand {
  readonly flow_id: string;
  readonly redirect_binding_id: string;
  readonly returned_state: string;
  readonly status: AuthCallbackStatus;
  readonly authorization_code_handle: string | null;
}

export function isValidProviderAuthCallbackCommandPresence(value: ProviderAuthCallbackCommand): boolean {
  return ((value.status === AuthCallbackStatus.AuthorizationCode) ? value.authorization_code_handle !== null : value.authorization_code_handle === null);
}

export interface CustomModelSpec {
  readonly model_id: string;
  readonly display_name: string;
  readonly context_window: number;
  readonly max_output_tokens: number;
  readonly reasoning: boolean;
  readonly tool_calling: boolean;
}

export interface DetectedServer {
  readonly server_kind: ServerKind;
}

export interface SaveCustomProviderCommand {
  readonly provider_id: string;
  readonly display_name: string;
  readonly endpoint: string;
  readonly wire_api: ProviderWireApi;
  readonly credential_handle: string | null;
  readonly models: ReadonlyArray<CustomModelSpec>;
  readonly detected_server: DetectedServer | null;
}

export interface RemoveCustomProviderCommand {
  readonly provider_id: string;
}

export interface ProbeCustomEndpointCommand {
  readonly endpoint: string;
  readonly wire_api: ProviderWireApi;
  readonly credential_handle: string | null;
  readonly provider_id: string;
}

export interface RequestComposerCompletionCommand {
  readonly request_id: string;
  readonly prefix: string;
  readonly suffix: string | null;
}

export interface CancelComposerCompletionCommand {
  readonly request_id: string;
}

export interface SetAssistantConfigurationCommand {
  readonly expected_revision: bigint;
  readonly disabled_abilities: ReadonlyArray<AssistantAbility>;
  readonly preset: PersonalityPreset;
  readonly pace: number;
  readonly length: number;
  readonly check_in: number;
}

export interface SavedSignInMetadata {
  readonly id: string;
  readonly site: string;
  readonly username: string;
  readonly last_used_epoch_ms: bigint;
}

export interface SavedDetailRecord {
  readonly id: string;
  readonly given_name: string;
  readonly family_name: string;
  readonly email: string;
  readonly phone: string;
  readonly address: string;
  readonly postcode: string;
  readonly country: string;
}

export interface ReplaceSavedDataSnapshotCommand {
  readonly sign_ins_availability: SavedDataAvailability;
  readonly sign_ins_revision: bigint;
  readonly sign_ins: ReadonlyArray<SavedSignInMetadata>;
  readonly details_availability: SavedDataAvailability;
  readonly details_revision: bigint;
  readonly details: ReadonlyArray<SavedDetailRecord>;
}

export interface AcceptTaskArtifactCommand {
  readonly task_id: string;
  readonly artifact_id: string;
}

export interface ExportTaskArtifactCommand {
  readonly task_id: string;
  readonly artifact_id: string;
  readonly kind: TaskArtifactKind;
}

export interface SkillObservedClause {
  readonly kind: SkillClauseKind;
  readonly role: number;
  readonly detail: number;
}

export interface SkillSemanticTarget {
  readonly role: number;
  readonly phrase: number;
}

export interface SkillObservedArgument {
  readonly parameter: number;
  readonly kind: SkillArgumentKind;
  readonly value: bigint;
  readonly purpose: number;
  readonly public_address: string | null;
  readonly semantic_target: SkillSemanticTarget | null;
}

export interface SkillObservedStep {
  readonly verb: string;
  readonly arguments: ReadonlyArray<SkillObservedArgument>;
  readonly postcondition: number;
  readonly has_fill: boolean;
  readonly fill_purpose: number;
}

export interface MutateSkillCommand {
  readonly kind: SkillMutationKind;
  readonly skill_id: string;
  readonly expected_version: number;
  readonly origin: string;
  readonly clauses: ReadonlyArray<SkillObservedClause>;
  readonly steps: ReadonlyArray<SkillObservedStep>;
  readonly admitted: number;
  readonly enabled: boolean;
  readonly recorded_at_epoch_ms: bigint;
}

export interface CoreServiceCommand {
  readonly operation: OperationEnvelope;
  readonly kind: CoreServiceCommandKind;
  readonly start_task: StartTaskCommand | null;
  readonly cancel_task: CancelTaskCommand | null;
  readonly user_decision: UserDecisionCommand | null;
  readonly auth_callback: AuthCallbackCommand | null;
  readonly permission_result: PermissionResultCommand | null;
  readonly start_auth: StartAuthCommand | null;
  readonly request_email_link: RequestEmailLinkCommand | null;
  readonly sign_out: SignOutCommand | null;
  readonly auth_credential_result: AuthCredentialResultCommand | null;
  readonly correct_workspace_fact: CorrectWorkspaceFactCommand | null;
  readonly exclude_workspace_source: ExcludeWorkspaceSourceCommand | null;
  readonly request_workspace_export: RequestWorkspaceExportCommand | null;
  readonly set_asset_delivery_policy: SetAssetDeliveryPolicyCommand | null;
  readonly request_asset: RequestAssetCommand | null;
  readonly remove_asset: RemoveAssetCommand | null;
  readonly save_provider_credential: SaveProviderCredentialCommand | null;
  readonly forget_provider_credential: ForgetProviderCredentialCommand | null;
  readonly start_provider_auth: StartProviderAuthCommand | null;
  readonly provider_auth_callback: ProviderAuthCallbackCommand | null;
  readonly save_custom_provider: SaveCustomProviderCommand | null;
  readonly remove_custom_provider: RemoveCustomProviderCommand | null;
  readonly complete_handover: CompleteHandoverCommand | null;
  readonly expire_handover: ExpireHandoverCommand | null;
  readonly supply_user_input: SupplyUserInputCommand | null;
  readonly set_provider_credential_state: SetProviderCredentialStateCommand | null;
  readonly probe_provider_credential: ProbeProviderCredentialCommand | null;
  readonly supply_field_values: SupplyFieldValuesCommand | null;
  readonly set_provider_model_preference: SetProviderModelPreferenceCommand | null;
  readonly probe_custom_endpoint: ProbeCustomEndpointCommand | null;
  readonly request_composer_completion: RequestComposerCompletionCommand | null;
  readonly cancel_composer_completion: CancelComposerCompletionCommand | null;
  readonly pause_task: PauseTaskCommand | null;
  readonly resume_task: ResumeTaskCommand | null;
  readonly take_over: TakeOverCommand | null;
  readonly set_assistant_configuration: SetAssistantConfigurationCommand | null;
  readonly save_workspace: SaveWorkspaceCommand | null;
  readonly rename_workspace: RenameWorkspaceCommand | null;
  readonly delete_workspace: DeleteWorkspaceCommand | null;
  readonly discard_workspace: DiscardWorkspaceCommand | null;
  readonly search_library: SearchLibraryCommand | null;
  readonly save_library_fact: SaveLibraryFactCommand | null;
  readonly remove_library_entry: RemoveLibraryEntryCommand | null;
  readonly request_library_export: RequestLibraryExportCommand | null;
  readonly search_memory: SearchMemoryCommand | null;
  readonly upsert_memory: UpsertMemoryCommand | null;
  readonly delete_memory: DeleteMemoryCommand | null;
  readonly accept_task_artifact: AcceptTaskArtifactCommand | null;
  readonly export_task_artifact: ExportTaskArtifactCommand | null;
  readonly replace_saved_data_snapshot: ReplaceSavedDataSnapshotCommand | null;
  readonly mutate_skill: MutateSkillCommand | null;
  readonly cancel_provider_auth: CancelProviderAuthCommand | null;
  readonly follow_up: FollowUpCommand | null;
}

export function isValidCoreServiceCommand(value: CoreServiceCommand): boolean {
  const bodyCount = [
    value.start_task !== null,
    value.cancel_task !== null,
    value.user_decision !== null,
    value.auth_callback !== null,
    value.permission_result !== null,
    value.start_auth !== null,
    value.request_email_link !== null,
    value.sign_out !== null,
    value.auth_credential_result !== null,
    value.correct_workspace_fact !== null,
    value.exclude_workspace_source !== null,
    value.request_workspace_export !== null,
    value.set_asset_delivery_policy !== null,
    value.request_asset !== null,
    value.remove_asset !== null,
    value.save_provider_credential !== null,
    value.forget_provider_credential !== null,
    value.start_provider_auth !== null,
    value.provider_auth_callback !== null,
    value.save_custom_provider !== null,
    value.remove_custom_provider !== null,
    value.complete_handover !== null,
    value.expire_handover !== null,
    value.supply_user_input !== null,
    value.set_provider_credential_state !== null,
    value.probe_provider_credential !== null,
    value.supply_field_values !== null,
    value.set_provider_model_preference !== null,
    value.probe_custom_endpoint !== null,
    value.request_composer_completion !== null,
    value.cancel_composer_completion !== null,
    value.pause_task !== null,
    value.resume_task !== null,
    value.take_over !== null,
    value.set_assistant_configuration !== null,
    value.save_workspace !== null,
    value.rename_workspace !== null,
    value.delete_workspace !== null,
    value.discard_workspace !== null,
    value.search_library !== null,
    value.save_library_fact !== null,
    value.remove_library_entry !== null,
    value.request_library_export !== null,
    value.search_memory !== null,
    value.upsert_memory !== null,
    value.delete_memory !== null,
    value.accept_task_artifact !== null,
    value.export_task_artifact !== null,
    value.replace_saved_data_snapshot !== null,
    value.mutate_skill !== null,
    value.cancel_provider_auth !== null,
    value.follow_up !== null,
  ].filter(Boolean).length;
  if (bodyCount !== 1) return false;
  switch (value.kind) {
    case CoreServiceCommandKind.StartTask:
      return value.start_task !== null;
    case CoreServiceCommandKind.CancelTask:
      return value.cancel_task !== null;
    case CoreServiceCommandKind.UserDecision:
      return value.user_decision !== null;
    case CoreServiceCommandKind.AuthCallback:
      return value.auth_callback !== null;
    case CoreServiceCommandKind.PermissionResult:
      return value.permission_result !== null;
    case CoreServiceCommandKind.StartAuth:
      return value.start_auth !== null;
    case CoreServiceCommandKind.RequestEmailLink:
      return value.request_email_link !== null;
    case CoreServiceCommandKind.SignOut:
      return value.sign_out !== null;
    case CoreServiceCommandKind.AuthCredentialResult:
      return value.auth_credential_result !== null;
    case CoreServiceCommandKind.CorrectWorkspaceFact:
      return value.correct_workspace_fact !== null;
    case CoreServiceCommandKind.ExcludeWorkspaceSource:
      return value.exclude_workspace_source !== null;
    case CoreServiceCommandKind.RequestWorkspaceExport:
      return value.request_workspace_export !== null;
    case CoreServiceCommandKind.SetAssetDeliveryPolicy:
      return value.set_asset_delivery_policy !== null;
    case CoreServiceCommandKind.RequestAsset:
      return value.request_asset !== null;
    case CoreServiceCommandKind.RemoveAsset:
      return value.remove_asset !== null;
    case CoreServiceCommandKind.SaveProviderCredential:
      return value.save_provider_credential !== null;
    case CoreServiceCommandKind.ForgetProviderCredential:
      return value.forget_provider_credential !== null;
    case CoreServiceCommandKind.StartProviderAuth:
      return value.start_provider_auth !== null;
    case CoreServiceCommandKind.ProviderAuthCallback:
      return value.provider_auth_callback !== null;
    case CoreServiceCommandKind.SaveCustomProvider:
      return value.save_custom_provider !== null;
    case CoreServiceCommandKind.RemoveCustomProvider:
      return value.remove_custom_provider !== null;
    case CoreServiceCommandKind.CompleteHandover:
      return value.complete_handover !== null;
    case CoreServiceCommandKind.ExpireHandover:
      return value.expire_handover !== null;
    case CoreServiceCommandKind.SupplyUserInput:
      return value.supply_user_input !== null;
    case CoreServiceCommandKind.SetProviderCredentialState:
      return value.set_provider_credential_state !== null;
    case CoreServiceCommandKind.ProbeProviderCredential:
      return value.probe_provider_credential !== null;
    case CoreServiceCommandKind.SupplyFieldValues:
      return value.supply_field_values !== null;
    case CoreServiceCommandKind.SetProviderModelPreference:
      return value.set_provider_model_preference !== null;
    case CoreServiceCommandKind.ProbeCustomEndpoint:
      return value.probe_custom_endpoint !== null;
    case CoreServiceCommandKind.RequestComposerCompletion:
      return value.request_composer_completion !== null;
    case CoreServiceCommandKind.CancelComposerCompletion:
      return value.cancel_composer_completion !== null;
    case CoreServiceCommandKind.PauseTask:
      return value.pause_task !== null;
    case CoreServiceCommandKind.ResumeTask:
      return value.resume_task !== null;
    case CoreServiceCommandKind.TakeOver:
      return value.take_over !== null;
    case CoreServiceCommandKind.SetAssistantConfiguration:
      return value.set_assistant_configuration !== null;
    case CoreServiceCommandKind.SaveWorkspace:
      return value.save_workspace !== null;
    case CoreServiceCommandKind.RenameWorkspace:
      return value.rename_workspace !== null;
    case CoreServiceCommandKind.DeleteWorkspace:
      return value.delete_workspace !== null;
    case CoreServiceCommandKind.DiscardWorkspace:
      return value.discard_workspace !== null;
    case CoreServiceCommandKind.SearchLibrary:
      return value.search_library !== null;
    case CoreServiceCommandKind.SaveLibraryFact:
      return value.save_library_fact !== null;
    case CoreServiceCommandKind.RemoveLibraryEntry:
      return value.remove_library_entry !== null;
    case CoreServiceCommandKind.RequestLibraryExport:
      return value.request_library_export !== null;
    case CoreServiceCommandKind.SearchMemory:
      return value.search_memory !== null;
    case CoreServiceCommandKind.UpsertMemory:
      return value.upsert_memory !== null;
    case CoreServiceCommandKind.DeleteMemory:
      return value.delete_memory !== null;
    case CoreServiceCommandKind.AcceptTaskArtifact:
      return value.accept_task_artifact !== null;
    case CoreServiceCommandKind.ExportTaskArtifact:
      return value.export_task_artifact !== null;
    case CoreServiceCommandKind.ReplaceSavedDataSnapshot:
      return value.replace_saved_data_snapshot !== null;
    case CoreServiceCommandKind.MutateSkill:
      return value.mutate_skill !== null;
    case CoreServiceCommandKind.CancelProviderAuth:
      return value.cancel_provider_auth !== null;
    case CoreServiceCommandKind.FollowUp:
      return value.follow_up !== null;
  }
}

export interface CoreBootstrapResult {
  readonly status: InitializationStatus;
  readonly accepted_generation: bigint;
}

export interface Admission {
  readonly operation_id: string;
  readonly status: AdmissionStatus;
}

export interface ModelArtifactRegistration {
  readonly asset_id: string;
  readonly asset_revision: string;
  readonly asset_kind: AssetKind;
  readonly format: ToolModelArtifactKind;
  readonly adapter: boolean;
  readonly byte_length: bigint;
  readonly digest: Uint8Array;
}

export interface CoreStateUpdate {
  readonly service_generation: bigint;
  readonly sequence: bigint;
  readonly core_status_schema_version: number;
  readonly payload: Uint8Array;
  readonly model_artifacts: ReadonlyArray<ModelArtifactRegistration>;
}

export interface PendingApprovalBinding {
  readonly task_id: string;
  readonly action_id: string;
  readonly proposal_digest: string;
  readonly service_generation: bigint;
  readonly task_revision: bigint;
}

export interface TaskRevisionBinding {
  readonly task_id: string;
  readonly service_generation: bigint;
  readonly task_revision: bigint;
  readonly allowed_controls: ReadonlyArray<TaskControlKind>;
}

export interface TaskSettlementBinding {
  readonly task_id: string;
  readonly service_generation: bigint;
  readonly task_revision: bigint;
  readonly kind: TaskSettlementKind;
}

export interface TerminalTaskBinding {
  readonly task_id: string;
  readonly service_generation: bigint;
  readonly task_revision: bigint;
  readonly kind: TerminalTaskKind;
}

export interface PendingPermissionBinding {
  readonly task_id: string;
  readonly request_id: string;
  readonly permission: PlatformPermission;
  readonly service_generation: bigint;
  readonly task_revision: bigint;
  readonly deadline_monotonic_ms: bigint;
  readonly deadline_utc_ms: bigint;
  readonly browser_session_id: string;
}

export interface AcceptedTaskConsentBinding {
  readonly task_id: string;
  readonly service_generation: bigint;
  readonly current_task_revision: bigint;
  readonly accepted_revision: bigint;
  readonly browser_session_id: string;
  readonly receipt_id: string;
  readonly consent_preview: TaskConsentPreview;
}

export interface CommittedActionApprovalBinding {
  readonly task_id: string;
  readonly action_id: string;
  readonly service_generation: bigint;
  readonly committed_revision: bigint;
  readonly receipt_id: string;
  readonly proposal_digest: string;
  readonly expires_at_monotonic_ms: bigint;
  readonly expires_at_utc_ms: bigint;
  readonly browser_session_id: string;
}

export interface CoreStateBrowserBindings {
  readonly service_generation: bigint;
  readonly state_sequence: bigint;
  readonly task_revisions: ReadonlyArray<TaskRevisionBinding>;
  readonly pending_approvals: ReadonlyArray<PendingApprovalBinding>;
  readonly task_settlements: ReadonlyArray<TaskSettlementBinding>;
  readonly pending_permissions: ReadonlyArray<PendingPermissionBinding>;
  readonly terminal_tasks: ReadonlyArray<TerminalTaskBinding>;
  readonly accepted_task_consents: ReadonlyArray<AcceptedTaskConsentBinding>;
  readonly committed_action_approvals: ReadonlyArray<CommittedActionApprovalBinding>;
}

export interface WorkspacePersistEffect {
  readonly workspace_id: string;
  readonly snapshot: Uint8Array;
  readonly expected_revision: bigint;
  readonly resulting_revision: bigint;
}

export interface WorkspaceDeletionEffect {
  readonly workspace_id: string;
  readonly expected_revision: bigint;
  readonly resulting_revision: bigint;
  readonly sources: number;
  readonly facts: number;
  readonly artifact_metadata: number;
  readonly derived_indexes: number;
  readonly confirmation_token: string;
}

export interface SkillInstallEffect {
  readonly skill_id: string;
  readonly origin: string;
  readonly provenance: SkillProvenance;
  readonly version: number;
  readonly definition: Uint8Array;
  readonly step_count: number;
  readonly recorded_at_utc_ms: bigint;
}

export interface SkillStatusEffect {
  readonly skill_id: string;
  readonly status: SkillStatus;
  readonly changed_at_utc_ms: bigint;
  readonly version: number;
}

export interface SkillRunEffect {
  readonly skill_id: string;
  readonly version: number;
  readonly task_id: string;
  readonly outcome: SkillRunOutcome;
  readonly ran_at_utc_ms: bigint;
}

export interface SkillForgetEffect {
  readonly skill_id: string;
}

export interface SourceDeletionEffect {
  readonly source_id: string;
  readonly origin: string;
}

export interface AssistantConfigurationPersistEffect {
  readonly disabled_abilities: ReadonlyArray<AssistantAbility>;
  readonly preset: PersonalityPreset;
  readonly pace: number;
  readonly length: number;
  readonly check_in: number;
}

export interface LibraryPersistEffect {
  readonly entry: LibraryEntryRecord;
  readonly expected_entry_revision: bigint;
}

export interface LibraryDeletionEffect {
  readonly entry_id: string;
  readonly expected_entry_revision: bigint;
  readonly resulting_entry_revision: bigint;
  readonly removed_at_epoch_ms: bigint;
}

export interface MemoryPersistEffect {
  readonly record: MemoryRecord;
  readonly expected_record_revision: bigint;
}

export interface MemoryDeletionEffect {
  readonly memory_id: string;
  readonly expected_record_revision: bigint;
  readonly resulting_record_revision: bigint;
  readonly deleted_at_epoch_ms: bigint;
}

export interface StorageCommitEffect {
  readonly operation_kind: StorageOperation;
  readonly task_id: string;
  readonly expected_revision: bigint;
  readonly resulting_revision: bigint;
  readonly transaction_batch: Uint8Array;
  readonly task_id_seed: Uint8Array;
  readonly workspace: WorkspacePersistEffect | null;
  readonly install_skill: SkillInstallEffect | null;
  readonly skill_status: SkillStatusEffect | null;
  readonly skill_run: SkillRunEffect | null;
  readonly forget_skill: SkillForgetEffect | null;
  readonly source_deletion: SourceDeletionEffect | null;
  readonly assistant_configuration: AssistantConfigurationPersistEffect | null;
  readonly workspace_deletion: WorkspaceDeletionEffect | null;
  readonly library_entry: LibraryPersistEffect | null;
  readonly library_deletion: LibraryDeletionEffect | null;
  readonly memory_record: MemoryPersistEffect | null;
  readonly memory_deletion: MemoryDeletionEffect | null;
}

export interface PageObservationEffect {
  readonly tab_id: string;
  readonly frame_id: string;
  readonly page_epoch: string;
  readonly scope: ObservationScope;
  readonly max_bytes: number;
  readonly task_id: string;
  readonly action_id: string;
  readonly capability_id: string;
  readonly proposal_digest: string;
  readonly idempotency_key: string;
  readonly authority_subject: AuthoritySubject;
  readonly max_nodes: number;
  readonly max_text_bytes: number;
  readonly max_frames: number;
  readonly deadline_ms: number;
  readonly expected_graph_revision: bigint;
}

export interface ModelStaticHeader {
  readonly name: string;
  readonly value: string;
}

export interface ModelRequestEffect {
  readonly route_id: string;
  readonly model_id: string;
  readonly disclosure: DisclosureClass;
  readonly request_body: Uint8Array;
  readonly max_output_bytes: number;
  readonly task_id: string;
  readonly provider_id: string;
  readonly wire_api: ProviderWireApi;
  readonly endpoint: string;
  readonly credential_handle: string | null;
  readonly static_headers: ReadonlyArray<ModelStaticHeader>;
  readonly probe: boolean;
  readonly endpoint_kind: ModelEndpointKind;
  readonly media_attachment_handle: string | null;
  readonly media_attachment_mime_type: string | null;
  readonly not_before_monotonic_ms: bigint;
}

export interface ExchangeAuthorizationCodeRequest {
  readonly flow_id: string;
  readonly auth_method: AccountAuthMethod;
  readonly authorization_code_handle: string;
  readonly pkce_verifier_handle: string;
  readonly redirect_binding_id: string;
}

export interface ExchangeNativeCredentialRequest {
  readonly flow_id: string;
  readonly auth_method: AccountAuthMethod;
  readonly credential_handle: string;
  readonly raw_nonce_handle: string;
}

export interface EmailLinkNetworkRequest {
  readonly flow_id: string;
  readonly email: string;
  readonly pkce_verifier_handle: string;
  readonly redirect_binding_id: string;
  readonly pkce_challenge: string;
  readonly state: string;
}

export interface RefreshSessionRequest {
  readonly session_handle: string;
  readonly expected_rotation: bigint;
  readonly expected_account_subject: string;
  readonly expected_auth_method: AccountAuthMethod;
}

export interface RevokeSessionRequest {
  readonly session_handle: string;
}

export interface FetchEntitlementRequest {
  readonly reason: EntitlementFetchReason;
}

export interface NetworkRequestEffect {
  readonly operation_kind: AccountNetworkOperation;
  readonly exchange_authorization_code: ExchangeAuthorizationCodeRequest | null;
  readonly exchange_native_credential: ExchangeNativeCredentialRequest | null;
  readonly request_email_link: EmailLinkNetworkRequest | null;
  readonly refresh_session: RefreshSessionRequest | null;
  readonly revoke_session: RevokeSessionRequest | null;
  readonly max_response_bytes: number;
  readonly fetch_entitlement: FetchEntitlementRequest | null;
}

export function isValidNetworkRequestEffect(value: NetworkRequestEffect): boolean {
  const bodyCount = [
    value.exchange_authorization_code !== null,
    value.exchange_native_credential !== null,
    value.request_email_link !== null,
    value.refresh_session !== null,
    value.revoke_session !== null,
    value.fetch_entitlement !== null,
  ].filter(Boolean).length;
  if (bodyCount !== 1) return false;
  switch (value.operation_kind) {
    case AccountNetworkOperation.ExchangeAuthorizationCode:
      return value.exchange_authorization_code !== null;
    case AccountNetworkOperation.ExchangeNativeCredential:
      return value.exchange_native_credential !== null;
    case AccountNetworkOperation.RequestEmailLink:
      return value.request_email_link !== null;
    case AccountNetworkOperation.RefreshSession:
      return value.refresh_session !== null;
    case AccountNetworkOperation.RevokeSession:
      return value.revoke_session !== null;
    case AccountNetworkOperation.FetchEntitlement:
      return value.fetch_entitlement !== null;
  }
}

export interface BrowserActionEffect {
  readonly operation_kind: BrowserActionOperation;
  readonly action_id: string;
  readonly grant_reference: string;
  readonly approval_digest: string;
  readonly origin_scope_digest: string;
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

export interface ToolResourceBudget {
  readonly max_input_bytes: bigint;
  readonly max_output_bytes: bigint;
  readonly max_memory_bytes: bigint;
  readonly max_cpu_ms: bigint;
  readonly max_temporary_bytes: bigint;
  readonly max_output_chunks: number;
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

export interface ToolJobEffect {
  readonly job_id: string;
  readonly runtime: ToolRuntimeKind;
  readonly tool_id: string;
  readonly tool_version: string;
  readonly operation_kind: ToolOperation;
  readonly budget: ToolResourceBudget;
  readonly bundled_python: BundledPythonArguments | null;
  readonly local_model: LocalModelArguments | null;
  readonly media_probe: MediaProbeArguments | null;
  readonly audio_extract: AudioExtractArguments | null;
  readonly frame_sample: FrameSampleArguments | null;
  readonly transcode: TranscodeArguments | null;
  readonly signed_wasm: SignedWasmArguments | null;
  readonly local_embedding: LocalEmbeddingArguments | null;
  readonly task_id: string;
}

export function isValidToolJobEffect(value: ToolJobEffect): boolean {
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

export interface GenerateEntropyRequest {
  readonly flow_id: string;
  readonly byte_count: number;
}

export interface WriteTransientSecretRequest {
  readonly flow_id: string;
  readonly purpose: SecretMaterialPurpose;
  readonly material: Uint8Array;
}

export interface DeleteSecretHandleRequest {
  readonly secret_handle: string;
}

export interface SecureStoreEffect {
  readonly operation_kind: SecureStoreOperation;
  readonly generate_entropy: GenerateEntropyRequest | null;
  readonly write_transient: WriteTransientSecretRequest | null;
  readonly delete_handle: DeleteSecretHandleRequest | null;
}

export function isValidSecureStoreEffect(value: SecureStoreEffect): boolean {
  const bodyCount = [
    value.generate_entropy !== null,
    value.write_transient !== null,
    value.delete_handle !== null,
  ].filter(Boolean).length;
  if (bodyCount !== 1) return false;
  switch (value.operation_kind) {
    case SecureStoreOperation.GenerateEntropy:
      return value.generate_entropy !== null;
    case SecureStoreOperation.WriteTransient:
      return value.write_transient !== null;
    case SecureStoreOperation.DeleteHandle:
      return value.delete_handle !== null;
  }
}

export interface OAuthSurfaceRequest {
  readonly flow_id: string;
  readonly auth_method: AccountAuthMethod;
  readonly redirect_binding_id: string;
  readonly pkce_challenge: string;
  readonly state: string;
  readonly scopes: ReadonlyArray<AccountScope>;
  readonly pkce_verifier_handle: string;
}

export interface NativeCredentialSurfaceRequest {
  readonly flow_id: string;
  readonly auth_method: AccountAuthMethod;
  readonly raw_nonce_handle: string;
  readonly hashed_nonce: string;
}

export interface AuthSurfaceEffect {
  readonly operation_kind: AuthSurfaceOperation;
  readonly oauth: OAuthSurfaceRequest | null;
  readonly native_credential: NativeCredentialSurfaceRequest | null;
}

export function isValidAuthSurfaceEffect(value: AuthSurfaceEffect): boolean {
  const bodyCount = [
    value.oauth !== null,
    value.native_credential !== null,
  ].filter(Boolean).length;
  if (bodyCount !== 1) return false;
  switch (value.operation_kind) {
    case AuthSurfaceOperation.OpenOauth:
      return value.oauth !== null;
    case AuthSurfaceOperation.RequestNativeCredential:
      return value.native_credential !== null;
  }
}

export interface PermissionRequestEffect {
  readonly request_id: string;
  readonly permission: PlatformPermission;
  readonly task_id: string;
  readonly task_revision: bigint;
  readonly deadline_monotonic_ms: bigint;
  readonly deadline_utc_ms: bigint;
  readonly browser_session_id: string;
}

export interface ProviderListingFetchEffect {
  readonly provider_id: string;
  readonly endpoint: string;
  readonly wire_api: ProviderWireApi;
  readonly credential_handle: string | null;
  readonly max_response_bytes: number;
}

export interface ProviderListingFetchResult {
  readonly provider_id: string;
  readonly disposition: CatalogFetchDisposition;
  readonly body: Uint8Array;
}

export interface CustomEndpointProbeEffect {
  readonly provider_id: string;
  readonly endpoint: string;
  readonly wire_api: ProviderWireApi;
  readonly credential_handle: string | null;
  readonly max_response_bytes: number;
}

export interface CustomEndpointProbeResult {
  readonly provider_id: string;
  readonly reached: boolean;
  readonly detected_server: DetectedServer | null;
  readonly model_count: number;
  readonly models: ReadonlyArray<CustomModelSpec>;
  readonly proved_base: string | null;
}

export interface ComposerCompletionEffect {
  readonly request_id: string;
  readonly text: string | null;
}

export interface ComposerCompletionEffectResult {
  readonly request_id: string;
  readonly delivered: boolean;
}

export interface EffectEnvelope {
  readonly operation: OperationEnvelope;
  readonly effect_id: string;
  readonly kind: EffectKind;
  readonly retry_class: RetryClass;
  readonly storage_commit: StorageCommitEffect | null;
  readonly page_observation: PageObservationEffect | null;
  readonly model_request: ModelRequestEffect | null;
  readonly network_request: NetworkRequestEffect | null;
  readonly browser_action: BrowserActionEffect | null;
  readonly tool_job: ToolJobEffect | null;
  readonly secure_store: SecureStoreEffect | null;
  readonly auth_surface: AuthSurfaceEffect | null;
  readonly permission_request: PermissionRequestEffect | null;
  readonly asset_delivery: AssetDeliveryEffect | null;
  readonly catalog_fetch: CatalogFetchEffect | null;
  readonly provider_listing_fetch: ProviderListingFetchEffect | null;
  readonly composer_completion: ComposerCompletionEffect | null;
  readonly custom_endpoint_probe: CustomEndpointProbeEffect | null;
}

export function isValidEffectEnvelope(value: EffectEnvelope): boolean {
  const bodyCount = [
    value.storage_commit !== null,
    value.page_observation !== null,
    value.model_request !== null,
    value.network_request !== null,
    value.browser_action !== null,
    value.tool_job !== null,
    value.secure_store !== null,
    value.auth_surface !== null,
    value.permission_request !== null,
    value.asset_delivery !== null,
    value.catalog_fetch !== null,
    value.provider_listing_fetch !== null,
    value.composer_completion !== null,
    value.custom_endpoint_probe !== null,
  ].filter(Boolean).length;
  if (bodyCount !== 1) return false;
  switch (value.kind) {
    case EffectKind.StorageCommit:
      return value.storage_commit !== null;
    case EffectKind.PageObservation:
      return value.page_observation !== null;
    case EffectKind.ModelRequest:
      return value.model_request !== null;
    case EffectKind.NetworkRequest:
      return value.network_request !== null;
    case EffectKind.BrowserAction:
      return value.browser_action !== null;
    case EffectKind.ToolJob:
      return value.tool_job !== null;
    case EffectKind.SecureStore:
      return value.secure_store !== null;
    case EffectKind.OpenAuthSurface:
      return value.auth_surface !== null;
    case EffectKind.RequestPermission:
      return value.permission_request !== null;
    case EffectKind.DeliverAsset:
      return value.asset_delivery !== null;
    case EffectKind.FetchCatalog:
      return value.catalog_fetch !== null;
    case EffectKind.FetchProviderListing:
      return value.provider_listing_fetch !== null;
    case EffectKind.DeliverComposerCompletion:
      return value.composer_completion !== null;
    case EffectKind.ProbeCustomEndpoint:
      return value.custom_endpoint_probe !== null;
  }
}

export interface StorageEffectResult {
  readonly committed_revision: bigint;
}

export interface MediaObservationFact {
  readonly kind: MediaFactKind;
  readonly evidence: MediaEvidenceKind;
  readonly text: string;
  readonly source_locator: string;
  readonly source_start: number;
  readonly source_end: number;
  readonly page_index_plus_one: number;
  readonly timestamp_start_ms: bigint;
  readonly timestamp_end_ms: bigint;
  readonly row_index_plus_one: number;
  readonly confidence_ppm: number;
  readonly truncated: boolean;
}

export interface MediaCaptureProvenance {
  readonly capture_x_dip: number;
  readonly capture_y_dip: number;
  readonly capture_width_dip: number;
  readonly capture_height_dip: number;
  readonly viewport_width_dip: number;
  readonly viewport_height_dip: number;
  readonly output_scale_ppm: number;
  readonly captured_at_monotonic_ms: bigint;
  readonly redacted_region_count: number;
}

export interface MediaObservationResult {
  readonly kind: MediaObservationKind;
  readonly facts: ReadonlyArray<MediaObservationFact>;
  readonly attachment_handle: string | null;
  readonly attachment_mime_type: string | null;
  readonly width_px: number;
  readonly height_px: number;
  readonly has_meaningful_text: boolean;
  readonly scanned_pdf_ocr_required: boolean;
  readonly capture_provenance: MediaCaptureProvenance | null;
}

export interface ObservationEffectResult {
  readonly status: BipObservationStatus;
  readonly schema_version: string;
  readonly tab_id: string;
  readonly frame_id: string;
  readonly page_epoch: string;
  readonly graph_revision: bigint;
  readonly origin: string;
  readonly is_potentially_trustworthy: boolean;
  readonly private_profile: boolean;
  readonly node_count: number;
  readonly total_bytes: number;
  readonly truncated: boolean;
  readonly may_change_answer: boolean;
  readonly redacted_field_count: number;
  readonly suppressed_secret_value_count: number;
  readonly sensitive_zone_count: number;
  readonly policy_filtered_frame_count: number;
  readonly highest_sensitivity: BipSensitivity;
  readonly graph_encoding: BipGraphEncoding;
  readonly graph_payload: Uint8Array;
  readonly media: MediaObservationResult | null;
}

export interface PageSnapshotExportCommand {
  readonly operation: OperationEnvelope;
  readonly format: PageSnapshotExportFormat;
  readonly expected_tab_id: string;
  readonly expected_frame_id: string;
  readonly expected_page_epoch: string;
  readonly expected_graph_revision: bigint;
  readonly expected_origin: string;
  readonly max_bytes: number;
  readonly observation: ObservationEffectResult;
  readonly captured_at_epoch_ms: bigint;
  readonly source_query_withheld: boolean;
  readonly source_fragment_withheld: boolean;
}

export interface PageSnapshotExportResult {
  readonly operation: OperationEnvelope;
  readonly status: PageSnapshotExportStatus;
  readonly format: PageSnapshotExportFormat;
  readonly tab_id: string;
  readonly frame_id: string;
  readonly page_epoch: string;
  readonly graph_revision: bigint;
  readonly origin: string;
  readonly mime_type: string;
  readonly suggested_file_name: string;
  readonly content: Uint8Array;
  readonly node_count: number;
  readonly redacted_field_count: number;
  readonly suppressed_secret_value_count: number;
  readonly withheld_field_count: number;
  readonly captured_at_epoch_ms: bigint;
  readonly source_query_withheld: boolean;
  readonly source_fragment_withheld: boolean;
  readonly secure_context: boolean;
}

export interface SiteSkillMatchCommand {
  readonly operation: OperationEnvelope;
  readonly expected_tab_id: string;
  readonly expected_frame_id: string;
  readonly expected_page_epoch: string;
  readonly expected_graph_revision: bigint;
  readonly expected_origin: string;
  readonly observation: ObservationEffectResult;
}

export interface SiteSkillMatchOffer {
  readonly skill_version_id: string;
  readonly skill_id: string;
  readonly active_version: number;
  readonly step_count: number;
}

export interface SiteSkillMatchResult {
  readonly operation: OperationEnvelope;
  readonly status: SiteSkillMatchStatus;
  readonly tab_id: string;
  readonly frame_id: string;
  readonly page_epoch: string;
  readonly graph_revision: bigint;
  readonly origin: string;
  readonly offers: ReadonlyArray<SiteSkillMatchOffer>;
}

export interface ModelStreamChunk {
  readonly operation: OperationEnvelope;
  readonly effect_id: string;
  readonly sequence: number;
  readonly data: Uint8Array;
}

export interface TaskAnswerEvent {
  readonly task_id: string;
  readonly call_id: string;
  readonly sequence: number;
  readonly text: string | null;
  readonly terminal: boolean;
  readonly complete: boolean;
}

export interface ModelFailure {
  readonly error_class: ModelErrorClass;
  readonly has_retry_after: boolean;
  readonly retry_after_millis: bigint;
}

export interface ModelEffectResult {
  readonly model_id: string;
  readonly completion: Uint8Array;
  readonly input_units: bigint;
  readonly output_units: bigint;
  readonly provider_http_status: number;
  readonly streamed: boolean;
  readonly failure: ModelFailure | null;
}

export interface AccountTokenValidationRequest {
  readonly operation: OperationEnvelope;
  readonly operation_kind: AccountNetworkOperation;
  readonly expected_auth_method: AccountAuthMethod;
  readonly expected_account_subject: string | null;
  readonly target_rotation: bigint;
  readonly response_body: Uint8Array;
}

export interface AccountTokenValidationResult {
  readonly status: AccountTokenValidationStatus;
  readonly operation_id: string;
  readonly operation_kind: AccountNetworkOperation;
  readonly auth_method: AccountAuthMethod;
  readonly account_subject: string;
  readonly expires_in_seconds: bigint;
  readonly target_rotation: bigint;
  readonly access_token: Uint8Array;
  readonly refresh_token: Uint8Array;
  readonly email: string | null;
  readonly display_name: string | null;
}

export interface AccountSessionReceipt {
  readonly session_handle: string;
  readonly account_subject: string;
  readonly expires_at_monotonic_ms: bigint;
  readonly rotation: bigint;
  readonly auth_method: AccountAuthMethod;
  readonly email: string | null;
  readonly display_name: string | null;
}

export interface EntitlementSummaryResult {
  readonly definitive_absent: boolean;
  readonly plan_id: string;
  readonly model_ids: ReadonlyArray<string>;
  readonly window_seconds: number;
  readonly requests_remaining: bigint;
  readonly credits_granted: bigint;
  readonly credits_remaining: bigint;
  readonly credit_unit_micros: bigint;
  readonly next_renewal_epoch_seconds: bigint;
  readonly valid_until_epoch_seconds: bigint;
  readonly minted_at_utc_ms: bigint;
  readonly worker_host: string;
  readonly gateway_host: string;
}

export interface EmailLinkNetworkResult {
  readonly flow_id: string;
  readonly accepted: boolean;
}

export interface RevokedSessionResult {
  readonly session_handle: string;
  readonly deleted: boolean;
}

export interface NetworkEffectResult {
  readonly operation_kind: AccountNetworkOperation;
  readonly authorization_code_session: AccountSessionReceipt | null;
  readonly native_credential_session: AccountSessionReceipt | null;
  readonly email_link: EmailLinkNetworkResult | null;
  readonly refreshed_session: AccountSessionReceipt | null;
  readonly revoked_session: RevokedSessionResult | null;
  readonly entitlement_summary: EntitlementSummaryResult | null;
}

export function isValidNetworkEffectResult(value: NetworkEffectResult): boolean {
  const bodyCount = [
    value.authorization_code_session !== null,
    value.native_credential_session !== null,
    value.email_link !== null,
    value.refreshed_session !== null,
    value.revoked_session !== null,
    value.entitlement_summary !== null,
  ].filter(Boolean).length;
  if (bodyCount !== 1) return false;
  switch (value.operation_kind) {
    case AccountNetworkOperation.ExchangeAuthorizationCode:
      return value.authorization_code_session !== null;
    case AccountNetworkOperation.ExchangeNativeCredential:
      return value.native_credential_session !== null;
    case AccountNetworkOperation.RequestEmailLink:
      return value.email_link !== null;
    case AccountNetworkOperation.RefreshSession:
      return value.refreshed_session !== null;
    case AccountNetworkOperation.RevokeSession:
      return value.revoked_session !== null;
    case AccountNetworkOperation.FetchEntitlement:
      return value.entitlement_summary !== null;
  }
}

export interface TaskTabSnapshot {
  readonly target: TaskTabDocumentTarget;
  readonly active: boolean;
}

export interface TaskTabActionResult {
  readonly browser_session_id: string;
  readonly operation_kind: TaskActionOperationKind;
  readonly postcondition: TaskTabPostcondition;
  readonly tabs: ReadonlyArray<TaskTabSnapshot>;
  readonly target: TaskTabDocumentTarget | null;
  readonly state_was_already_satisfied: boolean;
}

export interface TaskDownloadSnapshot {
  readonly download_id: string;
  readonly state: TaskDownloadState;
  readonly media_type: TaskDownloadMediaType;
  readonly received_bytes: bigint;
  readonly directory_class: TaskDownloadDirectoryClass;
}

export interface TaskDownloadActionResult {
  readonly browser_session_id: string;
  readonly operation_kind: TaskActionOperationKind;
  readonly postcondition: TaskDownloadPostcondition;
  readonly downloads: ReadonlyArray<TaskDownloadSnapshot>;
  readonly truncated: boolean;
}

export interface TaskActionRefusal {
  readonly code: TaskActionResultCode;
}

export interface BrowserActionEffectResult {
  readonly outcome: BrowserActionOutcome;
  readonly dispatch_id: string | null;
  readonly discovered_source: TaskConsentSource | null;
  readonly discovery_tab_id: string | null;
  readonly browser_session_id: string | null;
  readonly task_tab: TaskTabActionResult | null;
  readonly task_download: TaskDownloadActionResult | null;
  readonly task_store: TaskStoreActionResult | null;
  readonly refused_code: TaskActionRefusal | null;
}

export interface TaskStoreRow {
  readonly title: string;
  readonly host: string;
  readonly path: string;
  readonly when_utc_ms: bigint;
}

export interface TaskStoreActionResult {
  readonly operation_kind: TaskActionOperationKind;
  readonly rows: ReadonlyArray<TaskStoreRow>;
  readonly omitted: number;
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

export interface ToolEffectResult {
  readonly job_id: string;
  readonly status: ToolTerminalStatus;
  readonly progress: ReadonlyArray<ToolProgress>;
  readonly chunks: ReadonlyArray<ToolOutputChunk>;
  readonly success: ToolSuccess | null;
  readonly streamed_chunks: number;
}

export function isValidToolEffectResultPresence(value: ToolEffectResult): boolean {
  return ((value.status === ToolTerminalStatus.Completed) ? value.success !== null : value.success === null);
}

export interface ToolStreamChunk {
  readonly operation: OperationEnvelope;
  readonly effect_id: string;
  readonly job_id: string;
  readonly chunk: ToolOutputChunk;
}

export interface GeneratedEntropyResult {
  readonly flow_id: string;
  readonly entropy: Uint8Array;
}

export interface TransientSecretWriteResult {
  readonly flow_id: string;
  readonly purpose: SecretMaterialPurpose;
  readonly secret_handle: string;
}

export interface DeletedSecretHandleResult {
  readonly secret_handle: string;
  readonly deleted: boolean;
}

export interface SecureStoreEffectResult {
  readonly operation_kind: SecureStoreOperation;
  readonly generated_entropy: GeneratedEntropyResult | null;
  readonly transient_write: TransientSecretWriteResult | null;
  readonly deleted_handle: DeletedSecretHandleResult | null;
}

export function isValidSecureStoreEffectResult(value: SecureStoreEffectResult): boolean {
  const bodyCount = [
    value.generated_entropy !== null,
    value.transient_write !== null,
    value.deleted_handle !== null,
  ].filter(Boolean).length;
  if (bodyCount !== 1) return false;
  switch (value.operation_kind) {
    case SecureStoreOperation.GenerateEntropy:
      return value.generated_entropy !== null;
    case SecureStoreOperation.WriteTransient:
      return value.transient_write !== null;
    case SecureStoreOperation.DeleteHandle:
      return value.deleted_handle !== null;
  }
}

export interface OAuthSurfaceResult {
  readonly flow_id: string;
  readonly opened: boolean;
}

export interface NativeCredentialSurfaceResult {
  readonly flow_id: string;
  readonly opened: boolean;
}

export interface AuthSurfaceEffectResult {
  readonly operation_kind: AuthSurfaceOperation;
  readonly oauth: OAuthSurfaceResult | null;
  readonly native_credential: NativeCredentialSurfaceResult | null;
}

export function isValidAuthSurfaceEffectResult(value: AuthSurfaceEffectResult): boolean {
  const bodyCount = [
    value.oauth !== null,
    value.native_credential !== null,
  ].filter(Boolean).length;
  if (bodyCount !== 1) return false;
  switch (value.operation_kind) {
    case AuthSurfaceOperation.OpenOauth:
      return value.oauth !== null;
    case AuthSurfaceOperation.RequestNativeCredential:
      return value.native_credential !== null;
  }
}

export interface PermissionEffectResult {
  readonly request_id: string;
  readonly permission: PlatformPermission;
  readonly decision: PermissionDecision;
}

export interface EffectResult {
  readonly operation: OperationEnvelope;
  readonly effect_id: string;
  readonly status: EffectStatus;
  readonly kind: EffectKind;
  readonly storage: StorageEffectResult | null;
  readonly observation: ObservationEffectResult | null;
  readonly model: ModelEffectResult | null;
  readonly network: NetworkEffectResult | null;
  readonly browser_action: BrowserActionEffectResult | null;
  readonly tool: ToolEffectResult | null;
  readonly secure_store: SecureStoreEffectResult | null;
  readonly auth_surface: AuthSurfaceEffectResult | null;
  readonly permission: PermissionEffectResult | null;
  readonly asset_delivery: AssetDeliveryEffectResult | null;
  readonly catalog: CatalogFetchEffectResult | null;
  readonly provider_listing: ProviderListingFetchResult | null;
  readonly composer_completion: ComposerCompletionEffectResult | null;
  readonly custom_endpoint_probe: CustomEndpointProbeResult | null;
}

export function isValidEffectResult(value: EffectResult): boolean {
  const bodyCount = [
    value.storage !== null,
    value.observation !== null,
    value.model !== null,
    value.network !== null,
    value.browser_action !== null,
    value.tool !== null,
    value.secure_store !== null,
    value.auth_surface !== null,
    value.permission !== null,
    value.asset_delivery !== null,
    value.catalog !== null,
    value.provider_listing !== null,
    value.composer_completion !== null,
    value.custom_endpoint_probe !== null,
  ].filter(Boolean).length;
  if (bodyCount !== 1) return false;
  switch (value.kind) {
    case EffectKind.StorageCommit:
      return value.storage !== null;
    case EffectKind.PageObservation:
      return value.observation !== null;
    case EffectKind.ModelRequest:
      return value.model !== null;
    case EffectKind.NetworkRequest:
      return value.network !== null;
    case EffectKind.BrowserAction:
      return value.browser_action !== null;
    case EffectKind.ToolJob:
      return value.tool !== null;
    case EffectKind.SecureStore:
      return value.secure_store !== null;
    case EffectKind.OpenAuthSurface:
      return value.auth_surface !== null;
    case EffectKind.RequestPermission:
      return value.permission !== null;
    case EffectKind.DeliverAsset:
      return value.asset_delivery !== null;
    case EffectKind.FetchCatalog:
      return value.catalog !== null;
    case EffectKind.FetchProviderListing:
      return value.provider_listing !== null;
    case EffectKind.DeliverComposerCompletion:
      return value.composer_completion !== null;
    case EffectKind.ProbeCustomEndpoint:
      return value.custom_endpoint_probe !== null;
  }
}

export interface BackupRecordDescriptor {
  readonly kind: BackupRecordKind;
  readonly stable_id: string;
  readonly revision: bigint;
  readonly schema_version: number;
  readonly state: BackupRecordState;
  readonly plaintext_bytes: bigint;
  readonly plaintext_sha256: Uint8Array;
}

export interface BackupManifestPrepareRequest {
  readonly operation: OperationEnvelope;
  readonly backup_id: string;
  readonly source_installation_id: string;
  readonly created_at_utc: string;
  readonly selection: ReadonlyArray<BackupRecordKind>;
  readonly records: ReadonlyArray<BackupRecordDescriptor>;
}

export interface BackupManifestPrepareResult {
  readonly operation: OperationEnvelope;
  readonly status: BackupPlanningStatus;
  readonly manifest_plaintext: Uint8Array;
  readonly snapshot_sha256: Uint8Array;
  readonly payload_plaintext_bytes: bigint;
  readonly source_order: ReadonlyArray<number>;
  readonly expected_sealed_chunks: number;
}

export interface BackupManifestInspectRequest {
  readonly operation: OperationEnvelope;
  readonly manifest_plaintext: Uint8Array;
}

export interface BackupPayloadLayoutEntry {
  readonly state: BackupRecordState;
  readonly plaintext_bytes: bigint;
}

export interface BackupManifestInspectResult {
  readonly operation: OperationEnvelope;
  readonly status: BackupPlanningStatus;
  readonly backup_id: string;
  readonly source_installation_id: string;
  readonly created_at_utc: string;
  readonly selection: ReadonlyArray<BackupRecordKind>;
  readonly record_count: number;
  readonly snapshot_sha256: Uint8Array;
  readonly payload_plaintext_bytes: bigint;
  readonly records: ReadonlyArray<BackupPayloadLayoutEntry>;
}

export interface StagedBackupRecord {
  readonly plaintext_bytes: bigint;
  readonly plaintext_sha256: Uint8Array;
}

export interface BackupRestoreTarget {
  readonly kind: BackupRestoreTargetKind;
  readonly profile_id: string;
}

export interface BackupRestoreBinding {
  readonly planning_operation: OperationEnvelope;
  readonly owner_profile_id: string;
  readonly target: BackupRestoreTarget;
  readonly backup_id: string;
  readonly snapshot_sha256: Uint8Array;
  readonly confirmation_sha256: Uint8Array;
}

export interface BackupRestoreStageAuthorization {
  readonly binding: BackupRestoreBinding;
  readonly decision_operation: OperationEnvelope;
}

export interface BackupRestoreCommitAuthorization {
  readonly binding: BackupRestoreBinding;
  readonly decision_operation: OperationEnvelope;
}

export interface BackupRestoreResolutionAuthorization {
  readonly binding: BackupRestoreBinding;
  readonly decision_operation: OperationEnvelope;
  readonly choice: BackupRestoreResolutionChoice;
}

export interface BackupRestorePlanRequest {
  readonly operation: OperationEnvelope;
  readonly manifest_plaintext: Uint8Array;
  readonly staged_records: ReadonlyArray<StagedBackupRecord>;
  readonly current_records: ReadonlyArray<BackupRecordDescriptor>;
  readonly target: BackupRestoreTarget;
}

export interface BackupRestorePlanEntry {
  readonly kind: BackupRecordKind;
  readonly stable_id: string;
  readonly archive_revision: bigint;
  readonly action: BackupRestoreAction;
  readonly schema_version: number;
  readonly state: BackupRecordState;
  readonly plaintext_bytes: bigint;
  readonly plaintext_sha256: Uint8Array;
}

export interface BackupRestorePlanResult {
  readonly operation: OperationEnvelope;
  readonly status: BackupPlanningStatus;
  readonly backup_id: string;
  readonly snapshot_sha256: Uint8Array;
  readonly target: BackupRestoreTarget;
  readonly entries: ReadonlyArray<BackupRestorePlanEntry>;
  readonly has_conflicts: boolean;
  readonly confirmation_sha256: Uint8Array;
  readonly binding: BackupRestoreBinding | null;
}

export function isValidBackupRestorePlanResultPresence(value: BackupRestorePlanResult): boolean {
  return ((value.status === BackupPlanningStatus.Succeeded) ? value.binding !== null : value.binding === null);
}

export interface BackupRestorePlanConfirmationRequest {
  readonly operation: OperationEnvelope;
  readonly binding: BackupRestoreBinding;
  readonly confirmed_sha256: Uint8Array;
}

export interface BackupRestoreStageAuthorizationResult {
  readonly operation: OperationEnvelope;
  readonly status: BackupRestoreProtocolStatus;
  readonly authorization: BackupRestoreStageAuthorization | null;
}

export function isValidBackupRestoreStageAuthorizationResultPresence(value: BackupRestoreStageAuthorizationResult): boolean {
  return ((value.status === BackupRestoreProtocolStatus.Succeeded) ? value.authorization !== null : value.authorization === null);
}

export interface BackupRestoreStageVerificationRequest {
  readonly operation: OperationEnvelope;
  readonly authorization: BackupRestoreStageAuthorization;
  readonly staged_snapshot_sha256: Uint8Array;
  readonly skills: ReadonlyArray<SkillRecord>;
}

export interface BackupRestoreCommitAuthorizationResult {
  readonly operation: OperationEnvelope;
  readonly status: BackupRestoreProtocolStatus;
  readonly authorization: BackupRestoreCommitAuthorization | null;
}

export function isValidBackupRestoreCommitAuthorizationResultPresence(value: BackupRestoreCommitAuthorizationResult): boolean {
  return ((value.status === BackupRestoreProtocolStatus.Succeeded) ? value.authorization !== null : value.authorization === null);
}

export interface BackupRestoreCommitOutcomeReport {
  readonly operation: OperationEnvelope;
  readonly authorization: BackupRestoreCommitAuthorization;
  readonly outcome: BackupRestoreCommitOutcome;
}

export interface BackupRestoreResolutionRequest {
  readonly operation: OperationEnvelope;
  readonly binding: BackupRestoreBinding;
  readonly choice: BackupRestoreResolutionChoice;
}

export interface BackupRestoreResolutionAuthorizationResult {
  readonly operation: OperationEnvelope;
  readonly status: BackupRestoreProtocolStatus;
  readonly authorization: BackupRestoreResolutionAuthorization | null;
}

export function isValidBackupRestoreResolutionAuthorizationResultPresence(value: BackupRestoreResolutionAuthorizationResult): boolean {
  return ((value.status === BackupRestoreProtocolStatus.Succeeded) ? value.authorization !== null : value.authorization === null);
}

export interface BackupRestoreResolutionOutcomeReport {
  readonly operation: OperationEnvelope;
  readonly authorization: BackupRestoreResolutionAuthorization;
  readonly outcome: BackupRestoreResolutionOutcome;
}

export interface BackupRestoreCancellationRequest {
  readonly operation: OperationEnvelope;
  readonly binding: BackupRestoreBinding;
}

export interface BackupRestoreProtocolResult {
  readonly operation: OperationEnvelope;
  readonly status: BackupRestoreProtocolStatus;
}

export interface BackupRestoreCandidateWitness {
  readonly selection: ReadonlyArray<BackupRecordKind>;
  readonly record_count: bigint;
  readonly candidate_records_sha256: Uint8Array;
}

export interface BackupRestoreRecoveryBinding {
  readonly reservation_id: string;
  readonly owner_profile_id: string;
  readonly target_kind: BackupRestoreTargetKind;
  readonly target_profile_id: string;
  readonly backup_id: string;
  readonly snapshot_sha256: Uint8Array;
  readonly confirmation_sha256: Uint8Array;
  readonly selection: ReadonlyArray<BackupRecordKind>;
  readonly record_count: bigint;
  readonly candidate_records_sha256: Uint8Array;
}

export interface BackupRestoreRecoveryIntentFact {
  readonly intent_id: string;
  readonly intent: BackupRestorePhysicalIntent;
}

export interface BackupRestoreRecoveryOutcomeFact {
  readonly intent_id: string;
  readonly outcome: BackupRestoreObservedOutcome;
}

export interface BackupRestoreRecoveryRecord {
  readonly format_version: number;
  readonly sequence: bigint;
  readonly binding: BackupRestoreRecoveryBinding;
  readonly fact_kind: BackupRestoreRecoveryFactKind;
  readonly intent: BackupRestoreRecoveryIntentFact | null;
  readonly outcome: BackupRestoreRecoveryOutcomeFact | null;
}

export function isValidBackupRestoreRecoveryRecord(value: BackupRestoreRecoveryRecord): boolean {
  const bodyCount = [
    value.intent !== null,
    value.outcome !== null,
  ].filter(Boolean).length;
  if (bodyCount !== 1) return false;
  switch (value.fact_kind) {
    case BackupRestoreRecoveryFactKind.IntentRecorded:
      return value.intent !== null;
    case BackupRestoreRecoveryFactKind.OutcomeObserved:
      return value.outcome !== null;
  }
}

export interface BackupRestoreRecoveryInspectionRequest {
  readonly operation: OperationEnvelope;
  readonly records: ReadonlyArray<BackupRestoreRecoveryRecord>;
}

export interface BackupRestoreRecoveryReconciliation {
  readonly intent_id: string;
  readonly intent: BackupRestorePhysicalIntent;
}

export interface BackupRestoreRecoveryClassification {
  readonly kind: BackupRestoreRecoveryClassificationKind;
  readonly reconciliation: BackupRestoreRecoveryReconciliation | null;
}

export function isValidBackupRestoreRecoveryClassificationPresence(value: BackupRestoreRecoveryClassification): boolean {
  return ((value.kind === BackupRestoreRecoveryClassificationKind.ReconcileRequired) ? value.reconciliation !== null : value.reconciliation === null);
}

export interface BackupRestoreRecoveryFailure {
  readonly error: BackupRestoreRecoveryError;
}

export interface BackupRestoreRecoveryInspectionResult {
  readonly operation: OperationEnvelope;
  readonly status: BackupRestoreRecoveryInspectionStatus;
  readonly classification: BackupRestoreRecoveryClassification | null;
  readonly failure: BackupRestoreRecoveryFailure | null;
}

export function isValidBackupRestoreRecoveryInspectionResultPresence(value: BackupRestoreRecoveryInspectionResult): boolean {
  return ((value.status === BackupRestoreRecoveryInspectionStatus.Succeeded) ? value.classification !== null : value.classification === null) &&
      ((value.status === BackupRestoreRecoveryInspectionStatus.InvalidHistory) ? value.failure !== null : value.failure === null);
}

export interface BackupRestoreRecoveryResolutionRequest {
  readonly operation: OperationEnvelope;
  readonly history_prefix: ReadonlyArray<BackupRestoreRecoveryRecord>;
  readonly choice: BackupRestoreResolutionChoice;
  readonly intent_id: string;
}

export interface BackupRestoreRecoveryResolutionAuthorization {
  readonly binding: BackupRestoreRecoveryBinding;
  readonly decision_operation: OperationEnvelope;
  readonly choice: BackupRestoreResolutionChoice;
  readonly intent_id: string;
  readonly history_prefix: ReadonlyArray<BackupRestoreRecoveryRecord>;
}

export interface BackupRestoreRecoveryResolutionAuthorizationResult {
  readonly operation: OperationEnvelope;
  readonly status: BackupRestoreProtocolStatus;
  readonly authorization: BackupRestoreRecoveryResolutionAuthorization | null;
}

export function isValidBackupRestoreRecoveryResolutionAuthorizationResultPresence(value: BackupRestoreRecoveryResolutionAuthorizationResult): boolean {
  return ((value.status === BackupRestoreProtocolStatus.Succeeded) ? value.authorization !== null : value.authorization === null);
}

export interface BackupRestoreRecoveryResolutionOutcomeReport {
  readonly operation: OperationEnvelope;
  readonly authorization: BackupRestoreRecoveryResolutionAuthorization;
  readonly durable_history: ReadonlyArray<BackupRestoreRecoveryRecord>;
}

export interface SavedFlowReview {
  readonly skill_id: string;
  readonly origin: string;
  readonly provenance: SkillProvenance;
  readonly status: SkillStatus;
  readonly active_version: number;
  readonly step_count: number;
  readonly installed_at_epoch_ms: bigint;
  readonly updated_at_epoch_ms: bigint;
  readonly recorded_from_task_id: string | null;
  readonly reviewed_steps: ReadonlyArray<SkillObservedStep>;
}

export interface SavedFlowQueryCommand {
  readonly operation: OperationEnvelope;
  readonly kind: SavedFlowQueryKind;
  readonly goal: string;
  readonly skill_id: string;
  readonly expected_version: number;
}

export interface SavedFlowQueryResult {
  readonly operation: OperationEnvelope;
  readonly status: SavedFlowQueryStatus;
  readonly flows: ReadonlyArray<SavedFlowReview>;
}
