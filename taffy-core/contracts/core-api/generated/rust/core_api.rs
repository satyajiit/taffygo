// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Generated from schema/contract.json. Do not edit.
// Contract core_api 3.49.

#![allow(
    clippy::module_name_repetitions,
    clippy::needless_question_mark,
    clippy::struct_excessive_bools,
    clippy::too_many_lines,
    clippy::trivially_copy_pass_by_ref
)]

pub const MAX_SAVED_FLOW_QUERY_RESULTS: usize = 4;
pub const MAX_COMMAND_PAYLOAD_BYTES: usize = 65_536;
pub const MAX_EVENT_PAYLOAD_BYTES: usize = 262_144;
pub const MAX_ACTIVE_TASKS: usize = 64;
pub const MAX_PROGRESS_BASIS_POINTS: usize = 10_000;
pub const MAX_MESSAGE_KEY_BYTES: usize = 256;
pub const MAX_AUTH_EMAIL_BYTES: usize = 320;
pub const MAX_AUTH_DISPLAY_NAME_BYTES: usize = 512;
pub const MAX_AUTH_CREDENTIAL_HANDLE_BYTES: usize = 256;
pub const MAX_AUTH_METHODS: usize = 4;
pub const MAX_TASK_GOAL_BYTES: usize = 8_192;
pub const MAX_TASK_CONTROLS: usize = 4;
pub const MAX_TASK_ARTIFACTS: usize = 16;
pub const MAX_TASK_ACTIVITY: usize = 32;
pub const MAX_TASK_ARTIFACT_EXPORT_BYTES: usize = 16_777_216;
pub const MAX_IDENTIFIER_BYTES: usize = 256;
pub const MAX_WORKSPACES: usize = 32;
pub const MAX_WORKSPACE_SOURCES: usize = 64;
pub const MAX_WORKSPACE_FACTS: usize = 256;
pub const MAX_WORKSPACE_DISPLAY_NAME_BYTES: usize = 256;
pub const MAX_WORKSPACE_CONFIRMATION_TOKEN_BYTES: usize = 64;
pub const MAX_FACT_SOURCES: usize = 16;
pub const MAX_WORKSPACE_TITLE_BYTES: usize = 1_024;
pub const MAX_SOURCE_HOST_BYTES: usize = 253;
pub const MAX_TASK_CONSENT_SOURCES: usize = 64;
pub const MAX_NEW_SOURCE_CAP: usize = 64;
pub const MAX_FACT_FIELD_BYTES: usize = 256;
pub const MAX_FACT_VALUE_BYTES: usize = 16_384;
pub const MAX_EXPORT_CONTENT_BYTES: usize = 131_072;
pub const MAX_LIBRARY_ENTRIES: usize = 1_024;
pub const MAX_LIBRARY_SOURCES: usize = 16;
pub const MAX_LIBRARY_QUERY_BYTES: usize = 512;
pub const MAX_LIBRARY_SEARCH_RESULTS: usize = 32;
pub const MAX_LIBRARY_REFRESH_SOURCES: usize = 64;
pub const MAX_LIBRARY_REFRESH_RESULTS: usize = 64;
pub const MAX_LIBRARY_REFRESH_PREVIEW_ID_BYTES: usize = 64;
pub const MAX_MEMORY_RECORDS: usize = 512;
pub const MAX_MEMORY_STATEMENT_BYTES: usize = 2_048;
pub const MAX_MEMORY_QUERY_BYTES: usize = 512;
pub const MAX_MEMORY_SEARCH_RESULTS: usize = 32;
pub const MAX_PAGE_INSPECTOR_PAYLOAD_BYTES: usize = 131_072;
pub const MAX_PAGE_INSPECTOR_DOCUMENTS: usize = 1;
pub const MAX_PAGE_INSPECTOR_CLAIMS: usize = 4;
pub const MAX_PAGE_INSPECTOR_NODES: usize = 128;
pub const MAX_PAGE_INSPECTOR_EDGES: usize = 256;
pub const MAX_PAGE_INSPECTOR_ADAPTERS: usize = 8;
pub const MAX_PAGE_INSPECTOR_FRAMES: usize = 1;
pub const MAX_PAGE_INSPECTOR_WARNINGS: usize = 32;
pub const MAX_PAGE_INSPECTOR_BUDGETS: usize = 7;
pub const MAX_PAGE_INSPECTOR_IDENTIFIER_BYTES: usize = 64;
pub const MAX_PAGE_INSPECTOR_HOST_BYTES: usize = 253;
pub const MAX_PAGE_INSPECTOR_NAME_BYTES: usize = 512;
pub const MAX_PAGE_SNAPSHOT_EXPORT_BYTES: usize = 262_144;
pub const MAX_ASSETS: usize = 64;
pub const MAX_PART_MEMBER_PATH_BYTES: usize = 256;
pub const MAX_PART_MEMBER_BYTES: usize = 4_194_304;
pub const MAX_PROVIDER_ID_BYTES: usize = 64;
pub const MAX_PROVIDER_DISPLAY_NAME_BYTES: usize = 128;
pub const MAX_PROVIDER_ENDPOINT_BYTES: usize = 512;
pub const MAX_CUSTOM_PROVIDERS: usize = 32;
pub const MAX_PROVIDER_ROSTER_ENTRIES: usize = 96;
pub const MAX_USER_INPUT_ANSWER_BYTES: usize = 512;
pub const MAX_PROVIDER_MODEL_ENTRIES: usize = 256;
pub const MAX_MODEL_ID_BYTES: usize = 128;
pub const MAX_MODEL_DISPLAY_NAME_BYTES: usize = 128;
pub const MAX_MODEL_ROLES: usize = 4;
pub const MAX_MODEL_INPUT_MODALITIES: usize = 2;
pub const MAX_MODEL_THINKING_LEVELS: usize = 7;
pub const MAX_PROVIDER_PRESENTATION_BYTES: usize = 512;
pub const MAX_CUSTOM_MODEL_ENTRIES: usize = 32;
pub const MAX_COMPOSER_PREFIX_BYTES: usize = 4_096;
pub const MAX_COMPOSER_SUFFIX_BYTES: usize = 1_024;
pub const MAX_COMPOSER_COMPLETION_BYTES: usize = 512;
pub const MAX_TASK_ANSWER_DELTA_BYTES: usize = 16_384;
pub const MAX_TASK_ANSWER_RESIDENCY_BYTES: usize = 131_072;
pub const MAX_ASSISTANT_ABILITIES: usize = 16;
pub const MAX_BUILTIN_SKILLS: usize = 16;
pub const MAX_CORE_STATUS_PROJECTION_OMISSIONS: usize = 13;
pub const MAX_SITE_SKILLS: usize = 64;
pub const MAX_SKILL_ID_BYTES: usize = 128;
pub const MAX_SKILL_ORIGIN_BYTES: usize = 2_048;
pub const MAX_SKILL_MATCH_CLAUSES: usize = 8;
pub const MAX_SKILL_STEPS: usize = 32;
pub const MAX_SKILL_ARGUMENTS_PER_STEP: usize = 16;
pub const MAX_SKILL_TOOL_NAME_BYTES: usize = 128;
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

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum CoreCommandKind {
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

impl CoreCommandKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::StartTask),
            1 => Some(Self::CancelTask),
            2 => Some(Self::ApproveAction),
            3 => Some(Self::RetryCore),
            4 => Some(Self::PermissionResult),
            5 => Some(Self::StartAuth),
            6 => Some(Self::RequestEmailLink),
            7 => Some(Self::SignOut),
            8 => Some(Self::AuthCredentialResult),
            9 => Some(Self::CorrectWorkspaceFact),
            10 => Some(Self::ExcludeWorkspaceSource),
            11 => Some(Self::RequestWorkspaceExport),
            12 => Some(Self::RequestAsset),
            13 => Some(Self::RemoveAsset),
            14 => Some(Self::SetAssetPolicy),
            15 => Some(Self::SaveProviderCredential),
            16 => Some(Self::ForgetProviderCredential),
            17 => Some(Self::StartProviderAuth),
            18 => Some(Self::SaveCustomProvider),
            19 => Some(Self::RemoveCustomProvider),
            20 => Some(Self::CompleteHandover),
            21 => Some(Self::SupplyUserInput),
            22 => Some(Self::SetProviderCredentialState),
            23 => Some(Self::ProbeProviderKey),
            24 => Some(Self::SetProviderModelPreference),
            25 => Some(Self::ProbeCustomEndpoint),
            26 => Some(Self::RequestComposerCompletion),
            27 => Some(Self::CancelComposerCompletion),
            28 => Some(Self::PauseTask),
            29 => Some(Self::ResumeTask),
            30 => Some(Self::TakeOver),
            31 => Some(Self::SetAssistantConfiguration),
            32 => Some(Self::SaveWorkspace),
            33 => Some(Self::RenameWorkspace),
            34 => Some(Self::DeleteWorkspace),
            35 => Some(Self::DiscardWorkspace),
            36 => Some(Self::SearchLibrary),
            37 => Some(Self::SaveLibraryFact),
            38 => Some(Self::RemoveLibraryEntry),
            39 => Some(Self::RequestLibraryExport),
            40 => Some(Self::SearchMemory),
            41 => Some(Self::UpsertMemory),
            42 => Some(Self::DeleteMemory),
            43 => Some(Self::AcceptTaskArtifact),
            44 => Some(Self::RequestTaskArtifactExport),
            45 => Some(Self::MutateSiteSkill),
            46 => Some(Self::CancelProviderAuth),
            47 => Some(Self::StartLibraryRefresh),
            48 => Some(Self::FollowUp),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum AssistantAbilityView {
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

impl AssistantAbilityView {
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
pub enum BuiltinSkillIdView {
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

impl BuiltinSkillIdView {
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
pub enum BuiltinSkillAvailabilityView {
    Available = 0,
    RequiredToolUnavailable = 1,
    RequiredPartMissing = 2,
    ProfileUnavailable = 3,
    PolicyUnavailable = 4,
}

impl BuiltinSkillAvailabilityView {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Available),
            1 => Some(Self::RequiredToolUnavailable),
            2 => Some(Self::RequiredPartMissing),
            3 => Some(Self::ProfileUnavailable),
            4 => Some(Self::PolicyUnavailable),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum CoreStatusProjectionMode {
    Complete = 0,
    RecoveryRequired = 1,
}

impl CoreStatusProjectionMode {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Complete),
            1 => Some(Self::RecoveryRequired),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum CoreStatusProjectionFamily {
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

impl CoreStatusProjectionFamily {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::ActiveTasks),
            1 => Some(Self::Workspaces),
            2 => Some(Self::WorkspaceExport),
            3 => Some(Self::AssetDelivery),
            4 => Some(Self::ProviderRoster),
            5 => Some(Self::ProviderProbes),
            6 => Some(Self::ProviderModels),
            7 => Some(Self::Library),
            8 => Some(Self::LibraryExport),
            9 => Some(Self::Memory),
            10 => Some(Self::SavedSignIns),
            11 => Some(Self::SavedDetails),
            12 => Some(Self::SiteSkills),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum SiteSkillMutationKind {
    Teach = 0,
    Update = 1,
    SetEnabled = 2,
    Remove = 3,
}

impl SiteSkillMutationKind {
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
pub enum SiteSkillClauseKind {
    RolePresent = 0,
    PhraseAt = 1,
    StateAt = 2,
}

impl SiteSkillClauseKind {
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
pub enum SiteSkillArgumentKind {
    FromEarlierStep = 0,
    FromPerson = 1,
    Choice = 2,
    Count = 3,
    Flag = 4,
    PublicAddress = 5,
    SemanticTarget = 6,
}

impl SiteSkillArgumentKind {
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
pub enum SiteSkillProvenanceView {
    Authored = 0,
    RecordedFromTask = 1,
}

impl SiteSkillProvenanceView {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Authored),
            1 => Some(Self::RecordedFromTask),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum SiteSkillStatusView {
    Draft = 0,
    Active = 1,
    Superseded = 2,
    Retired = 3,
    Disabled = 4,
}

impl SiteSkillStatusView {
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
pub enum PersonalityPresetView {
    CarefulResearcher = 0,
    QuickShopper = 1,
    TripPlanner = 2,
}

impl PersonalityPresetView {
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
pub enum TaskAttachedStore {
    History = 0,
    Bookmarks = 1,
    OpenTabs = 2,
}

impl TaskAttachedStore {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::History),
            1 => Some(Self::Bookmarks),
            2 => Some(Self::OpenTabs),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum CoreAvailability {
    Starting = 0,
    Ready = 1,
    Unavailable = 2,
    CircuitOpen = 3,
}

impl CoreAvailability {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Starting),
            1 => Some(Self::Ready),
            2 => Some(Self::Unavailable),
            3 => Some(Self::CircuitOpen),
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
pub enum TaskPhase {
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

impl TaskPhase {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Idle),
            1 => Some(Self::Planning),
            2 => Some(Self::WaitingForUser),
            3 => Some(Self::Running),
            4 => Some(Self::Completed),
            5 => Some(Self::Failed),
            6 => Some(Self::Cancelled),
            7 => Some(Self::OutcomeUnknown),
            8 => Some(Self::Paused),
            9 => Some(Self::Partial),
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
pub enum CoreFailureCode {
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

impl CoreFailureCode {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Cancelled),
            1 => Some(Self::DeadlineExceeded),
            2 => Some(Self::CoreUnavailable),
            3 => Some(Self::PolicyDenied),
            4 => Some(Self::InvalidRequest),
            5 => Some(Self::Backpressure),
            6 => Some(Self::OutcomeUnknown),
            7 => Some(Self::Internal),
            8 => Some(Self::BudgetExceeded),
            9 => Some(Self::ProviderUnavailable),
            10 => Some(Self::SourcesUnavailable),
            11 => Some(Self::JournalUnusable),
            12 => Some(Self::UnverifiableAction),
            13 => Some(Self::ProviderRefused),
            14 => Some(Self::ProviderLimit),
            15 => Some(Self::Offline),
            16 => Some(Self::PolicyRefused),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PlatformPermission {
    Notifications = 0,
    Camera = 1,
    Microphone = 2,
    Location = 3,
    ReadUserFile = 4,
    WriteUserFile = 5,
}

impl PlatformPermission {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Notifications),
            1 => Some(Self::Camera),
            2 => Some(Self::Microphone),
            3 => Some(Self::Location),
            4 => Some(Self::ReadUserFile),
            5 => Some(Self::WriteUserFile),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PermissionDecision {
    Granted = 0,
    Denied = 1,
    Unavailable = 2,
}

impl PermissionDecision {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Granted),
            1 => Some(Self::Denied),
            2 => Some(Self::Unavailable),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum AuthProvider {
    Google = 0,
    EmailLink = 1,
    Github = 2,
    Facebook = 3,
}

impl AuthProvider {
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
pub enum AuthMethodAvailability {
    Available = 0,
    NotConfigured = 1,
    PlatformUnavailable = 2,
}

impl AuthMethodAvailability {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Available),
            1 => Some(Self::NotConfigured),
            2 => Some(Self::PlatformUnavailable),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum AuthPhase {
    Initializing = 0,
    SignedOut = 1,
    InFlight = 2,
    LinkSent = 3,
    SignedIn = 4,
    Failed = 5,
}

impl AuthPhase {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Initializing),
            1 => Some(Self::SignedOut),
            2 => Some(Self::InFlight),
            3 => Some(Self::LinkSent),
            4 => Some(Self::SignedIn),
            5 => Some(Self::Failed),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum AuthFailureCode {
    NotConfigured = 0,
    Cancelled = 1,
    NoCredential = 2,
    Network = 3,
    Rejected = 4,
    InvalidRedirect = 5,
    CoreUnavailable = 6,
    Unknown = 7,
}

impl AuthFailureCode {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::NotConfigured),
            1 => Some(Self::Cancelled),
            2 => Some(Self::NoCredential),
            3 => Some(Self::Network),
            4 => Some(Self::Rejected),
            5 => Some(Self::InvalidRedirect),
            6 => Some(Self::CoreUnavailable),
            7 => Some(Self::Unknown),
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
pub enum WorkspacePhase {
    Running = 0,
    WaitingForUser = 1,
    Paused = 2,
    Done = 3,
    PartlyDone = 4,
    Stopped = 5,
    Failed = 6,
}

impl WorkspacePhase {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Running),
            1 => Some(Self::WaitingForUser),
            2 => Some(Self::Paused),
            3 => Some(Self::Done),
            4 => Some(Self::PartlyDone),
            5 => Some(Self::Stopped),
            6 => Some(Self::Failed),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum WorkspaceFactKind {
    FromPage = 0,
    Summarized = 1,
    TaffyInference = 2,
    UserEntered = 3,
}

impl WorkspaceFactKind {
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
pub enum TaskActivityKind {
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

impl TaskActivityKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::OpenedPage),
            1 => Some(Self::ReadPage),
            2 => Some(Self::PageUnavailable),
            3 => Some(Self::MoveRefused),
            4 => Some(Self::AskedYou),
            5 => Some(Self::YouAnswered),
            6 => Some(Self::HandedBack),
            7 => Some(Self::YouTookOver),
            8 => Some(Self::BuiltOutput),
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
pub enum LibraryAvailability {
    Available = 0,
    PrivateProfile = 1,
    Unavailable = 2,
}

impl LibraryAvailability {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Available),
            1 => Some(Self::PrivateProfile),
            2 => Some(Self::Unavailable),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum LibraryRefreshDisposition {
    Unchanged = 0,
    Changed = 1,
    Missing = 2,
}

impl LibraryRefreshDisposition {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Unchanged),
            1 => Some(Self::Changed),
            2 => Some(Self::Missing),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum MemoryAvailability {
    Available = 0,
    PrivateProfile = 1,
    Unavailable = 2,
}

impl MemoryAvailability {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Available),
            1 => Some(Self::PrivateProfile),
            2 => Some(Self::Unavailable),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum MemorySourceKind {
    YouWrote = 0,
    TaffySuggested = 1,
}

impl MemorySourceKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::YouWrote),
            1 => Some(Self::TaffySuggested),
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
pub enum PageInspectorAvailability {
    Available = 0,
    NoSelectedPage = 1,
    DocumentUnavailable = 2,
    CoreUnavailable = 3,
    PolicyDenied = 4,
    Backpressure = 5,
    StaleDocument = 6,
    InvalidResponse = 7,
}

impl PageInspectorAvailability {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Available),
            1 => Some(Self::NoSelectedPage),
            2 => Some(Self::DocumentUnavailable),
            3 => Some(Self::CoreUnavailable),
            4 => Some(Self::PolicyDenied),
            5 => Some(Self::Backpressure),
            6 => Some(Self::StaleDocument),
            7 => Some(Self::InvalidResponse),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum SiteSkillOfferAvailability {
    Available = 0,
    CoreUnavailable = 1,
    StaleDocument = 2,
    PrivateProfile = 3,
    Incomplete = 4,
    InvalidResponse = 5,
}

impl SiteSkillOfferAvailability {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Available),
            1 => Some(Self::CoreUnavailable),
            2 => Some(Self::StaleDocument),
            3 => Some(Self::PrivateProfile),
            4 => Some(Self::Incomplete),
            5 => Some(Self::InvalidResponse),
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
pub enum PageSnapshotExportAvailability {
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

impl PageSnapshotExportAvailability {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Available),
            1 => Some(Self::NoSelectedPage),
            2 => Some(Self::DocumentUnavailable),
            3 => Some(Self::CoreUnavailable),
            4 => Some(Self::PolicyDenied),
            5 => Some(Self::Backpressure),
            6 => Some(Self::StaleDocument),
            7 => Some(Self::PrivateProfile),
            8 => Some(Self::Incomplete),
            9 => Some(Self::Oversize),
            10 => Some(Self::InvalidResponse),
            11 => Some(Self::Cancelled),
            12 => Some(Self::ReplayConflict),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PageInspectorDocumentKind {
    SemanticPage = 0,
}

impl PageInspectorDocumentKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::SemanticPage),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PageInspectorClaim {
    BrowserValidated = 0,
    PolicyAdmitted = 1,
    Redacted = 2,
    Bounded = 3,
}

impl PageInspectorClaim {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::BrowserValidated),
            1 => Some(Self::PolicyAdmitted),
            2 => Some(Self::Redacted),
            3 => Some(Self::Bounded),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PageInspectorDocumentState {
    Active = 0,
    Frozen = 1,
    Unavailable = 2,
}

impl PageInspectorDocumentState {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Active),
            1 => Some(Self::Frozen),
            2 => Some(Self::Unavailable),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PageInspectorNodeRole {
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

impl PageInspectorNodeRole {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Document),
            1 => Some(Self::Section),
            2 => Some(Self::Text),
            3 => Some(Self::List),
            4 => Some(Self::Table),
            5 => Some(Self::Link),
            6 => Some(Self::Control),
            7 => Some(Self::Media),
            8 => Some(Self::Commerce),
            9 => Some(Self::Reference),
            10 => Some(Self::Unknown),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PageInspectorSensitivity {
    Public = 0,
    Withheld = 1,
    Unknown = 2,
}

impl PageInspectorSensitivity {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Public),
            1 => Some(Self::Withheld),
            2 => Some(Self::Unknown),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PageInspectorRelationship {
    Hierarchy = 0,
    Label = 1,
    Description = 2,
    Control = 3,
    TableHeader = 4,
    Entity = 5,
    Source = 6,
    Other = 7,
}

impl PageInspectorRelationship {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Hierarchy),
            1 => Some(Self::Label),
            2 => Some(Self::Description),
            3 => Some(Self::Control),
            4 => Some(Self::TableHeader),
            5 => Some(Self::Entity),
            6 => Some(Self::Source),
            7 => Some(Self::Other),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PageInspectorAdapterKind {
    Structure = 0,
    Accessibility = 1,
    Forms = 2,
    Metadata = 3,
    Browser = 4,
}

impl PageInspectorAdapterKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Structure),
            1 => Some(Self::Accessibility),
            2 => Some(Self::Forms),
            3 => Some(Self::Metadata),
            4 => Some(Self::Browser),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PageInspectorAdapterStatus {
    Complete = 0,
    Partial = 1,
    Conflict = 2,
    Unsupported = 3,
    Failed = 4,
}

impl PageInspectorAdapterStatus {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Complete),
            1 => Some(Self::Partial),
            2 => Some(Self::Conflict),
            3 => Some(Self::Unsupported),
            4 => Some(Self::Failed),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PageInspectorBudgetKind {
    Items = 0,
    Text = 1,
    TotalBytes = 2,
    Depth = 3,
    Frames = 4,
    Message = 5,
    Deadline = 6,
}

impl PageInspectorBudgetKind {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Items),
            1 => Some(Self::Text),
            2 => Some(Self::TotalBytes),
            3 => Some(Self::Depth),
            4 => Some(Self::Frames),
            5 => Some(Self::Message),
            6 => Some(Self::Deadline),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum PageInspectorWarningCode {
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

impl PageInspectorWarningCode {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::SourceUnavailable),
            1 => Some(Self::SourceFailed),
            2 => Some(Self::Conflict),
            3 => Some(Self::FrameOmitted),
            4 => Some(Self::ContentPartial),
            5 => Some(Self::SemanticsMissing),
            6 => Some(Self::ContentWithheld),
            7 => Some(Self::LocationMinimized),
            8 => Some(Self::Deadline),
            9 => Some(Self::ResourcePressure),
            10 => Some(Self::SuspiciousContent),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum CoreApiSubmissionStatus {
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

impl CoreApiSubmissionStatus {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Accepted),
            1 => Some(Self::InvalidRequest),
            2 => Some(Self::StaleGeneration),
            3 => Some(Self::StaleRevision),
            4 => Some(Self::DeadlineExceeded),
            5 => Some(Self::Backpressure),
            6 => Some(Self::CoreUnavailable),
            7 => Some(Self::Duplicate),
            8 => Some(Self::SourceNotOpen),
            9 => Some(Self::SourceAmbiguous),
            10 => Some(Self::WindowUnavailable),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum AssetKindView {
    PythonStdlib = 0,
    PythonPackages = 1,
    ModelWeights = 2,
    ModelTokenizer = 3,
    FilterList = 4,
    CountryFlags = 5,
    StartScenes = 6,
}

impl AssetKindView {
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
pub enum AssetPresenceView {
    Absent = 0,
    Partial = 1,
    Complete = 2,
    Installed = 3,
}

impl AssetPresenceView {
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
pub enum AssetNetworkCostView {
    Offline = 0,
    Metered = 1,
    Unmetered = 2,
}

impl AssetNetworkCostView {
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
pub enum AssetRefusalView {
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

impl AssetRefusalView {
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
pub enum PartMemberStatus {
    Ok = 0,
    NotInstalled = 1,
    NotFound = 2,
    TooLarge = 3,
    Unreadable = 4,
    InvalidRequest = 5,
}

impl PartMemberStatus {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Ok),
            1 => Some(Self::NotInstalled),
            2 => Some(Self::NotFound),
            3 => Some(Self::TooLarge),
            4 => Some(Self::Unreadable),
            5 => Some(Self::InvalidRequest),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum ProviderAuthMethodView {
    ApiKey = 0,
    Oauth = 1,
}

impl ProviderAuthMethodView {
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
pub enum ProviderWireApiView {
    AnthropicMessages = 0,
    OpenAiResponses = 1,
    OpenAiCompletions = 2,
    GoogleGenerativeLanguage = 3,
    Managed = 4,
    OpenAiCodexResponses = 5,
    GoogleCloudCodeAssist = 6,
}

impl ProviderWireApiView {
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
pub enum ProviderProbeVerdictView {
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

impl ProviderProbeVerdictView {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Usable),
            1 => Some(Self::Auth),
            2 => Some(Self::Billing),
            3 => Some(Self::RateLimit),
            4 => Some(Self::Overloaded),
            5 => Some(Self::Timeout),
            6 => Some(Self::Network),
            7 => Some(Self::ModelNotFound),
            8 => Some(Self::Unknown),
            9 => Some(Self::EndpointReached),
            10 => Some(Self::NoModelListed),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum ProviderCredentialStateView {
    Usable = 0,
    NeedsSignIn = 1,
    RefreshFailed = 2,
}

impl ProviderCredentialStateView {
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
pub enum ProviderRefusalView {
    RateLimit = 0,
    Billing = 1,
    Overloaded = 2,
}

impl ProviderRefusalView {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::RateLimit),
            1 => Some(Self::Billing),
            2 => Some(Self::Overloaded),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum ProviderOriginView {
    Catalog = 0,
    Custom = 1,
}

impl ProviderOriginView {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Catalog),
            1 => Some(Self::Custom),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum CatalogLayerView {
    EmbeddedBaseline = 0,
    RemoteOverlay = 1,
    UserOverride = 2,
}

impl CatalogLayerView {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::EmbeddedBaseline),
            1 => Some(Self::RemoteOverlay),
            2 => Some(Self::UserOverride),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum ThinkingLevelView {
    Off = 0,
    Minimal = 1,
    Low = 2,
    Medium = 3,
    High = 4,
    Xhigh = 5,
    Max = 6,
}

impl ThinkingLevelView {
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
pub enum ModelRoleView {
    PrimaryReasoning = 0,
    FastBrowsing = 1,
    Vision = 2,
    Embedding = 3,
}

impl ModelRoleView {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::PrimaryReasoning),
            1 => Some(Self::FastBrowsing),
            2 => Some(Self::Vision),
            3 => Some(Self::Embedding),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum InputModalityView {
    Text = 0,
    Image = 1,
}

impl InputModalityView {
    pub const fn from_wire(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Text),
            1 => Some(Self::Image),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum ServerKindView {
    OpenaiCompatible = 0,
    Ollama = 1,
    LmStudio = 2,
    Vllm = 3,
    LlamaCpp = 4,
}

impl ServerKindView {
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
pub enum SavedFlowQueryAvailability {
    Available = 0,
    PrivateProfile = 1,
    Unavailable = 2,
    InvalidRequest = 3,
    StaleRequest = 4,
}

impl SavedFlowQueryAvailability {
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

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct OperationEnvelope {
    pub operation_id: String,
    pub service_generation: u64,
    pub task_revision: u64,
    pub deadline_monotonic_ms: u64,
    pub idempotency_key: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SiteSkillObservedClause {
    pub kind: SiteSkillClauseKind,
    pub role: u32,
    pub detail: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SiteSkillSemanticTarget {
    pub role: u32,
    pub phrase: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SiteSkillObservedArgument {
    pub parameter: u32,
    pub kind: SiteSkillArgumentKind,
    pub value: u64,
    pub purpose: u32,
    pub public_address: Option<String>,
    pub semantic_target: Option<SiteSkillSemanticTarget>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SiteSkillObservedStep {
    pub verb: String,
    pub arguments: Vec<SiteSkillObservedArgument>,
    pub postcondition: u32,
    pub has_fill: bool,
    pub fill_purpose: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SiteSkillMutationBody {
    pub kind: SiteSkillMutationKind,
    pub skill_id: String,
    pub expected_version: u32,
    pub origin: String,
    pub clauses: Vec<SiteSkillObservedClause>,
    pub steps: Vec<SiteSkillObservedStep>,
    pub admitted: u32,
    pub enabled: bool,
    pub recorded_at_epoch_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SiteSkillView {
    pub skill_id: String,
    pub origin: String,
    pub provenance: SiteSkillProvenanceView,
    pub status: SiteSkillStatusView,
    pub active_version: u32,
    pub step_count: u32,
    pub installed_at_epoch_ms: u64,
    pub updated_at_epoch_ms: u64,
    pub recorded_from_task_id: Option<String>,
    pub reviewed_steps: Vec<SiteSkillObservedStep>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SiteSkillOfferView {
    pub offer_id: String,
    pub skill_id: String,
    pub active_version: u32,
    pub step_count: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CoreCommand {
    pub operation: OperationEnvelope,
    pub kind: CoreCommandKind,
    pub start_task: Option<StartTaskBody>,
    pub cancel_task: Option<CancelTaskBody>,
    pub approve_action: Option<ApproveActionBody>,
    pub retry_core: Option<RetryCoreBody>,
    pub permission_result: Option<PermissionResultBody>,
    pub start_auth: Option<StartAuthBody>,
    pub request_email_link: Option<EmailLinkBody>,
    pub sign_out: Option<SignOutBody>,
    pub auth_credential_result: Option<AuthCredentialResultBody>,
    pub correct_workspace_fact: Option<CorrectWorkspaceFactBody>,
    pub exclude_workspace_source: Option<ExcludeWorkspaceSourceBody>,
    pub request_workspace_export: Option<RequestWorkspaceExportBody>,
    pub request_asset: Option<RequestAssetBody>,
    pub remove_asset: Option<RemoveAssetBody>,
    pub set_asset_policy: Option<SetAssetPolicyBody>,
    pub save_provider_credential: Option<SaveProviderCredentialBody>,
    pub forget_provider_credential: Option<ForgetProviderCredentialBody>,
    pub start_provider_auth: Option<StartProviderAuthBody>,
    pub save_custom_provider: Option<SaveCustomProviderBody>,
    pub remove_custom_provider: Option<RemoveCustomProviderBody>,
    pub complete_handover: Option<CompleteHandoverBody>,
    pub supply_user_input: Option<SupplyUserInputBody>,
    pub set_provider_credential_state: Option<SetProviderCredentialStateBody>,
    pub probe_provider_key: Option<ProbeProviderKeyBody>,
    pub set_provider_model_preference: Option<SetProviderModelPreferenceBody>,
    pub probe_custom_endpoint: Option<ProbeCustomEndpointBody>,
    pub request_composer_completion: Option<RequestComposerCompletionBody>,
    pub cancel_composer_completion: Option<CancelComposerCompletionBody>,
    pub pause_task: Option<PauseTaskBody>,
    pub resume_task: Option<ResumeTaskBody>,
    pub take_over: Option<TakeOverBody>,
    pub set_assistant_configuration: Option<SetAssistantConfigurationBody>,
    pub save_workspace: Option<SaveWorkspaceBody>,
    pub rename_workspace: Option<RenameWorkspaceBody>,
    pub delete_workspace: Option<DeleteWorkspaceBody>,
    pub discard_workspace: Option<DiscardWorkspaceBody>,
    pub search_library: Option<SearchLibraryBody>,
    pub save_library_fact: Option<SaveLibraryFactBody>,
    pub remove_library_entry: Option<RemoveLibraryEntryBody>,
    pub request_library_export: Option<RequestLibraryExportBody>,
    pub search_memory: Option<SearchMemoryBody>,
    pub upsert_memory: Option<UpsertMemoryBody>,
    pub delete_memory: Option<DeleteMemoryBody>,
    pub accept_task_artifact: Option<AcceptTaskArtifactBody>,
    pub request_task_artifact_export: Option<RequestTaskArtifactExportBody>,
    pub mutate_site_skill: Option<SiteSkillMutationBody>,
    pub cancel_provider_auth: Option<CancelProviderAuthBody>,
    pub start_library_refresh: Option<StartLibraryRefreshBody>,
    pub follow_up: Option<FollowUpBody>,
}

impl CoreCommand {
    pub fn has_valid_body(&self) -> bool {
        let body_count = [
            self.start_task.is_some(),
            self.cancel_task.is_some(),
            self.approve_action.is_some(),
            self.retry_core.is_some(),
            self.permission_result.is_some(),
            self.start_auth.is_some(),
            self.request_email_link.is_some(),
            self.sign_out.is_some(),
            self.auth_credential_result.is_some(),
            self.correct_workspace_fact.is_some(),
            self.exclude_workspace_source.is_some(),
            self.request_workspace_export.is_some(),
            self.request_asset.is_some(),
            self.remove_asset.is_some(),
            self.set_asset_policy.is_some(),
            self.save_provider_credential.is_some(),
            self.forget_provider_credential.is_some(),
            self.start_provider_auth.is_some(),
            self.save_custom_provider.is_some(),
            self.remove_custom_provider.is_some(),
            self.complete_handover.is_some(),
            self.supply_user_input.is_some(),
            self.set_provider_credential_state.is_some(),
            self.probe_provider_key.is_some(),
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
            self.request_task_artifact_export.is_some(),
            self.mutate_site_skill.is_some(),
            self.cancel_provider_auth.is_some(),
            self.start_library_refresh.is_some(),
            self.follow_up.is_some(),
        ]
        .into_iter()
        .filter(|present| *present)
        .count();
        body_count == 1
            && match self.kind {
                CoreCommandKind::StartTask => self.start_task.is_some(),
                CoreCommandKind::CancelTask => self.cancel_task.is_some(),
                CoreCommandKind::ApproveAction => self.approve_action.is_some(),
                CoreCommandKind::RetryCore => self.retry_core.is_some(),
                CoreCommandKind::PermissionResult => self.permission_result.is_some(),
                CoreCommandKind::StartAuth => self.start_auth.is_some(),
                CoreCommandKind::RequestEmailLink => self.request_email_link.is_some(),
                CoreCommandKind::SignOut => self.sign_out.is_some(),
                CoreCommandKind::AuthCredentialResult => self.auth_credential_result.is_some(),
                CoreCommandKind::CorrectWorkspaceFact => self.correct_workspace_fact.is_some(),
                CoreCommandKind::ExcludeWorkspaceSource => self.exclude_workspace_source.is_some(),
                CoreCommandKind::RequestWorkspaceExport => self.request_workspace_export.is_some(),
                CoreCommandKind::RequestAsset => self.request_asset.is_some(),
                CoreCommandKind::RemoveAsset => self.remove_asset.is_some(),
                CoreCommandKind::SetAssetPolicy => self.set_asset_policy.is_some(),
                CoreCommandKind::SaveProviderCredential => self.save_provider_credential.is_some(),
                CoreCommandKind::ForgetProviderCredential => self.forget_provider_credential.is_some(),
                CoreCommandKind::StartProviderAuth => self.start_provider_auth.is_some(),
                CoreCommandKind::SaveCustomProvider => self.save_custom_provider.is_some(),
                CoreCommandKind::RemoveCustomProvider => self.remove_custom_provider.is_some(),
                CoreCommandKind::CompleteHandover => self.complete_handover.is_some(),
                CoreCommandKind::SupplyUserInput => self.supply_user_input.is_some(),
                CoreCommandKind::SetProviderCredentialState => self.set_provider_credential_state.is_some(),
                CoreCommandKind::ProbeProviderKey => self.probe_provider_key.is_some(),
                CoreCommandKind::SetProviderModelPreference => self.set_provider_model_preference.is_some(),
                CoreCommandKind::ProbeCustomEndpoint => self.probe_custom_endpoint.is_some(),
                CoreCommandKind::RequestComposerCompletion => self.request_composer_completion.is_some(),
                CoreCommandKind::CancelComposerCompletion => self.cancel_composer_completion.is_some(),
                CoreCommandKind::PauseTask => self.pause_task.is_some(),
                CoreCommandKind::ResumeTask => self.resume_task.is_some(),
                CoreCommandKind::TakeOver => self.take_over.is_some(),
                CoreCommandKind::SetAssistantConfiguration => self.set_assistant_configuration.is_some(),
                CoreCommandKind::SaveWorkspace => self.save_workspace.is_some(),
                CoreCommandKind::RenameWorkspace => self.rename_workspace.is_some(),
                CoreCommandKind::DeleteWorkspace => self.delete_workspace.is_some(),
                CoreCommandKind::DiscardWorkspace => self.discard_workspace.is_some(),
                CoreCommandKind::SearchLibrary => self.search_library.is_some(),
                CoreCommandKind::SaveLibraryFact => self.save_library_fact.is_some(),
                CoreCommandKind::RemoveLibraryEntry => self.remove_library_entry.is_some(),
                CoreCommandKind::RequestLibraryExport => self.request_library_export.is_some(),
                CoreCommandKind::SearchMemory => self.search_memory.is_some(),
                CoreCommandKind::UpsertMemory => self.upsert_memory.is_some(),
                CoreCommandKind::DeleteMemory => self.delete_memory.is_some(),
                CoreCommandKind::AcceptTaskArtifact => self.accept_task_artifact.is_some(),
                CoreCommandKind::RequestTaskArtifactExport => self.request_task_artifact_export.is_some(),
                CoreCommandKind::MutateSiteSkill => self.mutate_site_skill.is_some(),
                CoreCommandKind::CancelProviderAuth => self.cancel_provider_auth.is_some(),
                CoreCommandKind::StartLibraryRefresh => self.start_library_refresh.is_some(),
                CoreCommandKind::FollowUp => self.follow_up.is_some(),
            }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskConsentPreview {
    pub source_hosts: Vec<String>,
    pub source_discovery_enabled: bool,
    pub new_source_cap: u32,
    pub provider_route: TaskProviderRoute,
    pub attached_stores: Vec<TaskAttachedStore>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct StartTaskBody {
    pub request_id: String,
    pub goal: String,
    pub template_id: TaskTemplateId,
    pub workspace_id: Option<String>,
    pub consent_preview: TaskConsentPreview,
    pub skill_offer_id: Option<String>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CancelTaskBody {
    pub task_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PauseTaskBody {
    pub task_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ResumeTaskBody {
    pub task_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TakeOverBody {
    pub task_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AssistantConfigurationView {
    pub revision: u64,
    pub disabled_abilities: Vec<AssistantAbilityView>,
    pub preset: PersonalityPresetView,
    pub pace: u32,
    pub length: u32,
    pub check_in: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BuiltinSkillReferenceView {
    pub skill_id: BuiltinSkillIdView,
    pub version: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BuiltinSkillView {
    pub reference: BuiltinSkillReferenceView,
    pub required_ability: AssistantAbilityView,
    pub enabled: bool,
    pub availability: BuiltinSkillAvailabilityView,
    pub required_tool_count: u32,
    pub available_tool_count: u32,
    pub required_part_count: u32,
    pub installed_part_count: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CoreStatusProjectionOmission {
    pub family: CoreStatusProjectionFamily,
    pub revision: u64,
    pub item_count: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SetAssistantConfigurationBody {
    pub expected_revision: u64,
    pub disabled_abilities: Vec<AssistantAbilityView>,
    pub preset: PersonalityPresetView,
    pub pace: u32,
    pub length: u32,
    pub check_in: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CompleteHandoverBody {
    pub task_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SupplyUserInputBody {
    pub task_id: String,
    pub answer: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct FollowUpBody {
    pub task_id: String,
    pub question: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ApproveActionBody {
    pub task_id: String,
    pub action_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RetryCoreBody {
    pub observed_generation: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CoreFailure {
    pub code: CoreFailureCode,
    pub retryable: bool,
    pub message_key: Option<String>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskActivityView {
    pub sequence: u64,
    pub kind: TaskActivityKind,
    pub host: Option<String>,
    pub count: u32,
    pub at_epoch_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskArtifactView {
    pub artifact_id: String,
    pub kind: TaskArtifactKind,
    pub workspace_revision: u64,
    pub accepted: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskViewState {
    pub task_id: String,
    pub revision: u64,
    pub phase: TaskPhase,
    pub progress_basis_points: u32,
    pub status_message_key: Option<String>,
    pub failure: Option<CoreFailure>,
    pub goal: String,
    pub template_id: TaskTemplateId,
    pub pending_action: Option<ActionApprovalView>,
    pub workspace_id: Option<String>,
    pub pending_ask_prompt: Option<String>,
    pub pending_field_value_request: Option<String>,
    pub allowed_controls: Vec<TaskControlKind>,
    pub artifacts: Vec<TaskArtifactView>,
    pub activity: Vec<TaskActivityView>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ActionApprovalView {
    pub action_id: String,
    pub host: Option<String>,
    pub item_count: u32,
    pub summary_message_key: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct StoredCredentialView {
    pub auth_method: ProviderAuthMethodView,
    pub state: ProviderCredentialStateView,
    pub subscription_backed: bool,
    pub account_label: Option<String>,
    pub plan_label: Option<String>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ThinkingPreferenceView {
    pub level: ThinkingLevelView,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ProviderRefusalStateView {
    pub refusal: ProviderRefusalView,
    pub at_monotonic_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ProviderPresentationView {
    pub key_prefix: Option<String>,
    pub get_key_url: Option<String>,
    pub docs_url: Option<String>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ProviderModelView {
    pub provider_id: String,
    pub model_id: String,
    pub display_name: String,
    pub context_window: u64,
    pub max_output_tokens: u64,
    pub reasoning: bool,
    pub tool_calling: bool,
    pub roles: Vec<ModelRoleView>,
    pub input_modalities: Vec<InputModalityView>,
    pub thinking_levels: Vec<ThinkingLevelView>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ProviderRosterEntry {
    pub provider_id: String,
    pub display_name: String,
    pub origin: ProviderOriginView,
    pub auth_methods: Vec<ProviderAuthMethodView>,
    pub stored: Option<StoredCredentialView>,
    pub signing_in: bool,
    pub enabled: bool,
    pub endpoint_host: Option<String>,
    pub configurable: bool,
    pub endpoint_changed: bool,
    pub catalog_layer: CatalogLayerView,
    pub selected_model_id: Option<String>,
    pub thinking: Option<ThinkingPreferenceView>,
    pub presentation: Option<ProviderPresentationView>,
    pub endpoint_base: Option<String>,
    pub last_refusal: Option<ProviderRefusalStateView>,
    pub model_count: u32,
    pub subscription: bool,
    pub refused_endpoint_host: Option<String>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SetProviderCredentialStateBody {
    pub provider_id: String,
    pub state: ProviderCredentialStateView,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ProbeProviderKeyBody {
    pub provider_id: String,
    pub credential_handle: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ProviderProbeView {
    pub provider_id: String,
    pub verdict: ProviderProbeVerdictView,
    pub at_monotonic_ms: u64,
    pub endpoint: Option<ProbeEndpointView>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ProbeEndpointView {
    pub server_kind: ServerKindView,
    pub model_count: u32,
    pub models: Vec<CustomModelSpecView>,
    pub proved_base: Option<String>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DetectedServerView {
    pub server_kind: ServerKindView,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SavedSignInView {
    pub id: String,
    pub site: String,
    pub username: String,
    pub last_used_epoch_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SavedSignInsView {
    pub availability: SavedDataAvailability,
    pub revision: u64,
    pub records: Vec<SavedSignInView>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SavedDetailView {
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
pub struct SavedDetailsView {
    pub availability: SavedDataAvailability,
    pub revision: u64,
    pub people: Vec<SavedDetailView>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CoreStatus {
    pub availability: CoreAvailability,
    pub generation: u64,
    pub active_tasks: Vec<TaskViewState>,
    pub auth_state: Option<AuthViewState>,
    pub workspaces: Vec<WorkspaceViewState>,
    pub workspace_export: Option<WorkspaceExportView>,
    pub asset_delivery: Option<AssetDeliveryView>,
    pub provider_roster: Vec<ProviderRosterEntry>,
    pub provider_probes: Vec<ProviderProbeView>,
    pub provider_models: Vec<ProviderModelView>,
    pub assistant_configuration: AssistantConfigurationView,
    pub library: LibraryViewState,
    pub library_export: Option<LibraryExportView>,
    pub memory: MemoryViewState,
    pub saved_sign_ins: SavedSignInsView,
    pub saved_details: SavedDetailsView,
    pub site_skills: Vec<SiteSkillView>,
    pub builtin_skills: Vec<BuiltinSkillView>,
    pub projection_mode: CoreStatusProjectionMode,
    pub projection_omissions: Vec<CoreStatusProjectionOmission>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct WorkspaceViewState {
    pub workspace_id: String,
    pub revision: u64,
    pub goal: String,
    pub phase: WorkspacePhase,
    pub last_updated_epoch_ms: u64,
    pub template_id: TaskTemplateId,
    pub sources: Vec<WorkspaceSourceView>,
    pub facts: Vec<WorkspaceFactView>,
    pub saved: bool,
    pub display_name: String,
    pub deletion_preview: Option<WorkspaceDeletionPreviewView>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct WorkspaceDeletionPreviewView {
    pub sources: u32,
    pub facts: u32,
    pub artifact_metadata: u32,
    pub derived_indexes: u32,
    pub confirmation_token: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct WorkspaceSourceView {
    pub source_id: String,
    pub title: String,
    pub host: String,
    pub read_at_epoch_ms: u64,
    pub fact_count: u32,
    pub excluded: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct WorkspaceFactView {
    pub fact_id: String,
    pub field: String,
    pub value: String,
    pub kind: WorkspaceFactKind,
    pub sources: Vec<String>,
    pub correction: Option<String>,
    pub has_conflict: bool,
    pub needs_new_source: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct WorkspaceExportView {
    pub request_id: String,
    pub workspace_id: String,
    pub revision: u64,
    pub format: WorkspaceExportFormat,
    pub content: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct LibrarySourceView {
    pub source_id: String,
    pub title: String,
    pub host: String,
    pub observed_at_epoch_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct LibraryEntryView {
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
    pub kind: WorkspaceFactKind,
    pub sources: Vec<LibrarySourceView>,
    pub captured_at_epoch_ms: u64,
    pub last_checked_epoch_ms: u64,
    pub has_conflict: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct LibrarySearchHitView {
    pub entry_id: String,
    pub age_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct LibrarySearchView {
    pub request_id: String,
    pub query: String,
    pub library_revision: u64,
    pub hits: Vec<LibrarySearchHitView>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct LibraryRefreshSourceView {
    pub source_id: String,
    pub title: String,
    pub host: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct LibraryRefreshPreviewView {
    pub preview_id: String,
    pub collection_id: String,
    pub library_revision: u64,
    pub source_workspace_revision: u64,
    pub provider_route: TaskProviderRoute,
    pub navigation_count: u32,
    pub observation_count: u32,
    pub sources: Vec<LibraryRefreshSourceView>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct LibraryRefreshResultItemView {
    pub source_id: String,
    pub disposition: LibraryRefreshDisposition,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct LibraryRefreshResultView {
    pub preview_id: String,
    pub collection_id: String,
    pub items: Vec<LibraryRefreshResultItemView>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct LibraryViewState {
    pub availability: LibraryAvailability,
    pub revision: u64,
    pub entries: Vec<LibraryEntryView>,
    pub search: Option<LibrarySearchView>,
    pub refresh_previews: Vec<LibraryRefreshPreviewView>,
    pub refresh_results: Vec<LibraryRefreshResultView>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct LibraryExportView {
    pub request_id: String,
    pub library_revision: u64,
    pub collection_id: Option<String>,
    pub format: WorkspaceExportFormat,
    pub content: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MemoryWorkspaceView {
    pub workspace_id: String,
    pub display_name: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MemoryRecordView {
    pub memory_id: String,
    pub revision: u64,
    pub statement: String,
    pub source_kind: MemorySourceKind,
    pub source_task_id: Option<String>,
    pub source_workspace: Option<MemoryWorkspaceView>,
    pub scope_kind: MemoryScopeKind,
    pub scope_workspace: Option<MemoryWorkspaceView>,
    pub sensitivity: MemorySensitivity,
    pub created_at_epoch_ms: u64,
    pub updated_at_epoch_ms: u64,
    pub reviewed_at_epoch_ms: u64,
    pub expires_at_epoch_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MemorySearchHitView {
    pub memory_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MemorySearchView {
    pub request_id: String,
    pub query: String,
    pub memory_revision: u64,
    pub hits: Vec<MemorySearchHitView>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MemoryViewState {
    pub availability: MemoryAvailability,
    pub revision: u64,
    pub records: Vec<MemoryRecordView>,
    pub search: Option<MemorySearchView>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CorrectWorkspaceFactBody {
    pub workspace_id: String,
    pub expected_revision: u64,
    pub fact_id: String,
    pub value: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ExcludeWorkspaceSourceBody {
    pub workspace_id: String,
    pub expected_revision: u64,
    pub source_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RequestWorkspaceExportBody {
    pub request_id: String,
    pub workspace_id: String,
    pub expected_revision: u64,
    pub format: WorkspaceExportFormat,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SaveWorkspaceBody {
    pub workspace_id: String,
    pub expected_revision: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RenameWorkspaceBody {
    pub workspace_id: String,
    pub expected_revision: u64,
    pub display_name: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DeleteWorkspaceBody {
    pub workspace_id: String,
    pub expected_revision: u64,
    pub confirmation_token: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DiscardWorkspaceBody {
    pub workspace_id: String,
    pub expected_revision: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SearchLibraryBody {
    pub request_id: String,
    pub query: String,
    pub limit: u32,
    pub requested_at_epoch_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SaveLibraryFactBody {
    pub workspace_id: String,
    pub expected_workspace_revision: u64,
    pub fact_id: String,
    pub expected_library_revision: u64,
    pub expected_entry_revision: u64,
    pub approved_at_epoch_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RemoveLibraryEntryBody {
    pub entry_id: String,
    pub expected_library_revision: u64,
    pub expected_entry_revision: u64,
    pub removed_at_epoch_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RequestLibraryExportBody {
    pub request_id: String,
    pub expected_library_revision: u64,
    pub collection_id: Option<String>,
    pub format: WorkspaceExportFormat,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SearchMemoryBody {
    pub request_id: String,
    pub query: String,
    pub limit: u32,
    pub requested_at_epoch_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct UpsertMemoryBody {
    pub memory_id: Option<String>,
    pub statement: String,
    pub scope_kind: MemoryScopeKind,
    pub scope_workspace: Option<MemoryWorkspaceView>,
    pub sensitivity: MemorySensitivity,
    pub expected_memory_revision: u64,
    pub expected_record_revision: u64,
    pub expires_at_epoch_ms: u64,
    pub approved_at_epoch_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DeleteMemoryBody {
    pub memory_id: String,
    pub expected_memory_revision: u64,
    pub expected_record_revision: u64,
    pub deleted_at_epoch_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AcceptTaskArtifactBody {
    pub task_id: String,
    pub artifact_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RequestTaskArtifactExportBody {
    pub request_id: String,
    pub task_id: String,
    pub artifact_id: String,
    pub kind: TaskArtifactKind,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct StartAuthBody {
    pub provider: AuthProvider,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct EmailLinkBody {
    pub email: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SignOutBody {
    pub account_id: Option<String>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AuthCredentialResultBody {
    pub provider: AuthProvider,
    pub credential_handle: Option<String>,
    pub status: AuthCredentialStatus,
    pub flow_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AuthAccountView {
    pub account_id: String,
    pub display_name: Option<String>,
    pub email: Option<String>,
    pub method: AuthProvider,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AuthFailure {
    pub code: AuthFailureCode,
    pub retryable: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AuthMethodView {
    pub provider: AuthProvider,
    pub availability: AuthMethodAvailability,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct EntitlementView {
    pub plan_id: String,
    pub credits_granted: u64,
    pub credits_remaining: u64,
    pub next_renewal_epoch_seconds: u64,
    pub valid_until_epoch_seconds: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AuthViewState {
    pub phase: AuthPhase,
    pub account: Option<AuthAccountView>,
    pub pending_email: Option<String>,
    pub failure: Option<AuthFailure>,
    pub methods: Vec<AuthMethodView>,
    pub entitlement: Option<EntitlementView>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PageInspectorDocumentView {
    pub document_id: String,
    pub kind: PageInspectorDocumentKind,
    pub host: String,
    pub claims: Vec<PageInspectorClaim>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PageInspectorDocumentsView {
    pub availability: PageInspectorAvailability,
    pub documents: Vec<PageInspectorDocumentView>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PageInspectorAdapterView {
    pub kind: PageInspectorAdapterKind,
    pub status: PageInspectorAdapterStatus,
    pub version: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PageInspectorNodeView {
    pub display_id: String,
    pub role: PageInspectorNodeRole,
    pub name: Option<String>,
    pub sensitivity: PageInspectorSensitivity,
    pub text_run_count: u32,
    pub text_byte_count: u64,
    pub value_present: bool,
    pub value_withheld: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PageInspectorEdgeView {
    pub from_display_id: String,
    pub to_display_id: String,
    pub relationship: PageInspectorRelationship,
    pub inferred: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PageInspectorFrameView {
    pub main_frame: bool,
    pub out_of_process: bool,
    pub cross_origin: bool,
    pub included: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PageInspectorTruncationView {
    pub truncated: bool,
    pub budgets_reached: Vec<PageInspectorBudgetKind>,
    pub omitted_node_count: u32,
    pub omitted_text_bytes: u32,
    pub omitted_frame_count: u32,
    pub may_change_answer: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PageInspectorRedactionView {
    pub redacted_field_count: u32,
    pub suppressed_secret_count: u32,
    pub sensitive_zone_count: u32,
    pub filtered_frame_count: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PageInspectorSnapshotView {
    pub document_id: String,
    pub document_revision: u64,
    pub host: String,
    pub secure_context: bool,
    pub private_profile: bool,
    pub document_state: PageInspectorDocumentState,
    pub adapters: Vec<PageInspectorAdapterView>,
    pub nodes: Vec<PageInspectorNodeView>,
    pub edges: Vec<PageInspectorEdgeView>,
    pub frames: Vec<PageInspectorFrameView>,
    pub truncation: PageInspectorTruncationView,
    pub redaction: PageInspectorRedactionView,
    pub warnings: Vec<PageInspectorWarningCode>,
    pub site_skill_offer_availability: SiteSkillOfferAvailability,
    pub site_skill_offers: Vec<SiteSkillOfferView>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PageInspectorSnapshotResult {
    pub availability: PageInspectorAvailability,
    pub snapshot: Option<PageInspectorSnapshotView>,
}

impl PageInspectorSnapshotResult {
    pub fn has_valid_presence(&self) -> bool {
        match self.availability {
            PageInspectorAvailability::Available => self.snapshot.is_some(),
            _ => self.snapshot.is_none(),
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PageSnapshotExportView {
    pub request_id: String,
    pub document_id: String,
    pub document_revision: u64,
    pub origin: String,
    pub format: PageSnapshotExportFormat,
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
pub struct PageSnapshotExportResult {
    pub availability: PageSnapshotExportAvailability,
    pub snapshot_export: Option<PageSnapshotExportView>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PermissionResultBody {
    pub request_id: String,
    pub permission: PlatformPermission,
    pub decision: PermissionDecision,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AssetRefusal {
    pub reason: AssetRefusalView,
    pub retryable: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AssetViewState {
    pub asset_id: String,
    pub asset_revision: String,
    pub kind: AssetKindView,
    pub presence: AssetPresenceView,
    pub written_bytes: u64,
    pub total_bytes: u64,
    pub attempts: u32,
    pub refusal: Option<AssetRefusal>,
    pub waiting_until_monotonic_ms: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AssetDeliveryView {
    pub platform_supported: bool,
    pub network_cost: AssetNetworkCostView,
    pub metered_permitted: bool,
    pub assets: Vec<AssetViewState>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RequestAssetBody {
    pub asset_id: String,
    pub asset_revision: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RemoveAssetBody {
    pub asset_id: String,
    pub asset_revision: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SetAssetPolicyBody {
    pub network_cost: AssetNetworkCostView,
    pub metered_permitted: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SaveProviderCredentialBody {
    pub provider_id: String,
    pub auth_method: ProviderAuthMethodView,
    pub credential_handle: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ForgetProviderCredentialBody {
    pub provider_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct StartProviderAuthBody {
    pub provider_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CancelProviderAuthBody {
    pub flow_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct StartLibraryRefreshBody {
    pub preview_id: String,
    pub collection_id: String,
    pub expected_library_revision: u64,
    pub expected_workspace_revision: u64,
    pub source_count: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CustomModelSpecView {
    pub model_id: String,
    pub display_name: String,
    pub context_window: u32,
    pub max_output_tokens: u32,
    pub reasoning: bool,
    pub tool_calling: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SaveCustomProviderBody {
    pub provider_id: String,
    pub display_name: String,
    pub endpoint: String,
    pub wire_api: ProviderWireApiView,
    pub credential_handle: Option<String>,
    pub models: Vec<CustomModelSpecView>,
    pub detected_server: Option<DetectedServerView>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ProbeCustomEndpointBody {
    pub endpoint: String,
    pub wire_api: ProviderWireApiView,
    pub credential_handle: Option<String>,
    pub provider_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SetProviderModelPreferenceBody {
    pub provider_id: String,
    pub model_id: Option<String>,
    pub thinking: Option<ThinkingPreferenceView>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RequestComposerCompletionBody {
    pub request_id: String,
    pub prefix: String,
    pub suffix: Option<String>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CancelComposerCompletionBody {
    pub request_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RemoveCustomProviderBody {
    pub provider_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SavedFlowQueryResult {
    pub request_id: String,
    pub service_generation: u64,
    pub availability: SavedFlowQueryAvailability,
    pub flows: Vec<SiteSkillView>,
}
