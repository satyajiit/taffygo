// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Generated from schema/contract.json. Do not edit.
// Contract core_api 3.49.

export const MAX_SAVED_FLOW_QUERY_RESULTS = 4 as const;
export const MAX_COMMAND_PAYLOAD_BYTES = 65536 as const;
export const MAX_EVENT_PAYLOAD_BYTES = 262144 as const;
export const MAX_ACTIVE_TASKS = 64 as const;
export const MAX_PROGRESS_BASIS_POINTS = 10000 as const;
export const MAX_MESSAGE_KEY_BYTES = 256 as const;
export const MAX_AUTH_EMAIL_BYTES = 320 as const;
export const MAX_AUTH_DISPLAY_NAME_BYTES = 512 as const;
export const MAX_AUTH_CREDENTIAL_HANDLE_BYTES = 256 as const;
export const MAX_AUTH_METHODS = 4 as const;
export const MAX_TASK_GOAL_BYTES = 8192 as const;
export const MAX_TASK_CONTROLS = 4 as const;
export const MAX_TASK_ARTIFACTS = 16 as const;
export const MAX_TASK_ACTIVITY = 32 as const;
export const MAX_TASK_ARTIFACT_EXPORT_BYTES = 16777216 as const;
export const MAX_IDENTIFIER_BYTES = 256 as const;
export const MAX_WORKSPACES = 32 as const;
export const MAX_WORKSPACE_SOURCES = 64 as const;
export const MAX_WORKSPACE_FACTS = 256 as const;
export const MAX_WORKSPACE_DISPLAY_NAME_BYTES = 256 as const;
export const MAX_WORKSPACE_CONFIRMATION_TOKEN_BYTES = 64 as const;
export const MAX_FACT_SOURCES = 16 as const;
export const MAX_WORKSPACE_TITLE_BYTES = 1024 as const;
export const MAX_SOURCE_HOST_BYTES = 253 as const;
export const MAX_TASK_CONSENT_SOURCES = 64 as const;
export const MAX_NEW_SOURCE_CAP = 64 as const;
export const MAX_FACT_FIELD_BYTES = 256 as const;
export const MAX_FACT_VALUE_BYTES = 16384 as const;
export const MAX_EXPORT_CONTENT_BYTES = 131072 as const;
export const MAX_LIBRARY_ENTRIES = 1024 as const;
export const MAX_LIBRARY_SOURCES = 16 as const;
export const MAX_LIBRARY_QUERY_BYTES = 512 as const;
export const MAX_LIBRARY_SEARCH_RESULTS = 32 as const;
export const MAX_LIBRARY_REFRESH_SOURCES = 64 as const;
export const MAX_LIBRARY_REFRESH_RESULTS = 64 as const;
export const MAX_LIBRARY_REFRESH_PREVIEW_ID_BYTES = 64 as const;
export const MAX_MEMORY_RECORDS = 512 as const;
export const MAX_MEMORY_STATEMENT_BYTES = 2048 as const;
export const MAX_MEMORY_QUERY_BYTES = 512 as const;
export const MAX_MEMORY_SEARCH_RESULTS = 32 as const;
export const MAX_PAGE_INSPECTOR_PAYLOAD_BYTES = 131072 as const;
export const MAX_PAGE_INSPECTOR_DOCUMENTS = 1 as const;
export const MAX_PAGE_INSPECTOR_CLAIMS = 4 as const;
export const MAX_PAGE_INSPECTOR_NODES = 128 as const;
export const MAX_PAGE_INSPECTOR_EDGES = 256 as const;
export const MAX_PAGE_INSPECTOR_ADAPTERS = 8 as const;
export const MAX_PAGE_INSPECTOR_FRAMES = 1 as const;
export const MAX_PAGE_INSPECTOR_WARNINGS = 32 as const;
export const MAX_PAGE_INSPECTOR_BUDGETS = 7 as const;
export const MAX_PAGE_INSPECTOR_IDENTIFIER_BYTES = 64 as const;
export const MAX_PAGE_INSPECTOR_HOST_BYTES = 253 as const;
export const MAX_PAGE_INSPECTOR_NAME_BYTES = 512 as const;
export const MAX_PAGE_SNAPSHOT_EXPORT_BYTES = 262144 as const;
export const MAX_ASSETS = 64 as const;
export const MAX_PART_MEMBER_PATH_BYTES = 256 as const;
export const MAX_PART_MEMBER_BYTES = 4194304 as const;
export const MAX_PROVIDER_ID_BYTES = 64 as const;
export const MAX_PROVIDER_DISPLAY_NAME_BYTES = 128 as const;
export const MAX_PROVIDER_ENDPOINT_BYTES = 512 as const;
export const MAX_CUSTOM_PROVIDERS = 32 as const;
export const MAX_PROVIDER_ROSTER_ENTRIES = 96 as const;
export const MAX_USER_INPUT_ANSWER_BYTES = 512 as const;
export const MAX_PROVIDER_MODEL_ENTRIES = 256 as const;
export const MAX_MODEL_ID_BYTES = 128 as const;
export const MAX_MODEL_DISPLAY_NAME_BYTES = 128 as const;
export const MAX_MODEL_ROLES = 4 as const;
export const MAX_MODEL_INPUT_MODALITIES = 2 as const;
export const MAX_MODEL_THINKING_LEVELS = 7 as const;
export const MAX_PROVIDER_PRESENTATION_BYTES = 512 as const;
export const MAX_CUSTOM_MODEL_ENTRIES = 32 as const;
export const MAX_COMPOSER_PREFIX_BYTES = 4096 as const;
export const MAX_COMPOSER_SUFFIX_BYTES = 1024 as const;
export const MAX_COMPOSER_COMPLETION_BYTES = 512 as const;
export const MAX_TASK_ANSWER_DELTA_BYTES = 16384 as const;
export const MAX_TASK_ANSWER_RESIDENCY_BYTES = 131072 as const;
export const MAX_ASSISTANT_ABILITIES = 16 as const;
export const MAX_BUILTIN_SKILLS = 16 as const;
export const MAX_CORE_STATUS_PROJECTION_OMISSIONS = 13 as const;
export const MAX_SITE_SKILLS = 64 as const;
export const MAX_SKILL_ID_BYTES = 128 as const;
export const MAX_SKILL_ORIGIN_BYTES = 2048 as const;
export const MAX_SKILL_MATCH_CLAUSES = 8 as const;
export const MAX_SKILL_STEPS = 32 as const;
export const MAX_SKILL_ARGUMENTS_PER_STEP = 16 as const;
export const MAX_SKILL_TOOL_NAME_BYTES = 128 as const;
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

export enum CoreCommandKind {
  StartTask = 0,
  CancelTask = 1,
  ApproveAction = 2,
  RetryCore = 3,
  PermissionResult = 4,
  StartAuth = 5,
  RequestEmailLink = 6,
  SignOut = 7,
  AuthCredentialResult = 8,
  CorrectWorkspaceFact = 9,
  ExcludeWorkspaceSource = 10,
  RequestWorkspaceExport = 11,
  RequestAsset = 12,
  RemoveAsset = 13,
  SetAssetPolicy = 14,
  SaveProviderCredential = 15,
  ForgetProviderCredential = 16,
  StartProviderAuth = 17,
  SaveCustomProvider = 18,
  RemoveCustomProvider = 19,
  CompleteHandover = 20,
  SupplyUserInput = 21,
  SetProviderCredentialState = 22,
  ProbeProviderKey = 23,
  SetProviderModelPreference = 24,
  ProbeCustomEndpoint = 25,
  RequestComposerCompletion = 26,
  CancelComposerCompletion = 27,
  PauseTask = 28,
  ResumeTask = 29,
  TakeOver = 30,
  SetAssistantConfiguration = 31,
  SaveWorkspace = 32,
  RenameWorkspace = 33,
  DeleteWorkspace = 34,
  DiscardWorkspace = 35,
  SearchLibrary = 36,
  SaveLibraryFact = 37,
  RemoveLibraryEntry = 38,
  RequestLibraryExport = 39,
  SearchMemory = 40,
  UpsertMemory = 41,
  DeleteMemory = 42,
  AcceptTaskArtifact = 43,
  RequestTaskArtifactExport = 44,
  MutateSiteSkill = 45,
  CancelProviderAuth = 46,
  StartLibraryRefresh = 47,
  FollowUp = 48,
}

export enum AssistantAbilityView {
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

export enum BuiltinSkillIdView {
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

export enum BuiltinSkillAvailabilityView {
  Available = 0,
  RequiredToolUnavailable = 1,
  RequiredPartMissing = 2,
  ProfileUnavailable = 3,
  PolicyUnavailable = 4,
}

export enum CoreStatusProjectionMode {
  Complete = 0,
  RecoveryRequired = 1,
}

export enum CoreStatusProjectionFamily {
  ActiveTasks = 0,
  Workspaces = 1,
  WorkspaceExport = 2,
  AssetDelivery = 3,
  ProviderRoster = 4,
  ProviderProbes = 5,
  ProviderModels = 6,
  Library = 7,
  LibraryExport = 8,
  Memory = 9,
  SavedSignIns = 10,
  SavedDetails = 11,
  SiteSkills = 12,
}

export enum SiteSkillMutationKind {
  Teach = 0,
  Update = 1,
  SetEnabled = 2,
  Remove = 3,
}

export enum SiteSkillClauseKind {
  RolePresent = 0,
  PhraseAt = 1,
  StateAt = 2,
}

export enum SiteSkillArgumentKind {
  FromEarlierStep = 0,
  FromPerson = 1,
  Choice = 2,
  Count = 3,
  Flag = 4,
  PublicAddress = 5,
  SemanticTarget = 6,
}

export enum SiteSkillProvenanceView {
  Authored = 0,
  RecordedFromTask = 1,
}

export enum SiteSkillStatusView {
  Draft = 0,
  Active = 1,
  Superseded = 2,
  Retired = 3,
  Disabled = 4,
}

export enum PersonalityPresetView {
  CarefulResearcher = 0,
  QuickShopper = 1,
  TripPlanner = 2,
}

export enum TaskTemplateId {
  CompareProducts = 0,
  SummarizeEvidence = 1,
  BuildSourceTable = 2,
  WebErrand = 3,
}

export enum TaskProviderRoute {
  NotConfigured = 0,
  DirectUserKey = 1,
  ManagedService = 2,
  NoModelRequired = 3,
}

export enum TaskAttachedStore {
  History = 0,
  Bookmarks = 1,
  OpenTabs = 2,
}

export enum CoreAvailability {
  Starting = 0,
  Ready = 1,
  Unavailable = 2,
  CircuitOpen = 3,
}

export enum SavedDataAvailability {
  Loading = 0,
  Ready = 1,
  Unavailable = 2,
}

export enum TaskPhase {
  Idle = 0,
  Planning = 1,
  WaitingForUser = 2,
  Running = 3,
  Completed = 4,
  Failed = 5,
  Cancelled = 6,
  OutcomeUnknown = 7,
  Paused = 8,
  Partial = 9,
}

export enum TaskControlKind {
  Pause = 0,
  Resume = 1,
  TakeOver = 2,
  Stop = 3,
}

export enum CoreFailureCode {
  Cancelled = 0,
  DeadlineExceeded = 1,
  CoreUnavailable = 2,
  PolicyDenied = 3,
  InvalidRequest = 4,
  Backpressure = 5,
  OutcomeUnknown = 6,
  Internal = 7,
  BudgetExceeded = 8,
  ProviderUnavailable = 9,
  SourcesUnavailable = 10,
  JournalUnusable = 11,
  UnverifiableAction = 12,
  ProviderRefused = 13,
  ProviderLimit = 14,
  Offline = 15,
  PolicyRefused = 16,
}

export enum PlatformPermission {
  Notifications = 0,
  Camera = 1,
  Microphone = 2,
  Location = 3,
  ReadUserFile = 4,
  WriteUserFile = 5,
}

export enum PermissionDecision {
  Granted = 0,
  Denied = 1,
  Unavailable = 2,
}

export enum AuthProvider {
  Google = 0,
  EmailLink = 1,
  Github = 2,
  Facebook = 3,
}

export enum AuthMethodAvailability {
  Available = 0,
  NotConfigured = 1,
  PlatformUnavailable = 2,
}

export enum AuthPhase {
  Initializing = 0,
  SignedOut = 1,
  InFlight = 2,
  LinkSent = 3,
  SignedIn = 4,
  Failed = 5,
}

export enum AuthFailureCode {
  NotConfigured = 0,
  Cancelled = 1,
  NoCredential = 2,
  Network = 3,
  Rejected = 4,
  InvalidRedirect = 5,
  CoreUnavailable = 6,
  Unknown = 7,
}

export enum AuthCredentialStatus {
  Success = 0,
  Cancelled = 1,
  NoCredential = 2,
  Unavailable = 3,
}

export enum WorkspacePhase {
  Running = 0,
  WaitingForUser = 1,
  Paused = 2,
  Done = 3,
  PartlyDone = 4,
  Stopped = 5,
  Failed = 6,
}

export enum WorkspaceFactKind {
  FromPage = 0,
  Summarized = 1,
  TaffyInference = 2,
  UserEntered = 3,
}

export enum WorkspaceExportFormat {
  Markdown = 0,
  Csv = 1,
}

export enum TaskActivityKind {
  OpenedPage = 0,
  ReadPage = 1,
  PageUnavailable = 2,
  MoveRefused = 3,
  AskedYou = 4,
  YouAnswered = 5,
  HandedBack = 6,
  YouTookOver = 7,
  BuiltOutput = 8,
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

export enum LibraryAvailability {
  Available = 0,
  PrivateProfile = 1,
  Unavailable = 2,
}

export enum LibraryRefreshDisposition {
  Unchanged = 0,
  Changed = 1,
  Missing = 2,
}

export enum MemoryAvailability {
  Available = 0,
  PrivateProfile = 1,
  Unavailable = 2,
}

export enum MemorySourceKind {
  YouWrote = 0,
  TaffySuggested = 1,
}

export enum MemoryScopeKind {
  AllTasks = 0,
  Workspace = 1,
}

export enum MemorySensitivity {
  Standard = 0,
  Sensitive = 1,
}

export enum PageInspectorAvailability {
  Available = 0,
  NoSelectedPage = 1,
  DocumentUnavailable = 2,
  CoreUnavailable = 3,
  PolicyDenied = 4,
  Backpressure = 5,
  StaleDocument = 6,
  InvalidResponse = 7,
}

export enum SiteSkillOfferAvailability {
  Available = 0,
  CoreUnavailable = 1,
  StaleDocument = 2,
  PrivateProfile = 3,
  Incomplete = 4,
  InvalidResponse = 5,
}

export enum PageSnapshotExportFormat {
  Markdown = 0,
  CanonicalJson = 1,
}

export enum PageSnapshotExportAvailability {
  Available = 0,
  NoSelectedPage = 1,
  DocumentUnavailable = 2,
  CoreUnavailable = 3,
  PolicyDenied = 4,
  Backpressure = 5,
  StaleDocument = 6,
  PrivateProfile = 7,
  Incomplete = 8,
  Oversize = 9,
  InvalidResponse = 10,
  Cancelled = 11,
  ReplayConflict = 12,
}

export enum PageInspectorDocumentKind {
  SemanticPage = 0,
}

export enum PageInspectorClaim {
  BrowserValidated = 0,
  PolicyAdmitted = 1,
  Redacted = 2,
  Bounded = 3,
}

export enum PageInspectorDocumentState {
  Active = 0,
  Frozen = 1,
  Unavailable = 2,
}

export enum PageInspectorNodeRole {
  Document = 0,
  Section = 1,
  Text = 2,
  List = 3,
  Table = 4,
  Link = 5,
  Control = 6,
  Media = 7,
  Commerce = 8,
  Reference = 9,
  Unknown = 10,
}

export enum PageInspectorSensitivity {
  Public = 0,
  Withheld = 1,
  Unknown = 2,
}

export enum PageInspectorRelationship {
  Hierarchy = 0,
  Label = 1,
  Description = 2,
  Control = 3,
  TableHeader = 4,
  Entity = 5,
  Source = 6,
  Other = 7,
}

export enum PageInspectorAdapterKind {
  Structure = 0,
  Accessibility = 1,
  Forms = 2,
  Metadata = 3,
  Browser = 4,
}

export enum PageInspectorAdapterStatus {
  Complete = 0,
  Partial = 1,
  Conflict = 2,
  Unsupported = 3,
  Failed = 4,
}

export enum PageInspectorBudgetKind {
  Items = 0,
  Text = 1,
  TotalBytes = 2,
  Depth = 3,
  Frames = 4,
  Message = 5,
  Deadline = 6,
}

export enum PageInspectorWarningCode {
  SourceUnavailable = 0,
  SourceFailed = 1,
  Conflict = 2,
  FrameOmitted = 3,
  ContentPartial = 4,
  SemanticsMissing = 5,
  ContentWithheld = 6,
  LocationMinimized = 7,
  Deadline = 8,
  ResourcePressure = 9,
  SuspiciousContent = 10,
}

export enum CoreApiSubmissionStatus {
  Accepted = 0,
  InvalidRequest = 1,
  StaleGeneration = 2,
  StaleRevision = 3,
  DeadlineExceeded = 4,
  Backpressure = 5,
  CoreUnavailable = 6,
  Duplicate = 7,
  SourceNotOpen = 8,
  SourceAmbiguous = 9,
  WindowUnavailable = 10,
}

export enum AssetKindView {
  PythonStdlib = 0,
  PythonPackages = 1,
  ModelWeights = 2,
  ModelTokenizer = 3,
  FilterList = 4,
  CountryFlags = 5,
  StartScenes = 6,
}

export enum AssetPresenceView {
  Absent = 0,
  Partial = 1,
  Complete = 2,
  Installed = 3,
}

export enum AssetNetworkCostView {
  Offline = 0,
  Metered = 1,
  Unmetered = 2,
}

export enum AssetRefusalView {
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

export enum PartMemberStatus {
  Ok = 0,
  NotInstalled = 1,
  NotFound = 2,
  TooLarge = 3,
  Unreadable = 4,
  InvalidRequest = 5,
}

export enum ProviderAuthMethodView {
  ApiKey = 0,
  Oauth = 1,
}

export enum ProviderWireApiView {
  AnthropicMessages = 0,
  OpenAiResponses = 1,
  OpenAiCompletions = 2,
  GoogleGenerativeLanguage = 3,
  Managed = 4,
  OpenAiCodexResponses = 5,
  GoogleCloudCodeAssist = 6,
}

export enum ProviderProbeVerdictView {
  Usable = 0,
  Auth = 1,
  Billing = 2,
  RateLimit = 3,
  Overloaded = 4,
  Timeout = 5,
  Network = 6,
  ModelNotFound = 7,
  Unknown = 8,
  EndpointReached = 9,
  NoModelListed = 10,
}

export enum ProviderCredentialStateView {
  Usable = 0,
  NeedsSignIn = 1,
  RefreshFailed = 2,
}

export enum ProviderRefusalView {
  RateLimit = 0,
  Billing = 1,
  Overloaded = 2,
}

export enum ProviderOriginView {
  Catalog = 0,
  Custom = 1,
}

export enum CatalogLayerView {
  EmbeddedBaseline = 0,
  RemoteOverlay = 1,
  UserOverride = 2,
}

export enum ThinkingLevelView {
  Off = 0,
  Minimal = 1,
  Low = 2,
  Medium = 3,
  High = 4,
  Xhigh = 5,
  Max = 6,
}

export enum ModelRoleView {
  PrimaryReasoning = 0,
  FastBrowsing = 1,
  Vision = 2,
  Embedding = 3,
}

export enum InputModalityView {
  Text = 0,
  Image = 1,
}

export enum ServerKindView {
  OpenaiCompatible = 0,
  Ollama = 1,
  LmStudio = 2,
  Vllm = 3,
  LlamaCpp = 4,
}

export enum SavedFlowQueryAvailability {
  Available = 0,
  PrivateProfile = 1,
  Unavailable = 2,
  InvalidRequest = 3,
  StaleRequest = 4,
}

export interface OperationEnvelope {
  readonly operation_id: string;
  readonly service_generation: bigint;
  readonly task_revision: bigint;
  readonly deadline_monotonic_ms: bigint;
  readonly idempotency_key: string;
}

export interface SiteSkillObservedClause {
  readonly kind: SiteSkillClauseKind;
  readonly role: number;
  readonly detail: number;
}

export interface SiteSkillSemanticTarget {
  readonly role: number;
  readonly phrase: number;
}

export interface SiteSkillObservedArgument {
  readonly parameter: number;
  readonly kind: SiteSkillArgumentKind;
  readonly value: bigint;
  readonly purpose: number;
  readonly public_address: string | null;
  readonly semantic_target: SiteSkillSemanticTarget | null;
}

export interface SiteSkillObservedStep {
  readonly verb: string;
  readonly arguments: ReadonlyArray<SiteSkillObservedArgument>;
  readonly postcondition: number;
  readonly has_fill: boolean;
  readonly fill_purpose: number;
}

export interface SiteSkillMutationBody {
  readonly kind: SiteSkillMutationKind;
  readonly skill_id: string;
  readonly expected_version: number;
  readonly origin: string;
  readonly clauses: ReadonlyArray<SiteSkillObservedClause>;
  readonly steps: ReadonlyArray<SiteSkillObservedStep>;
  readonly admitted: number;
  readonly enabled: boolean;
  readonly recorded_at_epoch_ms: bigint;
}

export interface SiteSkillView {
  readonly skill_id: string;
  readonly origin: string;
  readonly provenance: SiteSkillProvenanceView;
  readonly status: SiteSkillStatusView;
  readonly active_version: number;
  readonly step_count: number;
  readonly installed_at_epoch_ms: bigint;
  readonly updated_at_epoch_ms: bigint;
  readonly recorded_from_task_id: string | null;
  readonly reviewed_steps: ReadonlyArray<SiteSkillObservedStep>;
}

export interface SiteSkillOfferView {
  readonly offer_id: string;
  readonly skill_id: string;
  readonly active_version: number;
  readonly step_count: number;
}

export interface CoreCommand {
  readonly operation: OperationEnvelope;
  readonly kind: CoreCommandKind;
  readonly start_task: StartTaskBody | null;
  readonly cancel_task: CancelTaskBody | null;
  readonly approve_action: ApproveActionBody | null;
  readonly retry_core: RetryCoreBody | null;
  readonly permission_result: PermissionResultBody | null;
  readonly start_auth: StartAuthBody | null;
  readonly request_email_link: EmailLinkBody | null;
  readonly sign_out: SignOutBody | null;
  readonly auth_credential_result: AuthCredentialResultBody | null;
  readonly correct_workspace_fact: CorrectWorkspaceFactBody | null;
  readonly exclude_workspace_source: ExcludeWorkspaceSourceBody | null;
  readonly request_workspace_export: RequestWorkspaceExportBody | null;
  readonly request_asset: RequestAssetBody | null;
  readonly remove_asset: RemoveAssetBody | null;
  readonly set_asset_policy: SetAssetPolicyBody | null;
  readonly save_provider_credential: SaveProviderCredentialBody | null;
  readonly forget_provider_credential: ForgetProviderCredentialBody | null;
  readonly start_provider_auth: StartProviderAuthBody | null;
  readonly save_custom_provider: SaveCustomProviderBody | null;
  readonly remove_custom_provider: RemoveCustomProviderBody | null;
  readonly complete_handover: CompleteHandoverBody | null;
  readonly supply_user_input: SupplyUserInputBody | null;
  readonly set_provider_credential_state: SetProviderCredentialStateBody | null;
  readonly probe_provider_key: ProbeProviderKeyBody | null;
  readonly set_provider_model_preference: SetProviderModelPreferenceBody | null;
  readonly probe_custom_endpoint: ProbeCustomEndpointBody | null;
  readonly request_composer_completion: RequestComposerCompletionBody | null;
  readonly cancel_composer_completion: CancelComposerCompletionBody | null;
  readonly pause_task: PauseTaskBody | null;
  readonly resume_task: ResumeTaskBody | null;
  readonly take_over: TakeOverBody | null;
  readonly set_assistant_configuration: SetAssistantConfigurationBody | null;
  readonly save_workspace: SaveWorkspaceBody | null;
  readonly rename_workspace: RenameWorkspaceBody | null;
  readonly delete_workspace: DeleteWorkspaceBody | null;
  readonly discard_workspace: DiscardWorkspaceBody | null;
  readonly search_library: SearchLibraryBody | null;
  readonly save_library_fact: SaveLibraryFactBody | null;
  readonly remove_library_entry: RemoveLibraryEntryBody | null;
  readonly request_library_export: RequestLibraryExportBody | null;
  readonly search_memory: SearchMemoryBody | null;
  readonly upsert_memory: UpsertMemoryBody | null;
  readonly delete_memory: DeleteMemoryBody | null;
  readonly accept_task_artifact: AcceptTaskArtifactBody | null;
  readonly request_task_artifact_export: RequestTaskArtifactExportBody | null;
  readonly mutate_site_skill: SiteSkillMutationBody | null;
  readonly cancel_provider_auth: CancelProviderAuthBody | null;
  readonly start_library_refresh: StartLibraryRefreshBody | null;
  readonly follow_up: FollowUpBody | null;
}

export function isValidCoreCommand(value: CoreCommand): boolean {
  const bodyCount = [
    value.start_task !== null,
    value.cancel_task !== null,
    value.approve_action !== null,
    value.retry_core !== null,
    value.permission_result !== null,
    value.start_auth !== null,
    value.request_email_link !== null,
    value.sign_out !== null,
    value.auth_credential_result !== null,
    value.correct_workspace_fact !== null,
    value.exclude_workspace_source !== null,
    value.request_workspace_export !== null,
    value.request_asset !== null,
    value.remove_asset !== null,
    value.set_asset_policy !== null,
    value.save_provider_credential !== null,
    value.forget_provider_credential !== null,
    value.start_provider_auth !== null,
    value.save_custom_provider !== null,
    value.remove_custom_provider !== null,
    value.complete_handover !== null,
    value.supply_user_input !== null,
    value.set_provider_credential_state !== null,
    value.probe_provider_key !== null,
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
    value.request_task_artifact_export !== null,
    value.mutate_site_skill !== null,
    value.cancel_provider_auth !== null,
    value.start_library_refresh !== null,
    value.follow_up !== null,
  ].filter(Boolean).length;
  if (bodyCount !== 1) return false;
  switch (value.kind) {
    case CoreCommandKind.StartTask:
      return value.start_task !== null;
    case CoreCommandKind.CancelTask:
      return value.cancel_task !== null;
    case CoreCommandKind.ApproveAction:
      return value.approve_action !== null;
    case CoreCommandKind.RetryCore:
      return value.retry_core !== null;
    case CoreCommandKind.PermissionResult:
      return value.permission_result !== null;
    case CoreCommandKind.StartAuth:
      return value.start_auth !== null;
    case CoreCommandKind.RequestEmailLink:
      return value.request_email_link !== null;
    case CoreCommandKind.SignOut:
      return value.sign_out !== null;
    case CoreCommandKind.AuthCredentialResult:
      return value.auth_credential_result !== null;
    case CoreCommandKind.CorrectWorkspaceFact:
      return value.correct_workspace_fact !== null;
    case CoreCommandKind.ExcludeWorkspaceSource:
      return value.exclude_workspace_source !== null;
    case CoreCommandKind.RequestWorkspaceExport:
      return value.request_workspace_export !== null;
    case CoreCommandKind.RequestAsset:
      return value.request_asset !== null;
    case CoreCommandKind.RemoveAsset:
      return value.remove_asset !== null;
    case CoreCommandKind.SetAssetPolicy:
      return value.set_asset_policy !== null;
    case CoreCommandKind.SaveProviderCredential:
      return value.save_provider_credential !== null;
    case CoreCommandKind.ForgetProviderCredential:
      return value.forget_provider_credential !== null;
    case CoreCommandKind.StartProviderAuth:
      return value.start_provider_auth !== null;
    case CoreCommandKind.SaveCustomProvider:
      return value.save_custom_provider !== null;
    case CoreCommandKind.RemoveCustomProvider:
      return value.remove_custom_provider !== null;
    case CoreCommandKind.CompleteHandover:
      return value.complete_handover !== null;
    case CoreCommandKind.SupplyUserInput:
      return value.supply_user_input !== null;
    case CoreCommandKind.SetProviderCredentialState:
      return value.set_provider_credential_state !== null;
    case CoreCommandKind.ProbeProviderKey:
      return value.probe_provider_key !== null;
    case CoreCommandKind.SetProviderModelPreference:
      return value.set_provider_model_preference !== null;
    case CoreCommandKind.ProbeCustomEndpoint:
      return value.probe_custom_endpoint !== null;
    case CoreCommandKind.RequestComposerCompletion:
      return value.request_composer_completion !== null;
    case CoreCommandKind.CancelComposerCompletion:
      return value.cancel_composer_completion !== null;
    case CoreCommandKind.PauseTask:
      return value.pause_task !== null;
    case CoreCommandKind.ResumeTask:
      return value.resume_task !== null;
    case CoreCommandKind.TakeOver:
      return value.take_over !== null;
    case CoreCommandKind.SetAssistantConfiguration:
      return value.set_assistant_configuration !== null;
    case CoreCommandKind.SaveWorkspace:
      return value.save_workspace !== null;
    case CoreCommandKind.RenameWorkspace:
      return value.rename_workspace !== null;
    case CoreCommandKind.DeleteWorkspace:
      return value.delete_workspace !== null;
    case CoreCommandKind.DiscardWorkspace:
      return value.discard_workspace !== null;
    case CoreCommandKind.SearchLibrary:
      return value.search_library !== null;
    case CoreCommandKind.SaveLibraryFact:
      return value.save_library_fact !== null;
    case CoreCommandKind.RemoveLibraryEntry:
      return value.remove_library_entry !== null;
    case CoreCommandKind.RequestLibraryExport:
      return value.request_library_export !== null;
    case CoreCommandKind.SearchMemory:
      return value.search_memory !== null;
    case CoreCommandKind.UpsertMemory:
      return value.upsert_memory !== null;
    case CoreCommandKind.DeleteMemory:
      return value.delete_memory !== null;
    case CoreCommandKind.AcceptTaskArtifact:
      return value.accept_task_artifact !== null;
    case CoreCommandKind.RequestTaskArtifactExport:
      return value.request_task_artifact_export !== null;
    case CoreCommandKind.MutateSiteSkill:
      return value.mutate_site_skill !== null;
    case CoreCommandKind.CancelProviderAuth:
      return value.cancel_provider_auth !== null;
    case CoreCommandKind.StartLibraryRefresh:
      return value.start_library_refresh !== null;
    case CoreCommandKind.FollowUp:
      return value.follow_up !== null;
  }
}

export interface TaskConsentPreview {
  readonly source_hosts: ReadonlyArray<string>;
  readonly source_discovery_enabled: boolean;
  readonly new_source_cap: number;
  readonly provider_route: TaskProviderRoute;
  readonly attached_stores: ReadonlyArray<TaskAttachedStore>;
}

export interface StartTaskBody {
  readonly request_id: string;
  readonly goal: string;
  readonly template_id: TaskTemplateId;
  readonly workspace_id: string | null;
  readonly consent_preview: TaskConsentPreview;
  readonly skill_offer_id: string | null;
}

export interface CancelTaskBody {
  readonly task_id: string;
}

export interface PauseTaskBody {
  readonly task_id: string;
}

export interface ResumeTaskBody {
  readonly task_id: string;
}

export interface TakeOverBody {
  readonly task_id: string;
}

export interface AssistantConfigurationView {
  readonly revision: bigint;
  readonly disabled_abilities: ReadonlyArray<AssistantAbilityView>;
  readonly preset: PersonalityPresetView;
  readonly pace: number;
  readonly length: number;
  readonly check_in: number;
}

export interface BuiltinSkillReferenceView {
  readonly skill_id: BuiltinSkillIdView;
  readonly version: number;
}

export interface BuiltinSkillView {
  readonly reference: BuiltinSkillReferenceView;
  readonly required_ability: AssistantAbilityView;
  readonly enabled: boolean;
  readonly availability: BuiltinSkillAvailabilityView;
  readonly required_tool_count: number;
  readonly available_tool_count: number;
  readonly required_part_count: number;
  readonly installed_part_count: number;
}

export interface CoreStatusProjectionOmission {
  readonly family: CoreStatusProjectionFamily;
  readonly revision: bigint;
  readonly item_count: number;
}

export interface SetAssistantConfigurationBody {
  readonly expected_revision: bigint;
  readonly disabled_abilities: ReadonlyArray<AssistantAbilityView>;
  readonly preset: PersonalityPresetView;
  readonly pace: number;
  readonly length: number;
  readonly check_in: number;
}

export interface CompleteHandoverBody {
  readonly task_id: string;
}

export interface SupplyUserInputBody {
  readonly task_id: string;
  readonly answer: string;
}

export interface FollowUpBody {
  readonly task_id: string;
  readonly question: string;
}

export interface ApproveActionBody {
  readonly task_id: string;
  readonly action_id: string;
}

export interface RetryCoreBody {
  readonly observed_generation: bigint;
}

export interface CoreFailure {
  readonly code: CoreFailureCode;
  readonly retryable: boolean;
  readonly message_key: string | null;
}

export interface TaskActivityView {
  readonly sequence: bigint;
  readonly kind: TaskActivityKind;
  readonly host: string | null;
  readonly count: number;
  readonly at_epoch_ms: bigint;
}

export interface TaskArtifactView {
  readonly artifact_id: string;
  readonly kind: TaskArtifactKind;
  readonly workspace_revision: bigint;
  readonly accepted: boolean;
}

export interface TaskViewState {
  readonly task_id: string;
  readonly revision: bigint;
  readonly phase: TaskPhase;
  readonly progress_basis_points: number;
  readonly status_message_key: string | null;
  readonly failure: CoreFailure | null;
  readonly goal: string;
  readonly template_id: TaskTemplateId;
  readonly pending_action: ActionApprovalView | null;
  readonly workspace_id: string | null;
  readonly pending_ask_prompt: string | null;
  readonly pending_field_value_request: string | null;
  readonly allowed_controls: ReadonlyArray<TaskControlKind>;
  readonly artifacts: ReadonlyArray<TaskArtifactView>;
  readonly activity: ReadonlyArray<TaskActivityView>;
}

export interface ActionApprovalView {
  readonly action_id: string;
  readonly host: string | null;
  readonly item_count: number;
  readonly summary_message_key: string;
}

export interface StoredCredentialView {
  readonly auth_method: ProviderAuthMethodView;
  readonly state: ProviderCredentialStateView;
  readonly subscription_backed: boolean;
  readonly account_label: string | null;
  readonly plan_label: string | null;
}

export interface ThinkingPreferenceView {
  readonly level: ThinkingLevelView;
}

export interface ProviderRefusalStateView {
  readonly refusal: ProviderRefusalView;
  readonly at_monotonic_ms: bigint;
}

export interface ProviderPresentationView {
  readonly key_prefix: string | null;
  readonly get_key_url: string | null;
  readonly docs_url: string | null;
}

export interface ProviderModelView {
  readonly provider_id: string;
  readonly model_id: string;
  readonly display_name: string;
  readonly context_window: bigint;
  readonly max_output_tokens: bigint;
  readonly reasoning: boolean;
  readonly tool_calling: boolean;
  readonly roles: ReadonlyArray<ModelRoleView>;
  readonly input_modalities: ReadonlyArray<InputModalityView>;
  readonly thinking_levels: ReadonlyArray<ThinkingLevelView>;
}

export interface ProviderRosterEntry {
  readonly provider_id: string;
  readonly display_name: string;
  readonly origin: ProviderOriginView;
  readonly auth_methods: ReadonlyArray<ProviderAuthMethodView>;
  readonly stored: StoredCredentialView | null;
  readonly signing_in: boolean;
  readonly enabled: boolean;
  readonly endpoint_host: string | null;
  readonly configurable: boolean;
  readonly endpoint_changed: boolean;
  readonly catalog_layer: CatalogLayerView;
  readonly selected_model_id: string | null;
  readonly thinking: ThinkingPreferenceView | null;
  readonly presentation: ProviderPresentationView | null;
  readonly endpoint_base: string | null;
  readonly last_refusal: ProviderRefusalStateView | null;
  readonly model_count: number;
  readonly subscription: boolean;
  readonly refused_endpoint_host: string | null;
}

export interface SetProviderCredentialStateBody {
  readonly provider_id: string;
  readonly state: ProviderCredentialStateView;
}

export interface ProbeProviderKeyBody {
  readonly provider_id: string;
  readonly credential_handle: string;
}

export interface ProviderProbeView {
  readonly provider_id: string;
  readonly verdict: ProviderProbeVerdictView;
  readonly at_monotonic_ms: bigint;
  readonly endpoint: ProbeEndpointView | null;
}

export interface ProbeEndpointView {
  readonly server_kind: ServerKindView;
  readonly model_count: number;
  readonly models: ReadonlyArray<CustomModelSpecView>;
  readonly proved_base: string | null;
}

export interface DetectedServerView {
  readonly server_kind: ServerKindView;
}

export interface SavedSignInView {
  readonly id: string;
  readonly site: string;
  readonly username: string;
  readonly last_used_epoch_ms: bigint;
}

export interface SavedSignInsView {
  readonly availability: SavedDataAvailability;
  readonly revision: bigint;
  readonly records: ReadonlyArray<SavedSignInView>;
}

export interface SavedDetailView {
  readonly id: string;
  readonly given_name: string;
  readonly family_name: string;
  readonly email: string;
  readonly phone: string;
  readonly address: string;
  readonly postcode: string;
  readonly country: string;
}

export interface SavedDetailsView {
  readonly availability: SavedDataAvailability;
  readonly revision: bigint;
  readonly people: ReadonlyArray<SavedDetailView>;
}

export interface CoreStatus {
  readonly availability: CoreAvailability;
  readonly generation: bigint;
  readonly active_tasks: ReadonlyArray<TaskViewState>;
  readonly auth_state: AuthViewState | null;
  readonly workspaces: ReadonlyArray<WorkspaceViewState>;
  readonly workspace_export: WorkspaceExportView | null;
  readonly asset_delivery: AssetDeliveryView | null;
  readonly provider_roster: ReadonlyArray<ProviderRosterEntry>;
  readonly provider_probes: ReadonlyArray<ProviderProbeView>;
  readonly provider_models: ReadonlyArray<ProviderModelView>;
  readonly assistant_configuration: AssistantConfigurationView;
  readonly library: LibraryViewState;
  readonly library_export: LibraryExportView | null;
  readonly memory: MemoryViewState;
  readonly saved_sign_ins: SavedSignInsView;
  readonly saved_details: SavedDetailsView;
  readonly site_skills: ReadonlyArray<SiteSkillView>;
  readonly builtin_skills: ReadonlyArray<BuiltinSkillView>;
  readonly projection_mode: CoreStatusProjectionMode;
  readonly projection_omissions: ReadonlyArray<CoreStatusProjectionOmission>;
}

export interface WorkspaceViewState {
  readonly workspace_id: string;
  readonly revision: bigint;
  readonly goal: string;
  readonly phase: WorkspacePhase;
  readonly last_updated_epoch_ms: bigint;
  readonly template_id: TaskTemplateId;
  readonly sources: ReadonlyArray<WorkspaceSourceView>;
  readonly facts: ReadonlyArray<WorkspaceFactView>;
  readonly saved: boolean;
  readonly display_name: string;
  readonly deletion_preview: WorkspaceDeletionPreviewView | null;
}

export interface WorkspaceDeletionPreviewView {
  readonly sources: number;
  readonly facts: number;
  readonly artifact_metadata: number;
  readonly derived_indexes: number;
  readonly confirmation_token: string;
}

export interface WorkspaceSourceView {
  readonly source_id: string;
  readonly title: string;
  readonly host: string;
  readonly read_at_epoch_ms: bigint;
  readonly fact_count: number;
  readonly excluded: boolean;
}

export interface WorkspaceFactView {
  readonly fact_id: string;
  readonly field: string;
  readonly value: string;
  readonly kind: WorkspaceFactKind;
  readonly sources: ReadonlyArray<string>;
  readonly correction: string | null;
  readonly has_conflict: boolean;
  readonly needs_new_source: boolean;
}

export interface WorkspaceExportView {
  readonly request_id: string;
  readonly workspace_id: string;
  readonly revision: bigint;
  readonly format: WorkspaceExportFormat;
  readonly content: string;
}

export interface LibrarySourceView {
  readonly source_id: string;
  readonly title: string;
  readonly host: string;
  readonly observed_at_epoch_ms: bigint;
}

export interface LibraryEntryView {
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
  readonly kind: WorkspaceFactKind;
  readonly sources: ReadonlyArray<LibrarySourceView>;
  readonly captured_at_epoch_ms: bigint;
  readonly last_checked_epoch_ms: bigint;
  readonly has_conflict: boolean;
}

export interface LibrarySearchHitView {
  readonly entry_id: string;
  readonly age_ms: bigint;
}

export interface LibrarySearchView {
  readonly request_id: string;
  readonly query: string;
  readonly library_revision: bigint;
  readonly hits: ReadonlyArray<LibrarySearchHitView>;
}

export interface LibraryRefreshSourceView {
  readonly source_id: string;
  readonly title: string;
  readonly host: string;
}

export interface LibraryRefreshPreviewView {
  readonly preview_id: string;
  readonly collection_id: string;
  readonly library_revision: bigint;
  readonly source_workspace_revision: bigint;
  readonly provider_route: TaskProviderRoute;
  readonly navigation_count: number;
  readonly observation_count: number;
  readonly sources: ReadonlyArray<LibraryRefreshSourceView>;
}

export interface LibraryRefreshResultItemView {
  readonly source_id: string;
  readonly disposition: LibraryRefreshDisposition;
}

export interface LibraryRefreshResultView {
  readonly preview_id: string;
  readonly collection_id: string;
  readonly items: ReadonlyArray<LibraryRefreshResultItemView>;
}

export interface LibraryViewState {
  readonly availability: LibraryAvailability;
  readonly revision: bigint;
  readonly entries: ReadonlyArray<LibraryEntryView>;
  readonly search: LibrarySearchView | null;
  readonly refresh_previews: ReadonlyArray<LibraryRefreshPreviewView>;
  readonly refresh_results: ReadonlyArray<LibraryRefreshResultView>;
}

export interface LibraryExportView {
  readonly request_id: string;
  readonly library_revision: bigint;
  readonly collection_id: string | null;
  readonly format: WorkspaceExportFormat;
  readonly content: string;
}

export interface MemoryWorkspaceView {
  readonly workspace_id: string;
  readonly display_name: string;
}

export interface MemoryRecordView {
  readonly memory_id: string;
  readonly revision: bigint;
  readonly statement: string;
  readonly source_kind: MemorySourceKind;
  readonly source_task_id: string | null;
  readonly source_workspace: MemoryWorkspaceView | null;
  readonly scope_kind: MemoryScopeKind;
  readonly scope_workspace: MemoryWorkspaceView | null;
  readonly sensitivity: MemorySensitivity;
  readonly created_at_epoch_ms: bigint;
  readonly updated_at_epoch_ms: bigint;
  readonly reviewed_at_epoch_ms: bigint;
  readonly expires_at_epoch_ms: bigint;
}

export interface MemorySearchHitView {
  readonly memory_id: string;
}

export interface MemorySearchView {
  readonly request_id: string;
  readonly query: string;
  readonly memory_revision: bigint;
  readonly hits: ReadonlyArray<MemorySearchHitView>;
}

export interface MemoryViewState {
  readonly availability: MemoryAvailability;
  readonly revision: bigint;
  readonly records: ReadonlyArray<MemoryRecordView>;
  readonly search: MemorySearchView | null;
}

export interface CorrectWorkspaceFactBody {
  readonly workspace_id: string;
  readonly expected_revision: bigint;
  readonly fact_id: string;
  readonly value: string;
}

export interface ExcludeWorkspaceSourceBody {
  readonly workspace_id: string;
  readonly expected_revision: bigint;
  readonly source_id: string;
}

export interface RequestWorkspaceExportBody {
  readonly request_id: string;
  readonly workspace_id: string;
  readonly expected_revision: bigint;
  readonly format: WorkspaceExportFormat;
}

export interface SaveWorkspaceBody {
  readonly workspace_id: string;
  readonly expected_revision: bigint;
}

export interface RenameWorkspaceBody {
  readonly workspace_id: string;
  readonly expected_revision: bigint;
  readonly display_name: string;
}

export interface DeleteWorkspaceBody {
  readonly workspace_id: string;
  readonly expected_revision: bigint;
  readonly confirmation_token: string;
}

export interface DiscardWorkspaceBody {
  readonly workspace_id: string;
  readonly expected_revision: bigint;
}

export interface SearchLibraryBody {
  readonly request_id: string;
  readonly query: string;
  readonly limit: number;
  readonly requested_at_epoch_ms: bigint;
}

export interface SaveLibraryFactBody {
  readonly workspace_id: string;
  readonly expected_workspace_revision: bigint;
  readonly fact_id: string;
  readonly expected_library_revision: bigint;
  readonly expected_entry_revision: bigint;
  readonly approved_at_epoch_ms: bigint;
}

export interface RemoveLibraryEntryBody {
  readonly entry_id: string;
  readonly expected_library_revision: bigint;
  readonly expected_entry_revision: bigint;
  readonly removed_at_epoch_ms: bigint;
}

export interface RequestLibraryExportBody {
  readonly request_id: string;
  readonly expected_library_revision: bigint;
  readonly collection_id: string | null;
  readonly format: WorkspaceExportFormat;
}

export interface SearchMemoryBody {
  readonly request_id: string;
  readonly query: string;
  readonly limit: number;
  readonly requested_at_epoch_ms: bigint;
}

export interface UpsertMemoryBody {
  readonly memory_id: string | null;
  readonly statement: string;
  readonly scope_kind: MemoryScopeKind;
  readonly scope_workspace: MemoryWorkspaceView | null;
  readonly sensitivity: MemorySensitivity;
  readonly expected_memory_revision: bigint;
  readonly expected_record_revision: bigint;
  readonly expires_at_epoch_ms: bigint;
  readonly approved_at_epoch_ms: bigint;
}

export interface DeleteMemoryBody {
  readonly memory_id: string;
  readonly expected_memory_revision: bigint;
  readonly expected_record_revision: bigint;
  readonly deleted_at_epoch_ms: bigint;
}

export interface AcceptTaskArtifactBody {
  readonly task_id: string;
  readonly artifact_id: string;
}

export interface RequestTaskArtifactExportBody {
  readonly request_id: string;
  readonly task_id: string;
  readonly artifact_id: string;
  readonly kind: TaskArtifactKind;
}

export interface StartAuthBody {
  readonly provider: AuthProvider;
}

export interface EmailLinkBody {
  readonly email: string;
}

export interface SignOutBody {
  readonly account_id: string | null;
}

export interface AuthCredentialResultBody {
  readonly provider: AuthProvider;
  readonly credential_handle: string | null;
  readonly status: AuthCredentialStatus;
  readonly flow_id: string;
}

export interface AuthAccountView {
  readonly account_id: string;
  readonly display_name: string | null;
  readonly email: string | null;
  readonly method: AuthProvider;
}

export interface AuthFailure {
  readonly code: AuthFailureCode;
  readonly retryable: boolean;
}

export interface AuthMethodView {
  readonly provider: AuthProvider;
  readonly availability: AuthMethodAvailability;
}

export interface EntitlementView {
  readonly plan_id: string;
  readonly credits_granted: bigint;
  readonly credits_remaining: bigint;
  readonly next_renewal_epoch_seconds: bigint;
  readonly valid_until_epoch_seconds: bigint;
}

export interface AuthViewState {
  readonly phase: AuthPhase;
  readonly account: AuthAccountView | null;
  readonly pending_email: string | null;
  readonly failure: AuthFailure | null;
  readonly methods: ReadonlyArray<AuthMethodView>;
  readonly entitlement: EntitlementView | null;
}

export interface PageInspectorDocumentView {
  readonly document_id: string;
  readonly kind: PageInspectorDocumentKind;
  readonly host: string;
  readonly claims: ReadonlyArray<PageInspectorClaim>;
}

export interface PageInspectorDocumentsView {
  readonly availability: PageInspectorAvailability;
  readonly documents: ReadonlyArray<PageInspectorDocumentView>;
}

export interface PageInspectorAdapterView {
  readonly kind: PageInspectorAdapterKind;
  readonly status: PageInspectorAdapterStatus;
  readonly version: number;
}

export interface PageInspectorNodeView {
  readonly display_id: string;
  readonly role: PageInspectorNodeRole;
  readonly name: string | null;
  readonly sensitivity: PageInspectorSensitivity;
  readonly text_run_count: number;
  readonly text_byte_count: bigint;
  readonly value_present: boolean;
  readonly value_withheld: boolean;
}

export interface PageInspectorEdgeView {
  readonly from_display_id: string;
  readonly to_display_id: string;
  readonly relationship: PageInspectorRelationship;
  readonly inferred: boolean;
}

export interface PageInspectorFrameView {
  readonly main_frame: boolean;
  readonly out_of_process: boolean;
  readonly cross_origin: boolean;
  readonly included: boolean;
}

export interface PageInspectorTruncationView {
  readonly truncated: boolean;
  readonly budgets_reached: ReadonlyArray<PageInspectorBudgetKind>;
  readonly omitted_node_count: number;
  readonly omitted_text_bytes: number;
  readonly omitted_frame_count: number;
  readonly may_change_answer: boolean;
}

export interface PageInspectorRedactionView {
  readonly redacted_field_count: number;
  readonly suppressed_secret_count: number;
  readonly sensitive_zone_count: number;
  readonly filtered_frame_count: number;
}

export interface PageInspectorSnapshotView {
  readonly document_id: string;
  readonly document_revision: bigint;
  readonly host: string;
  readonly secure_context: boolean;
  readonly private_profile: boolean;
  readonly document_state: PageInspectorDocumentState;
  readonly adapters: ReadonlyArray<PageInspectorAdapterView>;
  readonly nodes: ReadonlyArray<PageInspectorNodeView>;
  readonly edges: ReadonlyArray<PageInspectorEdgeView>;
  readonly frames: ReadonlyArray<PageInspectorFrameView>;
  readonly truncation: PageInspectorTruncationView;
  readonly redaction: PageInspectorRedactionView;
  readonly warnings: ReadonlyArray<PageInspectorWarningCode>;
  readonly site_skill_offer_availability: SiteSkillOfferAvailability;
  readonly site_skill_offers: ReadonlyArray<SiteSkillOfferView>;
}

export interface PageInspectorSnapshotResult {
  readonly availability: PageInspectorAvailability;
  readonly snapshot: PageInspectorSnapshotView | null;
}

export function isValidPageInspectorSnapshotResultPresence(value: PageInspectorSnapshotResult): boolean {
  return ((value.availability === PageInspectorAvailability.Available) ? value.snapshot !== null : value.snapshot === null);
}

export interface PageSnapshotExportView {
  readonly request_id: string;
  readonly document_id: string;
  readonly document_revision: bigint;
  readonly origin: string;
  readonly format: PageSnapshotExportFormat;
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

export interface PageSnapshotExportResult {
  readonly availability: PageSnapshotExportAvailability;
  readonly snapshot_export: PageSnapshotExportView | null;
}

export interface PermissionResultBody {
  readonly request_id: string;
  readonly permission: PlatformPermission;
  readonly decision: PermissionDecision;
}

export interface AssetRefusal {
  readonly reason: AssetRefusalView;
  readonly retryable: boolean;
}

export interface AssetViewState {
  readonly asset_id: string;
  readonly asset_revision: string;
  readonly kind: AssetKindView;
  readonly presence: AssetPresenceView;
  readonly written_bytes: bigint;
  readonly total_bytes: bigint;
  readonly attempts: number;
  readonly refusal: AssetRefusal | null;
  readonly waiting_until_monotonic_ms: bigint;
}

export interface AssetDeliveryView {
  readonly platform_supported: boolean;
  readonly network_cost: AssetNetworkCostView;
  readonly metered_permitted: boolean;
  readonly assets: ReadonlyArray<AssetViewState>;
}

export interface RequestAssetBody {
  readonly asset_id: string;
  readonly asset_revision: string;
}

export interface RemoveAssetBody {
  readonly asset_id: string;
  readonly asset_revision: string;
}

export interface SetAssetPolicyBody {
  readonly network_cost: AssetNetworkCostView;
  readonly metered_permitted: boolean;
}

export interface SaveProviderCredentialBody {
  readonly provider_id: string;
  readonly auth_method: ProviderAuthMethodView;
  readonly credential_handle: string;
}

export interface ForgetProviderCredentialBody {
  readonly provider_id: string;
}

export interface StartProviderAuthBody {
  readonly provider_id: string;
}

export interface CancelProviderAuthBody {
  readonly flow_id: string;
}

export interface StartLibraryRefreshBody {
  readonly preview_id: string;
  readonly collection_id: string;
  readonly expected_library_revision: bigint;
  readonly expected_workspace_revision: bigint;
  readonly source_count: number;
}

export interface CustomModelSpecView {
  readonly model_id: string;
  readonly display_name: string;
  readonly context_window: number;
  readonly max_output_tokens: number;
  readonly reasoning: boolean;
  readonly tool_calling: boolean;
}

export interface SaveCustomProviderBody {
  readonly provider_id: string;
  readonly display_name: string;
  readonly endpoint: string;
  readonly wire_api: ProviderWireApiView;
  readonly credential_handle: string | null;
  readonly models: ReadonlyArray<CustomModelSpecView>;
  readonly detected_server: DetectedServerView | null;
}

export interface ProbeCustomEndpointBody {
  readonly endpoint: string;
  readonly wire_api: ProviderWireApiView;
  readonly credential_handle: string | null;
  readonly provider_id: string;
}

export interface SetProviderModelPreferenceBody {
  readonly provider_id: string;
  readonly model_id: string | null;
  readonly thinking: ThinkingPreferenceView | null;
}

export interface RequestComposerCompletionBody {
  readonly request_id: string;
  readonly prefix: string;
  readonly suffix: string | null;
}

export interface CancelComposerCompletionBody {
  readonly request_id: string;
}

export interface RemoveCustomProviderBody {
  readonly provider_id: string;
}

export interface SavedFlowQueryResult {
  readonly request_id: string;
  readonly service_generation: bigint;
  readonly availability: SavedFlowQueryAvailability;
  readonly flows: ReadonlyArray<SiteSkillView>;
}
