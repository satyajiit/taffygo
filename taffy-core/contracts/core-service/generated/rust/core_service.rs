// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Generated from schema/contract.json. Do not edit.
// Contract core_service 2.113.

#![allow(
    clippy::module_name_repetitions,
    clippy::needless_question_mark,
    clippy::struct_excessive_bools,
    clippy::too_many_lines,
    clippy::trivially_copy_pass_by_ref
)]

pub const MAX_SAVED_FLOW_QUERY_RESULTS: usize = 4;
pub const MAX_COMMAND_BYTES: usize = 262_144;
pub const MAX_EFFECT_BYTES: usize = 1_048_576;
pub const MAX_IN_FLIGHT_PER_PROFILE: usize = 64;
pub const MAX_QUEUED_BYTES_PER_PROFILE: usize = 4_194_304;
pub const MAX_OPERATION_ID_BYTES: usize = 128;
pub const MAX_IDEMPOTENCY_KEY_BYTES: usize = 256;
pub const MAX_IDENTIFIER_BYTES: usize = 256;
pub const MAX_USER_INPUT_ANSWER_BYTES: usize = 512;
pub const MAX_AUTHORITY_SUBJECT_ID_BYTES: usize = 128;
pub const MAX_DIRECT_OBSERVATION_NODES: usize = 1_500;
pub const MAX_DIRECT_OBSERVATION_TEXT_BYTES: usize = 65_536;
pub const MAX_DIRECT_OBSERVATION_TOTAL_BYTES: usize = 1_048_576;
pub const MAX_DIRECT_OBSERVATION_FRAMES: usize = 1;
pub const MAX_DIRECT_OBSERVATION_DEADLINE_MS: usize = 1_500;
pub const MAX_DIRECT_OBSERVATION_LEASE_MS: usize = 2_000;
pub const MAX_PAGE_SNAPSHOT_EXPORT_BYTES: usize = 262_144;
pub const MAX_TASK_ARTIFACT_EXPORT_BYTES: usize = 262_144;
pub const MAX_ACCOUNT_EMAIL_BYTES: usize = 320;
pub const MAX_ACCOUNT_DISPLAY_NAME_BYTES: usize = 128;
pub const MAX_ACCOUNT_SCOPES: usize = 3;
pub const MAX_ACCOUNT_RESPONSE_BYTES: usize = 65_536;
pub const MAX_ACCOUNT_ACCESS_TOKEN_BYTES: usize = 16_384;
pub const MAX_ACCOUNT_REFRESH_TOKEN_BYTES: usize = 16_384;
pub const MAX_ACCOUNT_ID_TOKEN_BYTES: usize = 16_384;
pub const MAX_ACCOUNT_LINKED_PROVIDERS: usize = 4;
pub const MAX_ENTITLED_MODELS: usize = 64;
pub const MAX_ACCOUNT_SESSION_LIFETIME_SECONDS: usize = 31_536_000;
pub const MAX_PENDING_ACCOUNT_FLOWS: usize = 8;
pub const GOOGLE_NONCE_ENTROPY_BYTES: usize = 32;
pub const GOOGLE_NONCE_HASH_HEX_BYTES: usize = 64;
pub const MAX_GOOGLE_RAW_NONCE_BYTES: usize = 43;
pub const MAX_GOAL_BYTES: usize = 65_536;
pub const MAX_TASK_CONSENT_SOURCES: usize = 64;
pub const MAX_NORMALIZED_ORIGIN_BYTES: usize = 2_048;
pub const MAX_LIBRARY_REFRESH_SOURCES: usize = 64;
pub const MAX_SOURCE_LOCATOR_BYTES: usize = 4_096;
pub const MAX_SOURCE_TITLE_BYTES: usize = 1_024;
pub const MAX_SOURCE_HOST_BYTES: usize = 253;
pub const MAX_LIBRARY_REFRESH_PREVIEW_ID_BYTES: usize = 64;
pub const MAX_DESTINATION_ADDRESS_BYTES: usize = 4_096;
pub const MAX_CANONICAL_ACTION_INTENT_BYTES: usize = 16_384;
pub const MAX_TRANSIENT_SEARCH_QUERY_BYTES: usize = 4_096;
pub const MAX_NEW_SOURCE_CAP: usize = 64;
pub const MAX_WORKSPACES_PER_PROFILE: usize = 32;
pub const MAX_WORKSPACE_SNAPSHOT_BYTES: usize = 262_144;
pub const MAX_WORKSPACE_BOOTSTRAP_BYTES: usize = 4_194_304;
pub const MAX_WORKSPACE_VALUE_BYTES: usize = 16_384;
pub const MAX_WORKSPACE_DISPLAY_NAME_BYTES: usize = 256;
pub const MAX_WORKSPACE_CONFIRMATION_TOKEN_BYTES: usize = 64;
pub const MAX_LIBRARY_ENTRIES: usize = 1_024;
pub const MAX_LIBRARY_SOURCES: usize = 16;
pub const MAX_LIBRARY_QUERY_BYTES: usize = 512;
pub const MAX_LIBRARY_SEARCH_TERMS: usize = 16;
pub const MAX_LIBRARY_SEARCH_RESULTS: usize = 32;
pub const MAX_MEMORY_RECORDS: usize = 512;
pub const MAX_MEMORY_STATEMENT_BYTES: usize = 2_048;
pub const MAX_MEMORY_QUERY_BYTES: usize = 512;
pub const MAX_MEMORY_SEARCH_TERMS: usize = 16;
pub const MAX_MEMORY_SEARCH_RESULTS: usize = 32;
pub const MAX_ASSET_PATH_BYTES: usize = 256;
pub const MAX_ASSET_TRANSFER_BYTES: usize = 4_294_967_296;
pub const MAX_TOOL_ALLOWLIST_ENTRIES: usize = 128;
pub const MAX_TASK_BUDGET_ENTRIES: usize = 10;
pub const MAX_PENDING_APPROVALS_PER_PROFILE: usize = 64;
pub const MAX_PENDING_PERMISSIONS_PER_PROFILE: usize = 64;
pub const MAX_PENDING_TASK_POLICY_PER_PROFILE: usize = 64;
pub const MAX_TASK_EFFECTS_PER_STATE: usize = 64;
pub const MAX_TASK_TAB_RESULTS: usize = 16;
pub const MAX_TASK_DOWNLOAD_RESULTS: usize = 16;
pub const MAX_TASK_STORE_RESULTS: usize = 32;
pub const MAX_TASK_STORE_ROW_FIELD_BYTES: usize = 512;
pub const MAX_TASK_STORE_QUERY_BYTES: usize = 512;
pub const MAX_TASK_SUPPLIED_VALUES: usize = 8;
pub const MAX_TASK_ACTION_PRECONDITIONS: usize = 4;
pub const MAX_TASK_OBSERVATION_NODES: usize = 1_500;
pub const MAX_TASK_OBSERVATION_TEXT_BYTES: usize = 65_536;
pub const MAX_TASK_OBSERVATION_TOTAL_BYTES: usize = 1_048_576;
pub const MAX_TASK_OBSERVATION_FRAMES: usize = 1;
pub const MAX_TASK_OBSERVATION_DEADLINE_MS: usize = 1_500;
pub const MAX_MEDIA_FACTS: usize = 256;
pub const MAX_MEDIA_FACT_TEXT_BYTES: usize = 8_192;
pub const MAX_MEDIA_FACT_LOCATOR_BYTES: usize = 512;
pub const MAX_MEDIA_FACT_TOTAL_BYTES: usize = 131_072;
pub const MAX_MEDIA_ATTACHMENT_HANDLE_BYTES: usize = 128;
pub const MAX_MEDIA_ATTACHMENT_BYTES: usize = 4_194_304;
pub const MAX_MEDIA_DIMENSION_PX: usize = 4_096;
pub const MAX_MEDIA_PDF_PAGES: usize = 256;
pub const MAX_TASK_REVISIONS_PER_PROFILE: usize = 64;
pub const MAX_TASK_CONTROLS: usize = 4;
pub const MAX_TOOL_JOB_INPUT_BYTES: usize = 16_777_216;
pub const MAX_TOOL_JOB_OUTPUT_BYTES: usize = 16_777_216;
pub const MAX_TOOL_OUTPUT_CHUNK_BYTES: usize = 65_536;
pub const MAX_TOOL_OUTPUT_CHUNKS: usize = 256;
pub const MAX_TOOL_PROGRESS_EVENTS: usize = 256;
pub const MAX_TOOL_JOB_ID_BYTES: usize = 128;
pub const MAX_TOOL_OPERATION_ID_BYTES: usize = 128;
pub const MAX_TOOL_IDEMPOTENCY_KEY_BYTES: usize = 128;
pub const MAX_TOOL_ID_BYTES: usize = 128;
pub const MAX_TOOL_VERSION_BYTES: usize = 64;
pub const MAX_TOOL_CONVERSATION_ID_BYTES: usize = 128;
pub const MAX_TOOL_ENTRYPOINT_ID_BYTES: usize = 128;
pub const MAX_TOOL_HANDLE_ID_BYTES: usize = 256;
pub const MAX_TOOL_PRESET_ID_BYTES: usize = 128;
pub const MAX_TOOL_JOB_MEMORY_BYTES: usize = 4_294_967_296;
pub const MAX_TOOL_JOB_CPU_MS: usize = 600_000;
pub const MAX_TOOL_JOB_TEMPORARY_BYTES: usize = 1_073_741_824;
pub const MAX_TOOL_JOB_WALL_TIME_MS: usize = 600_000;
pub const MAX_TOOL_INLINE_PAYLOAD_BYTES: usize = 262_144;
pub const MAX_TOOL_STREAM_OUTPUT_CHUNKS: usize = 65_536;
pub const MAX_TOOL_STREAM_CHUNK_BYTES: usize = 4_096;
pub const MAX_TOOL_MODEL_ID_BYTES: usize = 128;
pub const MAX_TOOL_MODEL_REVISION_BYTES: usize = 64;
pub const MAX_TOOL_MODEL_ARTIFACT_BYTES: usize = 2_147_483_648;
pub const MAX_REGISTERED_MODEL_ARTIFACTS: usize = 64;
pub const MAX_TOOL_EMBEDDING_DIMENSIONS: usize = 4_096;
pub const MAX_TOOL_EMBEDDING_VALUE_BYTES: usize = 16_384;
pub const MAX_PROVIDER_ID_BYTES: usize = 64;
pub const MAX_PROVIDER_DISPLAY_NAME_BYTES: usize = 128;
pub const MAX_PROVIDER_ENDPOINT_BYTES: usize = 512;
pub const MAX_CUSTOM_PROVIDERS: usize = 32;
pub const MAX_MODEL_ID_BYTES: usize = 128;
pub const MAX_AVAILABLE_MODEL_IDS: usize = 64;
pub const MAX_MODEL_STATIC_HEADERS: usize = 8;
pub const MAX_MODEL_HEADER_NAME_BYTES: usize = 64;
pub const MAX_MODEL_HEADER_VALUE_BYTES: usize = 1_024;
pub const MAX_MODEL_STREAM_CHUNK_BYTES: usize = 16_384;
pub const MAX_TASK_ANSWER_DELTA_BYTES: usize = 16_384;
pub const MAX_TASK_ANSWER_EVENTS_PER_BATCH: usize = 64;
pub const MAX_SKILLS_PER_PROFILE: usize = 64;
pub const MAX_SKILL_VERSIONS_PER_SKILL: usize = 16;
pub const MAX_SKILL_ID_BYTES: usize = 128;
pub const MAX_SKILL_DEFINITION_BYTES: usize = 65_536;
pub const MAX_SKILL_STEPS: usize = 32;
pub const MAX_SKILL_MATCH_CLAUSES: usize = 8;
pub const MAX_SKILL_ARGUMENTS_PER_STEP: usize = 16;
pub const MAX_SKILL_RECALL_ENTRIES: usize = 32;
pub const MAX_CUSTOM_MODEL_ENTRIES: usize = 32;
pub const MAX_MODEL_DISPLAY_NAME_BYTES: usize = 128;
pub const MAX_COMPOSER_PREFIX_BYTES: usize = 4_096;
pub const MAX_COMPOSER_SUFFIX_BYTES: usize = 1_024;
pub const MAX_COMPOSER_COMPLETION_BYTES: usize = 512;
pub const MAX_PROVIDER_LISTING_BYTES: usize = 262_144;
pub const MAX_ASSISTANT_ABILITIES: usize = 16;
pub const MAX_PERSONALITY_SCALE: usize = 2;
pub const MAX_SAVED_SIGN_INS: usize = 256;
pub const MAX_SAVED_DETAILS: usize = 64;
pub const MAX_SAVED_SIGN_IN_SITE_BYTES: usize = 253;
pub const MAX_SAVED_SIGN_IN_USERNAME_BYTES: usize = 320;
pub const MAX_SAVED_DETAIL_NAME_BYTES: usize = 256;
pub const MAX_SAVED_DETAIL_EMAIL_BYTES: usize = 320;
pub const MAX_SAVED_DETAIL_PHONE_BYTES: usize = 128;
pub const MAX_SAVED_DETAIL_ADDRESS_BYTES: usize = 2_048;
pub const MAX_SAVED_DETAIL_POSTCODE_BYTES: usize = 64;
pub const MAX_SAVED_DETAIL_COUNTRY_BYTES: usize = 128;
pub const MAX_BACKUP_RECORDS: usize = 100_000;
pub const MAX_BACKUP_RECORD_BYTES: usize = 67_108_864;
pub const MAX_BACKUP_PLAINTEXT_BYTES: usize = 8_589_934_592;
pub const MAX_BACKUP_MANIFEST_BYTES: usize = 67_108_864;
pub const MAX_BACKUP_ID_BYTES: usize = 160;
pub const MAX_BACKUP_TIMESTAMP_BYTES: usize = 40;
pub const MAX_BACKUP_SELECTION_KINDS: usize = 8;
pub const MAX_BACKUP_RESTORE_RECOVERY_RECORDS: usize = 128;
pub const MAX_BACKUP_SEALED_CHUNKS: usize = 8_193;
pub const BACKUP_PAYLOAD_CHUNK_BYTES: usize = 1_048_576;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum CoreServiceCommandKind {
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

impl CoreServiceCommandKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::StartTask),
            1 => Some(Self::CancelTask),
            2 => Some(Self::UserDecision),
            3 => Some(Self::AuthCallback),
            4 => Some(Self::PermissionResult),
            5 => Some(Self::StartAuth),
            6 => Some(Self::RequestEmailLink),
            7 => Some(Self::SignOut),
            8 => Some(Self::AuthCredentialResult),
            9 => Some(Self::CorrectWorkspaceFact),
            10 => Some(Self::ExcludeWorkspaceSource),
            11 => Some(Self::RequestWorkspaceExport),
            12 => Some(Self::SetAssetDeliveryPolicy),
            13 => Some(Self::RequestAsset),
            14 => Some(Self::RemoveAsset),
            15 => Some(Self::SaveProviderCredential),
            16 => Some(Self::ForgetProviderCredential),
            17 => Some(Self::StartProviderAuth),
            18 => Some(Self::ProviderAuthCallback),
            19 => Some(Self::SaveCustomProvider),
            20 => Some(Self::RemoveCustomProvider),
            21 => Some(Self::CompleteHandover),
            22 => Some(Self::ExpireHandover),
            23 => Some(Self::SupplyUserInput),
            24 => Some(Self::SetProviderCredentialState),
            25 => Some(Self::ProbeProviderCredential),
            26 => Some(Self::SupplyFieldValues),
            27 => Some(Self::SetProviderModelPreference),
            28 => Some(Self::ProbeCustomEndpoint),
            29 => Some(Self::RequestComposerCompletion),
            30 => Some(Self::CancelComposerCompletion),
            31 => Some(Self::PauseTask),
            32 => Some(Self::ResumeTask),
            33 => Some(Self::TakeOver),
            34 => Some(Self::SetAssistantConfiguration),
            35 => Some(Self::SaveWorkspace),
            36 => Some(Self::RenameWorkspace),
            37 => Some(Self::DeleteWorkspace),
            38 => Some(Self::DiscardWorkspace),
            39 => Some(Self::SearchLibrary),
            40 => Some(Self::SaveLibraryFact),
            41 => Some(Self::RemoveLibraryEntry),
            42 => Some(Self::RequestLibraryExport),
            43 => Some(Self::SearchMemory),
            44 => Some(Self::UpsertMemory),
            45 => Some(Self::DeleteMemory),
            46 => Some(Self::AcceptTaskArtifact),
            47 => Some(Self::ExportTaskArtifact),
            48 => Some(Self::ReplaceSavedDataSnapshot),
            49 => Some(Self::MutateSkill),
            50 => Some(Self::CancelProviderAuth),
            51 => Some(Self::FollowUp),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum SavedDataAvailability {
    Loading = 0,
    Ready = 1,
    Unavailable = 2,
}

impl SavedDataAvailability {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Loading),
            1 => Some(Self::Ready),
            2 => Some(Self::Unavailable),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum TaskControlKind {
    Pause = 0,
    Resume = 1,
    TakeOver = 2,
    Stop = 3,
}

impl TaskControlKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Pause),
            1 => Some(Self::Resume),
            2 => Some(Self::TakeOver),
            3 => Some(Self::Stop),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum EffectKind {
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

impl EffectKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::StorageCommit),
            1 => Some(Self::PageObservation),
            2 => Some(Self::ModelRequest),
            3 => Some(Self::NetworkRequest),
            4 => Some(Self::BrowserAction),
            5 => Some(Self::ToolJob),
            6 => Some(Self::SecureStore),
            7 => Some(Self::OpenAuthSurface),
            8 => Some(Self::RequestPermission),
            9 => Some(Self::DeliverAsset),
            10 => Some(Self::FetchCatalog),
            11 => Some(Self::FetchProviderListing),
            12 => Some(Self::DeliverComposerCompletion),
            13 => Some(Self::ProbeCustomEndpoint),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum CatalogNetworkOperation {
    FetchPublishedCatalog = 0,
}

impl CatalogNetworkOperation {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::FetchPublishedCatalog),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum CatalogFetchDisposition {
    Success = 0,
    NotModified = 1,
    Unavailable = 2,
    Oversized = 3,
    MalformedTransport = 4,
}

impl CatalogFetchDisposition {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Success),
            1 => Some(Self::NotModified),
            2 => Some(Self::Unavailable),
            3 => Some(Self::Oversized),
            4 => Some(Self::MalformedTransport),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum RetryClass {
    Idempotent = 0,
    Consequential = 1,
    Never = 2,
}

impl RetryClass {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Idempotent),
            1 => Some(Self::Consequential),
            2 => Some(Self::Never),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum EffectStatus {
    Completed = 0,
    Denied = 1,
    Cancelled = 2,
    DeadlineExceeded = 3,
    ResourceLimit = 4,
    Unavailable = 5,
    OutcomeUnknown = 6,
    InvalidResult = 7,
}

impl EffectStatus {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Completed),
            1 => Some(Self::Denied),
            2 => Some(Self::Cancelled),
            3 => Some(Self::DeadlineExceeded),
            4 => Some(Self::ResourceLimit),
            5 => Some(Self::Unavailable),
            6 => Some(Self::OutcomeUnknown),
            7 => Some(Self::InvalidResult),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum DisclosureClass {
    ContentFree = 0,
    AccountMetadata = 1,
    UserSelectedContent = 2,
    PageContent = 3,
}

impl DisclosureClass {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::ContentFree),
            1 => Some(Self::AccountMetadata),
            2 => Some(Self::UserSelectedContent),
            3 => Some(Self::PageContent),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum InitializationStatus {
    Ready = 0,
    InvalidBootstrap = 1,
    IncompatibleVersion = 2,
    ResourceLimit = 3,
}

impl InitializationStatus {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Ready),
            1 => Some(Self::InvalidBootstrap),
            2 => Some(Self::IncompatibleVersion),
            3 => Some(Self::ResourceLimit),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum AdmissionStatus {
    Accepted = 0,
    StaleGeneration = 1,
    StaleRevision = 2,
    DeadlineExceeded = 3,
    Backpressure = 4,
    InvalidCommand = 5,
    CoreUnavailable = 6,
    Duplicate = 7,
}

impl AdmissionStatus {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Accepted),
            1 => Some(Self::StaleGeneration),
            2 => Some(Self::StaleRevision),
            3 => Some(Self::DeadlineExceeded),
            4 => Some(Self::Backpressure),
            5 => Some(Self::InvalidCommand),
            6 => Some(Self::CoreUnavailable),
            7 => Some(Self::Duplicate),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum TaskKind {
    Research = 0,
    Errand = 1,
}

impl TaskKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Research),
            1 => Some(Self::Errand),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum TaskTemplateId {
    CompareProducts = 0,
    SummarizeEvidence = 1,
    BuildSourceTable = 2,
    WebErrand = 3,
}

impl TaskTemplateId {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::CompareProducts),
            1 => Some(Self::SummarizeEvidence),
            2 => Some(Self::BuildSourceTable),
            3 => Some(Self::WebErrand),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum BuiltinSkillId {
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

impl BuiltinSkillId {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::GeneralWebResearch),
            1 => Some(Self::DeepResearch),
            2 => Some(Self::ProductComparison),
            3 => Some(Self::MultiTabComparison),
            4 => Some(Self::WebsiteSummarizer),
            5 => Some(Self::PdfAnalysis),
            6 => Some(Self::DataExtraction),
            7 => Some(Self::FormAssistant),
            8 => Some(Self::Shopping),
            9 => Some(Self::DownloadOrganizer),
            10 => Some(Self::TravelResearch),
            11 => Some(Self::VideoTranscriptAnalyzer),
            12 => Some(Self::ImageUnderstanding),
            13 => Some(Self::LibraryBuilder),
            14 => Some(Self::SpreadsheetBuilder),
            15 => Some(Self::DocumentGenerator),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum TaskProviderRoute {
    NotConfigured = 0,
    DirectUserKey = 1,
    ManagedService = 2,
    NoModelRequired = 3,
}

impl TaskProviderRoute {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::NotConfigured),
            1 => Some(Self::DirectUserKey),
            2 => Some(Self::ManagedService),
            3 => Some(Self::NoModelRequired),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum TaskControlMode {
    User = 0,
    Shared = 1,
    Assistant = 2,
}

impl TaskControlMode {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::User),
            1 => Some(Self::Shared),
            2 => Some(Self::Assistant),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum TaskMilestone {
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

impl TaskMilestone {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::M0),
            1 => Some(Self::M1),
            2 => Some(Self::M2),
            3 => Some(Self::M3),
            4 => Some(Self::M4),
            5 => Some(Self::M5),
            6 => Some(Self::M6),
            7 => Some(Self::M7),
            8 => Some(Self::M8),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum TaskBudgetKind {
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

impl TaskBudgetKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::MaxSources),
            1 => Some(Self::MaxWorkingTabs),
            2 => Some(Self::MaxWallTimeMs),
            3 => Some(Self::MaxModelRequests),
            4 => Some(Self::MaxInputUnits),
            5 => Some(Self::MaxOutputUnits),
            6 => Some(Self::MaxCostUnits),
            7 => Some(Self::MaxNavigationDepth),
            8 => Some(Self::MaxRetriesPerStep),
            9 => Some(Self::MaxArtifactBytes),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum UserDecisionKind {
    Accept = 0,
    Deny = 1,
    Dismiss = 2,
}

impl UserDecisionKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Accept),
            1 => Some(Self::Deny),
            2 => Some(Self::Dismiss),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum CancelReason {
    User = 0,
    ProfileShutdown = 1,
    Deadline = 2,
}

impl CancelReason {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::User),
            1 => Some(Self::ProfileShutdown),
            2 => Some(Self::Deadline),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum TaskSettlementKind {
    Pause = 0,
    Cancel = 1,
}

impl TaskSettlementKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Pause),
            1 => Some(Self::Cancel),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum TerminalTaskKind {
    Completed = 0,
    Partial = 1,
    Failed = 2,
    Cancelled = 3,
}

impl TerminalTaskKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Completed),
            1 => Some(Self::Partial),
            2 => Some(Self::Failed),
            3 => Some(Self::Cancelled),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum TaskReducerEffectKind {
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

impl TaskReducerEffectKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::RevokeAuthority),
            1 => Some(Self::AskPolicy),
            2 => Some(Self::RequestApproval),
            3 => Some(Self::RequestPermission),
            4 => Some(Self::DispatchAction),
            5 => Some(Self::AwaitInFlightWork),
            6 => Some(Self::ReconcileAction),
            7 => Some(Self::ReleaseTaskTabs),
            8 => Some(Self::GenerateArtifact),
            9 => Some(Self::ExportArtifact),
            10 => Some(Self::CallModel),
            11 => Some(Self::AwaitHandover),
            12 => Some(Self::RunToolJob),
            13 => Some(Self::RequestFieldValues),
            14 => Some(Self::PrepareDiscoveryTab),
            15 => Some(Self::RunLibraryTool),
            16 => Some(Self::RunMemoryTool),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum TaskRevocationReason {
    UserTookOver = 0,
    DirectUserInput = 1,
    TaskCancelled = 2,
    PolicyRevoked = 3,
    TabClosed = 4,
}

impl TaskRevocationReason {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::UserTookOver),
            1 => Some(Self::DirectUserInput),
            2 => Some(Self::TaskCancelled),
            3 => Some(Self::PolicyRevoked),
            4 => Some(Self::TabClosed),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum TaskRecoveryRule {
    RetryWithinEpochAndBudget = 0,
    RetryAfterStateCheck = 1,
    ReconcileFirst = 2,
    NeverAutomatically = 3,
}

impl TaskRecoveryRule {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::RetryWithinEpochAndBudget),
            1 => Some(Self::RetryAfterStateCheck),
            2 => Some(Self::ReconcileFirst),
            3 => Some(Self::NeverAutomatically),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum TaskArtifactKind {
    Markdown = 0,
    Csv = 1,
    Xlsx = 2,
    Pdf = 3,
    Docx = 4,
    Pptx = 5,
    WaveAudio = 6,
    FrameArchive = 7,
}

impl TaskArtifactKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Markdown),
            1 => Some(Self::Csv),
            2 => Some(Self::Xlsx),
            3 => Some(Self::Pdf),
            4 => Some(Self::Docx),
            5 => Some(Self::Pptx),
            6 => Some(Self::WaveAudio),
            7 => Some(Self::FrameArchive),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum TaskActionPrecondition {
    DocumentUnchanged = 0,
    GraphRevisionAtLeast = 1,
    NodePresent = 2,
    DestinationUnchanged = 3,
}

impl TaskActionPrecondition {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::DocumentUnchanged),
            1 => Some(Self::GraphRevisionAtLeast),
            2 => Some(Self::NodePresent),
            3 => Some(Self::DestinationUnchanged),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum TaskActionPostcondition {
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

impl TaskActionPostcondition {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::ObservationCaptured),
            1 => Some(Self::DocumentNavigated),
            2 => Some(Self::NodeStateChanged),
            3 => Some(Self::DownloadStarted),
            4 => Some(Self::PlatformAcknowledged),
            5 => Some(Self::TaskTabsListed),
            6 => Some(Self::TaskTabActive),
            7 => Some(Self::TaskTabAbsent),
            8 => Some(Self::DownloadCancelled),
            9 => Some(Self::PageReloaded),
            10 => Some(Self::LoadingStopped),
            11 => Some(Self::StoreRowsListed),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum TaskTabPostcondition {
    Listed = 0,
    Active = 1,
    Absent = 2,
}

impl TaskTabPostcondition {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Listed),
            1 => Some(Self::Active),
            2 => Some(Self::Absent),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum TaskDownloadPostcondition {
    Started = 0,
    Listed = 1,
    Cancelled = 2,
}

impl TaskDownloadPostcondition {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Started),
            1 => Some(Self::Listed),
            2 => Some(Self::Cancelled),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum TaskDownloadState {
    Created = 0,
    InProgress = 1,
    Paused = 2,
    Complete = 3,
    Interrupted = 4,
    Cancelled = 5,
}

impl TaskDownloadState {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Created),
            1 => Some(Self::InProgress),
            2 => Some(Self::Paused),
            3 => Some(Self::Complete),
            4 => Some(Self::Interrupted),
            5 => Some(Self::Cancelled),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum TaskDownloadMediaType {
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

impl TaskDownloadMediaType {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Unknown),
            1 => Some(Self::Application),
            2 => Some(Self::Audio),
            3 => Some(Self::Font),
            4 => Some(Self::Image),
            5 => Some(Self::Message),
            6 => Some(Self::Model),
            7 => Some(Self::Multipart),
            8 => Some(Self::Text),
            9 => Some(Self::Video),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum TaskDownloadDirectoryClass {
    Undecided = 0,
    PersonChosen = 1,
    DefaultDownloads = 2,
    ApplicationPrivate = 3,
}

impl TaskDownloadDirectoryClass {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Undecided),
            1 => Some(Self::PersonChosen),
            2 => Some(Self::DefaultDownloads),
            3 => Some(Self::ApplicationPrivate),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum TaskEffectCompletionStatus {
    Succeeded = 0,
    Refused = 1,
    Unavailable = 2,
    Cancelled = 3,
    OutcomeUnknown = 4,
    ValueReferenceUnknown = 5,
}

impl TaskEffectCompletionStatus {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Succeeded),
            1 => Some(Self::Refused),
            2 => Some(Self::Unavailable),
            3 => Some(Self::Cancelled),
            4 => Some(Self::OutcomeUnknown),
            5 => Some(Self::ValueReferenceUnknown),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum AuthCallbackStatus {
    AuthorizationCode = 0,
    Denied = 1,
    ProviderError = 2,
    DeadlineExceeded = 3,
    PlatformUnavailable = 4,
}

impl AuthCallbackStatus {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::AuthorizationCode),
            1 => Some(Self::Denied),
            2 => Some(Self::ProviderError),
            3 => Some(Self::DeadlineExceeded),
            4 => Some(Self::PlatformUnavailable),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum AccountAuthMethod {
    Google = 0,
    EmailLink = 1,
    Github = 2,
    Facebook = 3,
}

impl AccountAuthMethod {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Google),
            1 => Some(Self::EmailLink),
            2 => Some(Self::Github),
            3 => Some(Self::Facebook),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum AccountTokenValidationStatus {
    Validated = 0,
    InvalidResponse = 1,
    StaleGeneration = 2,
    DeadlineExceeded = 3,
    ResourceLimit = 4,
}

impl AccountTokenValidationStatus {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Validated),
            1 => Some(Self::InvalidResponse),
            2 => Some(Self::StaleGeneration),
            3 => Some(Self::DeadlineExceeded),
            4 => Some(Self::ResourceLimit),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum AccountScope {
    OpenId = 0,
    Email = 1,
    Profile = 2,
}

impl AccountScope {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::OpenId),
            1 => Some(Self::Email),
            2 => Some(Self::Profile),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum AuthCredentialStatus {
    Success = 0,
    Cancelled = 1,
    NoCredential = 2,
    Unavailable = 3,
}

impl AuthCredentialStatus {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Success),
            1 => Some(Self::Cancelled),
            2 => Some(Self::NoCredential),
            3 => Some(Self::Unavailable),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PlatformPermission {
    Notifications = 0,
    Microphone = 1,
    Camera = 2,
    Location = 3,
}

impl PlatformPermission {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Notifications),
            1 => Some(Self::Microphone),
            2 => Some(Self::Camera),
            3 => Some(Self::Location),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PermissionDecision {
    Granted = 0,
    Denied = 1,
    Dismissed = 2,
    Unavailable = 3,
}

impl PermissionDecision {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Granted),
            1 => Some(Self::Denied),
            2 => Some(Self::Dismissed),
            3 => Some(Self::Unavailable),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum StorageOperation {
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

impl StorageOperation {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::AppendTaskCommit),
            1 => Some(Self::QueryWorkspace),
            2 => Some(Self::DeleteSource),
            3 => Some(Self::UpsertWorkspace),
            4 => Some(Self::InstallSkill),
            5 => Some(Self::SetSkillStatus),
            6 => Some(Self::RecordSkillRun),
            7 => Some(Self::ForgetSkill),
            8 => Some(Self::SetAssistantConfiguration),
            9 => Some(Self::DeleteWorkspace),
            10 => Some(Self::UpsertLibraryEntry),
            11 => Some(Self::RemoveLibraryEntry),
            12 => Some(Self::UpsertMemory),
            13 => Some(Self::DeleteMemory),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum LibraryFactKind {
    FromPage = 0,
    Summarized = 1,
    TaffyInference = 2,
    UserEntered = 3,
}

impl LibraryFactKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::FromPage),
            1 => Some(Self::Summarized),
            2 => Some(Self::TaffyInference),
            3 => Some(Self::UserEntered),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum MemorySourceKind {
    UserEntered = 0,
    AcceptedTaskSuggestion = 1,
}

impl MemorySourceKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::UserEntered),
            1 => Some(Self::AcceptedTaskSuggestion),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum MemoryScopeKind {
    AllTasks = 0,
    Workspace = 1,
}

impl MemoryScopeKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::AllTasks),
            1 => Some(Self::Workspace),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum MemorySensitivity {
    Standard = 0,
    Sensitive = 1,
}

impl MemorySensitivity {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Standard),
            1 => Some(Self::Sensitive),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum AssistantAbility {
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

impl AssistantAbility {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::PagesLookup),
            1 => Some(Self::PagesCompare),
            2 => Some(Self::PagesSummarize),
            3 => Some(Self::PagesTable),
            4 => Some(Self::Products),
            5 => Some(Self::Offers),
            6 => Some(Self::Form),
            7 => Some(Self::Downloads),
            8 => Some(Self::Pdf),
            9 => Some(Self::Sheet),
            10 => Some(Self::Document),
            11 => Some(Self::Depth),
            12 => Some(Self::Trip),
            13 => Some(Self::Pictures),
            14 => Some(Self::Video),
            15 => Some(Self::Keep),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersonalityPreset {
    CarefulResearcher = 0,
    QuickShopper = 1,
    TripPlanner = 2,
}

impl PersonalityPreset {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::CarefulResearcher),
            1 => Some(Self::QuickShopper),
            2 => Some(Self::TripPlanner),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum SkillProvenance {
    Authored = 0,
    RecordedFromTask = 1,
    InstalledFromPack = 2,
}

impl SkillProvenance {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Authored),
            1 => Some(Self::RecordedFromTask),
            2 => Some(Self::InstalledFromPack),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum SkillMutationKind {
    Teach = 0,
    Update = 1,
    SetEnabled = 2,
    Remove = 3,
}

impl SkillMutationKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Teach),
            1 => Some(Self::Update),
            2 => Some(Self::SetEnabled),
            3 => Some(Self::Remove),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum SkillClauseKind {
    RolePresent = 0,
    PhraseAt = 1,
    StateAt = 2,
}

impl SkillClauseKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::RolePresent),
            1 => Some(Self::PhraseAt),
            2 => Some(Self::StateAt),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum SkillArgumentKind {
    FromEarlierStep = 0,
    FromPerson = 1,
    Choice = 2,
    Count = 3,
    Flag = 4,
    PublicAddress = 5,
    SemanticTarget = 6,
}

impl SkillArgumentKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::FromEarlierStep),
            1 => Some(Self::FromPerson),
            2 => Some(Self::Choice),
            3 => Some(Self::Count),
            4 => Some(Self::Flag),
            5 => Some(Self::PublicAddress),
            6 => Some(Self::SemanticTarget),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum SkillStatus {
    Draft = 0,
    Active = 1,
    Superseded = 2,
    Retired = 3,
    Disabled = 4,
}

impl SkillStatus {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Draft),
            1 => Some(Self::Active),
            2 => Some(Self::Superseded),
            3 => Some(Self::Retired),
            4 => Some(Self::Disabled),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum SkillRunOutcome {
    Completed = 0,
    Refused = 1,
    Abandoned = 2,
    Unavailable = 3,
}

impl SkillRunOutcome {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Completed),
            1 => Some(Self::Refused),
            2 => Some(Self::Abandoned),
            3 => Some(Self::Unavailable),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum WorkspaceExportFormat {
    Markdown = 0,
    Csv = 1,
}

impl WorkspaceExportFormat {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Markdown),
            1 => Some(Self::Csv),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PageSnapshotExportFormat {
    Markdown = 0,
    CanonicalJson = 1,
}

impl PageSnapshotExportFormat {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Markdown),
            1 => Some(Self::CanonicalJson),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PageSnapshotExportStatus {
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

impl PageSnapshotExportStatus {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Exported),
            1 => Some(Self::StalePage),
            2 => Some(Self::PrivateProfile),
            3 => Some(Self::Incomplete),
            4 => Some(Self::Oversize),
            5 => Some(Self::Malformed),
            6 => Some(Self::Cancelled),
            7 => Some(Self::ReplayConflict),
            8 => Some(Self::Unavailable),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum SiteSkillMatchStatus {
    Available = 0,
    StalePage = 1,
    PrivateProfile = 2,
    Incomplete = 3,
    Malformed = 4,
    Unavailable = 5,
}

impl SiteSkillMatchStatus {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Available),
            1 => Some(Self::StalePage),
            2 => Some(Self::PrivateProfile),
            3 => Some(Self::Incomplete),
            4 => Some(Self::Malformed),
            5 => Some(Self::Unavailable),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum ObservationScope {
    CurrentDocument = 0,
    SelectedSources = 1,
}

impl ObservationScope {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::CurrentDocument),
            1 => Some(Self::SelectedSources),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum BipObservationStatus {
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

impl BipObservationStatus {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Ok),
            1 => Some(Self::Unsupported),
            2 => Some(Self::Incomplete),
            3 => Some(Self::Conflicted),
            4 => Some(Self::StalePageEpoch),
            5 => Some(Self::DocumentInactive),
            6 => Some(Self::BudgetExceeded),
            7 => Some(Self::DeadlineExceeded),
            8 => Some(Self::Cancelled),
            9 => Some(Self::ResourcePressure),
            10 => Some(Self::InternalError),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum BipGraphEncoding {
    None = 0,
    BipContract = 1,
}

impl BipGraphEncoding {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::None),
            1 => Some(Self::BipContract),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum MediaObservationKind {
    Image = 0,
    Video = 1,
    Pdf = 2,
    PageScreenshot = 3,
}

impl MediaObservationKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Image),
            1 => Some(Self::Video),
            2 => Some(Self::Pdf),
            3 => Some(Self::PageScreenshot),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum MediaFactKind {
    Description = 0,
    OcrText = 1,
    Transcript = 2,
    PdfText = 3,
    PdfTableRow = 4,
    Metadata = 5,
}

impl MediaFactKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Description),
            1 => Some(Self::OcrText),
            2 => Some(Self::Transcript),
            3 => Some(Self::PdfText),
            4 => Some(Self::PdfTableRow),
            5 => Some(Self::Metadata),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum MediaEvidenceKind {
    Dom = 0,
    Accessibility = 1,
    CaptionTrack = 2,
    PdfTextLayer = 3,
    VisualInference = 4,
    TableHeuristic = 5,
}

impl MediaEvidenceKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Dom),
            1 => Some(Self::Accessibility),
            2 => Some(Self::CaptionTrack),
            3 => Some(Self::PdfTextLayer),
            4 => Some(Self::VisualInference),
            5 => Some(Self::TableHeuristic),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum BipSensitivity {
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

impl BipSensitivity {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::NotSensitive),
            1 => Some(Self::Personal),
            2 => Some(Self::Account),
            3 => Some(Self::Payment),
            4 => Some(Self::Identity),
            5 => Some(Self::Health),
            6 => Some(Self::Financial),
            7 => Some(Self::Legal),
            8 => Some(Self::PrivateCommunication),
            9 => Some(Self::Administration),
            10 => Some(Self::Credential),
            11 => Some(Self::UnknownSensitive),
            12 => Some(Self::OneTimeCode),
            13 => Some(Self::ChallengeResponse),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum AccountNetworkOperation {
    ExchangeAuthorizationCode = 0,
    ExchangeNativeCredential = 1,
    RequestEmailLink = 2,
    RefreshSession = 3,
    RevokeSession = 4,
    FetchEntitlement = 5,
}

impl AccountNetworkOperation {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::ExchangeAuthorizationCode),
            1 => Some(Self::ExchangeNativeCredential),
            2 => Some(Self::RequestEmailLink),
            3 => Some(Self::RefreshSession),
            4 => Some(Self::RevokeSession),
            5 => Some(Self::FetchEntitlement),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum EntitlementFetchReason {
    Bootstrap = 0,
    SignIn = 1,
    Cadence = 2,
    QuotaRefused = 3,
}

impl EntitlementFetchReason {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Bootstrap),
            1 => Some(Self::SignIn),
            2 => Some(Self::Cadence),
            3 => Some(Self::QuotaRefused),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum BrowserActionOperation {
    Dispatch = 0,
    Reconcile = 1,
    ReleaseTaskTabs = 2,
    ExportArtifact = 3,
}

impl BrowserActionOperation {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Dispatch),
            1 => Some(Self::Reconcile),
            2 => Some(Self::ReleaseTaskTabs),
            3 => Some(Self::ExportArtifact),
            _ => None,
        }
    }
}

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
pub enum SecureStoreOperation {
    GenerateEntropy = 0,
    WriteTransient = 1,
    DeleteHandle = 2,
}

impl SecureStoreOperation {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::GenerateEntropy),
            1 => Some(Self::WriteTransient),
            2 => Some(Self::DeleteHandle),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum SecretMaterialPurpose {
    PkceVerifier = 0,
    GoogleRawNonce = 1,
}

impl SecretMaterialPurpose {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::PkceVerifier),
            1 => Some(Self::GoogleRawNonce),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum AuthSurfaceOperation {
    OpenOauth = 0,
    RequestNativeCredential = 1,
}

impl AuthSurfaceOperation {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::OpenOauth),
            1 => Some(Self::RequestNativeCredential),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum BrowserActionOutcome {
    Completed = 0,
    Refused = 1,
    OutcomeUnknown = 2,
}

impl BrowserActionOutcome {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Completed),
            1 => Some(Self::Refused),
            2 => Some(Self::OutcomeUnknown),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum CapabilityRegistrationStatus {
    Registered = 0,
    Duplicate = 1,
    StaleGeneration = 2,
    LeaseMissing = 3,
    InvalidGrant = 4,
}

impl CapabilityRegistrationStatus {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Registered),
            1 => Some(Self::Duplicate),
            2 => Some(Self::StaleGeneration),
            3 => Some(Self::LeaseMissing),
            4 => Some(Self::InvalidGrant),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PendingApprovalRegistrationStatus {
    Registered = 0,
    StaleGeneration = 1,
    StaleSequence = 2,
    InvalidBinding = 3,
    TooManyBindings = 4,
}

impl PendingApprovalRegistrationStatus {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Registered),
            1 => Some(Self::StaleGeneration),
            2 => Some(Self::StaleSequence),
            3 => Some(Self::InvalidBinding),
            4 => Some(Self::TooManyBindings),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PolicyActionClass {
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

impl PolicyActionClass {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::ObservePage),
            1 => Some(Self::ScrollIntoView),
            2 => Some(Self::OpenLink),
            3 => Some(Self::CreateTaskTab),
            4 => Some(Self::SyntheticClick),
            5 => Some(Self::MoveFocus),
            6 => Some(Self::FillField),
            7 => Some(Self::SelectOption),
            8 => Some(Self::ToggleControl),
            9 => Some(Self::SubmitForm),
            10 => Some(Self::StartDownload),
            11 => Some(Self::UploadFile),
            12 => Some(Self::SendMessage),
            13 => Some(Self::Purchase),
            14 => Some(Self::ExtractCredential),
            15 => Some(Self::BypassAccessControl),
            16 => Some(Self::ExecuteToolJob),
            17 => Some(Self::LibraryRead),
            18 => Some(Self::LibraryWrite),
            19 => Some(Self::MemoryRead),
            20 => Some(Self::MemoryWrite),
            21 => Some(Self::ControlTab),
            22 => Some(Self::ProfileStoreRead),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum TaskActionOperationKind {
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

impl TaskActionOperationKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Navigate),
            1 => Some(Self::Search),
            2 => Some(Self::HistoryBack),
            3 => Some(Self::HistoryForward),
            4 => Some(Self::TabsOpen),
            5 => Some(Self::TabsList),
            6 => Some(Self::TabsActivate),
            7 => Some(Self::TabsClose),
            8 => Some(Self::DomQuery),
            9 => Some(Self::DomRead),
            10 => Some(Self::DomClick),
            11 => Some(Self::DomScroll),
            12 => Some(Self::FormInspect),
            13 => Some(Self::FormFill),
            14 => Some(Self::FormSubmit),
            15 => Some(Self::DownloadStart),
            16 => Some(Self::DownloadList),
            17 => Some(Self::SelectionRead),
            18 => Some(Self::ImageDescribe),
            19 => Some(Self::ImageReadText),
            20 => Some(Self::VideoInspect),
            21 => Some(Self::PdfInspect),
            22 => Some(Self::ToolJob),
            23 => Some(Self::LinkOpen),
            24 => Some(Self::FormSelect),
            25 => Some(Self::FormToggle),
            26 => Some(Self::LibrarySearch),
            27 => Some(Self::LibrarySave),
            28 => Some(Self::LibraryRemove),
            29 => Some(Self::MemorySearch),
            30 => Some(Self::MemorySave),
            31 => Some(Self::MemoryUpdate),
            32 => Some(Self::MemoryDelete),
            33 => Some(Self::DomFocus),
            34 => Some(Self::PageScreenshotInspect),
            35 => Some(Self::DownloadCancel),
            36 => Some(Self::Reload),
            37 => Some(Self::StopLoading),
            38 => Some(Self::HistorySearch),
            39 => Some(Self::HistoryRecent),
            40 => Some(Self::BookmarksSearch),
            41 => Some(Self::BookmarksList),
            42 => Some(Self::OpenTabsList),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum TaskActionInputKind {
    None = 0,
    SuppliedValue = 1,
    ToggleState = 2,
}

impl TaskActionInputKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::None),
            1 => Some(Self::SuppliedValue),
            2 => Some(Self::ToggleState),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PolicyPrincipalKind {
    Assistant = 0,
    Skill = 1,
}

impl PolicyPrincipalKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Assistant),
            1 => Some(Self::Skill),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PolicyEvaluationContext {
    Task = 0,
    DirectUserObservation = 1,
    TaskDiscovery = 2,
}

impl PolicyEvaluationContext {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Task),
            1 => Some(Self::DirectUserObservation),
            2 => Some(Self::TaskDiscovery),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum AuthoritySubjectKind {
    Task = 0,
    DirectUserIntent = 1,
}

impl AuthoritySubjectKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Task),
            1 => Some(Self::DirectUserIntent),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PolicyOriginKind {
    Tuple = 0,
    Opaque = 1,
}

impl PolicyOriginKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Tuple),
            1 => Some(Self::Opaque),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PolicyRiskClass {
    LocalRead = 0,
    ReversibleDisclosure = 1,
    SensitiveDisclosure = 2,
    ExcludedCommitment = 3,
    ProhibitedAbuse = 4,
}

impl PolicyRiskClass {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::LocalRead),
            1 => Some(Self::ReversibleDisclosure),
            2 => Some(Self::SensitiveDisclosure),
            3 => Some(Self::ExcludedCommitment),
            4 => Some(Self::ProhibitedAbuse),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PolicyEvaluationStatus {
    Granted = 0,
    ApprovalRequired = 1,
    Denied = 2,
    InvalidRequest = 3,
    CoreUnavailable = 4,
}

impl PolicyEvaluationStatus {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Granted),
            1 => Some(Self::ApprovalRequired),
            2 => Some(Self::Denied),
            3 => Some(Self::InvalidRequest),
            4 => Some(Self::CoreUnavailable),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum TaskActionResultCode {
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

impl TaskActionResultCode {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Verified),
            1 => Some(Self::DeniedByPolicy),
            2 => Some(Self::ApprovalRequired),
            3 => Some(Self::ApprovalDenied),
            4 => Some(Self::ActorLeaseMissing),
            5 => Some(Self::CapabilityExpired),
            6 => Some(Self::TabGone),
            7 => Some(Self::FrameGone),
            8 => Some(Self::DocumentInactive),
            9 => Some(Self::StalePageEpoch),
            10 => Some(Self::StaleGraph),
            11 => Some(Self::NodeGone),
            12 => Some(Self::OriginChanged),
            13 => Some(Self::RoleOrActionChanged),
            14 => Some(Self::NotVisible),
            15 => Some(Self::Occluded),
            16 => Some(Self::NotEnabled),
            17 => Some(Self::NotEditable),
            18 => Some(Self::SensitiveField),
            19 => Some(Self::DestinationChanged),
            20 => Some(Self::Unsupported),
            21 => Some(Self::BudgetExceeded),
            22 => Some(Self::DispatchFailed),
            23 => Some(Self::NavigationStarted),
            24 => Some(Self::PostconditionTimeout),
            25 => Some(Self::PostconditionFailed),
            26 => Some(Self::CancelledByUser),
            27 => Some(Self::CancelledByNavigation),
            28 => Some(Self::RendererCrashed),
            29 => Some(Self::OutcomeUnknown),
            30 => Some(Self::InternalError),
            31 => Some(Self::EgressNotAuthorized),
            32 => Some(Self::DestinationClassRestricted),
            33 => Some(Self::UntrustedContentOrigin),
            34 => Some(Self::PreparedEffectChanged),
            35 => Some(Self::CommitWithoutPrepare),
            36 => Some(Self::GraphMovedDuringPreflight),
            37 => Some(Self::ValueReferenceUnknown),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum AssetDeliveryOperation {
    FetchAsset = 0,
    RemoveAsset = 1,
}

impl AssetDeliveryOperation {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::FetchAsset),
            1 => Some(Self::RemoveAsset),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum AssetPlatform {
    AndroidArm64 = 0,
    AndroidX64 = 1,
    MacosArm64 = 2,
    MacosX64 = 3,
    WindowsX64 = 4,
    WindowsArm64 = 5,
    Unsupported = 6,
}

impl AssetPlatform {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::AndroidArm64),
            1 => Some(Self::AndroidX64),
            2 => Some(Self::MacosArm64),
            3 => Some(Self::MacosX64),
            4 => Some(Self::WindowsX64),
            5 => Some(Self::WindowsArm64),
            6 => Some(Self::Unsupported),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum AssetKind {
    PythonStdlib = 0,
    PythonPackages = 1,
    ModelWeights = 2,
    ModelTokenizer = 3,
    FilterList = 4,
    CountryFlags = 5,
    StartScenes = 6,
}

impl AssetKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::PythonStdlib),
            1 => Some(Self::PythonPackages),
            2 => Some(Self::ModelWeights),
            3 => Some(Self::ModelTokenizer),
            4 => Some(Self::FilterList),
            5 => Some(Self::CountryFlags),
            6 => Some(Self::StartScenes),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum AssetContainer {
    Raw = 0,
    Zip = 1,
}

impl AssetContainer {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Raw),
            1 => Some(Self::Zip),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum AssetPresence {
    Absent = 0,
    Partial = 1,
    Complete = 2,
    Installed = 3,
}

impl AssetPresence {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Absent),
            1 => Some(Self::Partial),
            2 => Some(Self::Complete),
            3 => Some(Self::Installed),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum AssetRefusalReason {
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

impl AssetRefusalReason {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::UnknownAsset),
            1 => Some(Self::NoVariantForPlatform),
            2 => Some(Self::NotPublishedYet),
            3 => Some(Self::CatalogRowIncomplete),
            4 => Some(Self::VariantTooLarge),
            5 => Some(Self::AttemptsExhausted),
            6 => Some(Self::IntegrityFailed),
            7 => Some(Self::NetworkNotPermitted),
            8 => Some(Self::DeclinedByPerson),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum AssetTransferOutcome {
    Interrupted = 0,
    OriginRefusedTemporary = 1,
    OriginRefusedPermanent = 2,
    IntegritySound = 3,
    IntegrityWrongLength = 4,
    IntegrityWrongDigest = 5,
    Installed = 6,
    Declined = 7,
}

impl AssetTransferOutcome {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Interrupted),
            1 => Some(Self::OriginRefusedTemporary),
            2 => Some(Self::OriginRefusedPermanent),
            3 => Some(Self::IntegritySound),
            4 => Some(Self::IntegrityWrongLength),
            5 => Some(Self::IntegrityWrongDigest),
            6 => Some(Self::Installed),
            7 => Some(Self::Declined),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum AssetNetworkCost {
    Offline = 0,
    Metered = 1,
    Unmetered = 2,
}

impl AssetNetworkCost {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Offline),
            1 => Some(Self::Metered),
            2 => Some(Self::Unmetered),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum ProviderAuthMethod {
    ApiKey = 0,
    Oauth = 1,
}

impl ProviderAuthMethod {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::ApiKey),
            1 => Some(Self::Oauth),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum ThinkingLevel {
    Off = 0,
    Minimal = 1,
    Low = 2,
    Medium = 3,
    High = 4,
    Xhigh = 5,
    Max = 6,
}

impl ThinkingLevel {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Off),
            1 => Some(Self::Minimal),
            2 => Some(Self::Low),
            3 => Some(Self::Medium),
            4 => Some(Self::High),
            5 => Some(Self::Xhigh),
            6 => Some(Self::Max),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum ProviderCredentialState {
    Usable = 0,
    NeedsSignIn = 1,
    RefreshFailed = 2,
}

impl ProviderCredentialState {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Usable),
            1 => Some(Self::NeedsSignIn),
            2 => Some(Self::RefreshFailed),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum ProviderWireApi {
    AnthropicMessages = 0,
    OpenAiResponses = 1,
    OpenAiCompletions = 2,
    GoogleGenerativeLanguage = 3,
    Managed = 4,
    OpenAiCodexResponses = 5,
    GoogleCloudCodeAssist = 6,
}

impl ProviderWireApi {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::AnthropicMessages),
            1 => Some(Self::OpenAiResponses),
            2 => Some(Self::OpenAiCompletions),
            3 => Some(Self::GoogleGenerativeLanguage),
            4 => Some(Self::Managed),
            5 => Some(Self::OpenAiCodexResponses),
            6 => Some(Self::GoogleCloudCodeAssist),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum ModelErrorClass {
    Auth = 0,
    Quota = 1,
    Overloaded = 2,
    InvalidRequest = 3,
    Network = 4,
    Overflow = 5,
    Canceled = 6,
    Unknown = 7,
}

impl ModelErrorClass {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Auth),
            1 => Some(Self::Quota),
            2 => Some(Self::Overloaded),
            3 => Some(Self::InvalidRequest),
            4 => Some(Self::Network),
            5 => Some(Self::Overflow),
            6 => Some(Self::Canceled),
            7 => Some(Self::Unknown),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum ModelEndpointKind {
    CatalogOrigin = 0,
    UserBaseUrl = 1,
}

impl ModelEndpointKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::CatalogOrigin),
            1 => Some(Self::UserBaseUrl),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum ServerKind {
    OpenaiCompatible = 0,
    Ollama = 1,
    LmStudio = 2,
    Vllm = 3,
    LlamaCpp = 4,
}

impl ServerKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::OpenaiCompatible),
            1 => Some(Self::Ollama),
            2 => Some(Self::LmStudio),
            3 => Some(Self::Vllm),
            4 => Some(Self::LlamaCpp),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum ModelStreamChunkStatus {
    Accepted = 0,
    Invalid = 1,
    Stale = 2,
    Unavailable = 3,
}

impl ModelStreamChunkStatus {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Accepted),
            1 => Some(Self::Invalid),
            2 => Some(Self::Stale),
            3 => Some(Self::Unavailable),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum BackupRecordKind {
    AssistantConfiguration = 0,
    SavedWorkspace = 1,
    LibraryEntry = 2,
    MemoryRecord = 3,
    UserAuthoredSkill = 4,
    LearnedProcedure = 5,
    Bookmark = 6,
    BrowserPreference = 7,
}

impl BackupRecordKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::AssistantConfiguration),
            1 => Some(Self::SavedWorkspace),
            2 => Some(Self::LibraryEntry),
            3 => Some(Self::MemoryRecord),
            4 => Some(Self::UserAuthoredSkill),
            5 => Some(Self::LearnedProcedure),
            6 => Some(Self::Bookmark),
            7 => Some(Self::BrowserPreference),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum BackupRecordState {
    Active = 0,
    Tombstone = 1,
}

impl BackupRecordState {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Active),
            1 => Some(Self::Tombstone),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum BackupPlanningStatus {
    Succeeded = 0,
    InvalidRequest = 1,
    InvalidManifest = 2,
    SnapshotMismatch = 3,
    StagedPayloadMismatch = 4,
    RestoreConflict = 5,
    DigestUnavailable = 6,
    Unavailable = 7,
}

impl BackupPlanningStatus {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Succeeded),
            1 => Some(Self::InvalidRequest),
            2 => Some(Self::InvalidManifest),
            3 => Some(Self::SnapshotMismatch),
            4 => Some(Self::StagedPayloadMismatch),
            5 => Some(Self::RestoreConflict),
            6 => Some(Self::DigestUnavailable),
            7 => Some(Self::Unavailable),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum BackupRestoreTargetKind {
    NewRegularProfile = 0,
    ExistingRegularProfile = 1,
}

impl BackupRestoreTargetKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::NewRegularProfile),
            1 => Some(Self::ExistingRegularProfile),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum BackupRestoreAction {
    StageCreate = 0,
    StageDeletion = 1,
    AlreadyPresent = 2,
    KeepNewerCurrent = 3,
    BlockedByDeletion = 4,
    NeedsExplicitConflictChoice = 5,
}

impl BackupRestoreAction {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::StageCreate),
            1 => Some(Self::StageDeletion),
            2 => Some(Self::AlreadyPresent),
            3 => Some(Self::KeepNewerCurrent),
            4 => Some(Self::BlockedByDeletion),
            5 => Some(Self::NeedsExplicitConflictChoice),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum BackupRestoreProtocolStatus {
    Succeeded = 0,
    InvalidOperation = 1,
    Unavailable = 2,
    BindingMismatch = 3,
    WrongPhase = 4,
    ConfirmationMismatch = 5,
    SnapshotMismatch = 6,
    ReconcileRequired = 7,
}

impl BackupRestoreProtocolStatus {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Succeeded),
            1 => Some(Self::InvalidOperation),
            2 => Some(Self::Unavailable),
            3 => Some(Self::BindingMismatch),
            4 => Some(Self::WrongPhase),
            5 => Some(Self::ConfirmationMismatch),
            6 => Some(Self::SnapshotMismatch),
            7 => Some(Self::ReconcileRequired),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum BackupRestoreCommitOutcome {
    Committed = 0,
    DefinitelyNotCommitted = 1,
    OutcomeUnknown = 2,
}

impl BackupRestoreCommitOutcome {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Committed),
            1 => Some(Self::DefinitelyNotCommitted),
            2 => Some(Self::OutcomeUnknown),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum BackupRestoreResolutionChoice {
    AcceptCandidate = 0,
    DiscardCandidate = 1,
}

impl BackupRestoreResolutionChoice {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::AcceptCandidate),
            1 => Some(Self::DiscardCandidate),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum BackupRestoreResolutionOutcome {
    Completed = 0,
    DefinitelyNotCompleted = 1,
    OutcomeUnknown = 2,
}

impl BackupRestoreResolutionOutcome {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Completed),
            1 => Some(Self::DefinitelyNotCompleted),
            2 => Some(Self::OutcomeUnknown),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum BackupRestoreRecoveryFactKind {
    IntentRecorded = 0,
    OutcomeObserved = 1,
}

impl BackupRestoreRecoveryFactKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::IntentRecorded),
            1 => Some(Self::OutcomeObserved),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum BackupRestorePhysicalIntent {
    CommitCandidate = 0,
    AcceptCandidate = 1,
    DiscardCandidate = 2,
}

impl BackupRestorePhysicalIntent {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::CommitCandidate),
            1 => Some(Self::AcceptCandidate),
            2 => Some(Self::DiscardCandidate),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum BackupRestoreObservedOutcome {
    Completed = 0,
    DefinitelyNotCompleted = 1,
    OutcomeUnknown = 2,
}

impl BackupRestoreObservedOutcome {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Completed),
            1 => Some(Self::DefinitelyNotCompleted),
            2 => Some(Self::OutcomeUnknown),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum BackupRestoreRecoveryClassificationKind {
    ReconcileRequired = 0,
    RollbackAvailable = 1,
    CleanupRequired = 2,
    Published = 3,
    VerifiedDeleted = 4,
}

impl BackupRestoreRecoveryClassificationKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::ReconcileRequired),
            1 => Some(Self::RollbackAvailable),
            2 => Some(Self::CleanupRequired),
            3 => Some(Self::Published),
            4 => Some(Self::VerifiedDeleted),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum BackupRestoreRecoveryError {
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

impl BackupRestoreRecoveryError {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::MissingCommitIntent),
            1 => Some(Self::TooManyRecords),
            2 => Some(Self::UnsupportedVersion),
            3 => Some(Self::InvalidSequence),
            4 => Some(Self::InvalidBinding),
            5 => Some(Self::BindingChanged),
            6 => Some(Self::InvalidIntentId),
            7 => Some(Self::IntentIdReused),
            8 => Some(Self::UnexpectedIntent),
            9 => Some(Self::UnresolvedIntent),
            10 => Some(Self::OutcomeWithoutIntent),
            11 => Some(Self::OutcomeIntentMismatch),
            12 => Some(Self::DuplicateUnknownOutcome),
            13 => Some(Self::TerminalHistoryExtended),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum BackupRestoreRecoveryInspectionStatus {
    Succeeded = 0,
    InvalidOperation = 1,
    InvalidRecord = 2,
    InvalidHistory = 3,
    Unavailable = 4,
}

impl BackupRestoreRecoveryInspectionStatus {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Succeeded),
            1 => Some(Self::InvalidOperation),
            2 => Some(Self::InvalidRecord),
            3 => Some(Self::InvalidHistory),
            4 => Some(Self::Unavailable),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum SavedFlowQueryStatus {
    Available = 0,
    PrivateProfile = 1,
    Unavailable = 2,
    InvalidRequest = 3,
    StaleRequest = 4,
}

impl SavedFlowQueryStatus {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Available),
            1 => Some(Self::PrivateProfile),
            2 => Some(Self::Unavailable),
            3 => Some(Self::InvalidRequest),
            4 => Some(Self::StaleRequest),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum SavedFlowQueryKind {
    ExactGoal = 0,
    Review = 1,
    PublicStart = 2,
}

impl SavedFlowQueryKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::ExactGoal),
            1 => Some(Self::Review),
            2 => Some(Self::PublicStart),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum FieldValueAskOutcome {
    Answered = 0,
    Dismissed = 1,
    NotAField = 2,
    ChallengeOffScreen = 3,
    CannotBeShown = 4,
    PageMoved = 5,
    NoSurface = 6,
}

impl FieldValueAskOutcome {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Answered),
            1 => Some(Self::Dismissed),
            2 => Some(Self::NotAField),
            3 => Some(Self::ChallengeOffScreen),
            4 => Some(Self::CannotBeShown),
            5 => Some(Self::PageMoved),
            6 => Some(Self::NoSurface),
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
pub struct PolicyPrincipal {
    pub kind: PolicyPrincipalKind,
    pub skill_version_id: Option<String>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AuthoritySubject {
    pub kind: AuthoritySubjectKind,
    pub authority_subject_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PolicyOrigin {
    pub kind: PolicyOriginKind,
    pub serialization: Option<String>,
    pub opaque_id: Option<String>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PolicyCapabilityScope {
    pub profile_id: String,
    pub tab_id: String,
    pub frame_id: String,
    pub page_epoch: String,
    pub origin: PolicyOrigin,
    pub node_id: Option<String>,
    pub destination_scope: Option<PolicyOrigin>,
    pub required_graph_revision: u64,
    pub allowed_redirects: Vec<PolicyOrigin>,
    pub destination_address: Option<String>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PolicyApprovalFact {
    pub receipt_reference: String,
    pub proposal_digest: String,
    pub service_generation: u64,
    pub expires_at_monotonic_ms: u64,
    pub expires_at_utc_ms: u64,
    pub browser_session_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ActorLeaseFact {
    pub lease_id: String,
    pub service_generation: u64,
    pub task_id: String,
    pub profile_id: String,
    pub tab_id: String,
    pub control_mode: TaskControlMode,
    pub expires_at_monotonic_ms: u64,
    pub authority_subject: AuthoritySubject,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskDiscoveryAuthorityFact {
    pub discovery_tab_id: String,
    pub browser_session_id: String,
    pub remaining_new_source_cap: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PolicyEvaluationRequest {
    pub operation: OperationEnvelope,
    pub now_monotonic_ms: u64,
    pub action_id: String,
    pub task_id: String,
    pub principal: PolicyPrincipal,
    pub action_class: PolicyActionClass,
    pub proposal_digest: String,
    pub scope: PolicyCapabilityScope,
    pub data_classes: Vec<BipSensitivity>,
    pub context_risk: PolicyRiskClass,
    pub expires_at_monotonic_ms: u64,
    pub actor_lease: ActorLeaseFact,
    pub approval: Option<PolicyApprovalFact>,
    pub context: PolicyEvaluationContext,
    pub authority_subject: AuthoritySubject,
    pub policy_version: u32,
    pub now_utc_ms: u64,
    pub operation_kind: TaskActionOperationKind,
    pub canonical_intent_digest: [u8; 32],
    pub discovery: Option<TaskDiscoveryAuthorityFact>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PolicyEvaluationResult {
    pub operation_id: String,
    pub status: PolicyEvaluationStatus,
    pub minted_grant: Option<MintedCapabilityGrant>,
    pub direct_observation_effect: Option<EffectEnvelope>,
    pub denial: Option<PolicyDenial>,
}

impl PolicyEvaluationResult {
    pub fn has_valid_presence(&self) -> bool {
        (match self.status {
            PolicyEvaluationStatus::Granted => self.minted_grant.is_some(),
            _ => self.minted_grant.is_none(),
        })
            && (match self.status {
            PolicyEvaluationStatus::Denied => self.denial.is_some(),
            _ => self.denial.is_none(),
        })
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PolicyDenial {
    pub code: TaskActionResultCode,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MintedCapabilityGrant {
    pub capability_id: String,
    pub service_generation: u64,
    pub policy_version: u32,
    pub actor_lease_id: String,
    pub task_id: String,
    pub action_id: String,
    pub action_class: PolicyActionClass,
    pub principal: PolicyPrincipal,
    pub proposal_digest: String,
    pub idempotency_key: String,
    pub scope: PolicyCapabilityScope,
    pub data_classes: Vec<BipSensitivity>,
    pub effective_risk: PolicyRiskClass,
    pub approval: Option<PolicyApprovalFact>,
    pub issued_at_monotonic_ms: u64,
    pub expires_at_monotonic_ms: u64,
    pub authority_subject: AuthoritySubject,
    pub operation_kind: TaskActionOperationKind,
    pub canonical_intent_digest: [u8; 32],
    pub discovery: Option<TaskDiscoveryAuthorityFact>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CommittedTaskBatch {
    pub effect_id: String,
    pub expected_revision: u64,
    pub resulting_revision: u64,
    pub transaction_batch: Vec<u8>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskRestoreRecord {
    pub task_id: String,
    pub batches: Vec<CommittedTaskBatch>,
    pub task_id_seed: [u8; 32],
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AccountSessionHandle {
    pub session_handle: String,
    pub account_subject: String,
    pub expires_at_monotonic_ms: u64,
    pub rotation: u64,
    pub auth_method: AccountAuthMethod,
    pub email: Option<String>,
    pub display_name: Option<String>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct WorkspaceRestoreRecord {
    pub workspace_id: String,
    pub revision: u64,
    pub snapshot: Vec<u8>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct LibrarySourceRecord {
    pub source_id: String,
    pub title: String,
    pub host: String,
    pub observed_at_epoch_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct LibraryEntryRecord {
    pub entry_id: String,
    pub revision: u64,
    pub collection_id: String,
    pub collection_name: String,
    pub source_workspace_id: String,
    pub source_workspace_revision: u64,
    pub source_fact_id: String,
    pub field: String,
    pub original_value: String,
    pub correction: Option<String>,
    pub kind: LibraryFactKind,
    pub sources: Vec<LibrarySourceRecord>,
    pub captured_at_epoch_ms: u64,
    pub last_checked_epoch_ms: u64,
    pub has_conflict: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MemoryWorkspaceRecord {
    pub workspace_id: String,
    pub display_name: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MemoryRecord {
    pub memory_id: String,
    pub revision: u64,
    pub statement: String,
    pub source_kind: MemorySourceKind,
    pub source_task_id: Option<String>,
    pub source_workspace: Option<MemoryWorkspaceRecord>,
    pub scope_kind: MemoryScopeKind,
    pub scope_workspace: Option<MemoryWorkspaceRecord>,
    pub sensitivity: MemorySensitivity,
    pub created_at_epoch_ms: u64,
    pub updated_at_epoch_ms: u64,
    pub reviewed_at_epoch_ms: u64,
    pub expires_at_epoch_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AssetFetchRequest {
    pub asset_id: String,
    pub asset_revision: String,
    pub origin_path: String,
    pub offset_bytes: u64,
    pub total_bytes: u64,
    pub expected_digest: [u8; 32],
    pub container: AssetContainer,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AssetRemoveRequest {
    pub asset_id: String,
    pub asset_revision: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AssetDeliveryEffect {
    pub operation_kind: AssetDeliveryOperation,
    pub fetch: Option<AssetFetchRequest>,
    pub remove: Option<AssetRemoveRequest>,
}

impl AssetDeliveryEffect {
    pub fn has_valid_body(&self) -> bool {
        let body_count = [
            self.fetch.is_some(),
            self.remove.is_some(),
        ]
        .into_iter()
        .filter(|present| *present)
        .count();
        body_count == 1
            && match self.operation_kind {
                AssetDeliveryOperation::FetchAsset => self.fetch.is_some(),
                AssetDeliveryOperation::RemoveAsset => self.remove.is_some(),
            }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AssetTransferReport {
    pub asset_id: String,
    pub asset_revision: String,
    pub outcome: AssetTransferOutcome,
    pub written_bytes: u64,
    pub observed_bytes: u64,
    pub observed_digest: [u8; 32],
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AssetRemovalReport {
    pub asset_id: String,
    pub asset_revision: String,
    pub reclaimed_bytes: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AssetDeliveryEffectResult {
    pub operation_kind: AssetDeliveryOperation,
    pub transfer: Option<AssetTransferReport>,
    pub removal: Option<AssetRemovalReport>,
}

impl AssetDeliveryEffectResult {
    pub fn has_valid_body(&self) -> bool {
        let body_count = [
            self.transfer.is_some(),
            self.removal.is_some(),
        ]
        .into_iter()
        .filter(|present| *present)
        .count();
        body_count == 1
            && match self.operation_kind {
                AssetDeliveryOperation::FetchAsset => self.transfer.is_some(),
                AssetDeliveryOperation::RemoveAsset => self.removal.is_some(),
            }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CatalogFetchEffect {
    pub operation_kind: CatalogNetworkOperation,
    pub known_catalog_version: Option<String>,
    pub max_response_bytes: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CatalogFetchEffectResult {
    pub operation_kind: CatalogNetworkOperation,
    pub disposition: CatalogFetchDisposition,
    pub body: Vec<u8>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CachedCatalogOverlay {
    pub fetched_at_utc_ms: u64,
    pub document: Vec<u8>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AssetOnDisk {
    pub asset_id: String,
    pub asset_revision: String,
    pub presence: AssetPresence,
    pub written_bytes: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SetAssetDeliveryPolicyCommand {
    pub network_cost: AssetNetworkCost,
    pub metered_permitted: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RequestAssetCommand {
    pub asset_id: String,
    pub asset_revision: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RemoveAssetCommand {
    pub asset_id: String,
    pub asset_revision: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SkillRecord {
    pub skill_id: String,
    pub origin: String,
    pub provenance: SkillProvenance,
    pub status: SkillStatus,
    pub active_version: u32,
    pub definition: Vec<u8>,
    pub step_count: u32,
    pub installed_at_utc_ms: u64,
    pub updated_at_utc_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SkillRunRecord {
    pub skill_id: String,
    pub version: u32,
    pub task_id: String,
    pub outcome: SkillRunOutcome,
    pub ran_at_utc_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AssistantConfiguration {
    pub revision: u64,
    pub disabled_abilities: Vec<AssistantAbility>,
    pub preset: PersonalityPreset,
    pub pace: u32,
    pub length: u32,
    pub check_in: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CoreBootstrap {
    pub service_generation: u64,
    pub private_profile: bool,
    pub core_journal_schema_version: u32,
    pub core_journal_schema_checksum: String,
    pub tasks: Vec<TaskRestoreRecord>,
    pub generation_capability_entropy: [u8; 32],
    pub account_session: Option<AccountSessionHandle>,
    pub browser_profile_id: String,
    pub workspaces: Vec<WorkspaceRestoreRecord>,
    pub browser_session_id: String,
    pub available_account_methods: Vec<AccountAuthMethod>,
    pub asset_platform: AssetPlatform,
    pub assets: Vec<AssetOnDisk>,
    pub skills: Vec<SkillRecord>,
    pub recall: Vec<SkillRunRecord>,
    pub cached_catalog_overlay: Option<CachedCatalogOverlay>,
    pub assistant_configuration: Option<AssistantConfiguration>,
    pub library_revision: u64,
    pub library_entries: Vec<LibraryEntryRecord>,
    pub memory_revision: u64,
    pub memory_records: Vec<MemoryRecord>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskBudget {
    pub kind: TaskBudgetKind,
    pub limit: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskConsentSource {
    pub source_id: String,
    pub tab_id: String,
    pub normalized_origin: String,
    pub canonical_locator: Option<String>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct LibraryRefreshSource {
    pub source_id: String,
    pub title: String,
    pub host: String,
    pub canonical_locator: String,
    pub original_content_digest: [u8; 32],
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct LibraryRefreshRequest {
    pub preview_id: String,
    pub library_revision: u64,
    pub collection_id: String,
    pub source_workspace_revision: u64,
    pub sources: Vec<LibraryRefreshSource>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskConsentPreview {
    pub sources: Vec<TaskConsentSource>,
    pub source_discovery_enabled: bool,
    pub new_source_cap: u32,
    pub provider_route: TaskProviderRoute,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskSuppliedValuePosition {
    pub index: u32,
    pub request_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskToggleState {
    pub checked: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskActionInput {
    pub kind: TaskActionInputKind,
    pub supplied_value: Option<TaskSuppliedValuePosition>,
    pub toggle_state: Option<TaskToggleState>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskPolicyEffect {
    pub operation: OperationEnvelope,
    pub effect_id: String,
    pub task_id: String,
    pub action_id: String,
    pub action_class: PolicyActionClass,
    pub proposal_digest: String,
    pub idempotency_key: String,
    pub tab_id: String,
    pub node_id: Option<String>,
    pub principal: PolicyPrincipal,
    pub data_classes: Vec<BipSensitivity>,
    pub context_risk: PolicyRiskClass,
    pub approval: Option<PolicyApprovalFact>,
    pub control_mode: TaskControlMode,
    pub policy_version: u32,
    pub operation_kind: TaskActionOperationKind,
    pub tool_name: String,
    pub destination_address: Option<String>,
    pub canonical_intent: Vec<u8>,
    pub transient_search_query: Option<String>,
    pub input: TaskActionInput,
    pub discovery: Option<TaskDiscoveryAuthorityFact>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskFrozenDocument {
    pub frame_id: String,
    pub page_epoch: String,
    pub graph_revision: u64,
    pub normalized_origin: String,
    pub opaque_origin_id: Option<String>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskTabDocumentTarget {
    pub tab_id: String,
    pub frame_id: String,
    pub page_epoch: String,
    pub graph_revision: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskTabActionBinding {
    pub browser_session_id: String,
    pub target: Option<TaskTabDocumentTarget>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskDownloadActionBinding {
    pub browser_session_id: String,
    pub download_id: Option<String>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskStoreActionBinding {
    pub query: Option<String>,
    pub limit: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskExecutableAction {
    pub action_class: PolicyActionClass,
    pub tool_name: String,
    pub tab_id: String,
    pub node_id: Option<String>,
    pub destination_origin: Option<String>,
    pub operand_handle: Option<String>,
    pub destination_address: Option<String>,
    pub operation_kind: TaskActionOperationKind,
    pub canonical_intent: Vec<u8>,
    pub transient_search_query: Option<String>,
    pub input: TaskActionInput,
    pub task_tab: Option<TaskTabActionBinding>,
    pub task_download: Option<TaskDownloadActionBinding>,
    pub task_store: Option<TaskStoreActionBinding>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskObservationBounds {
    pub scope: ObservationScope,
    pub max_bytes: u32,
    pub max_nodes: u32,
    pub max_text_bytes: u32,
    pub max_frames: u32,
    pub deadline_ms: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskActionEffect {
    pub action_id: String,
    pub proposal_digest: String,
    pub idempotency_key: String,
    pub capability_id: String,
    pub dispatch_id: String,
    pub document: TaskFrozenDocument,
    pub executable: TaskExecutableAction,
    pub preconditions: Vec<TaskActionPrecondition>,
    pub postcondition: TaskActionPostcondition,
    pub observation: Option<TaskObservationBounds>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskRevocationEffect {
    pub reason: TaskRevocationReason,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskApprovalEffect {
    pub action_id: String,
    pub proposal_digest: String,
    pub form_action: Option<TaskExecutableAction>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskPermissionEffect {
    pub request_id: String,
    pub permission: PlatformPermission,
    pub deadline_monotonic_ms: u64,
    pub deadline_utc_ms: u64,
    pub browser_session_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskSettlementEffect {
    pub kind: TaskSettlementKind,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskReconcileEffect {
    pub action_id: String,
    pub rule: TaskRecoveryRule,
    pub dispatch_id: String,
    pub operation: TaskActionOperationKind,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskReleaseTabsEffect {
    pub terminal_revision: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskArtifactEffect {
    pub kind: TaskArtifactKind,
    pub artifact_id: String,
    pub workspace_revision: u64,
    pub content: Vec<u8>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskModelEffect {
    pub call_id: String,
    pub request: ModelRequestEffect,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskHandoverEffect {
    pub handover_id: String,
    pub window_ms: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskToolJobEffect {
    pub action_id: String,
    pub job_id: String,
    pub runtime: ToolRuntimeKind,
    pub job: Option<ToolJobEffect>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskFieldValuesEffect {
    pub request_id: String,
    pub tab_id: String,
    pub node_id: String,
    pub companion_node_ids: Vec<String>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskDiscoveryBootstrapEffect {
    pub browser_session_id: String,
    pub remaining_new_source_cap: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskLibraryToolEffect {
    pub action_id: String,
    pub operation_kind: TaskActionOperationKind,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskMemoryToolEffect {
    pub action_id: String,
    pub operation_kind: TaskActionOperationKind,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskEffectBinding {
    pub operation: OperationEnvelope,
    pub effect_id: String,
    pub task_id: String,
    pub ordinal: u32,
    pub kind: TaskReducerEffectKind,
    pub revocation: Option<TaskRevocationEffect>,
    pub policy: Option<TaskPolicyEffect>,
    pub approval: Option<TaskApprovalEffect>,
    pub permission: Option<TaskPermissionEffect>,
    pub action: Option<TaskActionEffect>,
    pub settlement: Option<TaskSettlementEffect>,
    pub reconcile: Option<TaskReconcileEffect>,
    pub release_tabs: Option<TaskReleaseTabsEffect>,
    pub generate_artifact: Option<TaskArtifactEffect>,
    pub export_artifact: Option<TaskArtifactEffect>,
    pub model: Option<TaskModelEffect>,
    pub handover: Option<TaskHandoverEffect>,
    pub tool_job: Option<TaskToolJobEffect>,
    pub field_values: Option<TaskFieldValuesEffect>,
    pub discovery_bootstrap: Option<TaskDiscoveryBootstrapEffect>,
    pub library_tool: Option<TaskLibraryToolEffect>,
    pub memory_tool: Option<TaskMemoryToolEffect>,
}

impl TaskEffectBinding {
    pub fn has_valid_body(&self) -> bool {
        let body_count = [
            self.revocation.is_some(),
            self.policy.is_some(),
            self.approval.is_some(),
            self.permission.is_some(),
            self.action.is_some(),
            self.settlement.is_some(),
            self.reconcile.is_some(),
            self.release_tabs.is_some(),
            self.generate_artifact.is_some(),
            self.export_artifact.is_some(),
            self.model.is_some(),
            self.handover.is_some(),
            self.tool_job.is_some(),
            self.field_values.is_some(),
            self.discovery_bootstrap.is_some(),
            self.library_tool.is_some(),
            self.memory_tool.is_some(),
        ]
        .into_iter()
        .filter(|present| *present)
        .count();
        body_count == 1
            && match self.kind {
                TaskReducerEffectKind::RevokeAuthority => self.revocation.is_some(),
                TaskReducerEffectKind::AskPolicy => self.policy.is_some(),
                TaskReducerEffectKind::RequestApproval => self.approval.is_some(),
                TaskReducerEffectKind::RequestPermission => self.permission.is_some(),
                TaskReducerEffectKind::DispatchAction => self.action.is_some(),
                TaskReducerEffectKind::AwaitInFlightWork => self.settlement.is_some(),
                TaskReducerEffectKind::ReconcileAction => self.reconcile.is_some(),
                TaskReducerEffectKind::ReleaseTaskTabs => self.release_tabs.is_some(),
                TaskReducerEffectKind::GenerateArtifact => self.generate_artifact.is_some(),
                TaskReducerEffectKind::ExportArtifact => self.export_artifact.is_some(),
                TaskReducerEffectKind::CallModel => self.model.is_some(),
                TaskReducerEffectKind::AwaitHandover => self.handover.is_some(),
                TaskReducerEffectKind::RunToolJob => self.tool_job.is_some(),
                TaskReducerEffectKind::RequestFieldValues => self.field_values.is_some(),
                TaskReducerEffectKind::PrepareDiscoveryTab => self.discovery_bootstrap.is_some(),
                TaskReducerEffectKind::RunLibraryTool => self.library_tool.is_some(),
                TaskReducerEffectKind::RunMemoryTool => self.memory_tool.is_some(),
            }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskProducedArtifactReceipt {
    pub artifact_id: String,
    pub kind: TaskArtifactKind,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskToolOutputReceipt {
    pub digest: Vec<u8>,
    pub byte_count: u64,
    pub chunk_count: u32,
    pub artifact: Option<TaskProducedArtifactReceipt>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskReconciledActionResult {
    pub result_code: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskEffectCompletion {
    pub operation: OperationEnvelope,
    pub effect_id: String,
    pub task_id: String,
    pub kind: TaskReducerEffectKind,
    pub status: TaskEffectCompletionStatus,
    pub effect_result: Option<EffectResult>,
    pub tool_output: Option<TaskToolOutputReceipt>,
    pub reconciled_action_result: Option<TaskReconciledActionResult>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BuiltinSkillReference {
    pub skill_id: BuiltinSkillId,
    pub version: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct StartTaskCommand {
    pub task_id: String,
    pub workspace_id: Option<String>,
    pub browser_profile_id: String,
    pub kind: TaskKind,
    pub goal: String,
    pub control_mode: TaskControlMode,
    pub provider_route_id: Option<String>,
    pub assistant_config_version: u32,
    pub policy_version: u32,
    pub skill_version_id: Option<String>,
    pub tool_allowlist: Vec<String>,
    pub milestone: TaskMilestone,
    pub budgets: Vec<TaskBudget>,
    pub has_task_deadline: bool,
    pub task_deadline_monotonic_ms: u64,
    pub predecessor_task_id: Option<String>,
    pub trace_id: String,
    pub task_id_seed: [u8; 32],
    pub template_id: TaskTemplateId,
    pub consent_preview: TaskConsentPreview,
    pub initial_consent_receipt_id: String,
    pub browser_session_id: String,
    pub task_deadline_utc_ms: u64,
    pub library_refresh: Option<LibraryRefreshRequest>,
    pub builtin_skill: Option<BuiltinSkillReference>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CancelTaskCommand {
    pub task_id: String,
    pub reason: CancelReason,
    pub trace_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PauseTaskCommand {
    pub task_id: String,
    pub trace_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ResumeTaskCommand {
    pub task_id: String,
    pub trace_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TakeOverCommand {
    pub task_id: String,
    pub trace_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct UserDecisionCommand {
    pub task_id: String,
    pub action_id: String,
    pub decision: UserDecisionKind,
    pub approval_digest: String,
    pub trace_id: String,
    pub approval_receipt_id: String,
    pub approval_expires_at_monotonic_ms: u64,
    pub approval_expires_at_utc_ms: u64,
    pub browser_session_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AuthCallbackCommand {
    pub flow_id: String,
    pub redirect_binding_id: String,
    pub returned_state: String,
    pub status: AuthCallbackStatus,
    pub authorization_code_handle: Option<String>,
}

impl AuthCallbackCommand {
    pub fn has_valid_presence(&self) -> bool {
        match self.status {
            AuthCallbackStatus::AuthorizationCode => self.authorization_code_handle.is_some(),
            _ => self.authorization_code_handle.is_none(),
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PermissionResultCommand {
    pub task_id: String,
    pub request_id: String,
    pub permission: PlatformPermission,
    pub decision: PermissionDecision,
    pub trace_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CompleteHandoverCommand {
    pub task_id: String,
    pub handover_id: String,
    pub lease_before: String,
    pub resumed_with: String,
    pub person_input: u32,
    pub trace_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ExpireHandoverCommand {
    pub task_id: String,
    pub handover_id: String,
    pub trace_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SupplyUserInputCommand {
    pub task_id: String,
    pub answer: String,
    pub trace_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct FollowUpCommand {
    pub task_id: String,
    pub question: String,
    pub trace_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SupplyFieldValuesCommand {
    pub task_id: String,
    pub request_id: String,
    pub supplied: u32,
    pub trace_id: String,
    pub outcome: FieldValueAskOutcome,
    pub field_node_ids: Vec<String>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct StartAuthCommand {
    pub flow_id: String,
    pub method: AccountAuthMethod,
    pub redirect_binding_id: String,
    pub scopes: Vec<AccountScope>,
    pub issued_at_monotonic_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RequestEmailLinkCommand {
    pub flow_id: String,
    pub email: String,
    pub redirect_binding_id: String,
    pub scopes: Vec<AccountScope>,
    pub issued_at_monotonic_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SignOutCommand {
    pub account_subject: Option<String>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AuthCredentialResultCommand {
    pub flow_id: String,
    pub method: AccountAuthMethod,
    pub credential_handle: Option<String>,
    pub status: AuthCredentialStatus,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CorrectWorkspaceFactCommand {
    pub workspace_id: String,
    pub expected_revision: u64,
    pub fact_id: String,
    pub value: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ExcludeWorkspaceSourceCommand {
    pub workspace_id: String,
    pub expected_revision: u64,
    pub source_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RequestWorkspaceExportCommand {
    pub request_id: String,
    pub workspace_id: String,
    pub expected_revision: u64,
    pub format: WorkspaceExportFormat,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SaveWorkspaceCommand {
    pub workspace_id: String,
    pub expected_revision: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RenameWorkspaceCommand {
    pub workspace_id: String,
    pub expected_revision: u64,
    pub display_name: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DeleteWorkspaceCommand {
    pub workspace_id: String,
    pub expected_revision: u64,
    pub confirmation_token: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DiscardWorkspaceCommand {
    pub workspace_id: String,
    pub expected_revision: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SearchLibraryCommand {
    pub request_id: String,
    pub query: String,
    pub limit: u32,
    pub requested_at_epoch_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SaveLibraryFactCommand {
    pub workspace_id: String,
    pub expected_workspace_revision: u64,
    pub fact_id: String,
    pub expected_library_revision: u64,
    pub expected_entry_revision: u64,
    pub approved_at_epoch_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RemoveLibraryEntryCommand {
    pub entry_id: String,
    pub expected_library_revision: u64,
    pub expected_entry_revision: u64,
    pub removed_at_epoch_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RequestLibraryExportCommand {
    pub request_id: String,
    pub expected_library_revision: u64,
    pub collection_id: Option<String>,
    pub format: WorkspaceExportFormat,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SearchMemoryCommand {
    pub request_id: String,
    pub query: String,
    pub limit: u32,
    pub requested_at_epoch_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct UpsertMemoryCommand {
    pub memory_id: Option<String>,
    pub statement: String,
    pub scope_kind: MemoryScopeKind,
    pub scope_workspace: Option<MemoryWorkspaceRecord>,
    pub sensitivity: MemorySensitivity,
    pub expected_memory_revision: u64,
    pub expected_record_revision: u64,
    pub expires_at_epoch_ms: u64,
    pub approved_at_epoch_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DeleteMemoryCommand {
    pub memory_id: String,
    pub expected_memory_revision: u64,
    pub expected_record_revision: u64,
    pub deleted_at_epoch_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SaveProviderCredentialCommand {
    pub provider_id: String,
    pub auth_method: ProviderAuthMethod,
    pub credential_handle: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SetProviderCredentialStateCommand {
    pub provider_id: String,
    pub state: ProviderCredentialState,
    pub available_model_ids: Vec<String>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ThinkingPreference {
    pub level: ThinkingLevel,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SetProviderModelPreferenceCommand {
    pub provider_id: String,
    pub model_id: Option<String>,
    pub thinking: Option<ThinkingPreference>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ProbeProviderCredentialCommand {
    pub provider_id: String,
    pub credential_handle: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ForgetProviderCredentialCommand {
    pub provider_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct StartProviderAuthCommand {
    pub flow_id: String,
    pub provider_id: String,
    pub redirect_binding_id: String,
    pub issued_at_monotonic_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CancelProviderAuthCommand {
    pub flow_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ProviderAuthCallbackCommand {
    pub flow_id: String,
    pub redirect_binding_id: String,
    pub returned_state: String,
    pub status: AuthCallbackStatus,
    pub authorization_code_handle: Option<String>,
}

impl ProviderAuthCallbackCommand {
    pub fn has_valid_presence(&self) -> bool {
        match self.status {
            AuthCallbackStatus::AuthorizationCode => self.authorization_code_handle.is_some(),
            _ => self.authorization_code_handle.is_none(),
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CustomModelSpec {
    pub model_id: String,
    pub display_name: String,
    pub context_window: u32,
    pub max_output_tokens: u32,
    pub reasoning: bool,
    pub tool_calling: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DetectedServer {
    pub server_kind: ServerKind,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SaveCustomProviderCommand {
    pub provider_id: String,
    pub display_name: String,
    pub endpoint: String,
    pub wire_api: ProviderWireApi,
    pub credential_handle: Option<String>,
    pub models: Vec<CustomModelSpec>,
    pub detected_server: Option<DetectedServer>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RemoveCustomProviderCommand {
    pub provider_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ProbeCustomEndpointCommand {
    pub endpoint: String,
    pub wire_api: ProviderWireApi,
    pub credential_handle: Option<String>,
    pub provider_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RequestComposerCompletionCommand {
    pub request_id: String,
    pub prefix: String,
    pub suffix: Option<String>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CancelComposerCompletionCommand {
    pub request_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SetAssistantConfigurationCommand {
    pub expected_revision: u64,
    pub disabled_abilities: Vec<AssistantAbility>,
    pub preset: PersonalityPreset,
    pub pace: u32,
    pub length: u32,
    pub check_in: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SavedSignInMetadata {
    pub id: String,
    pub site: String,
    pub username: String,
    pub last_used_epoch_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SavedDetailRecord {
    pub id: String,
    pub given_name: String,
    pub family_name: String,
    pub email: String,
    pub phone: String,
    pub address: String,
    pub postcode: String,
    pub country: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ReplaceSavedDataSnapshotCommand {
    pub sign_ins_availability: SavedDataAvailability,
    pub sign_ins_revision: u64,
    pub sign_ins: Vec<SavedSignInMetadata>,
    pub details_availability: SavedDataAvailability,
    pub details_revision: u64,
    pub details: Vec<SavedDetailRecord>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AcceptTaskArtifactCommand {
    pub task_id: String,
    pub artifact_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ExportTaskArtifactCommand {
    pub task_id: String,
    pub artifact_id: String,
    pub kind: TaskArtifactKind,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SkillObservedClause {
    pub kind: SkillClauseKind,
    pub role: u32,
    pub detail: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SkillSemanticTarget {
    pub role: u32,
    pub phrase: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SkillObservedArgument {
    pub parameter: u32,
    pub kind: SkillArgumentKind,
    pub value: u64,
    pub purpose: u32,
    pub public_address: Option<String>,
    pub semantic_target: Option<SkillSemanticTarget>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SkillObservedStep {
    pub verb: String,
    pub arguments: Vec<SkillObservedArgument>,
    pub postcondition: u32,
    pub has_fill: bool,
    pub fill_purpose: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MutateSkillCommand {
    pub kind: SkillMutationKind,
    pub skill_id: String,
    pub expected_version: u32,
    pub origin: String,
    pub clauses: Vec<SkillObservedClause>,
    pub steps: Vec<SkillObservedStep>,
    pub admitted: u32,
    pub enabled: bool,
    pub recorded_at_epoch_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CoreServiceCommand {
    pub operation: OperationEnvelope,
    pub kind: CoreServiceCommandKind,
    pub start_task: Option<StartTaskCommand>,
    pub cancel_task: Option<CancelTaskCommand>,
    pub user_decision: Option<UserDecisionCommand>,
    pub auth_callback: Option<AuthCallbackCommand>,
    pub permission_result: Option<PermissionResultCommand>,
    pub start_auth: Option<StartAuthCommand>,
    pub request_email_link: Option<RequestEmailLinkCommand>,
    pub sign_out: Option<SignOutCommand>,
    pub auth_credential_result: Option<AuthCredentialResultCommand>,
    pub correct_workspace_fact: Option<CorrectWorkspaceFactCommand>,
    pub exclude_workspace_source: Option<ExcludeWorkspaceSourceCommand>,
    pub request_workspace_export: Option<RequestWorkspaceExportCommand>,
    pub set_asset_delivery_policy: Option<SetAssetDeliveryPolicyCommand>,
    pub request_asset: Option<RequestAssetCommand>,
    pub remove_asset: Option<RemoveAssetCommand>,
    pub save_provider_credential: Option<SaveProviderCredentialCommand>,
    pub forget_provider_credential: Option<ForgetProviderCredentialCommand>,
    pub start_provider_auth: Option<StartProviderAuthCommand>,
    pub provider_auth_callback: Option<ProviderAuthCallbackCommand>,
    pub save_custom_provider: Option<SaveCustomProviderCommand>,
    pub remove_custom_provider: Option<RemoveCustomProviderCommand>,
    pub complete_handover: Option<CompleteHandoverCommand>,
    pub expire_handover: Option<ExpireHandoverCommand>,
    pub supply_user_input: Option<SupplyUserInputCommand>,
    pub set_provider_credential_state: Option<SetProviderCredentialStateCommand>,
    pub probe_provider_credential: Option<ProbeProviderCredentialCommand>,
    pub supply_field_values: Option<SupplyFieldValuesCommand>,
    pub set_provider_model_preference: Option<SetProviderModelPreferenceCommand>,
    pub probe_custom_endpoint: Option<ProbeCustomEndpointCommand>,
    pub request_composer_completion: Option<RequestComposerCompletionCommand>,
    pub cancel_composer_completion: Option<CancelComposerCompletionCommand>,
    pub pause_task: Option<PauseTaskCommand>,
    pub resume_task: Option<ResumeTaskCommand>,
    pub take_over: Option<TakeOverCommand>,
    pub set_assistant_configuration: Option<SetAssistantConfigurationCommand>,
    pub save_workspace: Option<SaveWorkspaceCommand>,
    pub rename_workspace: Option<RenameWorkspaceCommand>,
    pub delete_workspace: Option<DeleteWorkspaceCommand>,
    pub discard_workspace: Option<DiscardWorkspaceCommand>,
    pub search_library: Option<SearchLibraryCommand>,
    pub save_library_fact: Option<SaveLibraryFactCommand>,
    pub remove_library_entry: Option<RemoveLibraryEntryCommand>,
    pub request_library_export: Option<RequestLibraryExportCommand>,
    pub search_memory: Option<SearchMemoryCommand>,
    pub upsert_memory: Option<UpsertMemoryCommand>,
    pub delete_memory: Option<DeleteMemoryCommand>,
    pub accept_task_artifact: Option<AcceptTaskArtifactCommand>,
    pub export_task_artifact: Option<ExportTaskArtifactCommand>,
    pub replace_saved_data_snapshot: Option<ReplaceSavedDataSnapshotCommand>,
    pub mutate_skill: Option<MutateSkillCommand>,
    pub cancel_provider_auth: Option<CancelProviderAuthCommand>,
    pub follow_up: Option<FollowUpCommand>,
}

impl CoreServiceCommand {
    pub fn has_valid_body(&self) -> bool {
        let body_count = [
            self.start_task.is_some(),
            self.cancel_task.is_some(),
            self.user_decision.is_some(),
            self.auth_callback.is_some(),
            self.permission_result.is_some(),
            self.start_auth.is_some(),
            self.request_email_link.is_some(),
            self.sign_out.is_some(),
            self.auth_credential_result.is_some(),
            self.correct_workspace_fact.is_some(),
            self.exclude_workspace_source.is_some(),
            self.request_workspace_export.is_some(),
            self.set_asset_delivery_policy.is_some(),
            self.request_asset.is_some(),
            self.remove_asset.is_some(),
            self.save_provider_credential.is_some(),
            self.forget_provider_credential.is_some(),
            self.start_provider_auth.is_some(),
            self.provider_auth_callback.is_some(),
            self.save_custom_provider.is_some(),
            self.remove_custom_provider.is_some(),
            self.complete_handover.is_some(),
            self.expire_handover.is_some(),
            self.supply_user_input.is_some(),
            self.set_provider_credential_state.is_some(),
            self.probe_provider_credential.is_some(),
            self.supply_field_values.is_some(),
            self.set_provider_model_preference.is_some(),
            self.probe_custom_endpoint.is_some(),
            self.request_composer_completion.is_some(),
            self.cancel_composer_completion.is_some(),
            self.pause_task.is_some(),
            self.resume_task.is_some(),
            self.take_over.is_some(),
            self.set_assistant_configuration.is_some(),
            self.save_workspace.is_some(),
            self.rename_workspace.is_some(),
            self.delete_workspace.is_some(),
            self.discard_workspace.is_some(),
            self.search_library.is_some(),
            self.save_library_fact.is_some(),
            self.remove_library_entry.is_some(),
            self.request_library_export.is_some(),
            self.search_memory.is_some(),
            self.upsert_memory.is_some(),
            self.delete_memory.is_some(),
            self.accept_task_artifact.is_some(),
            self.export_task_artifact.is_some(),
            self.replace_saved_data_snapshot.is_some(),
            self.mutate_skill.is_some(),
            self.cancel_provider_auth.is_some(),
            self.follow_up.is_some(),
        ]
        .into_iter()
        .filter(|present| *present)
        .count();
        body_count == 1
            && match self.kind {
                CoreServiceCommandKind::StartTask => self.start_task.is_some(),
                CoreServiceCommandKind::CancelTask => self.cancel_task.is_some(),
                CoreServiceCommandKind::UserDecision => self.user_decision.is_some(),
                CoreServiceCommandKind::AuthCallback => self.auth_callback.is_some(),
                CoreServiceCommandKind::PermissionResult => self.permission_result.is_some(),
                CoreServiceCommandKind::StartAuth => self.start_auth.is_some(),
                CoreServiceCommandKind::RequestEmailLink => self.request_email_link.is_some(),
                CoreServiceCommandKind::SignOut => self.sign_out.is_some(),
                CoreServiceCommandKind::AuthCredentialResult => self.auth_credential_result.is_some(),
                CoreServiceCommandKind::CorrectWorkspaceFact => self.correct_workspace_fact.is_some(),
                CoreServiceCommandKind::ExcludeWorkspaceSource => self.exclude_workspace_source.is_some(),
                CoreServiceCommandKind::RequestWorkspaceExport => self.request_workspace_export.is_some(),
                CoreServiceCommandKind::SetAssetDeliveryPolicy => self.set_asset_delivery_policy.is_some(),
                CoreServiceCommandKind::RequestAsset => self.request_asset.is_some(),
                CoreServiceCommandKind::RemoveAsset => self.remove_asset.is_some(),
                CoreServiceCommandKind::SaveProviderCredential => self.save_provider_credential.is_some(),
                CoreServiceCommandKind::ForgetProviderCredential => self.forget_provider_credential.is_some(),
                CoreServiceCommandKind::StartProviderAuth => self.start_provider_auth.is_some(),
                CoreServiceCommandKind::ProviderAuthCallback => self.provider_auth_callback.is_some(),
                CoreServiceCommandKind::SaveCustomProvider => self.save_custom_provider.is_some(),
                CoreServiceCommandKind::RemoveCustomProvider => self.remove_custom_provider.is_some(),
                CoreServiceCommandKind::CompleteHandover => self.complete_handover.is_some(),
                CoreServiceCommandKind::ExpireHandover => self.expire_handover.is_some(),
                CoreServiceCommandKind::SupplyUserInput => self.supply_user_input.is_some(),
                CoreServiceCommandKind::SetProviderCredentialState => self.set_provider_credential_state.is_some(),
                CoreServiceCommandKind::ProbeProviderCredential => self.probe_provider_credential.is_some(),
                CoreServiceCommandKind::SupplyFieldValues => self.supply_field_values.is_some(),
                CoreServiceCommandKind::SetProviderModelPreference => self.set_provider_model_preference.is_some(),
                CoreServiceCommandKind::ProbeCustomEndpoint => self.probe_custom_endpoint.is_some(),
                CoreServiceCommandKind::RequestComposerCompletion => self.request_composer_completion.is_some(),
                CoreServiceCommandKind::CancelComposerCompletion => self.cancel_composer_completion.is_some(),
                CoreServiceCommandKind::PauseTask => self.pause_task.is_some(),
                CoreServiceCommandKind::ResumeTask => self.resume_task.is_some(),
                CoreServiceCommandKind::TakeOver => self.take_over.is_some(),
                CoreServiceCommandKind::SetAssistantConfiguration => self.set_assistant_configuration.is_some(),
                CoreServiceCommandKind::SaveWorkspace => self.save_workspace.is_some(),
                CoreServiceCommandKind::RenameWorkspace => self.rename_workspace.is_some(),
                CoreServiceCommandKind::DeleteWorkspace => self.delete_workspace.is_some(),
                CoreServiceCommandKind::DiscardWorkspace => self.discard_workspace.is_some(),
                CoreServiceCommandKind::SearchLibrary => self.search_library.is_some(),
                CoreServiceCommandKind::SaveLibraryFact => self.save_library_fact.is_some(),
                CoreServiceCommandKind::RemoveLibraryEntry => self.remove_library_entry.is_some(),
                CoreServiceCommandKind::RequestLibraryExport => self.request_library_export.is_some(),
                CoreServiceCommandKind::SearchMemory => self.search_memory.is_some(),
                CoreServiceCommandKind::UpsertMemory => self.upsert_memory.is_some(),
                CoreServiceCommandKind::DeleteMemory => self.delete_memory.is_some(),
                CoreServiceCommandKind::AcceptTaskArtifact => self.accept_task_artifact.is_some(),
                CoreServiceCommandKind::ExportTaskArtifact => self.export_task_artifact.is_some(),
                CoreServiceCommandKind::ReplaceSavedDataSnapshot => self.replace_saved_data_snapshot.is_some(),
                CoreServiceCommandKind::MutateSkill => self.mutate_skill.is_some(),
                CoreServiceCommandKind::CancelProviderAuth => self.cancel_provider_auth.is_some(),
                CoreServiceCommandKind::FollowUp => self.follow_up.is_some(),
            }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CoreBootstrapResult {
    pub status: InitializationStatus,
    pub accepted_generation: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Admission {
    pub operation_id: String,
    pub status: AdmissionStatus,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ModelArtifactRegistration {
    pub asset_id: String,
    pub asset_revision: String,
    pub asset_kind: AssetKind,
    pub format: ToolModelArtifactKind,
    pub adapter: bool,
    pub byte_length: u64,
    pub digest: [u8; 32],
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CoreStateUpdate {
    pub service_generation: u64,
    pub sequence: u64,
    pub core_status_schema_version: u32,
    pub payload: Vec<u8>,
    pub model_artifacts: Vec<ModelArtifactRegistration>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PendingApprovalBinding {
    pub task_id: String,
    pub action_id: String,
    pub proposal_digest: String,
    pub service_generation: u64,
    pub task_revision: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskRevisionBinding {
    pub task_id: String,
    pub service_generation: u64,
    pub task_revision: u64,
    pub allowed_controls: Vec<TaskControlKind>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskSettlementBinding {
    pub task_id: String,
    pub service_generation: u64,
    pub task_revision: u64,
    pub kind: TaskSettlementKind,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TerminalTaskBinding {
    pub task_id: String,
    pub service_generation: u64,
    pub task_revision: u64,
    pub kind: TerminalTaskKind,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PendingPermissionBinding {
    pub task_id: String,
    pub request_id: String,
    pub permission: PlatformPermission,
    pub service_generation: u64,
    pub task_revision: u64,
    pub deadline_monotonic_ms: u64,
    pub deadline_utc_ms: u64,
    pub browser_session_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AcceptedTaskConsentBinding {
    pub task_id: String,
    pub service_generation: u64,
    pub current_task_revision: u64,
    pub accepted_revision: u64,
    pub browser_session_id: String,
    pub receipt_id: String,
    pub consent_preview: TaskConsentPreview,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CommittedActionApprovalBinding {
    pub task_id: String,
    pub action_id: String,
    pub service_generation: u64,
    pub committed_revision: u64,
    pub receipt_id: String,
    pub proposal_digest: String,
    pub expires_at_monotonic_ms: u64,
    pub expires_at_utc_ms: u64,
    pub browser_session_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CoreStateBrowserBindings {
    pub service_generation: u64,
    pub state_sequence: u64,
    pub task_revisions: Vec<TaskRevisionBinding>,
    pub pending_approvals: Vec<PendingApprovalBinding>,
    pub task_settlements: Vec<TaskSettlementBinding>,
    pub pending_permissions: Vec<PendingPermissionBinding>,
    pub terminal_tasks: Vec<TerminalTaskBinding>,
    pub accepted_task_consents: Vec<AcceptedTaskConsentBinding>,
    pub committed_action_approvals: Vec<CommittedActionApprovalBinding>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct WorkspacePersistEffect {
    pub workspace_id: String,
    pub snapshot: Vec<u8>,
    pub expected_revision: u64,
    pub resulting_revision: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct WorkspaceDeletionEffect {
    pub workspace_id: String,
    pub expected_revision: u64,
    pub resulting_revision: u64,
    pub sources: u32,
    pub facts: u32,
    pub artifact_metadata: u32,
    pub derived_indexes: u32,
    pub confirmation_token: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SkillInstallEffect {
    pub skill_id: String,
    pub origin: String,
    pub provenance: SkillProvenance,
    pub version: u32,
    pub definition: Vec<u8>,
    pub step_count: u32,
    pub recorded_at_utc_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SkillStatusEffect {
    pub skill_id: String,
    pub status: SkillStatus,
    pub changed_at_utc_ms: u64,
    pub version: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SkillRunEffect {
    pub skill_id: String,
    pub version: u32,
    pub task_id: String,
    pub outcome: SkillRunOutcome,
    pub ran_at_utc_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SkillForgetEffect {
    pub skill_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SourceDeletionEffect {
    pub source_id: String,
    pub origin: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AssistantConfigurationPersistEffect {
    pub disabled_abilities: Vec<AssistantAbility>,
    pub preset: PersonalityPreset,
    pub pace: u32,
    pub length: u32,
    pub check_in: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct LibraryPersistEffect {
    pub entry: LibraryEntryRecord,
    pub expected_entry_revision: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct LibraryDeletionEffect {
    pub entry_id: String,
    pub expected_entry_revision: u64,
    pub resulting_entry_revision: u64,
    pub removed_at_epoch_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MemoryPersistEffect {
    pub record: MemoryRecord,
    pub expected_record_revision: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MemoryDeletionEffect {
    pub memory_id: String,
    pub expected_record_revision: u64,
    pub resulting_record_revision: u64,
    pub deleted_at_epoch_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct StorageCommitEffect {
    pub operation_kind: StorageOperation,
    pub task_id: String,
    pub expected_revision: u64,
    pub resulting_revision: u64,
    pub transaction_batch: Vec<u8>,
    pub task_id_seed: [u8; 32],
    pub workspace: Option<WorkspacePersistEffect>,
    pub install_skill: Option<SkillInstallEffect>,
    pub skill_status: Option<SkillStatusEffect>,
    pub skill_run: Option<SkillRunEffect>,
    pub forget_skill: Option<SkillForgetEffect>,
    pub source_deletion: Option<SourceDeletionEffect>,
    pub assistant_configuration: Option<AssistantConfigurationPersistEffect>,
    pub workspace_deletion: Option<WorkspaceDeletionEffect>,
    pub library_entry: Option<LibraryPersistEffect>,
    pub library_deletion: Option<LibraryDeletionEffect>,
    pub memory_record: Option<MemoryPersistEffect>,
    pub memory_deletion: Option<MemoryDeletionEffect>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PageObservationEffect {
    pub tab_id: String,
    pub frame_id: String,
    pub page_epoch: String,
    pub scope: ObservationScope,
    pub max_bytes: u32,
    pub task_id: String,
    pub action_id: String,
    pub capability_id: String,
    pub proposal_digest: String,
    pub idempotency_key: String,
    pub authority_subject: AuthoritySubject,
    pub max_nodes: u32,
    pub max_text_bytes: u32,
    pub max_frames: u32,
    pub deadline_ms: u32,
    pub expected_graph_revision: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ModelStaticHeader {
    pub name: String,
    pub value: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ModelRequestEffect {
    pub route_id: String,
    pub model_id: String,
    pub disclosure: DisclosureClass,
    pub request_body: Vec<u8>,
    pub max_output_bytes: u32,
    pub task_id: String,
    pub provider_id: String,
    pub wire_api: ProviderWireApi,
    pub endpoint: String,
    pub credential_handle: Option<String>,
    pub static_headers: Vec<ModelStaticHeader>,
    pub probe: bool,
    pub endpoint_kind: ModelEndpointKind,
    pub media_attachment_handle: Option<String>,
    pub media_attachment_mime_type: Option<String>,
    pub not_before_monotonic_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ExchangeAuthorizationCodeRequest {
    pub flow_id: String,
    pub auth_method: AccountAuthMethod,
    pub authorization_code_handle: String,
    pub pkce_verifier_handle: String,
    pub redirect_binding_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ExchangeNativeCredentialRequest {
    pub flow_id: String,
    pub auth_method: AccountAuthMethod,
    pub credential_handle: String,
    pub raw_nonce_handle: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct EmailLinkNetworkRequest {
    pub flow_id: String,
    pub email: String,
    pub pkce_verifier_handle: String,
    pub redirect_binding_id: String,
    pub pkce_challenge: String,
    pub state: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RefreshSessionRequest {
    pub session_handle: String,
    pub expected_rotation: u64,
    pub expected_account_subject: String,
    pub expected_auth_method: AccountAuthMethod,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RevokeSessionRequest {
    pub session_handle: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct FetchEntitlementRequest {
    pub reason: EntitlementFetchReason,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct NetworkRequestEffect {
    pub operation_kind: AccountNetworkOperation,
    pub exchange_authorization_code: Option<ExchangeAuthorizationCodeRequest>,
    pub exchange_native_credential: Option<ExchangeNativeCredentialRequest>,
    pub request_email_link: Option<EmailLinkNetworkRequest>,
    pub refresh_session: Option<RefreshSessionRequest>,
    pub revoke_session: Option<RevokeSessionRequest>,
    pub max_response_bytes: u32,
    pub fetch_entitlement: Option<FetchEntitlementRequest>,
}

impl NetworkRequestEffect {
    pub fn has_valid_body(&self) -> bool {
        let body_count = [
            self.exchange_authorization_code.is_some(),
            self.exchange_native_credential.is_some(),
            self.request_email_link.is_some(),
            self.refresh_session.is_some(),
            self.revoke_session.is_some(),
            self.fetch_entitlement.is_some(),
        ]
        .into_iter()
        .filter(|present| *present)
        .count();
        body_count == 1
            && match self.operation_kind {
                AccountNetworkOperation::ExchangeAuthorizationCode => self.exchange_authorization_code.is_some(),
                AccountNetworkOperation::ExchangeNativeCredential => self.exchange_native_credential.is_some(),
                AccountNetworkOperation::RequestEmailLink => self.request_email_link.is_some(),
                AccountNetworkOperation::RefreshSession => self.refresh_session.is_some(),
                AccountNetworkOperation::RevokeSession => self.revoke_session.is_some(),
                AccountNetworkOperation::FetchEntitlement => self.fetch_entitlement.is_some(),
            }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BrowserActionEffect {
    pub operation_kind: BrowserActionOperation,
    pub action_id: String,
    pub grant_reference: String,
    pub approval_digest: String,
    pub origin_scope_digest: String,
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
pub struct ToolResourceBudget {
    pub max_input_bytes: u64,
    pub max_output_bytes: u64,
    pub max_memory_bytes: u64,
    pub max_cpu_ms: u64,
    pub max_temporary_bytes: u64,
    pub max_output_chunks: u32,
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
pub struct ToolJobEffect {
    pub job_id: String,
    pub runtime: ToolRuntimeKind,
    pub tool_id: String,
    pub tool_version: String,
    pub operation_kind: ToolOperation,
    pub budget: ToolResourceBudget,
    pub bundled_python: Option<BundledPythonArguments>,
    pub local_model: Option<LocalModelArguments>,
    pub media_probe: Option<MediaProbeArguments>,
    pub audio_extract: Option<AudioExtractArguments>,
    pub frame_sample: Option<FrameSampleArguments>,
    pub transcode: Option<TranscodeArguments>,
    pub signed_wasm: Option<SignedWasmArguments>,
    pub local_embedding: Option<LocalEmbeddingArguments>,
    pub task_id: String,
}

impl ToolJobEffect {
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
pub struct GenerateEntropyRequest {
    pub flow_id: String,
    pub byte_count: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct WriteTransientSecretRequest {
    pub flow_id: String,
    pub purpose: SecretMaterialPurpose,
    pub material: Vec<u8>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DeleteSecretHandleRequest {
    pub secret_handle: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SecureStoreEffect {
    pub operation_kind: SecureStoreOperation,
    pub generate_entropy: Option<GenerateEntropyRequest>,
    pub write_transient: Option<WriteTransientSecretRequest>,
    pub delete_handle: Option<DeleteSecretHandleRequest>,
}

impl SecureStoreEffect {
    pub fn has_valid_body(&self) -> bool {
        let body_count = [
            self.generate_entropy.is_some(),
            self.write_transient.is_some(),
            self.delete_handle.is_some(),
        ]
        .into_iter()
        .filter(|present| *present)
        .count();
        body_count == 1
            && match self.operation_kind {
                SecureStoreOperation::GenerateEntropy => self.generate_entropy.is_some(),
                SecureStoreOperation::WriteTransient => self.write_transient.is_some(),
                SecureStoreOperation::DeleteHandle => self.delete_handle.is_some(),
            }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct OAuthSurfaceRequest {
    pub flow_id: String,
    pub auth_method: AccountAuthMethod,
    pub redirect_binding_id: String,
    pub pkce_challenge: String,
    pub state: String,
    pub scopes: Vec<AccountScope>,
    pub pkce_verifier_handle: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct NativeCredentialSurfaceRequest {
    pub flow_id: String,
    pub auth_method: AccountAuthMethod,
    pub raw_nonce_handle: String,
    pub hashed_nonce: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AuthSurfaceEffect {
    pub operation_kind: AuthSurfaceOperation,
    pub oauth: Option<OAuthSurfaceRequest>,
    pub native_credential: Option<NativeCredentialSurfaceRequest>,
}

impl AuthSurfaceEffect {
    pub fn has_valid_body(&self) -> bool {
        let body_count = [
            self.oauth.is_some(),
            self.native_credential.is_some(),
        ]
        .into_iter()
        .filter(|present| *present)
        .count();
        body_count == 1
            && match self.operation_kind {
                AuthSurfaceOperation::OpenOauth => self.oauth.is_some(),
                AuthSurfaceOperation::RequestNativeCredential => self.native_credential.is_some(),
            }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PermissionRequestEffect {
    pub request_id: String,
    pub permission: PlatformPermission,
    pub task_id: String,
    pub task_revision: u64,
    pub deadline_monotonic_ms: u64,
    pub deadline_utc_ms: u64,
    pub browser_session_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ProviderListingFetchEffect {
    pub provider_id: String,
    pub endpoint: String,
    pub wire_api: ProviderWireApi,
    pub credential_handle: Option<String>,
    pub max_response_bytes: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ProviderListingFetchResult {
    pub provider_id: String,
    pub disposition: CatalogFetchDisposition,
    pub body: Vec<u8>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CustomEndpointProbeEffect {
    pub provider_id: String,
    pub endpoint: String,
    pub wire_api: ProviderWireApi,
    pub credential_handle: Option<String>,
    pub max_response_bytes: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CustomEndpointProbeResult {
    pub provider_id: String,
    pub reached: bool,
    pub detected_server: Option<DetectedServer>,
    pub model_count: u32,
    pub models: Vec<CustomModelSpec>,
    pub proved_base: Option<String>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ComposerCompletionEffect {
    pub request_id: String,
    pub text: Option<String>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ComposerCompletionEffectResult {
    pub request_id: String,
    pub delivered: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct EffectEnvelope {
    pub operation: OperationEnvelope,
    pub effect_id: String,
    pub kind: EffectKind,
    pub retry_class: RetryClass,
    pub storage_commit: Option<StorageCommitEffect>,
    pub page_observation: Option<PageObservationEffect>,
    pub model_request: Option<ModelRequestEffect>,
    pub network_request: Option<NetworkRequestEffect>,
    pub browser_action: Option<BrowserActionEffect>,
    pub tool_job: Option<ToolJobEffect>,
    pub secure_store: Option<SecureStoreEffect>,
    pub auth_surface: Option<AuthSurfaceEffect>,
    pub permission_request: Option<PermissionRequestEffect>,
    pub asset_delivery: Option<AssetDeliveryEffect>,
    pub catalog_fetch: Option<CatalogFetchEffect>,
    pub provider_listing_fetch: Option<ProviderListingFetchEffect>,
    pub composer_completion: Option<ComposerCompletionEffect>,
    pub custom_endpoint_probe: Option<CustomEndpointProbeEffect>,
}

impl EffectEnvelope {
    pub fn has_valid_body(&self) -> bool {
        let body_count = [
            self.storage_commit.is_some(),
            self.page_observation.is_some(),
            self.model_request.is_some(),
            self.network_request.is_some(),
            self.browser_action.is_some(),
            self.tool_job.is_some(),
            self.secure_store.is_some(),
            self.auth_surface.is_some(),
            self.permission_request.is_some(),
            self.asset_delivery.is_some(),
            self.catalog_fetch.is_some(),
            self.provider_listing_fetch.is_some(),
            self.composer_completion.is_some(),
            self.custom_endpoint_probe.is_some(),
        ]
        .into_iter()
        .filter(|present| *present)
        .count();
        body_count == 1
            && match self.kind {
                EffectKind::StorageCommit => self.storage_commit.is_some(),
                EffectKind::PageObservation => self.page_observation.is_some(),
                EffectKind::ModelRequest => self.model_request.is_some(),
                EffectKind::NetworkRequest => self.network_request.is_some(),
                EffectKind::BrowserAction => self.browser_action.is_some(),
                EffectKind::ToolJob => self.tool_job.is_some(),
                EffectKind::SecureStore => self.secure_store.is_some(),
                EffectKind::OpenAuthSurface => self.auth_surface.is_some(),
                EffectKind::RequestPermission => self.permission_request.is_some(),
                EffectKind::DeliverAsset => self.asset_delivery.is_some(),
                EffectKind::FetchCatalog => self.catalog_fetch.is_some(),
                EffectKind::FetchProviderListing => self.provider_listing_fetch.is_some(),
                EffectKind::DeliverComposerCompletion => self.composer_completion.is_some(),
                EffectKind::ProbeCustomEndpoint => self.custom_endpoint_probe.is_some(),
            }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct StorageEffectResult {
    pub committed_revision: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MediaObservationFact {
    pub kind: MediaFactKind,
    pub evidence: MediaEvidenceKind,
    pub text: String,
    pub source_locator: String,
    pub source_start: u32,
    pub source_end: u32,
    pub page_index_plus_one: u32,
    pub timestamp_start_ms: u64,
    pub timestamp_end_ms: u64,
    pub row_index_plus_one: u32,
    pub confidence_ppm: u32,
    pub truncated: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MediaCaptureProvenance {
    pub capture_x_dip: u32,
    pub capture_y_dip: u32,
    pub capture_width_dip: u32,
    pub capture_height_dip: u32,
    pub viewport_width_dip: u32,
    pub viewport_height_dip: u32,
    pub output_scale_ppm: u32,
    pub captured_at_monotonic_ms: u64,
    pub redacted_region_count: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MediaObservationResult {
    pub kind: MediaObservationKind,
    pub facts: Vec<MediaObservationFact>,
    pub attachment_handle: Option<String>,
    pub attachment_mime_type: Option<String>,
    pub width_px: u32,
    pub height_px: u32,
    pub has_meaningful_text: bool,
    pub scanned_pdf_ocr_required: bool,
    pub capture_provenance: Option<MediaCaptureProvenance>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ObservationEffectResult {
    pub status: BipObservationStatus,
    pub schema_version: String,
    pub tab_id: String,
    pub frame_id: String,
    pub page_epoch: String,
    pub graph_revision: u64,
    pub origin: String,
    pub is_potentially_trustworthy: bool,
    pub private_profile: bool,
    pub node_count: u32,
    pub total_bytes: u32,
    pub truncated: bool,
    pub may_change_answer: bool,
    pub redacted_field_count: u32,
    pub suppressed_secret_value_count: u32,
    pub sensitive_zone_count: u32,
    pub policy_filtered_frame_count: u32,
    pub highest_sensitivity: BipSensitivity,
    pub graph_encoding: BipGraphEncoding,
    pub graph_payload: Vec<u8>,
    pub media: Option<MediaObservationResult>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PageSnapshotExportCommand {
    pub operation: OperationEnvelope,
    pub format: PageSnapshotExportFormat,
    pub expected_tab_id: String,
    pub expected_frame_id: String,
    pub expected_page_epoch: String,
    pub expected_graph_revision: u64,
    pub expected_origin: String,
    pub max_bytes: u32,
    pub observation: ObservationEffectResult,
    pub captured_at_epoch_ms: u64,
    pub source_query_withheld: bool,
    pub source_fragment_withheld: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PageSnapshotExportResult {
    pub operation: OperationEnvelope,
    pub status: PageSnapshotExportStatus,
    pub format: PageSnapshotExportFormat,
    pub tab_id: String,
    pub frame_id: String,
    pub page_epoch: String,
    pub graph_revision: u64,
    pub origin: String,
    pub mime_type: String,
    pub suggested_file_name: String,
    pub content: Vec<u8>,
    pub node_count: u32,
    pub redacted_field_count: u32,
    pub suppressed_secret_value_count: u32,
    pub withheld_field_count: u32,
    pub captured_at_epoch_ms: u64,
    pub source_query_withheld: bool,
    pub source_fragment_withheld: bool,
    pub secure_context: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SiteSkillMatchCommand {
    pub operation: OperationEnvelope,
    pub expected_tab_id: String,
    pub expected_frame_id: String,
    pub expected_page_epoch: String,
    pub expected_graph_revision: u64,
    pub expected_origin: String,
    pub observation: ObservationEffectResult,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SiteSkillMatchOffer {
    pub skill_version_id: String,
    pub skill_id: String,
    pub active_version: u32,
    pub step_count: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SiteSkillMatchResult {
    pub operation: OperationEnvelope,
    pub status: SiteSkillMatchStatus,
    pub tab_id: String,
    pub frame_id: String,
    pub page_epoch: String,
    pub graph_revision: u64,
    pub origin: String,
    pub offers: Vec<SiteSkillMatchOffer>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ModelStreamChunk {
    pub operation: OperationEnvelope,
    pub effect_id: String,
    pub sequence: u32,
    pub data: Vec<u8>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskAnswerEvent {
    pub task_id: String,
    pub call_id: String,
    pub sequence: u32,
    pub text: Option<String>,
    pub terminal: bool,
    pub complete: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ModelFailure {
    pub error_class: ModelErrorClass,
    pub has_retry_after: bool,
    pub retry_after_millis: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ModelEffectResult {
    pub model_id: String,
    pub completion: Vec<u8>,
    pub input_units: u64,
    pub output_units: u64,
    pub provider_http_status: u32,
    pub streamed: bool,
    pub failure: Option<ModelFailure>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AccountTokenValidationRequest {
    pub operation: OperationEnvelope,
    pub operation_kind: AccountNetworkOperation,
    pub expected_auth_method: AccountAuthMethod,
    pub expected_account_subject: Option<String>,
    pub target_rotation: u64,
    pub response_body: Vec<u8>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AccountTokenValidationResult {
    pub status: AccountTokenValidationStatus,
    pub operation_id: String,
    pub operation_kind: AccountNetworkOperation,
    pub auth_method: AccountAuthMethod,
    pub account_subject: String,
    pub expires_in_seconds: u64,
    pub target_rotation: u64,
    pub access_token: Vec<u8>,
    pub refresh_token: Vec<u8>,
    pub email: Option<String>,
    pub display_name: Option<String>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AccountSessionReceipt {
    pub session_handle: String,
    pub account_subject: String,
    pub expires_at_monotonic_ms: u64,
    pub rotation: u64,
    pub auth_method: AccountAuthMethod,
    pub email: Option<String>,
    pub display_name: Option<String>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct EntitlementSummaryResult {
    pub definitive_absent: bool,
    pub plan_id: String,
    pub model_ids: Vec<String>,
    pub window_seconds: u32,
    pub requests_remaining: u64,
    pub credits_granted: u64,
    pub credits_remaining: u64,
    pub credit_unit_micros: u64,
    pub next_renewal_epoch_seconds: u64,
    pub valid_until_epoch_seconds: u64,
    pub minted_at_utc_ms: u64,
    pub worker_host: String,
    pub gateway_host: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct EmailLinkNetworkResult {
    pub flow_id: String,
    pub accepted: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RevokedSessionResult {
    pub session_handle: String,
    pub deleted: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct NetworkEffectResult {
    pub operation_kind: AccountNetworkOperation,
    pub authorization_code_session: Option<AccountSessionReceipt>,
    pub native_credential_session: Option<AccountSessionReceipt>,
    pub email_link: Option<EmailLinkNetworkResult>,
    pub refreshed_session: Option<AccountSessionReceipt>,
    pub revoked_session: Option<RevokedSessionResult>,
    pub entitlement_summary: Option<EntitlementSummaryResult>,
}

impl NetworkEffectResult {
    pub fn has_valid_body(&self) -> bool {
        let body_count = [
            self.authorization_code_session.is_some(),
            self.native_credential_session.is_some(),
            self.email_link.is_some(),
            self.refreshed_session.is_some(),
            self.revoked_session.is_some(),
            self.entitlement_summary.is_some(),
        ]
        .into_iter()
        .filter(|present| *present)
        .count();
        body_count == 1
            && match self.operation_kind {
                AccountNetworkOperation::ExchangeAuthorizationCode => self.authorization_code_session.is_some(),
                AccountNetworkOperation::ExchangeNativeCredential => self.native_credential_session.is_some(),
                AccountNetworkOperation::RequestEmailLink => self.email_link.is_some(),
                AccountNetworkOperation::RefreshSession => self.refreshed_session.is_some(),
                AccountNetworkOperation::RevokeSession => self.revoked_session.is_some(),
                AccountNetworkOperation::FetchEntitlement => self.entitlement_summary.is_some(),
            }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskTabSnapshot {
    pub target: TaskTabDocumentTarget,
    pub active: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskTabActionResult {
    pub browser_session_id: String,
    pub operation_kind: TaskActionOperationKind,
    pub postcondition: TaskTabPostcondition,
    pub tabs: Vec<TaskTabSnapshot>,
    pub target: Option<TaskTabDocumentTarget>,
    pub state_was_already_satisfied: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskDownloadSnapshot {
    pub download_id: String,
    pub state: TaskDownloadState,
    pub media_type: TaskDownloadMediaType,
    pub received_bytes: u64,
    pub directory_class: TaskDownloadDirectoryClass,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskDownloadActionResult {
    pub browser_session_id: String,
    pub operation_kind: TaskActionOperationKind,
    pub postcondition: TaskDownloadPostcondition,
    pub downloads: Vec<TaskDownloadSnapshot>,
    pub truncated: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskActionRefusal {
    pub code: TaskActionResultCode,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BrowserActionEffectResult {
    pub outcome: BrowserActionOutcome,
    pub dispatch_id: Option<String>,
    pub discovered_source: Option<TaskConsentSource>,
    pub discovery_tab_id: Option<String>,
    pub browser_session_id: Option<String>,
    pub task_tab: Option<TaskTabActionResult>,
    pub task_download: Option<TaskDownloadActionResult>,
    pub task_store: Option<TaskStoreActionResult>,
    pub refused_code: Option<TaskActionRefusal>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskStoreRow {
    pub title: String,
    pub host: String,
    pub path: String,
    pub when_utc_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskStoreActionResult {
    pub operation_kind: TaskActionOperationKind,
    pub rows: Vec<TaskStoreRow>,
    pub omitted: u32,
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
pub struct ToolEffectResult {
    pub job_id: String,
    pub status: ToolTerminalStatus,
    pub progress: Vec<ToolProgress>,
    pub chunks: Vec<ToolOutputChunk>,
    pub success: Option<ToolSuccess>,
    pub streamed_chunks: u32,
}

impl ToolEffectResult {
    pub fn has_valid_presence(&self) -> bool {
        match self.status {
            ToolTerminalStatus::Completed => self.success.is_some(),
            _ => self.success.is_none(),
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ToolStreamChunk {
    pub operation: OperationEnvelope,
    pub effect_id: String,
    pub job_id: String,
    pub chunk: ToolOutputChunk,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct GeneratedEntropyResult {
    pub flow_id: String,
    pub entropy: Vec<u8>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TransientSecretWriteResult {
    pub flow_id: String,
    pub purpose: SecretMaterialPurpose,
    pub secret_handle: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DeletedSecretHandleResult {
    pub secret_handle: String,
    pub deleted: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SecureStoreEffectResult {
    pub operation_kind: SecureStoreOperation,
    pub generated_entropy: Option<GeneratedEntropyResult>,
    pub transient_write: Option<TransientSecretWriteResult>,
    pub deleted_handle: Option<DeletedSecretHandleResult>,
}

impl SecureStoreEffectResult {
    pub fn has_valid_body(&self) -> bool {
        let body_count = [
            self.generated_entropy.is_some(),
            self.transient_write.is_some(),
            self.deleted_handle.is_some(),
        ]
        .into_iter()
        .filter(|present| *present)
        .count();
        body_count == 1
            && match self.operation_kind {
                SecureStoreOperation::GenerateEntropy => self.generated_entropy.is_some(),
                SecureStoreOperation::WriteTransient => self.transient_write.is_some(),
                SecureStoreOperation::DeleteHandle => self.deleted_handle.is_some(),
            }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct OAuthSurfaceResult {
    pub flow_id: String,
    pub opened: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct NativeCredentialSurfaceResult {
    pub flow_id: String,
    pub opened: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AuthSurfaceEffectResult {
    pub operation_kind: AuthSurfaceOperation,
    pub oauth: Option<OAuthSurfaceResult>,
    pub native_credential: Option<NativeCredentialSurfaceResult>,
}

impl AuthSurfaceEffectResult {
    pub fn has_valid_body(&self) -> bool {
        let body_count = [
            self.oauth.is_some(),
            self.native_credential.is_some(),
        ]
        .into_iter()
        .filter(|present| *present)
        .count();
        body_count == 1
            && match self.operation_kind {
                AuthSurfaceOperation::OpenOauth => self.oauth.is_some(),
                AuthSurfaceOperation::RequestNativeCredential => self.native_credential.is_some(),
            }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PermissionEffectResult {
    pub request_id: String,
    pub permission: PlatformPermission,
    pub decision: PermissionDecision,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct EffectResult {
    pub operation: OperationEnvelope,
    pub effect_id: String,
    pub status: EffectStatus,
    pub kind: EffectKind,
    pub storage: Option<StorageEffectResult>,
    pub observation: Option<ObservationEffectResult>,
    pub model: Option<ModelEffectResult>,
    pub network: Option<NetworkEffectResult>,
    pub browser_action: Option<BrowserActionEffectResult>,
    pub tool: Option<ToolEffectResult>,
    pub secure_store: Option<SecureStoreEffectResult>,
    pub auth_surface: Option<AuthSurfaceEffectResult>,
    pub permission: Option<PermissionEffectResult>,
    pub asset_delivery: Option<AssetDeliveryEffectResult>,
    pub catalog: Option<CatalogFetchEffectResult>,
    pub provider_listing: Option<ProviderListingFetchResult>,
    pub composer_completion: Option<ComposerCompletionEffectResult>,
    pub custom_endpoint_probe: Option<CustomEndpointProbeResult>,
}

impl EffectResult {
    pub fn has_valid_body(&self) -> bool {
        let body_count = [
            self.storage.is_some(),
            self.observation.is_some(),
            self.model.is_some(),
            self.network.is_some(),
            self.browser_action.is_some(),
            self.tool.is_some(),
            self.secure_store.is_some(),
            self.auth_surface.is_some(),
            self.permission.is_some(),
            self.asset_delivery.is_some(),
            self.catalog.is_some(),
            self.provider_listing.is_some(),
            self.composer_completion.is_some(),
            self.custom_endpoint_probe.is_some(),
        ]
        .into_iter()
        .filter(|present| *present)
        .count();
        body_count == 1
            && match self.kind {
                EffectKind::StorageCommit => self.storage.is_some(),
                EffectKind::PageObservation => self.observation.is_some(),
                EffectKind::ModelRequest => self.model.is_some(),
                EffectKind::NetworkRequest => self.network.is_some(),
                EffectKind::BrowserAction => self.browser_action.is_some(),
                EffectKind::ToolJob => self.tool.is_some(),
                EffectKind::SecureStore => self.secure_store.is_some(),
                EffectKind::OpenAuthSurface => self.auth_surface.is_some(),
                EffectKind::RequestPermission => self.permission.is_some(),
                EffectKind::DeliverAsset => self.asset_delivery.is_some(),
                EffectKind::FetchCatalog => self.catalog.is_some(),
                EffectKind::FetchProviderListing => self.provider_listing.is_some(),
                EffectKind::DeliverComposerCompletion => self.composer_completion.is_some(),
                EffectKind::ProbeCustomEndpoint => self.custom_endpoint_probe.is_some(),
            }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRecordDescriptor {
    pub kind: BackupRecordKind,
    pub stable_id: String,
    pub revision: u64,
    pub schema_version: u32,
    pub state: BackupRecordState,
    pub plaintext_bytes: u64,
    pub plaintext_sha256: [u8; 32],
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupManifestPrepareRequest {
    pub operation: OperationEnvelope,
    pub backup_id: String,
    pub source_installation_id: String,
    pub created_at_utc: String,
    pub selection: Vec<BackupRecordKind>,
    pub records: Vec<BackupRecordDescriptor>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupManifestPrepareResult {
    pub operation: OperationEnvelope,
    pub status: BackupPlanningStatus,
    pub manifest_plaintext: Vec<u8>,
    pub snapshot_sha256: [u8; 32],
    pub payload_plaintext_bytes: u64,
    pub source_order: Vec<u32>,
    pub expected_sealed_chunks: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupManifestInspectRequest {
    pub operation: OperationEnvelope,
    pub manifest_plaintext: Vec<u8>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupPayloadLayoutEntry {
    pub state: BackupRecordState,
    pub plaintext_bytes: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupManifestInspectResult {
    pub operation: OperationEnvelope,
    pub status: BackupPlanningStatus,
    pub backup_id: String,
    pub source_installation_id: String,
    pub created_at_utc: String,
    pub selection: Vec<BackupRecordKind>,
    pub record_count: u32,
    pub snapshot_sha256: [u8; 32],
    pub payload_plaintext_bytes: u64,
    pub records: Vec<BackupPayloadLayoutEntry>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct StagedBackupRecord {
    pub plaintext_bytes: u64,
    pub plaintext_sha256: [u8; 32],
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRestoreTarget {
    pub kind: BackupRestoreTargetKind,
    pub profile_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRestoreBinding {
    pub planning_operation: OperationEnvelope,
    pub owner_profile_id: String,
    pub target: BackupRestoreTarget,
    pub backup_id: String,
    pub snapshot_sha256: [u8; 32],
    pub confirmation_sha256: [u8; 32],
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRestoreStageAuthorization {
    pub binding: BackupRestoreBinding,
    pub decision_operation: OperationEnvelope,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRestoreCommitAuthorization {
    pub binding: BackupRestoreBinding,
    pub decision_operation: OperationEnvelope,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRestoreResolutionAuthorization {
    pub binding: BackupRestoreBinding,
    pub decision_operation: OperationEnvelope,
    pub choice: BackupRestoreResolutionChoice,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRestorePlanRequest {
    pub operation: OperationEnvelope,
    pub manifest_plaintext: Vec<u8>,
    pub staged_records: Vec<StagedBackupRecord>,
    pub current_records: Vec<BackupRecordDescriptor>,
    pub target: BackupRestoreTarget,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRestorePlanEntry {
    pub kind: BackupRecordKind,
    pub stable_id: String,
    pub archive_revision: u64,
    pub action: BackupRestoreAction,
    pub schema_version: u32,
    pub state: BackupRecordState,
    pub plaintext_bytes: u64,
    pub plaintext_sha256: [u8; 32],
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRestorePlanResult {
    pub operation: OperationEnvelope,
    pub status: BackupPlanningStatus,
    pub backup_id: String,
    pub snapshot_sha256: [u8; 32],
    pub target: BackupRestoreTarget,
    pub entries: Vec<BackupRestorePlanEntry>,
    pub has_conflicts: bool,
    pub confirmation_sha256: [u8; 32],
    pub binding: Option<BackupRestoreBinding>,
}

impl BackupRestorePlanResult {
    pub fn has_valid_presence(&self) -> bool {
        match self.status {
            BackupPlanningStatus::Succeeded => self.binding.is_some(),
            _ => self.binding.is_none(),
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRestorePlanConfirmationRequest {
    pub operation: OperationEnvelope,
    pub binding: BackupRestoreBinding,
    pub confirmed_sha256: [u8; 32],
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRestoreStageAuthorizationResult {
    pub operation: OperationEnvelope,
    pub status: BackupRestoreProtocolStatus,
    pub authorization: Option<BackupRestoreStageAuthorization>,
}

impl BackupRestoreStageAuthorizationResult {
    pub fn has_valid_presence(&self) -> bool {
        match self.status {
            BackupRestoreProtocolStatus::Succeeded => self.authorization.is_some(),
            _ => self.authorization.is_none(),
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRestoreStageVerificationRequest {
    pub operation: OperationEnvelope,
    pub authorization: BackupRestoreStageAuthorization,
    pub staged_snapshot_sha256: [u8; 32],
    pub skills: Vec<SkillRecord>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRestoreCommitAuthorizationResult {
    pub operation: OperationEnvelope,
    pub status: BackupRestoreProtocolStatus,
    pub authorization: Option<BackupRestoreCommitAuthorization>,
}

impl BackupRestoreCommitAuthorizationResult {
    pub fn has_valid_presence(&self) -> bool {
        match self.status {
            BackupRestoreProtocolStatus::Succeeded => self.authorization.is_some(),
            _ => self.authorization.is_none(),
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRestoreCommitOutcomeReport {
    pub operation: OperationEnvelope,
    pub authorization: BackupRestoreCommitAuthorization,
    pub outcome: BackupRestoreCommitOutcome,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRestoreResolutionRequest {
    pub operation: OperationEnvelope,
    pub binding: BackupRestoreBinding,
    pub choice: BackupRestoreResolutionChoice,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRestoreResolutionAuthorizationResult {
    pub operation: OperationEnvelope,
    pub status: BackupRestoreProtocolStatus,
    pub authorization: Option<BackupRestoreResolutionAuthorization>,
}

impl BackupRestoreResolutionAuthorizationResult {
    pub fn has_valid_presence(&self) -> bool {
        match self.status {
            BackupRestoreProtocolStatus::Succeeded => self.authorization.is_some(),
            _ => self.authorization.is_none(),
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRestoreResolutionOutcomeReport {
    pub operation: OperationEnvelope,
    pub authorization: BackupRestoreResolutionAuthorization,
    pub outcome: BackupRestoreResolutionOutcome,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRestoreCancellationRequest {
    pub operation: OperationEnvelope,
    pub binding: BackupRestoreBinding,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRestoreProtocolResult {
    pub operation: OperationEnvelope,
    pub status: BackupRestoreProtocolStatus,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRestoreCandidateWitness {
    pub selection: Vec<BackupRecordKind>,
    pub record_count: u64,
    pub candidate_records_sha256: [u8; 32],
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRestoreRecoveryBinding {
    pub reservation_id: String,
    pub owner_profile_id: String,
    pub target_kind: BackupRestoreTargetKind,
    pub target_profile_id: String,
    pub backup_id: String,
    pub snapshot_sha256: [u8; 32],
    pub confirmation_sha256: [u8; 32],
    pub selection: Vec<BackupRecordKind>,
    pub record_count: u64,
    pub candidate_records_sha256: [u8; 32],
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRestoreRecoveryIntentFact {
    pub intent_id: String,
    pub intent: BackupRestorePhysicalIntent,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRestoreRecoveryOutcomeFact {
    pub intent_id: String,
    pub outcome: BackupRestoreObservedOutcome,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRestoreRecoveryRecord {
    pub format_version: u32,
    pub sequence: u64,
    pub binding: BackupRestoreRecoveryBinding,
    pub fact_kind: BackupRestoreRecoveryFactKind,
    pub intent: Option<BackupRestoreRecoveryIntentFact>,
    pub outcome: Option<BackupRestoreRecoveryOutcomeFact>,
}

impl BackupRestoreRecoveryRecord {
    pub fn has_valid_body(&self) -> bool {
        let body_count = [
            self.intent.is_some(),
            self.outcome.is_some(),
        ]
        .into_iter()
        .filter(|present| *present)
        .count();
        body_count == 1
            && match self.fact_kind {
                BackupRestoreRecoveryFactKind::IntentRecorded => self.intent.is_some(),
                BackupRestoreRecoveryFactKind::OutcomeObserved => self.outcome.is_some(),
            }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRestoreRecoveryInspectionRequest {
    pub operation: OperationEnvelope,
    pub records: Vec<BackupRestoreRecoveryRecord>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRestoreRecoveryReconciliation {
    pub intent_id: String,
    pub intent: BackupRestorePhysicalIntent,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRestoreRecoveryClassification {
    pub kind: BackupRestoreRecoveryClassificationKind,
    pub reconciliation: Option<BackupRestoreRecoveryReconciliation>,
}

impl BackupRestoreRecoveryClassification {
    pub fn has_valid_presence(&self) -> bool {
        match self.kind {
            BackupRestoreRecoveryClassificationKind::ReconcileRequired => self.reconciliation.is_some(),
            _ => self.reconciliation.is_none(),
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRestoreRecoveryFailure {
    pub error: BackupRestoreRecoveryError,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRestoreRecoveryInspectionResult {
    pub operation: OperationEnvelope,
    pub status: BackupRestoreRecoveryInspectionStatus,
    pub classification: Option<BackupRestoreRecoveryClassification>,
    pub failure: Option<BackupRestoreRecoveryFailure>,
}

impl BackupRestoreRecoveryInspectionResult {
    pub fn has_valid_presence(&self) -> bool {
        (match self.status {
            BackupRestoreRecoveryInspectionStatus::Succeeded => self.classification.is_some(),
            _ => self.classification.is_none(),
        })
            && (match self.status {
            BackupRestoreRecoveryInspectionStatus::InvalidHistory => self.failure.is_some(),
            _ => self.failure.is_none(),
        })
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRestoreRecoveryResolutionRequest {
    pub operation: OperationEnvelope,
    pub history_prefix: Vec<BackupRestoreRecoveryRecord>,
    pub choice: BackupRestoreResolutionChoice,
    pub intent_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRestoreRecoveryResolutionAuthorization {
    pub binding: BackupRestoreRecoveryBinding,
    pub decision_operation: OperationEnvelope,
    pub choice: BackupRestoreResolutionChoice,
    pub intent_id: String,
    pub history_prefix: Vec<BackupRestoreRecoveryRecord>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRestoreRecoveryResolutionAuthorizationResult {
    pub operation: OperationEnvelope,
    pub status: BackupRestoreProtocolStatus,
    pub authorization: Option<BackupRestoreRecoveryResolutionAuthorization>,
}

impl BackupRestoreRecoveryResolutionAuthorizationResult {
    pub fn has_valid_presence(&self) -> bool {
        match self.status {
            BackupRestoreProtocolStatus::Succeeded => self.authorization.is_some(),
            _ => self.authorization.is_none(),
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRestoreRecoveryResolutionOutcomeReport {
    pub operation: OperationEnvelope,
    pub authorization: BackupRestoreRecoveryResolutionAuthorization,
    pub durable_history: Vec<BackupRestoreRecoveryRecord>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SavedFlowReview {
    pub skill_id: String,
    pub origin: String,
    pub provenance: SkillProvenance,
    pub status: SkillStatus,
    pub active_version: u32,
    pub step_count: u32,
    pub installed_at_epoch_ms: u64,
    pub updated_at_epoch_ms: u64,
    pub recorded_from_task_id: Option<String>,
    pub reviewed_steps: Vec<SkillObservedStep>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SavedFlowQueryCommand {
    pub operation: OperationEnvelope,
    pub kind: SavedFlowQueryKind,
    pub goal: String,
    pub skill_id: String,
    pub expected_version: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SavedFlowQueryResult {
    pub operation: OperationEnvelope,
    pub status: SavedFlowQueryStatus,
    pub flows: Vec<SavedFlowReview>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedTaskKind {
    Research = 0,
    Errand = 1,
}
impl PersistedTaskKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Research),
            1 => Some(Self::Errand),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedTaskTemplateId {
    CompareProducts = 0,
    SummarizeEvidence = 1,
    BuildSourceTable = 2,
    WebErrand = 3,
}
impl PersistedTaskTemplateId {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::CompareProducts),
            1 => Some(Self::SummarizeEvidence),
            2 => Some(Self::BuildSourceTable),
            3 => Some(Self::WebErrand),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedBuiltinSkillId {
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
impl PersistedBuiltinSkillId {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::GeneralWebResearch),
            1 => Some(Self::DeepResearch),
            2 => Some(Self::ProductComparison),
            3 => Some(Self::MultiTabComparison),
            4 => Some(Self::WebsiteSummarizer),
            5 => Some(Self::PdfAnalysis),
            6 => Some(Self::DataExtraction),
            7 => Some(Self::FormAssistant),
            8 => Some(Self::Shopping),
            9 => Some(Self::DownloadOrganizer),
            10 => Some(Self::TravelResearch),
            11 => Some(Self::VideoTranscriptAnalyzer),
            12 => Some(Self::ImageUnderstanding),
            13 => Some(Self::LibraryBuilder),
            14 => Some(Self::SpreadsheetBuilder),
            15 => Some(Self::DocumentGenerator),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedControlMode {
    User = 0,
    Shared = 1,
    Assistant = 2,
}
impl PersistedControlMode {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::User),
            1 => Some(Self::Shared),
            2 => Some(Self::Assistant),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedMilestone {
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
impl PersistedMilestone {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::M0),
            1 => Some(Self::M1),
            2 => Some(Self::M2),
            3 => Some(Self::M3),
            4 => Some(Self::M4),
            5 => Some(Self::M5),
            6 => Some(Self::M6),
            7 => Some(Self::M7),
            8 => Some(Self::M8),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedBudgetKind {
    MaxSources = 0,
    MaxWorkingTabs = 1,
    MaxWallTimeMillis = 2,
    MaxModelRequests = 3,
    MaxInputTokensOrBytes = 4,
    MaxOutputTokensOrBytes = 5,
    MaxCost = 6,
    MaxNavigationDepth = 7,
    MaxRetriesPerStep = 8,
    MaxArtifactBytes = 9,
}
impl PersistedBudgetKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::MaxSources),
            1 => Some(Self::MaxWorkingTabs),
            2 => Some(Self::MaxWallTimeMillis),
            3 => Some(Self::MaxModelRequests),
            4 => Some(Self::MaxInputTokensOrBytes),
            5 => Some(Self::MaxOutputTokensOrBytes),
            6 => Some(Self::MaxCost),
            7 => Some(Self::MaxNavigationDepth),
            8 => Some(Self::MaxRetriesPerStep),
            9 => Some(Self::MaxArtifactBytes),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedPauseCause {
    User = 0,
    BackgroundRestricted = 1,
    ProviderLimit = 2,
    Offline = 3,
    NoAnswer = 4,
    ProviderBusy = 5,
}
impl PersistedPauseCause {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::User),
            1 => Some(Self::BackgroundRestricted),
            2 => Some(Self::ProviderLimit),
            3 => Some(Self::Offline),
            4 => Some(Self::NoAnswer),
            5 => Some(Self::ProviderBusy),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedStepKind {
    Observe = 0,
    Navigate = 1,
    Extract = 2,
    Infer = 3,
    AskUser = 4,
    Export = 5,
}
impl PersistedStepKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Observe),
            1 => Some(Self::Navigate),
            2 => Some(Self::Extract),
            3 => Some(Self::Infer),
            4 => Some(Self::AskUser),
            5 => Some(Self::Export),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedStepState {
    Pending = 0,
    Ready = 1,
    Running = 2,
    Waiting = 3,
    Succeeded = 4,
    Skipped = 5,
    Failed = 6,
    Cancelled = 7,
    Superseded = 8,
}
impl PersistedStepState {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Pending),
            1 => Some(Self::Ready),
            2 => Some(Self::Running),
            3 => Some(Self::Waiting),
            4 => Some(Self::Succeeded),
            5 => Some(Self::Skipped),
            6 => Some(Self::Failed),
            7 => Some(Self::Cancelled),
            8 => Some(Self::Superseded),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedActionClass {
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
impl PersistedActionClass {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::ObservePage),
            1 => Some(Self::ScrollIntoView),
            2 => Some(Self::OpenLink),
            3 => Some(Self::CreateTaskTab),
            4 => Some(Self::SyntheticClick),
            5 => Some(Self::MoveFocus),
            6 => Some(Self::FillField),
            7 => Some(Self::SelectOption),
            8 => Some(Self::ToggleControl),
            9 => Some(Self::SubmitForm),
            10 => Some(Self::StartDownload),
            11 => Some(Self::UploadFile),
            12 => Some(Self::SendMessage),
            13 => Some(Self::Purchase),
            14 => Some(Self::ExtractCredential),
            15 => Some(Self::BypassAccessControl),
            16 => Some(Self::ExecuteToolJob),
            17 => Some(Self::LibraryRead),
            18 => Some(Self::LibraryWrite),
            19 => Some(Self::MemoryRead),
            20 => Some(Self::MemoryWrite),
            21 => Some(Self::ControlTab),
            22 => Some(Self::ProfileStoreRead),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedIdempotencyClass {
    PureRead = 0,
    IdempotentWrite = 1,
    ConditionallyIdempotent = 2,
    Consequential = 3,
}
impl PersistedIdempotencyClass {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::PureRead),
            1 => Some(Self::IdempotentWrite),
            2 => Some(Self::ConditionallyIdempotent),
            3 => Some(Self::Consequential),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedActionResultCode {
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
impl PersistedActionResultCode {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Verified),
            1 => Some(Self::DeniedByPolicy),
            2 => Some(Self::ApprovalRequired),
            3 => Some(Self::ApprovalDenied),
            4 => Some(Self::ActorLeaseMissing),
            5 => Some(Self::CapabilityExpired),
            6 => Some(Self::TabGone),
            7 => Some(Self::FrameGone),
            8 => Some(Self::DocumentInactive),
            9 => Some(Self::StalePageEpoch),
            10 => Some(Self::StaleGraph),
            11 => Some(Self::NodeGone),
            12 => Some(Self::OriginChanged),
            13 => Some(Self::RoleOrActionChanged),
            14 => Some(Self::NotVisible),
            15 => Some(Self::Occluded),
            16 => Some(Self::NotEnabled),
            17 => Some(Self::NotEditable),
            18 => Some(Self::SensitiveField),
            19 => Some(Self::DestinationChanged),
            20 => Some(Self::Unsupported),
            21 => Some(Self::BudgetExceeded),
            22 => Some(Self::DispatchFailed),
            23 => Some(Self::NavigationStarted),
            24 => Some(Self::PostconditionTimeout),
            25 => Some(Self::PostconditionFailed),
            26 => Some(Self::CancelledByUser),
            27 => Some(Self::CancelledByNavigation),
            28 => Some(Self::RendererCrashed),
            29 => Some(Self::OutcomeUnknown),
            30 => Some(Self::InternalError),
            31 => Some(Self::EgressNotAuthorized),
            32 => Some(Self::DestinationClassRestricted),
            33 => Some(Self::UntrustedContentOrigin),
            34 => Some(Self::PreparedEffectChanged),
            35 => Some(Self::CommitWithoutPrepare),
            36 => Some(Self::GraphMovedDuringPreflight),
            37 => Some(Self::ValueReferenceUnknown),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedObservationCompleteness {
    Complete = 0,
    Incomplete = 1,
}
impl PersistedObservationCompleteness {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Complete),
            1 => Some(Self::Incomplete),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedSensitivity {
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
}
impl PersistedSensitivity {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::NotSensitive),
            1 => Some(Self::Personal),
            2 => Some(Self::Account),
            3 => Some(Self::Payment),
            4 => Some(Self::Identity),
            5 => Some(Self::Health),
            6 => Some(Self::Financial),
            7 => Some(Self::Legal),
            8 => Some(Self::PrivateCommunication),
            9 => Some(Self::Administration),
            10 => Some(Self::Credential),
            11 => Some(Self::UnknownSensitive),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedFailureReason {
    BudgetExhausted = 0,
    ProviderUnavailable = 1,
    SourcesUnavailable = 2,
    UnverifiableAction = 3,
    JournalUnusable = 4,
    DeadlineExceeded = 5,
    ProviderRefused = 6,
    ProviderLimit = 7,
    Offline = 8,
    PolicyRefused = 9,
}
impl PersistedFailureReason {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::BudgetExhausted),
            1 => Some(Self::ProviderUnavailable),
            2 => Some(Self::SourcesUnavailable),
            3 => Some(Self::UnverifiableAction),
            4 => Some(Self::JournalUnusable),
            5 => Some(Self::DeadlineExceeded),
            6 => Some(Self::ProviderRefused),
            7 => Some(Self::ProviderLimit),
            8 => Some(Self::Offline),
            9 => Some(Self::PolicyRefused),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedPlatformPermission {
    Notifications = 0,
    Microphone = 1,
    Camera = 2,
    Location = 3,
}
impl PersistedPlatformPermission {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Notifications),
            1 => Some(Self::Microphone),
            2 => Some(Self::Camera),
            3 => Some(Self::Location),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedPermissionDecision {
    Granted = 0,
    Denied = 1,
    Dismissed = 2,
    Unavailable = 3,
}
impl PersistedPermissionDecision {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Granted),
            1 => Some(Self::Denied),
            2 => Some(Self::Dismissed),
            3 => Some(Self::Unavailable),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedArtifactKind {
    Markdown = 0,
    Csv = 1,
    Xlsx = 2,
    Pdf = 3,
    Docx = 4,
    Pptx = 5,
    WaveAudio = 6,
    FrameArchive = 7,
}
impl PersistedArtifactKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Markdown),
            1 => Some(Self::Csv),
            2 => Some(Self::Xlsx),
            3 => Some(Self::Pdf),
            4 => Some(Self::Docx),
            5 => Some(Self::Pptx),
            6 => Some(Self::WaveAudio),
            7 => Some(Self::FrameArchive),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedGapReason {
    NotFoundInScope = 0,
    ConflictUnresolved = 1,
    BudgetReached = 2,
    SourceUnavailable = 3,
    StoppedByUser = 4,
}
impl PersistedGapReason {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::NotFoundInScope),
            1 => Some(Self::ConflictUnresolved),
            2 => Some(Self::BudgetReached),
            3 => Some(Self::SourceUnavailable),
            4 => Some(Self::StoppedByUser),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedTaskState {
    Draft = 0,
    AwaitingConsent = 1,
    Queued = 2,
    Running = 3,
    WaitingUser = 4,
    Pausing = 5,
    Paused = 6,
    Cancelling = 7,
    Completing = 8,
    Cancelled = 9,
    Completed = 10,
    Partial = 11,
    Failed = 12,
}
impl PersistedTaskState {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Draft),
            1 => Some(Self::AwaitingConsent),
            2 => Some(Self::Queued),
            3 => Some(Self::Running),
            4 => Some(Self::WaitingUser),
            5 => Some(Self::Pausing),
            6 => Some(Self::Paused),
            7 => Some(Self::Cancelling),
            8 => Some(Self::Completing),
            9 => Some(Self::Cancelled),
            10 => Some(Self::Completed),
            11 => Some(Self::Partial),
            12 => Some(Self::Failed),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedStateReason {
    PreviewReady = 0,
    ScopeEdited = 1,
    InitialConsentAccepted = 2,
    ExecutorStarted = 3,
    ScopeExpansionNeedsConsent = 4,
    ApprovalAccepted = 5,
    ApprovalDenied = 6,
    UserInputNeeded = 7,
    UserInputSupplied = 8,
    UserPaused = 9,
    UserTookOver = 10,
    BackgroundRestricted = 11,
    SettlingComplete = 12,
    ResumeRequested = 13,
    StopRequested = 14,
    Discarded = 15,
    ResultCandidateReady = 16,
    ResultValidated = 17,
    ResultHasGaps = 18,
    CorrectionRequested = 19,
    TerminalFailure = 20,
    PermissionRequested = 21,
    PermissionDecided = 22,
    HandoverRequested = 23,
    HandoverCompleted = 24,
    HandoverExpired = 25,
    ProviderPaused = 26,
    FollowUpAsked = 27,
}
impl PersistedStateReason {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::PreviewReady),
            1 => Some(Self::ScopeEdited),
            2 => Some(Self::InitialConsentAccepted),
            3 => Some(Self::ExecutorStarted),
            4 => Some(Self::ScopeExpansionNeedsConsent),
            5 => Some(Self::ApprovalAccepted),
            6 => Some(Self::ApprovalDenied),
            7 => Some(Self::UserInputNeeded),
            8 => Some(Self::UserInputSupplied),
            9 => Some(Self::UserPaused),
            10 => Some(Self::UserTookOver),
            11 => Some(Self::BackgroundRestricted),
            12 => Some(Self::SettlingComplete),
            13 => Some(Self::ResumeRequested),
            14 => Some(Self::StopRequested),
            15 => Some(Self::Discarded),
            16 => Some(Self::ResultCandidateReady),
            17 => Some(Self::ResultValidated),
            18 => Some(Self::ResultHasGaps),
            19 => Some(Self::CorrectionRequested),
            20 => Some(Self::TerminalFailure),
            21 => Some(Self::PermissionRequested),
            22 => Some(Self::PermissionDecided),
            23 => Some(Self::HandoverRequested),
            24 => Some(Self::HandoverCompleted),
            25 => Some(Self::HandoverExpired),
            26 => Some(Self::ProviderPaused),
            27 => Some(Self::FollowUpAsked),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedCommandKind {
    CreateTask = 0,
    EditScope = 1,
    StartTask = 2,
    AcceptInitialConsent = 3,
    ApproveAction = 4,
    DenyAction = 5,
    PauseTask = 6,
    TakeOver = 7,
    PauseSettled = 8,
    ResumeTask = 9,
    CancelTask = 10,
    CancelSettled = 11,
    ExecutorStarted = 12,
    SetPlan = 13,
    AdvanceStep = 14,
    RequestApproval = 15,
    RequestUserInput = 16,
    SupplyUserInput = 17,
    ProposeAction = 18,
    RecordPolicyDecision = 19,
    DispatchAction = 20,
    RecordActionOutcome = 21,
    ResultCandidateReady = 22,
    CompleteResultValidated = 23,
    PartialResultValidated = 24,
    ResumeForCorrection = 25,
    FailTask = 26,
    CorrectFact = 27,
    ExcludeSource = 28,
    AcceptArtifact = 29,
    ExportArtifact = 30,
    RequestPermission = 31,
    RecordPermissionResult = 32,
    RequestModelTurn = 33,
    RecordModelTurn = 34,
    RecordModelTurnGap = 35,
    RequestHandover = 36,
    CompleteHandover = 37,
    ExpireHandover = 38,
    RecordContextEviction = 39,
    RecordToolJobOutcome = 40,
    RequestFieldValues = 41,
    SupplyFieldValues = 42,
    RequestArtifact = 43,
    RecordDiscoveryTab = 44,
    RequestModelAttempt = 45,
    FollowUp = 46,
}
impl PersistedCommandKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::CreateTask),
            1 => Some(Self::EditScope),
            2 => Some(Self::StartTask),
            3 => Some(Self::AcceptInitialConsent),
            4 => Some(Self::ApproveAction),
            5 => Some(Self::DenyAction),
            6 => Some(Self::PauseTask),
            7 => Some(Self::TakeOver),
            8 => Some(Self::PauseSettled),
            9 => Some(Self::ResumeTask),
            10 => Some(Self::CancelTask),
            11 => Some(Self::CancelSettled),
            12 => Some(Self::ExecutorStarted),
            13 => Some(Self::SetPlan),
            14 => Some(Self::AdvanceStep),
            15 => Some(Self::RequestApproval),
            16 => Some(Self::RequestUserInput),
            17 => Some(Self::SupplyUserInput),
            18 => Some(Self::ProposeAction),
            19 => Some(Self::RecordPolicyDecision),
            20 => Some(Self::DispatchAction),
            21 => Some(Self::RecordActionOutcome),
            22 => Some(Self::ResultCandidateReady),
            23 => Some(Self::CompleteResultValidated),
            24 => Some(Self::PartialResultValidated),
            25 => Some(Self::ResumeForCorrection),
            26 => Some(Self::FailTask),
            27 => Some(Self::CorrectFact),
            28 => Some(Self::ExcludeSource),
            29 => Some(Self::AcceptArtifact),
            30 => Some(Self::ExportArtifact),
            31 => Some(Self::RequestPermission),
            32 => Some(Self::RecordPermissionResult),
            33 => Some(Self::RequestModelTurn),
            34 => Some(Self::RecordModelTurn),
            35 => Some(Self::RecordModelTurnGap),
            36 => Some(Self::RequestHandover),
            37 => Some(Self::CompleteHandover),
            38 => Some(Self::ExpireHandover),
            39 => Some(Self::RecordContextEviction),
            40 => Some(Self::RecordToolJobOutcome),
            41 => Some(Self::RequestFieldValues),
            42 => Some(Self::SupplyFieldValues),
            43 => Some(Self::RequestArtifact),
            44 => Some(Self::RecordDiscoveryTab),
            45 => Some(Self::RequestModelAttempt),
            46 => Some(Self::FollowUp),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedEventKind {
    TaskCreated = 0,
    SourceScopeSet = 1,
    ProviderRouteSelected = 2,
    ConsentRequested = 3,
    TaskQueued = 4,
    PlanCreated = 5,
    PlanSuperseded = 6,
    PlanStepAdvanced = 7,
    TaskStarted = 8,
    ActionProposed = 9,
    ApprovalRequested = 10,
    ApprovalDecided = 11,
    CapabilityIssued = 12,
    ActionRejected = 13,
    ActionDispatchStarted = 14,
    ActionVerificationCompleted = 15,
    TaskWaitingUser = 16,
    UserInputSupplied = 17,
    UserTookOver = 18,
    TaskPausing = 19,
    TaskPaused = 20,
    TaskResumed = 21,
    TaskCancelling = 22,
    TaskCancelled = 23,
    TaskCompleting = 24,
    TaskCompleted = 25,
    TaskPartial = 26,
    TaskFailed = 27,
    FactCorrected = 28,
    SourceExcluded = 29,
    ArtifactAccepted = 30,
    ArtifactExported = 31,
    BudgetCharged = 32,
    PermissionRequested = 33,
    PermissionDecided = 34,
    HandoverRequested = 35,
    HandoverCompleted = 36,
    HandoverExpired = 37,
    ArtifactReady = 38,
    DiscoveryTabPrepared = 39,
    ModelTurnRecorded = 40,
    ModelTurnGapRecorded = 41,
    ContextEvicted = 42,
    FollowUpAsked = 43,
}
impl PersistedEventKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::TaskCreated),
            1 => Some(Self::SourceScopeSet),
            2 => Some(Self::ProviderRouteSelected),
            3 => Some(Self::ConsentRequested),
            4 => Some(Self::TaskQueued),
            5 => Some(Self::PlanCreated),
            6 => Some(Self::PlanSuperseded),
            7 => Some(Self::PlanStepAdvanced),
            8 => Some(Self::TaskStarted),
            9 => Some(Self::ActionProposed),
            10 => Some(Self::ApprovalRequested),
            11 => Some(Self::ApprovalDecided),
            12 => Some(Self::CapabilityIssued),
            13 => Some(Self::ActionRejected),
            14 => Some(Self::ActionDispatchStarted),
            15 => Some(Self::ActionVerificationCompleted),
            16 => Some(Self::TaskWaitingUser),
            17 => Some(Self::UserInputSupplied),
            18 => Some(Self::UserTookOver),
            19 => Some(Self::TaskPausing),
            20 => Some(Self::TaskPaused),
            21 => Some(Self::TaskResumed),
            22 => Some(Self::TaskCancelling),
            23 => Some(Self::TaskCancelled),
            24 => Some(Self::TaskCompleting),
            25 => Some(Self::TaskCompleted),
            26 => Some(Self::TaskPartial),
            27 => Some(Self::TaskFailed),
            28 => Some(Self::FactCorrected),
            29 => Some(Self::SourceExcluded),
            30 => Some(Self::ArtifactAccepted),
            31 => Some(Self::ArtifactExported),
            32 => Some(Self::BudgetCharged),
            33 => Some(Self::PermissionRequested),
            34 => Some(Self::PermissionDecided),
            35 => Some(Self::HandoverRequested),
            36 => Some(Self::HandoverCompleted),
            37 => Some(Self::HandoverExpired),
            38 => Some(Self::ArtifactReady),
            39 => Some(Self::DiscoveryTabPrepared),
            40 => Some(Self::ModelTurnRecorded),
            41 => Some(Self::ModelTurnGapRecorded),
            42 => Some(Self::ContextEvicted),
            43 => Some(Self::FollowUpAsked),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedRevocationReason {
    UserTookOver = 0,
    DirectUserInput = 1,
    TaskCancelled = 2,
    PolicyRevoked = 3,
    TabClosed = 4,
}
impl PersistedRevocationReason {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::UserTookOver),
            1 => Some(Self::DirectUserInput),
            2 => Some(Self::TaskCancelled),
            3 => Some(Self::PolicyRevoked),
            4 => Some(Self::TabClosed),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedRecoveryRule {
    RetryWithinEpochAndBudget = 0,
    RetryAfterStateCheck = 1,
    ReconcileFirst = 2,
    NeverAutomatically = 3,
}
impl PersistedRecoveryRule {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::RetryWithinEpochAndBudget),
            1 => Some(Self::RetryAfterStateCheck),
            2 => Some(Self::ReconcileFirst),
            3 => Some(Self::NeverAutomatically),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedAuditActor {
    User = 0,
    TaskRuntime = 1,
    Policy = 2,
    Browser = 3,
    System = 4,
}
impl PersistedAuditActor {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::User),
            1 => Some(Self::TaskRuntime),
            2 => Some(Self::Policy),
            3 => Some(Self::Browser),
            4 => Some(Self::System),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedAuditSubjectKind {
    Action = 0,
    Plan = 1,
    PlanStep = 2,
    Artifact = 3,
    Source = 4,
    Fact = 5,
    Capability = 6,
    PermissionRequest = 7,
    Handover = 8,
    ActorLease = 9,
    DiscoveryTab = 10,
}
impl PersistedAuditSubjectKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Action),
            1 => Some(Self::Plan),
            2 => Some(Self::PlanStep),
            3 => Some(Self::Artifact),
            4 => Some(Self::Source),
            5 => Some(Self::Fact),
            6 => Some(Self::Capability),
            7 => Some(Self::PermissionRequest),
            8 => Some(Self::Handover),
            9 => Some(Self::ActorLease),
            10 => Some(Self::DiscoveryTab),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedAuditEventType {
    TaskCreated = 0,
    SourceScopeSet = 1,
    ProviderRouteSelected = 2,
    ConsentRequested = 3,
    TaskQueued = 4,
    PlanCreated = 5,
    PlanSuperseded = 6,
    PlanStepAdvanced = 7,
    TaskStarted = 8,
    ActorLeaseIssued = 9,
    ObservationCaptured = 10,
    ModelInvocationStarted = 11,
    ModelInvocationCompleted = 12,
    ActionProposed = 13,
    ApprovalRequested = 14,
    ApprovalDecided = 15,
    CapabilityIssued = 16,
    ActionRejected = 17,
    ActionDispatchStarted = 18,
    ActionVerificationCompleted = 19,
    UserTookOver = 20,
    TaskPausing = 21,
    TaskPaused = 22,
    TaskResumed = 23,
    TaskWaitingUser = 24,
    UserInputSupplied = 25,
    TaskCancelling = 26,
    TaskCompleting = 27,
    TaskPartial = 28,
    FactAccepted = 29,
    FactCorrected = 30,
    SourceExcluded = 31,
    ConflictDetected = 32,
    ArtifactReady = 33,
    ArtifactAccepted = 34,
    ArtifactExported = 35,
    BudgetCharged = 36,
    TaskCompleted = 37,
    TaskCancelled = 38,
    TaskFailed = 39,
    DeletionRequested = 40,
    DeletionCompleted = 41,
    PermissionRequested = 42,
    PermissionDecided = 43,
    HandoverRequested = 44,
    HandoverCompleted = 45,
    HandoverExpired = 46,
    DiscoveryTabPrepared = 47,
    ModelInvocationFailed = 48,
    ContextEvicted = 49,
}
impl PersistedAuditEventType {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::TaskCreated),
            1 => Some(Self::SourceScopeSet),
            2 => Some(Self::ProviderRouteSelected),
            3 => Some(Self::ConsentRequested),
            4 => Some(Self::TaskQueued),
            5 => Some(Self::PlanCreated),
            6 => Some(Self::PlanSuperseded),
            7 => Some(Self::PlanStepAdvanced),
            8 => Some(Self::TaskStarted),
            9 => Some(Self::ActorLeaseIssued),
            10 => Some(Self::ObservationCaptured),
            11 => Some(Self::ModelInvocationStarted),
            12 => Some(Self::ModelInvocationCompleted),
            13 => Some(Self::ActionProposed),
            14 => Some(Self::ApprovalRequested),
            15 => Some(Self::ApprovalDecided),
            16 => Some(Self::CapabilityIssued),
            17 => Some(Self::ActionRejected),
            18 => Some(Self::ActionDispatchStarted),
            19 => Some(Self::ActionVerificationCompleted),
            20 => Some(Self::UserTookOver),
            21 => Some(Self::TaskPausing),
            22 => Some(Self::TaskPaused),
            23 => Some(Self::TaskResumed),
            24 => Some(Self::TaskWaitingUser),
            25 => Some(Self::UserInputSupplied),
            26 => Some(Self::TaskCancelling),
            27 => Some(Self::TaskCompleting),
            28 => Some(Self::TaskPartial),
            29 => Some(Self::FactAccepted),
            30 => Some(Self::FactCorrected),
            31 => Some(Self::SourceExcluded),
            32 => Some(Self::ConflictDetected),
            33 => Some(Self::ArtifactReady),
            34 => Some(Self::ArtifactAccepted),
            35 => Some(Self::ArtifactExported),
            36 => Some(Self::BudgetCharged),
            37 => Some(Self::TaskCompleted),
            38 => Some(Self::TaskCancelled),
            39 => Some(Self::TaskFailed),
            40 => Some(Self::DeletionRequested),
            41 => Some(Self::DeletionCompleted),
            42 => Some(Self::PermissionRequested),
            43 => Some(Self::PermissionDecided),
            44 => Some(Self::HandoverRequested),
            45 => Some(Self::HandoverCompleted),
            46 => Some(Self::HandoverExpired),
            47 => Some(Self::DiscoveryTabPrepared),
            48 => Some(Self::ModelInvocationFailed),
            49 => Some(Self::ContextEvicted),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedModelStopReason {
    Complete = 0,
    ToolCall = 1,
    Length = 2,
    ProviderStop = 3,
    Error = 4,
}
impl PersistedModelStopReason {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Complete),
            1 => Some(Self::ToolCall),
            2 => Some(Self::Length),
            3 => Some(Self::ProviderStop),
            4 => Some(Self::Error),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedModelAttemptKind {
    Retry = 0,
    Failover = 1,
}
impl PersistedModelAttemptKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Retry),
            1 => Some(Self::Failover),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedTurnOverflow {
    ProviderReported = 0,
    Silent = 1,
    Truncation = 2,
}
impl PersistedTurnOverflow {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::ProviderReported),
            1 => Some(Self::Silent),
            2 => Some(Self::Truncation),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedTurnGap {
    Refused = 0,
    Unavailable = 1,
    Cancelled = 2,
    OutcomeUnknown = 3,
    Unreadable = 4,
}
impl PersistedTurnGap {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Refused),
            1 => Some(Self::Unavailable),
            2 => Some(Self::Cancelled),
            3 => Some(Self::OutcomeUnknown),
            4 => Some(Self::Unreadable),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedPageReadability {
    Readable = 0,
    Empty = 1,
    Unreadable = 2,
}
impl PersistedPageReadability {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Readable),
            1 => Some(Self::Empty),
            2 => Some(Self::Unreadable),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedToolJobStatus {
    Succeeded = 0,
    Failed = 1,
    Cancelled = 2,
    OutcomeUnknown = 3,
    Unavailable = 4,
}
impl PersistedToolJobStatus {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Succeeded),
            1 => Some(Self::Failed),
            2 => Some(Self::Cancelled),
            3 => Some(Self::OutcomeUnknown),
            4 => Some(Self::Unavailable),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PersistedToolRuntime {
    Media = 0,
    Python = 1,
    LocalModel = 2,
    Wasm = 3,
}
impl PersistedToolRuntime {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Media),
            1 => Some(Self::Python),
            2 => Some(Self::LocalModel),
            3 => Some(Self::Wasm),
            _ => None,
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PersistedBudget {
    pub kind: PersistedBudgetKind,
    pub limit: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PersistedConsentedSource {
    pub source_id: String,
    pub tab_id: String,
    pub normalized_origin: String,
    pub canonical_locator: Option<String>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PersistedLibraryRefreshSource {
    pub source_id: String,
    pub title: String,
    pub host: String,
    pub canonical_locator: String,
    pub original_content_digest: [u8; 32],
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PersistedLibraryRefreshContext {
    pub preview_id: String,
    pub library_revision: u64,
    pub collection_id: String,
    pub source_workspace_revision: u64,
    pub sources: Vec<PersistedLibraryRefreshSource>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PersistedBuiltinSkillReference {
    pub skill_id: PersistedBuiltinSkillId,
    pub version: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PersistedTaskSnapshot {
    pub assistant_config_version: u32,
    pub skill_version_id: Option<String>,
    pub tool_allowlist: Vec<String>,
    pub capability_policy_version: u32,
    pub provider_route: Option<String>,
    pub milestone: PersistedMilestone,
    pub template_id: PersistedTaskTemplateId,
    pub consented_sources: Vec<PersistedConsentedSource>,
    pub source_discovery_enabled: bool,
    pub browser_session_id: String,
    pub remaining_new_source_cap: u32,
    pub discovery_tab_id: Option<String>,
    pub library_refresh: Option<PersistedLibraryRefreshContext>,
    pub builtin_skill: Option<PersistedBuiltinSkillReference>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PersistedTaskSeed {
    pub task_id: String,
    pub workspace_id: Option<String>,
    pub browser_profile_id: String,
    pub kind: PersistedTaskKind,
    pub user_goal: String,
    pub control_mode: PersistedControlMode,
    pub snapshot: PersistedTaskSnapshot,
    pub budgets: Vec<PersistedBudget>,
    pub deadline_monotonic_ms: Option<u64>,
    pub predecessor_task_id: Option<String>,
    pub deadline_utc_ms: Option<u64>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PersistedSourceScope {
    pub included: Vec<String>,
    pub excluded: Vec<String>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PersistedScopePreview {
    pub scope: PersistedSourceScope,
    pub provider_route: Option<String>,
    pub budgets: Vec<PersistedBudget>,
    pub sources: Vec<PersistedConsentedSource>,
    pub source_discovery_enabled: bool,
    pub new_source_cap: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PersistedStepDraft {
    pub kind: PersistedStepKind,
    pub description: String,
    pub dependencies: Vec<u64>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PersistedPlanDraft {
    pub summary: String,
    pub steps: Vec<PersistedStepDraft>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PersistedBudgetDraw {
    pub kind: PersistedBudgetKind,
    pub amount: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PersistedActionProposal {
    pub tool_name: String,
    pub plan_step_id: Option<String>,
    pub action_class: PersistedActionClass,
    pub tab_id: String,
    pub node_id: Option<String>,
    pub idempotency_key: String,
    pub idempotency: PersistedIdempotencyClass,
    pub required_for_step: bool,
    pub budget_draw: Option<PersistedBudgetDraw>,
    pub proposal_digest: String,
    pub destination_address: Option<String>,
    pub canonical_intent: Vec<u8>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PersistedAuthorization {
    pub capability_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PersistedDenial {
    pub code: PersistedActionResultCode,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PersistedObservationGraphSummary {
    pub node_count: u32,
    pub relationship_count: u32,
    pub named_node_count: u32,
    pub text_run_count: u64,
    pub text_byte_count: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PersistedPageObservationEvidence {
    pub service_generation: u64,
    pub schema_version: String,
    pub tab_id: String,
    pub frame_id: String,
    pub page_epoch: String,
    pub graph_revision: u64,
    pub normalized_origin: String,
    pub private_profile: bool,
    pub completeness: PersistedObservationCompleteness,
    pub graph: PersistedObservationGraphSummary,
    pub total_bytes: u32,
    pub truncated: bool,
    pub may_change_answer: bool,
    pub redacted_field_count: u32,
    pub suppressed_secret_value_count: u32,
    pub sensitive_zone_count: u32,
    pub policy_filtered_frame_count: u32,
    pub highest_sensitivity: PersistedSensitivity,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PersistedActionOutcome {
    pub code: PersistedActionResultCode,
    pub dispatch_id: Option<String>,
    pub observed_at_monotonic_ms: u64,
    pub observation: Option<PersistedPageObservationEvidence>,
    pub discovered_source: Option<PersistedConsentedSource>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PersistedUnmetRequirement {
    pub subject: String,
    pub reason: PersistedGapReason,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PersistedTaskResult {
    pub artifact_ids: Vec<String>,
    pub unmet: Vec<PersistedUnmetRequirement>,
    pub fact_count: u64,
    pub source_count: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PersistedPermissionRequest {
    pub request_id: String,
    pub permission: PersistedPlatformPermission,
    pub deadline_monotonic_ms: u64,
    pub deadline_utc_ms: u64,
    pub browser_session_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PersistedHandoverCompletion {
    pub handover_id: String,
    pub lease_before: String,
    pub resumed_with: String,
    pub person_input: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PersistedPermissionResult {
    pub request_id: String,
    pub permission: PersistedPlatformPermission,
    pub decision: PersistedPermissionDecision,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PersistedToolJobOutcome {
    pub status: PersistedToolJobStatus,
    pub output_digest: Option<String>,
    pub output_bytes: u64,
    pub output_chunks: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PersistedTurnDigest {
    pub stop: PersistedModelStopReason,
    pub overflow: Option<PersistedTurnOverflow>,
    pub input_units: u64,
    pub output_units: u64,
    pub cache_read_units: u64,
    pub cache_write_units: u64,
    pub answer_segment_count: u32,
    pub tool_call_count: u32,
    pub refused_tool_call_count: u32,
    pub readability: PersistedPageReadability,
    pub offered_node_count: u32,
    pub omitted_node_count: u32,
    pub unreadable_node_count: u32,
    pub unreadable_text_bytes: u64,
    pub render_digest: [u8; 32],
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PersistedCommandEnvelope {
    pub idempotency_key: String,
    pub expected_revision: u64,
    pub trace_id: String,
    pub command: PersistedCommand,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PersistedCommandRecord {
    pub sequence: u64,
    pub envelope: PersistedCommandEnvelope,
    pub recorded_at_utc_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PersistedTaskEvent {
    pub kind: PersistedEventKind,
    pub caused_by: PersistedCommandKind,
    pub from: Option<PersistedTaskState>,
    pub to: Option<PersistedTaskState>,
    pub reason: Option<PersistedStateReason>,
    pub subject: Option<PersistedEventSubject>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PersistedEventRecord {
    pub sequence: u64,
    pub event: PersistedTaskEvent,
    pub revision: u64,
    pub trace_id: String,
    pub causation_key: String,
    pub recorded_at_utc_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PersistedAuditRecord {
    pub event_id: String,
    pub event_type: PersistedAuditEventType,
    pub actor: PersistedAuditActor,
    pub task_id: String,
    pub subject_kind: Option<PersistedAuditSubjectKind>,
    pub subject_id: Option<String>,
    pub revision: u64,
    pub sequence: u64,
    pub trace_id: String,
    pub occurred_at_utc_ms: u64,
    pub dropped_field_count: u64,
    pub content_values_retained: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskTransactionBatch {
    pub schema_version: u32,
    pub seed: Option<PersistedTaskSeed>,
    pub journal_entries: Vec<PersistedJournalEntry>,
    pub effect_intents: Vec<PersistedEffectIntent>,
    pub audit_records: Vec<PersistedAuditRecord>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum PersistedProposalDecision {
    Authorize {
        authorization: PersistedAuthorization,
    },
    RequireApproval,
    Deny {
        denial: PersistedDenial,
    },
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum PersistedCommand {
    CreateTask,
    EditScope {
        scope: PersistedSourceScope,
    },
    StartTask {
        preview: PersistedScopePreview,
    },
    AcceptInitialConsent {
        approval: String,
    },
    ApproveAction {
        approval: String,
        still_current: bool,
        expires_at_monotonic_ms: u64,
        expires_at_utc_ms: u64,
        browser_session_id: String,
    },
    DenyAction {
        approval: String,
    },
    PauseTask {
        cause: PersistedPauseCause,
    },
    TakeOver,
    PauseSettled,
    ResumeTask,
    CancelTask,
    CancelSettled,
    ExecutorStarted,
    SetPlan {
        plan: PersistedPlanDraft,
    },
    AdvanceStep {
        plan_step_id: String,
        to: PersistedStepState,
    },
    RequestApproval {
        action_id: String,
    },
    RequestUserInput,
    SupplyUserInput,
    ProposeAction {
        proposal: PersistedActionProposal,
    },
    RecordPolicyDecision {
        action_id: String,
        decision: PersistedProposalDecision,
        dispatch_id: Option<String>,
    },
    DispatchAction {
        action_id: String,
        dispatch_id: String,
    },
    RecordActionOutcome {
        action_id: String,
        outcome: PersistedActionOutcome,
    },
    ResultCandidateReady,
    CompleteResultValidated {
        result: PersistedTaskResult,
    },
    PartialResultValidated {
        result: PersistedTaskResult,
    },
    ResumeForCorrection,
    FailTask {
        reason: PersistedFailureReason,
    },
    CorrectFact {
        fact_id: String,
    },
    ExcludeSource {
        source_id: String,
    },
    AcceptArtifact {
        artifact_id: String,
    },
    ExportArtifact {
        artifact_id: String,
        format: PersistedArtifactKind,
    },
    RequestPermission {
        request: PersistedPermissionRequest,
    },
    RecordPermissionResult {
        result: PersistedPermissionResult,
    },
    RequestModelTurn {
        call_id: String,
    },
    RecordModelTurn {
        call_id: String,
        digest: PersistedTurnDigest,
    },
    RecordModelTurnGap {
        call_id: String,
        gap: PersistedTurnGap,
    },
    RequestHandover {
        handover_id: String,
    },
    CompleteHandover {
        completion: PersistedHandoverCompletion,
    },
    ExpireHandover {
        handover_id: String,
    },
    RecordContextEviction {
        through_turn: u64,
    },
    RecordToolJobOutcome {
        action_id: String,
        job_id: String,
        outcome: PersistedToolJobOutcome,
    },
    RequestFieldValues {
        request_id: String,
        tab_id: String,
        node_id: String,
    },
    SupplyFieldValues {
        request_id: String,
        supplied: u32,
    },
    RequestArtifact {
        artifact_id: String,
        format: PersistedArtifactKind,
        workspace_revision: u64,
    },
    RecordDiscoveryTab {
        discovery_tab_id: String,
        browser_session_id: String,
    },
    RequestModelAttempt {
        call_id: String,
        attempt_ordinal: u32,
        candidate_ordinal: u32,
        kind: PersistedModelAttemptKind,
    },
    FollowUp,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum PersistedEventSubject {
    Action {
        id: String,
    },
    Plan {
        id: String,
    },
    PlanStep {
        id: String,
    },
    Artifact {
        id: String,
    },
    Source {
        id: String,
    },
    Fact {
        id: String,
    },
    Capability {
        id: String,
    },
    PermissionRequest {
        id: String,
    },
    Handover {
        id: String,
    },
    ActorLease {
        id: String,
    },
    DiscoveryTab {
        id: String,
    },
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum PersistedJournalEntry {
    Command {
        record: PersistedCommandRecord,
    },
    Event {
        record: PersistedEventRecord,
    },
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum PersistedEffectIntent {
    RevokeAuthority {
        reason: PersistedRevocationReason,
    },
    AskPolicy {
        action_id: String,
    },
    RequestApproval {
        action_id: String,
    },
    DispatchAction {
        action_id: String,
    },
    AwaitInFlightWork {
        settle_with: PersistedCommandKind,
    },
    ReconcileAction {
        action_id: String,
        rule: PersistedRecoveryRule,
    },
    ReleaseTaskTabs,
    GenerateArtifact {
        artifact_id: String,
        kind: PersistedArtifactKind,
        workspace_revision: u64,
    },
    ExportArtifact {
        artifact_id: String,
        kind: PersistedArtifactKind,
        workspace_revision: u64,
    },
    RequestPermission {
        request_id: String,
        permission: PersistedPlatformPermission,
        deadline_monotonic_ms: u64,
        deadline_utc_ms: u64,
        browser_session_id: String,
    },
    CallModel {
        call_id: String,
    },
    AwaitHandover {
        handover_id: String,
        window_ms: u32,
    },
    RunToolJob {
        action_id: String,
        job_id: String,
        runtime: PersistedToolRuntime,
    },
    RequestFieldValues {
        request_id: String,
        tab_id: String,
        node_id: String,
    },
    PrepareDiscoveryTab {
        browser_session_id: String,
        remaining_new_source_cap: u32,
    },
    RunLibraryTool {
        action_id: String,
    },
    RunMemoryTool {
        action_id: String,
    },
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TransactionBatchCodecError {
    SizeLimit,
    CollectionLimit,
    StringLimit,
    LengthOverflow,
    Truncated,
    InvalidMagic,
    UnsupportedVersion,
    InvalidBoolean,
    InvalidEnum,
    InvalidTaggedUnion,
    InvalidUtf8,
    TrailingBytes,
}

pub const TRANSACTION_BATCH_MAGIC: &[u8] = &[84, 65, 70, 70, 89, 84, 88, 78];
pub const TRANSACTION_BATCH_SCHEMA_VERSION: u32 = 32;
const TRANSACTION_BATCH_MAX_COLLECTION_ITEMS: usize = 4096;
const TRANSACTION_BATCH_MAX_STRING_BYTES: usize = 65536;

struct TransactionBatchEncoder {
    bytes: Vec<u8>,
}

#[allow(dead_code)]
impl TransactionBatchEncoder {
    fn put_raw(&mut self, value: &[u8]) -> Result<(), TransactionBatchCodecError> {
        let length = self.bytes.len().checked_add(value.len())
            .ok_or(TransactionBatchCodecError::LengthOverflow)?;
        if length > MAX_EFFECT_BYTES {
            return Err(TransactionBatchCodecError::SizeLimit);
        }
        self.bytes.extend_from_slice(value);
        Ok(())
    }
    fn put_len(&mut self, value: usize) -> Result<(), TransactionBatchCodecError> {
        if value > TRANSACTION_BATCH_MAX_COLLECTION_ITEMS {
            return Err(TransactionBatchCodecError::CollectionLimit);
        }
        let value = u32::try_from(value).map_err(|_| TransactionBatchCodecError::LengthOverflow)?;
        self.put_u32(value)
    }
    fn put_bool(&mut self, value: bool) -> Result<(), TransactionBatchCodecError> { self.put_u8(u8::from(value)) }
    fn put_u8(&mut self, value: u8) -> Result<(), TransactionBatchCodecError> { self.put_raw(&[value]) }
    fn put_u32(&mut self, value: u32) -> Result<(), TransactionBatchCodecError> { self.put_raw(&value.to_le_bytes()) }
    fn put_u64(&mut self, value: u64) -> Result<(), TransactionBatchCodecError> { self.put_raw(&value.to_le_bytes()) }
    fn put_i64(&mut self, value: i64) -> Result<(), TransactionBatchCodecError> { self.put_raw(&value.to_le_bytes()) }
    fn put_bytes32(&mut self, value: &[u8; 32]) -> Result<(), TransactionBatchCodecError> { self.put_raw(value) }
    fn put_bytes(&mut self, value: &[u8]) -> Result<(), TransactionBatchCodecError> {
        if value.len() > MAX_EFFECT_BYTES { return Err(TransactionBatchCodecError::SizeLimit); }
        let length = u32::try_from(value.len()).map_err(|_| TransactionBatchCodecError::LengthOverflow)?;
        self.put_u32(length)?;
        self.put_raw(value)
    }
    fn put_string(&mut self, value: &str) -> Result<(), TransactionBatchCodecError> {
        if value.len() > TRANSACTION_BATCH_MAX_STRING_BYTES {
            return Err(TransactionBatchCodecError::StringLimit);
        }
        let length = u32::try_from(value.len()).map_err(|_| TransactionBatchCodecError::LengthOverflow)?;
        self.put_u32(length)?;
        self.put_raw(value.as_bytes())
    }
}

struct TransactionBatchDecoder<'a> {
    bytes: &'a [u8],
    offset: usize,
}

#[allow(dead_code)]
impl<'a> TransactionBatchDecoder<'a> {
    fn take(&mut self, length: usize) -> Result<&'a [u8], TransactionBatchCodecError> {
        let end = self.offset.checked_add(length)
            .ok_or(TransactionBatchCodecError::LengthOverflow)?;
        let value = self.bytes.get(self.offset..end).ok_or(TransactionBatchCodecError::Truncated)?;
        self.offset = end;
        Ok(value)
    }
    fn read_u8(&mut self) -> Result<u8, TransactionBatchCodecError> {
        self.take(1)?.first().copied().ok_or(TransactionBatchCodecError::Truncated)
    }
    fn read_bool(&mut self) -> Result<bool, TransactionBatchCodecError> {
        match self.read_u8()? { 0 => Ok(false), 1 => Ok(true), _ => Err(TransactionBatchCodecError::InvalidBoolean) }
    }
    fn read_u32(&mut self) -> Result<u32, TransactionBatchCodecError> {
        let value: [u8; 4] = self.take(4)?.try_into().map_err(|_| TransactionBatchCodecError::Truncated)?;
        Ok(u32::from_le_bytes(value))
    }
    fn read_u64(&mut self) -> Result<u64, TransactionBatchCodecError> {
        let value: [u8; 8] = self.take(8)?.try_into().map_err(|_| TransactionBatchCodecError::Truncated)?;
        Ok(u64::from_le_bytes(value))
    }
    fn read_i64(&mut self) -> Result<i64, TransactionBatchCodecError> {
        let value: [u8; 8] = self.take(8)?.try_into().map_err(|_| TransactionBatchCodecError::Truncated)?;
        Ok(i64::from_le_bytes(value))
    }
    fn read_bytes32(&mut self) -> Result<[u8; 32], TransactionBatchCodecError> {
        self.take(32)?.try_into().map_err(|_| TransactionBatchCodecError::Truncated)
    }
    fn read_length(&mut self) -> Result<usize, TransactionBatchCodecError> {
        let value = usize::try_from(self.read_u32()?)
            .map_err(|_| TransactionBatchCodecError::LengthOverflow)?;
        if value > TRANSACTION_BATCH_MAX_COLLECTION_ITEMS {
            return Err(TransactionBatchCodecError::CollectionLimit);
        }
        Ok(value)
    }
    fn read_bytes(&mut self) -> Result<Vec<u8>, TransactionBatchCodecError> {
        let length = usize::try_from(self.read_u32()?)
            .map_err(|_| TransactionBatchCodecError::LengthOverflow)?;
        if length > MAX_EFFECT_BYTES { return Err(TransactionBatchCodecError::SizeLimit); }
        Ok(self.take(length)?.to_vec())
    }
    fn read_string(&mut self) -> Result<String, TransactionBatchCodecError> {
        let length = usize::try_from(self.read_u32()?)
            .map_err(|_| TransactionBatchCodecError::LengthOverflow)?;
        if length > TRANSACTION_BATCH_MAX_STRING_BYTES {
            return Err(TransactionBatchCodecError::StringLimit);
        }
        let value = core::str::from_utf8(self.take(length)?).map_err(|_| TransactionBatchCodecError::InvalidUtf8)?;
        Ok(value.to_owned())
    }
    fn read_optional<T>(&mut self, read: impl FnOnce(&mut Self) -> Result<T, TransactionBatchCodecError>) -> Result<Option<T>, TransactionBatchCodecError> {
        match self.read_u8()? { 0 => Ok(None), 1 => read(self).map(Some), _ => Err(TransactionBatchCodecError::InvalidBoolean) }
    }
    fn read_list<T>(&mut self, mut read: impl FnMut(&mut Self) -> Result<T, TransactionBatchCodecError>) -> Result<Vec<T>, TransactionBatchCodecError> {
        let length = self.read_length()?;
        let mut values = Vec::with_capacity(length);
        for _ in 0..length { values.push(read(self)?); }
        Ok(values)
    }
}

fn encode_persisted_task_kind(encoder: &mut TransactionBatchEncoder, value: &PersistedTaskKind) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_task_kind(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedTaskKind, TransactionBatchCodecError> {
    PersistedTaskKind::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_task_template_id(encoder: &mut TransactionBatchEncoder, value: &PersistedTaskTemplateId) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_task_template_id(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedTaskTemplateId, TransactionBatchCodecError> {
    PersistedTaskTemplateId::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_builtin_skill_id(encoder: &mut TransactionBatchEncoder, value: &PersistedBuiltinSkillId) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_builtin_skill_id(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedBuiltinSkillId, TransactionBatchCodecError> {
    PersistedBuiltinSkillId::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_control_mode(encoder: &mut TransactionBatchEncoder, value: &PersistedControlMode) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_control_mode(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedControlMode, TransactionBatchCodecError> {
    PersistedControlMode::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_milestone(encoder: &mut TransactionBatchEncoder, value: &PersistedMilestone) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_milestone(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedMilestone, TransactionBatchCodecError> {
    PersistedMilestone::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_budget_kind(encoder: &mut TransactionBatchEncoder, value: &PersistedBudgetKind) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_budget_kind(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedBudgetKind, TransactionBatchCodecError> {
    PersistedBudgetKind::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_pause_cause(encoder: &mut TransactionBatchEncoder, value: &PersistedPauseCause) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_pause_cause(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedPauseCause, TransactionBatchCodecError> {
    PersistedPauseCause::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_step_kind(encoder: &mut TransactionBatchEncoder, value: &PersistedStepKind) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_step_kind(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedStepKind, TransactionBatchCodecError> {
    PersistedStepKind::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_step_state(encoder: &mut TransactionBatchEncoder, value: &PersistedStepState) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_step_state(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedStepState, TransactionBatchCodecError> {
    PersistedStepState::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_action_class(encoder: &mut TransactionBatchEncoder, value: &PersistedActionClass) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_action_class(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedActionClass, TransactionBatchCodecError> {
    PersistedActionClass::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_idempotency_class(encoder: &mut TransactionBatchEncoder, value: &PersistedIdempotencyClass) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_idempotency_class(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedIdempotencyClass, TransactionBatchCodecError> {
    PersistedIdempotencyClass::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_action_result_code(encoder: &mut TransactionBatchEncoder, value: &PersistedActionResultCode) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_action_result_code(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedActionResultCode, TransactionBatchCodecError> {
    PersistedActionResultCode::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_observation_completeness(encoder: &mut TransactionBatchEncoder, value: &PersistedObservationCompleteness) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_observation_completeness(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedObservationCompleteness, TransactionBatchCodecError> {
    PersistedObservationCompleteness::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_sensitivity(encoder: &mut TransactionBatchEncoder, value: &PersistedSensitivity) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_sensitivity(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedSensitivity, TransactionBatchCodecError> {
    PersistedSensitivity::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_failure_reason(encoder: &mut TransactionBatchEncoder, value: &PersistedFailureReason) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_failure_reason(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedFailureReason, TransactionBatchCodecError> {
    PersistedFailureReason::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_platform_permission(encoder: &mut TransactionBatchEncoder, value: &PersistedPlatformPermission) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_platform_permission(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedPlatformPermission, TransactionBatchCodecError> {
    PersistedPlatformPermission::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_permission_decision(encoder: &mut TransactionBatchEncoder, value: &PersistedPermissionDecision) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_permission_decision(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedPermissionDecision, TransactionBatchCodecError> {
    PersistedPermissionDecision::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_artifact_kind(encoder: &mut TransactionBatchEncoder, value: &PersistedArtifactKind) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_artifact_kind(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedArtifactKind, TransactionBatchCodecError> {
    PersistedArtifactKind::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_gap_reason(encoder: &mut TransactionBatchEncoder, value: &PersistedGapReason) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_gap_reason(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedGapReason, TransactionBatchCodecError> {
    PersistedGapReason::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_task_state(encoder: &mut TransactionBatchEncoder, value: &PersistedTaskState) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_task_state(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedTaskState, TransactionBatchCodecError> {
    PersistedTaskState::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_state_reason(encoder: &mut TransactionBatchEncoder, value: &PersistedStateReason) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_state_reason(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedStateReason, TransactionBatchCodecError> {
    PersistedStateReason::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_command_kind(encoder: &mut TransactionBatchEncoder, value: &PersistedCommandKind) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_command_kind(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedCommandKind, TransactionBatchCodecError> {
    PersistedCommandKind::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_event_kind(encoder: &mut TransactionBatchEncoder, value: &PersistedEventKind) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_event_kind(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedEventKind, TransactionBatchCodecError> {
    PersistedEventKind::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_revocation_reason(encoder: &mut TransactionBatchEncoder, value: &PersistedRevocationReason) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_revocation_reason(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedRevocationReason, TransactionBatchCodecError> {
    PersistedRevocationReason::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_recovery_rule(encoder: &mut TransactionBatchEncoder, value: &PersistedRecoveryRule) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_recovery_rule(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedRecoveryRule, TransactionBatchCodecError> {
    PersistedRecoveryRule::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_audit_actor(encoder: &mut TransactionBatchEncoder, value: &PersistedAuditActor) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_audit_actor(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedAuditActor, TransactionBatchCodecError> {
    PersistedAuditActor::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_audit_subject_kind(encoder: &mut TransactionBatchEncoder, value: &PersistedAuditSubjectKind) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_audit_subject_kind(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedAuditSubjectKind, TransactionBatchCodecError> {
    PersistedAuditSubjectKind::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_audit_event_type(encoder: &mut TransactionBatchEncoder, value: &PersistedAuditEventType) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_audit_event_type(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedAuditEventType, TransactionBatchCodecError> {
    PersistedAuditEventType::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_model_stop_reason(encoder: &mut TransactionBatchEncoder, value: &PersistedModelStopReason) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_model_stop_reason(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedModelStopReason, TransactionBatchCodecError> {
    PersistedModelStopReason::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_model_attempt_kind(encoder: &mut TransactionBatchEncoder, value: &PersistedModelAttemptKind) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_model_attempt_kind(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedModelAttemptKind, TransactionBatchCodecError> {
    PersistedModelAttemptKind::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_turn_overflow(encoder: &mut TransactionBatchEncoder, value: &PersistedTurnOverflow) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_turn_overflow(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedTurnOverflow, TransactionBatchCodecError> {
    PersistedTurnOverflow::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_turn_gap(encoder: &mut TransactionBatchEncoder, value: &PersistedTurnGap) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_turn_gap(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedTurnGap, TransactionBatchCodecError> {
    PersistedTurnGap::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_page_readability(encoder: &mut TransactionBatchEncoder, value: &PersistedPageReadability) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_page_readability(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedPageReadability, TransactionBatchCodecError> {
    PersistedPageReadability::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_tool_job_status(encoder: &mut TransactionBatchEncoder, value: &PersistedToolJobStatus) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_tool_job_status(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedToolJobStatus, TransactionBatchCodecError> {
    PersistedToolJobStatus::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_tool_runtime(encoder: &mut TransactionBatchEncoder, value: &PersistedToolRuntime) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(*value as u32)
}
fn decode_persisted_tool_runtime(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedToolRuntime, TransactionBatchCodecError> {
    PersistedToolRuntime::from_wire(decoder.read_u32()?).ok_or(TransactionBatchCodecError::InvalidEnum)
}

fn encode_persisted_budget(encoder: &mut TransactionBatchEncoder, value: &PersistedBudget) -> Result<(), TransactionBatchCodecError> {
    encode_persisted_budget_kind(encoder, &value.kind)?;
    encoder.put_u64(value.limit)?;
    Ok(())
}
fn decode_persisted_budget(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedBudget, TransactionBatchCodecError> {
    let value = PersistedBudget {
        kind: decode_persisted_budget_kind(decoder)?,
        limit: decoder.read_u64()?,
    };
    Ok(value)
}

fn encode_persisted_consented_source(encoder: &mut TransactionBatchEncoder, value: &PersistedConsentedSource) -> Result<(), TransactionBatchCodecError> {
    encoder.put_string(&value.source_id)?;
    encoder.put_string(&value.tab_id)?;
    encoder.put_string(&value.normalized_origin)?;
    match &value.canonical_locator {
        Some(value) => {
            encoder.put_u8(1)?;
            encoder.put_string(value)?;
        }
        None => encoder.put_u8(0)?,
    }
    Ok(())
}
fn decode_persisted_consented_source(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedConsentedSource, TransactionBatchCodecError> {
    let value = PersistedConsentedSource {
        source_id: decoder.read_string()?,
        tab_id: decoder.read_string()?,
        normalized_origin: decoder.read_string()?,
        canonical_locator: decoder.read_optional(|decoder| Ok(decoder.read_string()?))?,
    };
    Ok(value)
}

fn encode_persisted_library_refresh_source(encoder: &mut TransactionBatchEncoder, value: &PersistedLibraryRefreshSource) -> Result<(), TransactionBatchCodecError> {
    encoder.put_string(&value.source_id)?;
    encoder.put_string(&value.title)?;
    encoder.put_string(&value.host)?;
    encoder.put_string(&value.canonical_locator)?;
    encoder.put_bytes32(&value.original_content_digest)?;
    Ok(())
}
fn decode_persisted_library_refresh_source(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedLibraryRefreshSource, TransactionBatchCodecError> {
    let value = PersistedLibraryRefreshSource {
        source_id: decoder.read_string()?,
        title: decoder.read_string()?,
        host: decoder.read_string()?,
        canonical_locator: decoder.read_string()?,
        original_content_digest: decoder.read_bytes32()?,
    };
    Ok(value)
}

fn encode_persisted_library_refresh_context(encoder: &mut TransactionBatchEncoder, value: &PersistedLibraryRefreshContext) -> Result<(), TransactionBatchCodecError> {
    encoder.put_string(&value.preview_id)?;
    encoder.put_u64(value.library_revision)?;
    encoder.put_string(&value.collection_id)?;
    encoder.put_u64(value.source_workspace_revision)?;
    encoder.put_len(value.sources.len())?;
    for item in &value.sources {
        encode_persisted_library_refresh_source(encoder, item)?;
    }
    Ok(())
}
fn decode_persisted_library_refresh_context(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedLibraryRefreshContext, TransactionBatchCodecError> {
    let value = PersistedLibraryRefreshContext {
        preview_id: decoder.read_string()?,
        library_revision: decoder.read_u64()?,
        collection_id: decoder.read_string()?,
        source_workspace_revision: decoder.read_u64()?,
        sources: decoder.read_list(|decoder| Ok(decode_persisted_library_refresh_source(decoder)?))?,
    };
    Ok(value)
}

fn encode_persisted_builtin_skill_reference(encoder: &mut TransactionBatchEncoder, value: &PersistedBuiltinSkillReference) -> Result<(), TransactionBatchCodecError> {
    encode_persisted_builtin_skill_id(encoder, &value.skill_id)?;
    encoder.put_u32(value.version)?;
    Ok(())
}
fn decode_persisted_builtin_skill_reference(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedBuiltinSkillReference, TransactionBatchCodecError> {
    let value = PersistedBuiltinSkillReference {
        skill_id: decode_persisted_builtin_skill_id(decoder)?,
        version: decoder.read_u32()?,
    };
    Ok(value)
}

fn encode_persisted_task_snapshot(encoder: &mut TransactionBatchEncoder, value: &PersistedTaskSnapshot) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(value.assistant_config_version)?;
    match &value.skill_version_id {
        Some(value) => {
            encoder.put_u8(1)?;
            encoder.put_string(value)?;
        }
        None => encoder.put_u8(0)?,
    }
    encoder.put_len(value.tool_allowlist.len())?;
    for item in &value.tool_allowlist {
        encoder.put_string(item)?;
    }
    encoder.put_u32(value.capability_policy_version)?;
    match &value.provider_route {
        Some(value) => {
            encoder.put_u8(1)?;
            encoder.put_string(value)?;
        }
        None => encoder.put_u8(0)?,
    }
    encode_persisted_milestone(encoder, &value.milestone)?;
    encode_persisted_task_template_id(encoder, &value.template_id)?;
    encoder.put_len(value.consented_sources.len())?;
    for item in &value.consented_sources {
        encode_persisted_consented_source(encoder, item)?;
    }
    encoder.put_bool(value.source_discovery_enabled)?;
    encoder.put_string(&value.browser_session_id)?;
    encoder.put_u32(value.remaining_new_source_cap)?;
    match &value.discovery_tab_id {
        Some(value) => {
            encoder.put_u8(1)?;
            encoder.put_string(value)?;
        }
        None => encoder.put_u8(0)?,
    }
    match &value.library_refresh {
        Some(value) => {
            encoder.put_u8(1)?;
            encode_persisted_library_refresh_context(encoder, value)?;
        }
        None => encoder.put_u8(0)?,
    }
    match &value.builtin_skill {
        Some(value) => {
            encoder.put_u8(1)?;
            encode_persisted_builtin_skill_reference(encoder, value)?;
        }
        None => encoder.put_u8(0)?,
    }
    Ok(())
}
fn decode_persisted_task_snapshot(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedTaskSnapshot, TransactionBatchCodecError> {
    let value = PersistedTaskSnapshot {
        assistant_config_version: decoder.read_u32()?,
        skill_version_id: decoder.read_optional(|decoder| Ok(decoder.read_string()?))?,
        tool_allowlist: decoder.read_list(|decoder| Ok(decoder.read_string()?))?,
        capability_policy_version: decoder.read_u32()?,
        provider_route: decoder.read_optional(|decoder| Ok(decoder.read_string()?))?,
        milestone: decode_persisted_milestone(decoder)?,
        template_id: decode_persisted_task_template_id(decoder)?,
        consented_sources: decoder.read_list(|decoder| Ok(decode_persisted_consented_source(decoder)?))?,
        source_discovery_enabled: decoder.read_bool()?,
        browser_session_id: decoder.read_string()?,
        remaining_new_source_cap: decoder.read_u32()?,
        discovery_tab_id: decoder.read_optional(|decoder| Ok(decoder.read_string()?))?,
        library_refresh: decoder.read_optional(|decoder| Ok(decode_persisted_library_refresh_context(decoder)?))?,
        builtin_skill: decoder.read_optional(|decoder| Ok(decode_persisted_builtin_skill_reference(decoder)?))?,
    };
    Ok(value)
}

fn encode_persisted_task_seed(encoder: &mut TransactionBatchEncoder, value: &PersistedTaskSeed) -> Result<(), TransactionBatchCodecError> {
    encoder.put_string(&value.task_id)?;
    match &value.workspace_id {
        Some(value) => {
            encoder.put_u8(1)?;
            encoder.put_string(value)?;
        }
        None => encoder.put_u8(0)?,
    }
    encoder.put_string(&value.browser_profile_id)?;
    encode_persisted_task_kind(encoder, &value.kind)?;
    encoder.put_string(&value.user_goal)?;
    encode_persisted_control_mode(encoder, &value.control_mode)?;
    encode_persisted_task_snapshot(encoder, &value.snapshot)?;
    encoder.put_len(value.budgets.len())?;
    for item in &value.budgets {
        encode_persisted_budget(encoder, item)?;
    }
    match &value.deadline_monotonic_ms {
        Some(value) => {
            encoder.put_u8(1)?;
            encoder.put_u64(*value)?;
        }
        None => encoder.put_u8(0)?,
    }
    match &value.predecessor_task_id {
        Some(value) => {
            encoder.put_u8(1)?;
            encoder.put_string(value)?;
        }
        None => encoder.put_u8(0)?,
    }
    match &value.deadline_utc_ms {
        Some(value) => {
            encoder.put_u8(1)?;
            encoder.put_u64(*value)?;
        }
        None => encoder.put_u8(0)?,
    }
    Ok(())
}
fn decode_persisted_task_seed(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedTaskSeed, TransactionBatchCodecError> {
    let value = PersistedTaskSeed {
        task_id: decoder.read_string()?,
        workspace_id: decoder.read_optional(|decoder| Ok(decoder.read_string()?))?,
        browser_profile_id: decoder.read_string()?,
        kind: decode_persisted_task_kind(decoder)?,
        user_goal: decoder.read_string()?,
        control_mode: decode_persisted_control_mode(decoder)?,
        snapshot: decode_persisted_task_snapshot(decoder)?,
        budgets: decoder.read_list(|decoder| Ok(decode_persisted_budget(decoder)?))?,
        deadline_monotonic_ms: decoder.read_optional(|decoder| Ok(decoder.read_u64()?))?,
        predecessor_task_id: decoder.read_optional(|decoder| Ok(decoder.read_string()?))?,
        deadline_utc_ms: decoder.read_optional(|decoder| Ok(decoder.read_u64()?))?,
    };
    Ok(value)
}

fn encode_persisted_source_scope(encoder: &mut TransactionBatchEncoder, value: &PersistedSourceScope) -> Result<(), TransactionBatchCodecError> {
    encoder.put_len(value.included.len())?;
    for item in &value.included {
        encoder.put_string(item)?;
    }
    encoder.put_len(value.excluded.len())?;
    for item in &value.excluded {
        encoder.put_string(item)?;
    }
    Ok(())
}
fn decode_persisted_source_scope(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedSourceScope, TransactionBatchCodecError> {
    let value = PersistedSourceScope {
        included: decoder.read_list(|decoder| Ok(decoder.read_string()?))?,
        excluded: decoder.read_list(|decoder| Ok(decoder.read_string()?))?,
    };
    Ok(value)
}

fn encode_persisted_scope_preview(encoder: &mut TransactionBatchEncoder, value: &PersistedScopePreview) -> Result<(), TransactionBatchCodecError> {
    encode_persisted_source_scope(encoder, &value.scope)?;
    match &value.provider_route {
        Some(value) => {
            encoder.put_u8(1)?;
            encoder.put_string(value)?;
        }
        None => encoder.put_u8(0)?,
    }
    encoder.put_len(value.budgets.len())?;
    for item in &value.budgets {
        encode_persisted_budget(encoder, item)?;
    }
    encoder.put_len(value.sources.len())?;
    for item in &value.sources {
        encode_persisted_consented_source(encoder, item)?;
    }
    encoder.put_bool(value.source_discovery_enabled)?;
    encoder.put_u32(value.new_source_cap)?;
    Ok(())
}
fn decode_persisted_scope_preview(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedScopePreview, TransactionBatchCodecError> {
    let value = PersistedScopePreview {
        scope: decode_persisted_source_scope(decoder)?,
        provider_route: decoder.read_optional(|decoder| Ok(decoder.read_string()?))?,
        budgets: decoder.read_list(|decoder| Ok(decode_persisted_budget(decoder)?))?,
        sources: decoder.read_list(|decoder| Ok(decode_persisted_consented_source(decoder)?))?,
        source_discovery_enabled: decoder.read_bool()?,
        new_source_cap: decoder.read_u32()?,
    };
    Ok(value)
}

fn encode_persisted_step_draft(encoder: &mut TransactionBatchEncoder, value: &PersistedStepDraft) -> Result<(), TransactionBatchCodecError> {
    encode_persisted_step_kind(encoder, &value.kind)?;
    encoder.put_string(&value.description)?;
    encoder.put_len(value.dependencies.len())?;
    for item in &value.dependencies {
        encoder.put_u64(*item)?;
    }
    Ok(())
}
fn decode_persisted_step_draft(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedStepDraft, TransactionBatchCodecError> {
    let value = PersistedStepDraft {
        kind: decode_persisted_step_kind(decoder)?,
        description: decoder.read_string()?,
        dependencies: decoder.read_list(|decoder| Ok(decoder.read_u64()?))?,
    };
    Ok(value)
}

fn encode_persisted_plan_draft(encoder: &mut TransactionBatchEncoder, value: &PersistedPlanDraft) -> Result<(), TransactionBatchCodecError> {
    encoder.put_string(&value.summary)?;
    encoder.put_len(value.steps.len())?;
    for item in &value.steps {
        encode_persisted_step_draft(encoder, item)?;
    }
    Ok(())
}
fn decode_persisted_plan_draft(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedPlanDraft, TransactionBatchCodecError> {
    let value = PersistedPlanDraft {
        summary: decoder.read_string()?,
        steps: decoder.read_list(|decoder| Ok(decode_persisted_step_draft(decoder)?))?,
    };
    Ok(value)
}

fn encode_persisted_budget_draw(encoder: &mut TransactionBatchEncoder, value: &PersistedBudgetDraw) -> Result<(), TransactionBatchCodecError> {
    encode_persisted_budget_kind(encoder, &value.kind)?;
    encoder.put_u64(value.amount)?;
    Ok(())
}
fn decode_persisted_budget_draw(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedBudgetDraw, TransactionBatchCodecError> {
    let value = PersistedBudgetDraw {
        kind: decode_persisted_budget_kind(decoder)?,
        amount: decoder.read_u64()?,
    };
    Ok(value)
}

fn encode_persisted_action_proposal(encoder: &mut TransactionBatchEncoder, value: &PersistedActionProposal) -> Result<(), TransactionBatchCodecError> {
    encoder.put_string(&value.tool_name)?;
    match &value.plan_step_id {
        Some(value) => {
            encoder.put_u8(1)?;
            encoder.put_string(value)?;
        }
        None => encoder.put_u8(0)?,
    }
    encode_persisted_action_class(encoder, &value.action_class)?;
    encoder.put_string(&value.tab_id)?;
    match &value.node_id {
        Some(value) => {
            encoder.put_u8(1)?;
            encoder.put_string(value)?;
        }
        None => encoder.put_u8(0)?,
    }
    encoder.put_string(&value.idempotency_key)?;
    encode_persisted_idempotency_class(encoder, &value.idempotency)?;
    encoder.put_bool(value.required_for_step)?;
    match &value.budget_draw {
        Some(value) => {
            encoder.put_u8(1)?;
            encode_persisted_budget_draw(encoder, value)?;
        }
        None => encoder.put_u8(0)?,
    }
    encoder.put_string(&value.proposal_digest)?;
    match &value.destination_address {
        Some(value) => {
            encoder.put_u8(1)?;
            encoder.put_string(value)?;
        }
        None => encoder.put_u8(0)?,
    }
    encoder.put_bytes(&value.canonical_intent)?;
    Ok(())
}
fn decode_persisted_action_proposal(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedActionProposal, TransactionBatchCodecError> {
    let value = PersistedActionProposal {
        tool_name: decoder.read_string()?,
        plan_step_id: decoder.read_optional(|decoder| Ok(decoder.read_string()?))?,
        action_class: decode_persisted_action_class(decoder)?,
        tab_id: decoder.read_string()?,
        node_id: decoder.read_optional(|decoder| Ok(decoder.read_string()?))?,
        idempotency_key: decoder.read_string()?,
        idempotency: decode_persisted_idempotency_class(decoder)?,
        required_for_step: decoder.read_bool()?,
        budget_draw: decoder.read_optional(|decoder| Ok(decode_persisted_budget_draw(decoder)?))?,
        proposal_digest: decoder.read_string()?,
        destination_address: decoder.read_optional(|decoder| Ok(decoder.read_string()?))?,
        canonical_intent: decoder.read_bytes()?,
    };
    Ok(value)
}

fn encode_persisted_authorization(encoder: &mut TransactionBatchEncoder, value: &PersistedAuthorization) -> Result<(), TransactionBatchCodecError> {
    encoder.put_string(&value.capability_id)?;
    Ok(())
}
fn decode_persisted_authorization(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedAuthorization, TransactionBatchCodecError> {
    let value = PersistedAuthorization {
        capability_id: decoder.read_string()?,
    };
    Ok(value)
}

fn encode_persisted_denial(encoder: &mut TransactionBatchEncoder, value: &PersistedDenial) -> Result<(), TransactionBatchCodecError> {
    encode_persisted_action_result_code(encoder, &value.code)?;
    Ok(())
}
fn decode_persisted_denial(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedDenial, TransactionBatchCodecError> {
    let value = PersistedDenial {
        code: decode_persisted_action_result_code(decoder)?,
    };
    Ok(value)
}

fn encode_persisted_observation_graph_summary(encoder: &mut TransactionBatchEncoder, value: &PersistedObservationGraphSummary) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(value.node_count)?;
    encoder.put_u32(value.relationship_count)?;
    encoder.put_u32(value.named_node_count)?;
    encoder.put_u64(value.text_run_count)?;
    encoder.put_u64(value.text_byte_count)?;
    Ok(())
}
fn decode_persisted_observation_graph_summary(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedObservationGraphSummary, TransactionBatchCodecError> {
    let value = PersistedObservationGraphSummary {
        node_count: decoder.read_u32()?,
        relationship_count: decoder.read_u32()?,
        named_node_count: decoder.read_u32()?,
        text_run_count: decoder.read_u64()?,
        text_byte_count: decoder.read_u64()?,
    };
    Ok(value)
}

fn encode_persisted_page_observation_evidence(encoder: &mut TransactionBatchEncoder, value: &PersistedPageObservationEvidence) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u64(value.service_generation)?;
    encoder.put_string(&value.schema_version)?;
    encoder.put_string(&value.tab_id)?;
    encoder.put_string(&value.frame_id)?;
    encoder.put_string(&value.page_epoch)?;
    encoder.put_u64(value.graph_revision)?;
    encoder.put_string(&value.normalized_origin)?;
    encoder.put_bool(value.private_profile)?;
    encode_persisted_observation_completeness(encoder, &value.completeness)?;
    encode_persisted_observation_graph_summary(encoder, &value.graph)?;
    encoder.put_u32(value.total_bytes)?;
    encoder.put_bool(value.truncated)?;
    encoder.put_bool(value.may_change_answer)?;
    encoder.put_u32(value.redacted_field_count)?;
    encoder.put_u32(value.suppressed_secret_value_count)?;
    encoder.put_u32(value.sensitive_zone_count)?;
    encoder.put_u32(value.policy_filtered_frame_count)?;
    encode_persisted_sensitivity(encoder, &value.highest_sensitivity)?;
    Ok(())
}
fn decode_persisted_page_observation_evidence(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedPageObservationEvidence, TransactionBatchCodecError> {
    let value = PersistedPageObservationEvidence {
        service_generation: decoder.read_u64()?,
        schema_version: decoder.read_string()?,
        tab_id: decoder.read_string()?,
        frame_id: decoder.read_string()?,
        page_epoch: decoder.read_string()?,
        graph_revision: decoder.read_u64()?,
        normalized_origin: decoder.read_string()?,
        private_profile: decoder.read_bool()?,
        completeness: decode_persisted_observation_completeness(decoder)?,
        graph: decode_persisted_observation_graph_summary(decoder)?,
        total_bytes: decoder.read_u32()?,
        truncated: decoder.read_bool()?,
        may_change_answer: decoder.read_bool()?,
        redacted_field_count: decoder.read_u32()?,
        suppressed_secret_value_count: decoder.read_u32()?,
        sensitive_zone_count: decoder.read_u32()?,
        policy_filtered_frame_count: decoder.read_u32()?,
        highest_sensitivity: decode_persisted_sensitivity(decoder)?,
    };
    Ok(value)
}

fn encode_persisted_action_outcome(encoder: &mut TransactionBatchEncoder, value: &PersistedActionOutcome) -> Result<(), TransactionBatchCodecError> {
    encode_persisted_action_result_code(encoder, &value.code)?;
    match &value.dispatch_id {
        Some(value) => {
            encoder.put_u8(1)?;
            encoder.put_string(value)?;
        }
        None => encoder.put_u8(0)?,
    }
    encoder.put_u64(value.observed_at_monotonic_ms)?;
    match &value.observation {
        Some(value) => {
            encoder.put_u8(1)?;
            encode_persisted_page_observation_evidence(encoder, value)?;
        }
        None => encoder.put_u8(0)?,
    }
    match &value.discovered_source {
        Some(value) => {
            encoder.put_u8(1)?;
            encode_persisted_consented_source(encoder, value)?;
        }
        None => encoder.put_u8(0)?,
    }
    Ok(())
}
fn decode_persisted_action_outcome(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedActionOutcome, TransactionBatchCodecError> {
    let value = PersistedActionOutcome {
        code: decode_persisted_action_result_code(decoder)?,
        dispatch_id: decoder.read_optional(|decoder| Ok(decoder.read_string()?))?,
        observed_at_monotonic_ms: decoder.read_u64()?,
        observation: decoder.read_optional(|decoder| Ok(decode_persisted_page_observation_evidence(decoder)?))?,
        discovered_source: decoder.read_optional(|decoder| Ok(decode_persisted_consented_source(decoder)?))?,
    };
    Ok(value)
}

fn encode_persisted_unmet_requirement(encoder: &mut TransactionBatchEncoder, value: &PersistedUnmetRequirement) -> Result<(), TransactionBatchCodecError> {
    encoder.put_string(&value.subject)?;
    encode_persisted_gap_reason(encoder, &value.reason)?;
    Ok(())
}
fn decode_persisted_unmet_requirement(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedUnmetRequirement, TransactionBatchCodecError> {
    let value = PersistedUnmetRequirement {
        subject: decoder.read_string()?,
        reason: decode_persisted_gap_reason(decoder)?,
    };
    Ok(value)
}

fn encode_persisted_task_result(encoder: &mut TransactionBatchEncoder, value: &PersistedTaskResult) -> Result<(), TransactionBatchCodecError> {
    encoder.put_len(value.artifact_ids.len())?;
    for item in &value.artifact_ids {
        encoder.put_string(item)?;
    }
    encoder.put_len(value.unmet.len())?;
    for item in &value.unmet {
        encode_persisted_unmet_requirement(encoder, item)?;
    }
    encoder.put_u64(value.fact_count)?;
    encoder.put_u64(value.source_count)?;
    Ok(())
}
fn decode_persisted_task_result(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedTaskResult, TransactionBatchCodecError> {
    let value = PersistedTaskResult {
        artifact_ids: decoder.read_list(|decoder| Ok(decoder.read_string()?))?,
        unmet: decoder.read_list(|decoder| Ok(decode_persisted_unmet_requirement(decoder)?))?,
        fact_count: decoder.read_u64()?,
        source_count: decoder.read_u64()?,
    };
    Ok(value)
}

fn encode_persisted_permission_request(encoder: &mut TransactionBatchEncoder, value: &PersistedPermissionRequest) -> Result<(), TransactionBatchCodecError> {
    encoder.put_string(&value.request_id)?;
    encode_persisted_platform_permission(encoder, &value.permission)?;
    encoder.put_u64(value.deadline_monotonic_ms)?;
    encoder.put_u64(value.deadline_utc_ms)?;
    encoder.put_string(&value.browser_session_id)?;
    Ok(())
}
fn decode_persisted_permission_request(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedPermissionRequest, TransactionBatchCodecError> {
    let value = PersistedPermissionRequest {
        request_id: decoder.read_string()?,
        permission: decode_persisted_platform_permission(decoder)?,
        deadline_monotonic_ms: decoder.read_u64()?,
        deadline_utc_ms: decoder.read_u64()?,
        browser_session_id: decoder.read_string()?,
    };
    Ok(value)
}

fn encode_persisted_handover_completion(encoder: &mut TransactionBatchEncoder, value: &PersistedHandoverCompletion) -> Result<(), TransactionBatchCodecError> {
    encoder.put_string(&value.handover_id)?;
    encoder.put_string(&value.lease_before)?;
    encoder.put_string(&value.resumed_with)?;
    encoder.put_u32(value.person_input)?;
    Ok(())
}
fn decode_persisted_handover_completion(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedHandoverCompletion, TransactionBatchCodecError> {
    let value = PersistedHandoverCompletion {
        handover_id: decoder.read_string()?,
        lease_before: decoder.read_string()?,
        resumed_with: decoder.read_string()?,
        person_input: decoder.read_u32()?,
    };
    Ok(value)
}

fn encode_persisted_permission_result(encoder: &mut TransactionBatchEncoder, value: &PersistedPermissionResult) -> Result<(), TransactionBatchCodecError> {
    encoder.put_string(&value.request_id)?;
    encode_persisted_platform_permission(encoder, &value.permission)?;
    encode_persisted_permission_decision(encoder, &value.decision)?;
    Ok(())
}
fn decode_persisted_permission_result(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedPermissionResult, TransactionBatchCodecError> {
    let value = PersistedPermissionResult {
        request_id: decoder.read_string()?,
        permission: decode_persisted_platform_permission(decoder)?,
        decision: decode_persisted_permission_decision(decoder)?,
    };
    Ok(value)
}

fn encode_persisted_tool_job_outcome(encoder: &mut TransactionBatchEncoder, value: &PersistedToolJobOutcome) -> Result<(), TransactionBatchCodecError> {
    encode_persisted_tool_job_status(encoder, &value.status)?;
    match &value.output_digest {
        Some(value) => {
            encoder.put_u8(1)?;
            encoder.put_string(value)?;
        }
        None => encoder.put_u8(0)?,
    }
    encoder.put_u64(value.output_bytes)?;
    encoder.put_u32(value.output_chunks)?;
    Ok(())
}
fn decode_persisted_tool_job_outcome(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedToolJobOutcome, TransactionBatchCodecError> {
    let value = PersistedToolJobOutcome {
        status: decode_persisted_tool_job_status(decoder)?,
        output_digest: decoder.read_optional(|decoder| Ok(decoder.read_string()?))?,
        output_bytes: decoder.read_u64()?,
        output_chunks: decoder.read_u32()?,
    };
    Ok(value)
}

fn encode_persisted_turn_digest(encoder: &mut TransactionBatchEncoder, value: &PersistedTurnDigest) -> Result<(), TransactionBatchCodecError> {
    encode_persisted_model_stop_reason(encoder, &value.stop)?;
    match &value.overflow {
        Some(value) => {
            encoder.put_u8(1)?;
            encode_persisted_turn_overflow(encoder, value)?;
        }
        None => encoder.put_u8(0)?,
    }
    encoder.put_u64(value.input_units)?;
    encoder.put_u64(value.output_units)?;
    encoder.put_u64(value.cache_read_units)?;
    encoder.put_u64(value.cache_write_units)?;
    encoder.put_u32(value.answer_segment_count)?;
    encoder.put_u32(value.tool_call_count)?;
    encoder.put_u32(value.refused_tool_call_count)?;
    encode_persisted_page_readability(encoder, &value.readability)?;
    encoder.put_u32(value.offered_node_count)?;
    encoder.put_u32(value.omitted_node_count)?;
    encoder.put_u32(value.unreadable_node_count)?;
    encoder.put_u64(value.unreadable_text_bytes)?;
    encoder.put_bytes32(&value.render_digest)?;
    Ok(())
}
fn decode_persisted_turn_digest(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedTurnDigest, TransactionBatchCodecError> {
    let value = PersistedTurnDigest {
        stop: decode_persisted_model_stop_reason(decoder)?,
        overflow: decoder.read_optional(|decoder| Ok(decode_persisted_turn_overflow(decoder)?))?,
        input_units: decoder.read_u64()?,
        output_units: decoder.read_u64()?,
        cache_read_units: decoder.read_u64()?,
        cache_write_units: decoder.read_u64()?,
        answer_segment_count: decoder.read_u32()?,
        tool_call_count: decoder.read_u32()?,
        refused_tool_call_count: decoder.read_u32()?,
        readability: decode_persisted_page_readability(decoder)?,
        offered_node_count: decoder.read_u32()?,
        omitted_node_count: decoder.read_u32()?,
        unreadable_node_count: decoder.read_u32()?,
        unreadable_text_bytes: decoder.read_u64()?,
        render_digest: decoder.read_bytes32()?,
    };
    Ok(value)
}

fn encode_persisted_command_envelope(encoder: &mut TransactionBatchEncoder, value: &PersistedCommandEnvelope) -> Result<(), TransactionBatchCodecError> {
    encoder.put_string(&value.idempotency_key)?;
    encoder.put_u64(value.expected_revision)?;
    encoder.put_string(&value.trace_id)?;
    encode_persisted_command(encoder, &value.command)?;
    Ok(())
}
fn decode_persisted_command_envelope(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedCommandEnvelope, TransactionBatchCodecError> {
    let value = PersistedCommandEnvelope {
        idempotency_key: decoder.read_string()?,
        expected_revision: decoder.read_u64()?,
        trace_id: decoder.read_string()?,
        command: decode_persisted_command(decoder)?,
    };
    Ok(value)
}

fn encode_persisted_command_record(encoder: &mut TransactionBatchEncoder, value: &PersistedCommandRecord) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u64(value.sequence)?;
    encode_persisted_command_envelope(encoder, &value.envelope)?;
    encoder.put_u64(value.recorded_at_utc_ms)?;
    Ok(())
}
fn decode_persisted_command_record(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedCommandRecord, TransactionBatchCodecError> {
    let value = PersistedCommandRecord {
        sequence: decoder.read_u64()?,
        envelope: decode_persisted_command_envelope(decoder)?,
        recorded_at_utc_ms: decoder.read_u64()?,
    };
    Ok(value)
}

fn encode_persisted_task_event(encoder: &mut TransactionBatchEncoder, value: &PersistedTaskEvent) -> Result<(), TransactionBatchCodecError> {
    encode_persisted_event_kind(encoder, &value.kind)?;
    encode_persisted_command_kind(encoder, &value.caused_by)?;
    match &value.from {
        Some(value) => {
            encoder.put_u8(1)?;
            encode_persisted_task_state(encoder, value)?;
        }
        None => encoder.put_u8(0)?,
    }
    match &value.to {
        Some(value) => {
            encoder.put_u8(1)?;
            encode_persisted_task_state(encoder, value)?;
        }
        None => encoder.put_u8(0)?,
    }
    match &value.reason {
        Some(value) => {
            encoder.put_u8(1)?;
            encode_persisted_state_reason(encoder, value)?;
        }
        None => encoder.put_u8(0)?,
    }
    match &value.subject {
        Some(value) => {
            encoder.put_u8(1)?;
            encode_persisted_event_subject(encoder, value)?;
        }
        None => encoder.put_u8(0)?,
    }
    Ok(())
}
fn decode_persisted_task_event(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedTaskEvent, TransactionBatchCodecError> {
    let value = PersistedTaskEvent {
        kind: decode_persisted_event_kind(decoder)?,
        caused_by: decode_persisted_command_kind(decoder)?,
        from: decoder.read_optional(|decoder| Ok(decode_persisted_task_state(decoder)?))?,
        to: decoder.read_optional(|decoder| Ok(decode_persisted_task_state(decoder)?))?,
        reason: decoder.read_optional(|decoder| Ok(decode_persisted_state_reason(decoder)?))?,
        subject: decoder.read_optional(|decoder| Ok(decode_persisted_event_subject(decoder)?))?,
    };
    Ok(value)
}

fn encode_persisted_event_record(encoder: &mut TransactionBatchEncoder, value: &PersistedEventRecord) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u64(value.sequence)?;
    encode_persisted_task_event(encoder, &value.event)?;
    encoder.put_u64(value.revision)?;
    encoder.put_string(&value.trace_id)?;
    encoder.put_string(&value.causation_key)?;
    encoder.put_u64(value.recorded_at_utc_ms)?;
    Ok(())
}
fn decode_persisted_event_record(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedEventRecord, TransactionBatchCodecError> {
    let value = PersistedEventRecord {
        sequence: decoder.read_u64()?,
        event: decode_persisted_task_event(decoder)?,
        revision: decoder.read_u64()?,
        trace_id: decoder.read_string()?,
        causation_key: decoder.read_string()?,
        recorded_at_utc_ms: decoder.read_u64()?,
    };
    Ok(value)
}

fn encode_persisted_audit_record(encoder: &mut TransactionBatchEncoder, value: &PersistedAuditRecord) -> Result<(), TransactionBatchCodecError> {
    encoder.put_string(&value.event_id)?;
    encode_persisted_audit_event_type(encoder, &value.event_type)?;
    encode_persisted_audit_actor(encoder, &value.actor)?;
    encoder.put_string(&value.task_id)?;
    match &value.subject_kind {
        Some(value) => {
            encoder.put_u8(1)?;
            encode_persisted_audit_subject_kind(encoder, value)?;
        }
        None => encoder.put_u8(0)?,
    }
    match &value.subject_id {
        Some(value) => {
            encoder.put_u8(1)?;
            encoder.put_string(value)?;
        }
        None => encoder.put_u8(0)?,
    }
    encoder.put_u64(value.revision)?;
    encoder.put_u64(value.sequence)?;
    encoder.put_string(&value.trace_id)?;
    encoder.put_u64(value.occurred_at_utc_ms)?;
    encoder.put_u64(value.dropped_field_count)?;
    encoder.put_bool(value.content_values_retained)?;
    Ok(())
}
fn decode_persisted_audit_record(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedAuditRecord, TransactionBatchCodecError> {
    let value = PersistedAuditRecord {
        event_id: decoder.read_string()?,
        event_type: decode_persisted_audit_event_type(decoder)?,
        actor: decode_persisted_audit_actor(decoder)?,
        task_id: decoder.read_string()?,
        subject_kind: decoder.read_optional(|decoder| Ok(decode_persisted_audit_subject_kind(decoder)?))?,
        subject_id: decoder.read_optional(|decoder| Ok(decoder.read_string()?))?,
        revision: decoder.read_u64()?,
        sequence: decoder.read_u64()?,
        trace_id: decoder.read_string()?,
        occurred_at_utc_ms: decoder.read_u64()?,
        dropped_field_count: decoder.read_u64()?,
        content_values_retained: decoder.read_bool()?,
    };
    Ok(value)
}

fn encode_task_transaction_batch(encoder: &mut TransactionBatchEncoder, value: &TaskTransactionBatch) -> Result<(), TransactionBatchCodecError> {
    encoder.put_u32(value.schema_version)?;
    match &value.seed {
        Some(value) => {
            encoder.put_u8(1)?;
            encode_persisted_task_seed(encoder, value)?;
        }
        None => encoder.put_u8(0)?,
    }
    encoder.put_len(value.journal_entries.len())?;
    for item in &value.journal_entries {
        encode_persisted_journal_entry(encoder, item)?;
    }
    encoder.put_len(value.effect_intents.len())?;
    for item in &value.effect_intents {
        encode_persisted_effect_intent(encoder, item)?;
    }
    encoder.put_len(value.audit_records.len())?;
    for item in &value.audit_records {
        encode_persisted_audit_record(encoder, item)?;
    }
    Ok(())
}
fn decode_task_transaction_batch(decoder: &mut TransactionBatchDecoder<'_>) -> Result<TaskTransactionBatch, TransactionBatchCodecError> {
    let value = TaskTransactionBatch {
        schema_version: decoder.read_u32()?,
        seed: decoder.read_optional(|decoder| Ok(decode_persisted_task_seed(decoder)?))?,
        journal_entries: decoder.read_list(|decoder| Ok(decode_persisted_journal_entry(decoder)?))?,
        effect_intents: decoder.read_list(|decoder| Ok(decode_persisted_effect_intent(decoder)?))?,
        audit_records: decoder.read_list(|decoder| Ok(decode_persisted_audit_record(decoder)?))?,
    };
    Ok(value)
}

fn encode_persisted_proposal_decision(encoder: &mut TransactionBatchEncoder, value: &PersistedProposalDecision) -> Result<(), TransactionBatchCodecError> {
    match value {
        PersistedProposalDecision::Authorize { authorization } => {
            encoder.put_u32(0)?;
            encode_persisted_authorization(encoder, authorization)?;
        }
        PersistedProposalDecision::RequireApproval => {
            encoder.put_u32(1)?;
        }
        PersistedProposalDecision::Deny { denial } => {
            encoder.put_u32(2)?;
            encode_persisted_denial(encoder, denial)?;
        }
    }
    Ok(())
}
fn decode_persisted_proposal_decision(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedProposalDecision, TransactionBatchCodecError> {
    match decoder.read_u32()? {
        0 => Ok(PersistedProposalDecision::Authorize {
            authorization: decode_persisted_authorization(decoder)?,
        }),
        1 => Ok(PersistedProposalDecision::RequireApproval),
        2 => Ok(PersistedProposalDecision::Deny {
            denial: decode_persisted_denial(decoder)?,
        }),
        _ => Err(TransactionBatchCodecError::InvalidEnum),
    }
}

fn encode_persisted_command(encoder: &mut TransactionBatchEncoder, value: &PersistedCommand) -> Result<(), TransactionBatchCodecError> {
    match value {
        PersistedCommand::CreateTask => {
            encoder.put_u32(0)?;
        }
        PersistedCommand::EditScope { scope } => {
            encoder.put_u32(1)?;
            encode_persisted_source_scope(encoder, scope)?;
        }
        PersistedCommand::StartTask { preview } => {
            encoder.put_u32(2)?;
            encode_persisted_scope_preview(encoder, preview)?;
        }
        PersistedCommand::AcceptInitialConsent { approval } => {
            encoder.put_u32(3)?;
            encoder.put_string(approval)?;
        }
        PersistedCommand::ApproveAction { approval, still_current, expires_at_monotonic_ms, expires_at_utc_ms, browser_session_id } => {
            encoder.put_u32(4)?;
            encoder.put_string(approval)?;
            encoder.put_bool(*still_current)?;
            encoder.put_u64(*expires_at_monotonic_ms)?;
            encoder.put_u64(*expires_at_utc_ms)?;
            encoder.put_string(browser_session_id)?;
        }
        PersistedCommand::DenyAction { approval } => {
            encoder.put_u32(5)?;
            encoder.put_string(approval)?;
        }
        PersistedCommand::PauseTask { cause } => {
            encoder.put_u32(6)?;
            encode_persisted_pause_cause(encoder, cause)?;
        }
        PersistedCommand::TakeOver => {
            encoder.put_u32(7)?;
        }
        PersistedCommand::PauseSettled => {
            encoder.put_u32(8)?;
        }
        PersistedCommand::ResumeTask => {
            encoder.put_u32(9)?;
        }
        PersistedCommand::CancelTask => {
            encoder.put_u32(10)?;
        }
        PersistedCommand::CancelSettled => {
            encoder.put_u32(11)?;
        }
        PersistedCommand::ExecutorStarted => {
            encoder.put_u32(12)?;
        }
        PersistedCommand::SetPlan { plan } => {
            encoder.put_u32(13)?;
            encode_persisted_plan_draft(encoder, plan)?;
        }
        PersistedCommand::AdvanceStep { plan_step_id, to } => {
            encoder.put_u32(14)?;
            encoder.put_string(plan_step_id)?;
            encode_persisted_step_state(encoder, to)?;
        }
        PersistedCommand::RequestApproval { action_id } => {
            encoder.put_u32(15)?;
            encoder.put_string(action_id)?;
        }
        PersistedCommand::RequestUserInput => {
            encoder.put_u32(16)?;
        }
        PersistedCommand::SupplyUserInput => {
            encoder.put_u32(17)?;
        }
        PersistedCommand::ProposeAction { proposal } => {
            encoder.put_u32(18)?;
            encode_persisted_action_proposal(encoder, proposal)?;
        }
        PersistedCommand::RecordPolicyDecision { action_id, decision, dispatch_id } => {
            encoder.put_u32(19)?;
            encoder.put_string(action_id)?;
            encode_persisted_proposal_decision(encoder, decision)?;
            match &dispatch_id {
                Some(value) => {
                    encoder.put_u8(1)?;
                    encoder.put_string(value)?;
                }
                None => encoder.put_u8(0)?,
            }
        }
        PersistedCommand::DispatchAction { action_id, dispatch_id } => {
            encoder.put_u32(20)?;
            encoder.put_string(action_id)?;
            encoder.put_string(dispatch_id)?;
        }
        PersistedCommand::RecordActionOutcome { action_id, outcome } => {
            encoder.put_u32(21)?;
            encoder.put_string(action_id)?;
            encode_persisted_action_outcome(encoder, outcome)?;
        }
        PersistedCommand::ResultCandidateReady => {
            encoder.put_u32(22)?;
        }
        PersistedCommand::CompleteResultValidated { result } => {
            encoder.put_u32(23)?;
            encode_persisted_task_result(encoder, result)?;
        }
        PersistedCommand::PartialResultValidated { result } => {
            encoder.put_u32(24)?;
            encode_persisted_task_result(encoder, result)?;
        }
        PersistedCommand::ResumeForCorrection => {
            encoder.put_u32(25)?;
        }
        PersistedCommand::FailTask { reason } => {
            encoder.put_u32(26)?;
            encode_persisted_failure_reason(encoder, reason)?;
        }
        PersistedCommand::CorrectFact { fact_id } => {
            encoder.put_u32(27)?;
            encoder.put_string(fact_id)?;
        }
        PersistedCommand::ExcludeSource { source_id } => {
            encoder.put_u32(28)?;
            encoder.put_string(source_id)?;
        }
        PersistedCommand::AcceptArtifact { artifact_id } => {
            encoder.put_u32(29)?;
            encoder.put_string(artifact_id)?;
        }
        PersistedCommand::ExportArtifact { artifact_id, format } => {
            encoder.put_u32(30)?;
            encoder.put_string(artifact_id)?;
            encode_persisted_artifact_kind(encoder, format)?;
        }
        PersistedCommand::RequestPermission { request } => {
            encoder.put_u32(31)?;
            encode_persisted_permission_request(encoder, request)?;
        }
        PersistedCommand::RecordPermissionResult { result } => {
            encoder.put_u32(32)?;
            encode_persisted_permission_result(encoder, result)?;
        }
        PersistedCommand::RequestModelTurn { call_id } => {
            encoder.put_u32(33)?;
            encoder.put_string(call_id)?;
        }
        PersistedCommand::RecordModelTurn { call_id, digest } => {
            encoder.put_u32(34)?;
            encoder.put_string(call_id)?;
            encode_persisted_turn_digest(encoder, digest)?;
        }
        PersistedCommand::RecordModelTurnGap { call_id, gap } => {
            encoder.put_u32(35)?;
            encoder.put_string(call_id)?;
            encode_persisted_turn_gap(encoder, gap)?;
        }
        PersistedCommand::RequestHandover { handover_id } => {
            encoder.put_u32(36)?;
            encoder.put_string(handover_id)?;
        }
        PersistedCommand::CompleteHandover { completion } => {
            encoder.put_u32(37)?;
            encode_persisted_handover_completion(encoder, completion)?;
        }
        PersistedCommand::ExpireHandover { handover_id } => {
            encoder.put_u32(38)?;
            encoder.put_string(handover_id)?;
        }
        PersistedCommand::RecordContextEviction { through_turn } => {
            encoder.put_u32(39)?;
            encoder.put_u64(*through_turn)?;
        }
        PersistedCommand::RecordToolJobOutcome { action_id, job_id, outcome } => {
            encoder.put_u32(40)?;
            encoder.put_string(action_id)?;
            encoder.put_string(job_id)?;
            encode_persisted_tool_job_outcome(encoder, outcome)?;
        }
        PersistedCommand::RequestFieldValues { request_id, tab_id, node_id } => {
            encoder.put_u32(41)?;
            encoder.put_string(request_id)?;
            encoder.put_string(tab_id)?;
            encoder.put_string(node_id)?;
        }
        PersistedCommand::SupplyFieldValues { request_id, supplied } => {
            encoder.put_u32(42)?;
            encoder.put_string(request_id)?;
            encoder.put_u32(*supplied)?;
        }
        PersistedCommand::RequestArtifact { artifact_id, format, workspace_revision } => {
            encoder.put_u32(43)?;
            encoder.put_string(artifact_id)?;
            encode_persisted_artifact_kind(encoder, format)?;
            encoder.put_u64(*workspace_revision)?;
        }
        PersistedCommand::RecordDiscoveryTab { discovery_tab_id, browser_session_id } => {
            encoder.put_u32(44)?;
            encoder.put_string(discovery_tab_id)?;
            encoder.put_string(browser_session_id)?;
        }
        PersistedCommand::RequestModelAttempt { call_id, attempt_ordinal, candidate_ordinal, kind } => {
            encoder.put_u32(45)?;
            encoder.put_string(call_id)?;
            encoder.put_u32(*attempt_ordinal)?;
            encoder.put_u32(*candidate_ordinal)?;
            encode_persisted_model_attempt_kind(encoder, kind)?;
        }
        PersistedCommand::FollowUp => {
            encoder.put_u32(46)?;
        }
    }
    Ok(())
}
fn decode_persisted_command(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedCommand, TransactionBatchCodecError> {
    match decoder.read_u32()? {
        0 => Ok(PersistedCommand::CreateTask),
        1 => Ok(PersistedCommand::EditScope {
            scope: decode_persisted_source_scope(decoder)?,
        }),
        2 => Ok(PersistedCommand::StartTask {
            preview: decode_persisted_scope_preview(decoder)?,
        }),
        3 => Ok(PersistedCommand::AcceptInitialConsent {
            approval: decoder.read_string()?,
        }),
        4 => Ok(PersistedCommand::ApproveAction {
            approval: decoder.read_string()?,
            still_current: decoder.read_bool()?,
            expires_at_monotonic_ms: decoder.read_u64()?,
            expires_at_utc_ms: decoder.read_u64()?,
            browser_session_id: decoder.read_string()?,
        }),
        5 => Ok(PersistedCommand::DenyAction {
            approval: decoder.read_string()?,
        }),
        6 => Ok(PersistedCommand::PauseTask {
            cause: decode_persisted_pause_cause(decoder)?,
        }),
        7 => Ok(PersistedCommand::TakeOver),
        8 => Ok(PersistedCommand::PauseSettled),
        9 => Ok(PersistedCommand::ResumeTask),
        10 => Ok(PersistedCommand::CancelTask),
        11 => Ok(PersistedCommand::CancelSettled),
        12 => Ok(PersistedCommand::ExecutorStarted),
        13 => Ok(PersistedCommand::SetPlan {
            plan: decode_persisted_plan_draft(decoder)?,
        }),
        14 => Ok(PersistedCommand::AdvanceStep {
            plan_step_id: decoder.read_string()?,
            to: decode_persisted_step_state(decoder)?,
        }),
        15 => Ok(PersistedCommand::RequestApproval {
            action_id: decoder.read_string()?,
        }),
        16 => Ok(PersistedCommand::RequestUserInput),
        17 => Ok(PersistedCommand::SupplyUserInput),
        18 => Ok(PersistedCommand::ProposeAction {
            proposal: decode_persisted_action_proposal(decoder)?,
        }),
        19 => Ok(PersistedCommand::RecordPolicyDecision {
            action_id: decoder.read_string()?,
            decision: decode_persisted_proposal_decision(decoder)?,
            dispatch_id: decoder.read_optional(|decoder| Ok(decoder.read_string()?))?,
        }),
        20 => Ok(PersistedCommand::DispatchAction {
            action_id: decoder.read_string()?,
            dispatch_id: decoder.read_string()?,
        }),
        21 => Ok(PersistedCommand::RecordActionOutcome {
            action_id: decoder.read_string()?,
            outcome: decode_persisted_action_outcome(decoder)?,
        }),
        22 => Ok(PersistedCommand::ResultCandidateReady),
        23 => Ok(PersistedCommand::CompleteResultValidated {
            result: decode_persisted_task_result(decoder)?,
        }),
        24 => Ok(PersistedCommand::PartialResultValidated {
            result: decode_persisted_task_result(decoder)?,
        }),
        25 => Ok(PersistedCommand::ResumeForCorrection),
        26 => Ok(PersistedCommand::FailTask {
            reason: decode_persisted_failure_reason(decoder)?,
        }),
        27 => Ok(PersistedCommand::CorrectFact {
            fact_id: decoder.read_string()?,
        }),
        28 => Ok(PersistedCommand::ExcludeSource {
            source_id: decoder.read_string()?,
        }),
        29 => Ok(PersistedCommand::AcceptArtifact {
            artifact_id: decoder.read_string()?,
        }),
        30 => Ok(PersistedCommand::ExportArtifact {
            artifact_id: decoder.read_string()?,
            format: decode_persisted_artifact_kind(decoder)?,
        }),
        31 => Ok(PersistedCommand::RequestPermission {
            request: decode_persisted_permission_request(decoder)?,
        }),
        32 => Ok(PersistedCommand::RecordPermissionResult {
            result: decode_persisted_permission_result(decoder)?,
        }),
        33 => Ok(PersistedCommand::RequestModelTurn {
            call_id: decoder.read_string()?,
        }),
        34 => Ok(PersistedCommand::RecordModelTurn {
            call_id: decoder.read_string()?,
            digest: decode_persisted_turn_digest(decoder)?,
        }),
        35 => Ok(PersistedCommand::RecordModelTurnGap {
            call_id: decoder.read_string()?,
            gap: decode_persisted_turn_gap(decoder)?,
        }),
        36 => Ok(PersistedCommand::RequestHandover {
            handover_id: decoder.read_string()?,
        }),
        37 => Ok(PersistedCommand::CompleteHandover {
            completion: decode_persisted_handover_completion(decoder)?,
        }),
        38 => Ok(PersistedCommand::ExpireHandover {
            handover_id: decoder.read_string()?,
        }),
        39 => Ok(PersistedCommand::RecordContextEviction {
            through_turn: decoder.read_u64()?,
        }),
        40 => Ok(PersistedCommand::RecordToolJobOutcome {
            action_id: decoder.read_string()?,
            job_id: decoder.read_string()?,
            outcome: decode_persisted_tool_job_outcome(decoder)?,
        }),
        41 => Ok(PersistedCommand::RequestFieldValues {
            request_id: decoder.read_string()?,
            tab_id: decoder.read_string()?,
            node_id: decoder.read_string()?,
        }),
        42 => Ok(PersistedCommand::SupplyFieldValues {
            request_id: decoder.read_string()?,
            supplied: decoder.read_u32()?,
        }),
        43 => Ok(PersistedCommand::RequestArtifact {
            artifact_id: decoder.read_string()?,
            format: decode_persisted_artifact_kind(decoder)?,
            workspace_revision: decoder.read_u64()?,
        }),
        44 => Ok(PersistedCommand::RecordDiscoveryTab {
            discovery_tab_id: decoder.read_string()?,
            browser_session_id: decoder.read_string()?,
        }),
        45 => Ok(PersistedCommand::RequestModelAttempt {
            call_id: decoder.read_string()?,
            attempt_ordinal: decoder.read_u32()?,
            candidate_ordinal: decoder.read_u32()?,
            kind: decode_persisted_model_attempt_kind(decoder)?,
        }),
        46 => Ok(PersistedCommand::FollowUp),
        _ => Err(TransactionBatchCodecError::InvalidEnum),
    }
}

fn encode_persisted_event_subject(encoder: &mut TransactionBatchEncoder, value: &PersistedEventSubject) -> Result<(), TransactionBatchCodecError> {
    match value {
        PersistedEventSubject::Action { id } => {
            encoder.put_u32(0)?;
            encoder.put_string(id)?;
        }
        PersistedEventSubject::Plan { id } => {
            encoder.put_u32(1)?;
            encoder.put_string(id)?;
        }
        PersistedEventSubject::PlanStep { id } => {
            encoder.put_u32(2)?;
            encoder.put_string(id)?;
        }
        PersistedEventSubject::Artifact { id } => {
            encoder.put_u32(3)?;
            encoder.put_string(id)?;
        }
        PersistedEventSubject::Source { id } => {
            encoder.put_u32(4)?;
            encoder.put_string(id)?;
        }
        PersistedEventSubject::Fact { id } => {
            encoder.put_u32(5)?;
            encoder.put_string(id)?;
        }
        PersistedEventSubject::Capability { id } => {
            encoder.put_u32(6)?;
            encoder.put_string(id)?;
        }
        PersistedEventSubject::PermissionRequest { id } => {
            encoder.put_u32(7)?;
            encoder.put_string(id)?;
        }
        PersistedEventSubject::Handover { id } => {
            encoder.put_u32(8)?;
            encoder.put_string(id)?;
        }
        PersistedEventSubject::ActorLease { id } => {
            encoder.put_u32(9)?;
            encoder.put_string(id)?;
        }
        PersistedEventSubject::DiscoveryTab { id } => {
            encoder.put_u32(10)?;
            encoder.put_string(id)?;
        }
    }
    Ok(())
}
fn decode_persisted_event_subject(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedEventSubject, TransactionBatchCodecError> {
    match decoder.read_u32()? {
        0 => Ok(PersistedEventSubject::Action {
            id: decoder.read_string()?,
        }),
        1 => Ok(PersistedEventSubject::Plan {
            id: decoder.read_string()?,
        }),
        2 => Ok(PersistedEventSubject::PlanStep {
            id: decoder.read_string()?,
        }),
        3 => Ok(PersistedEventSubject::Artifact {
            id: decoder.read_string()?,
        }),
        4 => Ok(PersistedEventSubject::Source {
            id: decoder.read_string()?,
        }),
        5 => Ok(PersistedEventSubject::Fact {
            id: decoder.read_string()?,
        }),
        6 => Ok(PersistedEventSubject::Capability {
            id: decoder.read_string()?,
        }),
        7 => Ok(PersistedEventSubject::PermissionRequest {
            id: decoder.read_string()?,
        }),
        8 => Ok(PersistedEventSubject::Handover {
            id: decoder.read_string()?,
        }),
        9 => Ok(PersistedEventSubject::ActorLease {
            id: decoder.read_string()?,
        }),
        10 => Ok(PersistedEventSubject::DiscoveryTab {
            id: decoder.read_string()?,
        }),
        _ => Err(TransactionBatchCodecError::InvalidEnum),
    }
}

fn encode_persisted_journal_entry(encoder: &mut TransactionBatchEncoder, value: &PersistedJournalEntry) -> Result<(), TransactionBatchCodecError> {
    match value {
        PersistedJournalEntry::Command { record } => {
            encoder.put_u32(0)?;
            encode_persisted_command_record(encoder, record)?;
        }
        PersistedJournalEntry::Event { record } => {
            encoder.put_u32(1)?;
            encode_persisted_event_record(encoder, record)?;
        }
    }
    Ok(())
}
fn decode_persisted_journal_entry(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedJournalEntry, TransactionBatchCodecError> {
    match decoder.read_u32()? {
        0 => Ok(PersistedJournalEntry::Command {
            record: decode_persisted_command_record(decoder)?,
        }),
        1 => Ok(PersistedJournalEntry::Event {
            record: decode_persisted_event_record(decoder)?,
        }),
        _ => Err(TransactionBatchCodecError::InvalidEnum),
    }
}

fn encode_persisted_effect_intent(encoder: &mut TransactionBatchEncoder, value: &PersistedEffectIntent) -> Result<(), TransactionBatchCodecError> {
    match value {
        PersistedEffectIntent::RevokeAuthority { reason } => {
            encoder.put_u32(0)?;
            encode_persisted_revocation_reason(encoder, reason)?;
        }
        PersistedEffectIntent::AskPolicy { action_id } => {
            encoder.put_u32(1)?;
            encoder.put_string(action_id)?;
        }
        PersistedEffectIntent::RequestApproval { action_id } => {
            encoder.put_u32(2)?;
            encoder.put_string(action_id)?;
        }
        PersistedEffectIntent::DispatchAction { action_id } => {
            encoder.put_u32(3)?;
            encoder.put_string(action_id)?;
        }
        PersistedEffectIntent::AwaitInFlightWork { settle_with } => {
            encoder.put_u32(4)?;
            encode_persisted_command_kind(encoder, settle_with)?;
        }
        PersistedEffectIntent::ReconcileAction { action_id, rule } => {
            encoder.put_u32(5)?;
            encoder.put_string(action_id)?;
            encode_persisted_recovery_rule(encoder, rule)?;
        }
        PersistedEffectIntent::ReleaseTaskTabs => {
            encoder.put_u32(6)?;
        }
        PersistedEffectIntent::GenerateArtifact { artifact_id, kind, workspace_revision } => {
            encoder.put_u32(7)?;
            encoder.put_string(artifact_id)?;
            encode_persisted_artifact_kind(encoder, kind)?;
            encoder.put_u64(*workspace_revision)?;
        }
        PersistedEffectIntent::ExportArtifact { artifact_id, kind, workspace_revision } => {
            encoder.put_u32(8)?;
            encoder.put_string(artifact_id)?;
            encode_persisted_artifact_kind(encoder, kind)?;
            encoder.put_u64(*workspace_revision)?;
        }
        PersistedEffectIntent::RequestPermission { request_id, permission, deadline_monotonic_ms, deadline_utc_ms, browser_session_id } => {
            encoder.put_u32(9)?;
            encoder.put_string(request_id)?;
            encode_persisted_platform_permission(encoder, permission)?;
            encoder.put_u64(*deadline_monotonic_ms)?;
            encoder.put_u64(*deadline_utc_ms)?;
            encoder.put_string(browser_session_id)?;
        }
        PersistedEffectIntent::CallModel { call_id } => {
            encoder.put_u32(10)?;
            encoder.put_string(call_id)?;
        }
        PersistedEffectIntent::AwaitHandover { handover_id, window_ms } => {
            encoder.put_u32(11)?;
            encoder.put_string(handover_id)?;
            encoder.put_u32(*window_ms)?;
        }
        PersistedEffectIntent::RunToolJob { action_id, job_id, runtime } => {
            encoder.put_u32(12)?;
            encoder.put_string(action_id)?;
            encoder.put_string(job_id)?;
            encode_persisted_tool_runtime(encoder, runtime)?;
        }
        PersistedEffectIntent::RequestFieldValues { request_id, tab_id, node_id } => {
            encoder.put_u32(13)?;
            encoder.put_string(request_id)?;
            encoder.put_string(tab_id)?;
            encoder.put_string(node_id)?;
        }
        PersistedEffectIntent::PrepareDiscoveryTab { browser_session_id, remaining_new_source_cap } => {
            encoder.put_u32(14)?;
            encoder.put_string(browser_session_id)?;
            encoder.put_u32(*remaining_new_source_cap)?;
        }
        PersistedEffectIntent::RunLibraryTool { action_id } => {
            encoder.put_u32(15)?;
            encoder.put_string(action_id)?;
        }
        PersistedEffectIntent::RunMemoryTool { action_id } => {
            encoder.put_u32(16)?;
            encoder.put_string(action_id)?;
        }
    }
    Ok(())
}
fn decode_persisted_effect_intent(decoder: &mut TransactionBatchDecoder<'_>) -> Result<PersistedEffectIntent, TransactionBatchCodecError> {
    match decoder.read_u32()? {
        0 => Ok(PersistedEffectIntent::RevokeAuthority {
            reason: decode_persisted_revocation_reason(decoder)?,
        }),
        1 => Ok(PersistedEffectIntent::AskPolicy {
            action_id: decoder.read_string()?,
        }),
        2 => Ok(PersistedEffectIntent::RequestApproval {
            action_id: decoder.read_string()?,
        }),
        3 => Ok(PersistedEffectIntent::DispatchAction {
            action_id: decoder.read_string()?,
        }),
        4 => Ok(PersistedEffectIntent::AwaitInFlightWork {
            settle_with: decode_persisted_command_kind(decoder)?,
        }),
        5 => Ok(PersistedEffectIntent::ReconcileAction {
            action_id: decoder.read_string()?,
            rule: decode_persisted_recovery_rule(decoder)?,
        }),
        6 => Ok(PersistedEffectIntent::ReleaseTaskTabs),
        7 => Ok(PersistedEffectIntent::GenerateArtifact {
            artifact_id: decoder.read_string()?,
            kind: decode_persisted_artifact_kind(decoder)?,
            workspace_revision: decoder.read_u64()?,
        }),
        8 => Ok(PersistedEffectIntent::ExportArtifact {
            artifact_id: decoder.read_string()?,
            kind: decode_persisted_artifact_kind(decoder)?,
            workspace_revision: decoder.read_u64()?,
        }),
        9 => Ok(PersistedEffectIntent::RequestPermission {
            request_id: decoder.read_string()?,
            permission: decode_persisted_platform_permission(decoder)?,
            deadline_monotonic_ms: decoder.read_u64()?,
            deadline_utc_ms: decoder.read_u64()?,
            browser_session_id: decoder.read_string()?,
        }),
        10 => Ok(PersistedEffectIntent::CallModel {
            call_id: decoder.read_string()?,
        }),
        11 => Ok(PersistedEffectIntent::AwaitHandover {
            handover_id: decoder.read_string()?,
            window_ms: decoder.read_u32()?,
        }),
        12 => Ok(PersistedEffectIntent::RunToolJob {
            action_id: decoder.read_string()?,
            job_id: decoder.read_string()?,
            runtime: decode_persisted_tool_runtime(decoder)?,
        }),
        13 => Ok(PersistedEffectIntent::RequestFieldValues {
            request_id: decoder.read_string()?,
            tab_id: decoder.read_string()?,
            node_id: decoder.read_string()?,
        }),
        14 => Ok(PersistedEffectIntent::PrepareDiscoveryTab {
            browser_session_id: decoder.read_string()?,
            remaining_new_source_cap: decoder.read_u32()?,
        }),
        15 => Ok(PersistedEffectIntent::RunLibraryTool {
            action_id: decoder.read_string()?,
        }),
        16 => Ok(PersistedEffectIntent::RunMemoryTool {
            action_id: decoder.read_string()?,
        }),
        _ => Err(TransactionBatchCodecError::InvalidEnum),
    }
}

pub fn encode_transaction_batch(value: &TaskTransactionBatch) -> Result<Vec<u8>, TransactionBatchCodecError> {
    if value.schema_version != TRANSACTION_BATCH_SCHEMA_VERSION {
        return Err(TransactionBatchCodecError::UnsupportedVersion);
    }
    let mut encoder = TransactionBatchEncoder { bytes: Vec::new() };
    encoder.put_raw(TRANSACTION_BATCH_MAGIC)?;
    encode_task_transaction_batch(&mut encoder, value)?;
    Ok(encoder.bytes)
}

pub fn decode_transaction_batch(bytes: &[u8]) -> Result<TaskTransactionBatch, TransactionBatchCodecError> {
    if bytes.len() > MAX_EFFECT_BYTES {
        return Err(TransactionBatchCodecError::SizeLimit);
    }
    let mut decoder = TransactionBatchDecoder { bytes, offset: 0 };
    if decoder.take(TRANSACTION_BATCH_MAGIC.len())? != TRANSACTION_BATCH_MAGIC {
        return Err(TransactionBatchCodecError::InvalidMagic);
    }
    let root_offset = decoder.offset;
    if decoder.read_u32()? != TRANSACTION_BATCH_SCHEMA_VERSION {
        return Err(TransactionBatchCodecError::UnsupportedVersion);
    }
    decoder.offset = root_offset;
    let value = decode_task_transaction_batch(&mut decoder)?;
    if decoder.offset != bytes.len() {
        return Err(TransactionBatchCodecError::TrailingBytes);
    }
    if value.schema_version != TRANSACTION_BATCH_SCHEMA_VERSION {
        return Err(TransactionBatchCodecError::UnsupportedVersion);
    }
    Ok(value)
}
