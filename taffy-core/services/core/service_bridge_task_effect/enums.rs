// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Exhaustive task-domain to generated effect enum projections.

use core_runtime::wire;
use core_runtime::{
    ActionClass, ActionId, ActionIntent, BrowserIntent, Effect, LibraryIntent, MemoryIntent,
    StoreIntent,
};

pub(super) const fn effect_kind(effect: &Effect) -> wire::TaskReducerEffectKind {
    match effect {
        Effect::RevokeAuthority { .. } => wire::TaskReducerEffectKind::RevokeAuthority,
        Effect::AskPolicy { .. } => wire::TaskReducerEffectKind::AskPolicy,
        Effect::RequestApproval { .. } => wire::TaskReducerEffectKind::RequestApproval,
        Effect::RequestPermission { .. } => wire::TaskReducerEffectKind::RequestPermission,
        Effect::DispatchAction { .. } => wire::TaskReducerEffectKind::DispatchAction,
        Effect::AwaitInFlightWork { .. } => wire::TaskReducerEffectKind::AwaitInFlightWork,
        Effect::ReconcileAction { .. } => wire::TaskReducerEffectKind::ReconcileAction,
        Effect::ReleaseTaskTabs => wire::TaskReducerEffectKind::ReleaseTaskTabs,
        Effect::GenerateArtifact { .. } => wire::TaskReducerEffectKind::GenerateArtifact,
        Effect::ExportArtifact { .. } => wire::TaskReducerEffectKind::ExportArtifact,
        Effect::CallModel { .. } => wire::TaskReducerEffectKind::CallModel,
        Effect::AwaitHandover { .. } => wire::TaskReducerEffectKind::AwaitHandover,
        Effect::RunToolJob { .. } => wire::TaskReducerEffectKind::RunToolJob,
        Effect::RequestFieldValues { .. } => wire::TaskReducerEffectKind::RequestFieldValues,
        Effect::PrepareDiscoveryTab { .. } => wire::TaskReducerEffectKind::PrepareDiscoveryTab,
        Effect::RunLibraryTool { .. } => wire::TaskReducerEffectKind::RunLibraryTool,
        Effect::RunMemoryTool { .. } => wire::TaskReducerEffectKind::RunMemoryTool,
    }
}

pub(super) const fn effect_action_id(effect: &Effect) -> Option<&ActionId> {
    match effect {
        Effect::AskPolicy { action_id }
        | Effect::RequestApproval { action_id }
        | Effect::DispatchAction { action_id }
        | Effect::RunToolJob { action_id, .. }
        | Effect::RunLibraryTool { action_id }
        | Effect::RunMemoryTool { action_id }
        | Effect::ReconcileAction { action_id, .. } => Some(action_id),
        Effect::RevokeAuthority { .. }
        | Effect::RequestPermission { .. }
        | Effect::AwaitInFlightWork { .. }
        | Effect::ReleaseTaskTabs
        | Effect::GenerateArtifact { .. }
        | Effect::ExportArtifact { .. }
        // A model call is named by its own durable call identity, not by an
        // action. There is no proposal behind it and no capability to spend:
        // it is the turn itself.
        | Effect::CallModel { .. }
        // A handover is named by its own identity and is about the person
        // rather than about a proposal. There is no action behind it and no
        // capability to spend: the assistant has stopped.
        | Effect::AwaitHandover { .. }
        // Asking the person to fill a form in is named by its own request
        // identity, for the handover's reason and one more: the fills it makes
        // possible are separate proposals, decided one at a time after the
        // person has answered. Naming an action here would tie the ask to
        // whichever of them happened to be composed first.
        | Effect::RequestFieldValues { .. } => None,
        Effect::PrepareDiscoveryTab { .. } => None,
    }
}

pub(super) const fn action_class(value: ActionClass) -> wire::PolicyActionClass {
    match value {
        ActionClass::ObservePage => wire::PolicyActionClass::ObservePage,
        ActionClass::ScrollIntoView => wire::PolicyActionClass::ScrollIntoView,
        ActionClass::OpenLink => wire::PolicyActionClass::OpenLink,
        ActionClass::CreateTaskTab => wire::PolicyActionClass::CreateTaskTab,
        ActionClass::SyntheticClick => wire::PolicyActionClass::SyntheticClick,
        ActionClass::MoveFocus => wire::PolicyActionClass::MoveFocus,
        ActionClass::FillField => wire::PolicyActionClass::FillField,
        ActionClass::SelectOption => wire::PolicyActionClass::SelectOption,
        ActionClass::ToggleControl => wire::PolicyActionClass::ToggleControl,
        ActionClass::SubmitForm => wire::PolicyActionClass::SubmitForm,
        ActionClass::StartDownload => wire::PolicyActionClass::StartDownload,
        ActionClass::UploadFile => wire::PolicyActionClass::UploadFile,
        ActionClass::SendMessage => wire::PolicyActionClass::SendMessage,
        ActionClass::Purchase => wire::PolicyActionClass::Purchase,
        ActionClass::ExtractCredential => wire::PolicyActionClass::ExtractCredential,
        ActionClass::BypassAccessControl => wire::PolicyActionClass::BypassAccessControl,
        ActionClass::ExecuteToolJob => wire::PolicyActionClass::ExecuteToolJob,
        ActionClass::LibraryRead => wire::PolicyActionClass::LibraryRead,
        ActionClass::LibraryWrite => wire::PolicyActionClass::LibraryWrite,
        ActionClass::MemoryRead => wire::PolicyActionClass::MemoryRead,
        ActionClass::MemoryWrite => wire::PolicyActionClass::MemoryWrite,
        ActionClass::ControlTab => wire::PolicyActionClass::ControlTab,
        ActionClass::ProfileStoreRead => wire::PolicyActionClass::ProfileStoreRead,
    }
}

/// Exact operation identity; unlike `ActionClass`, this never collapses
/// distinct browser commands into one policy family.
pub(super) const fn action_operation(value: &ActionIntent) -> wire::TaskActionOperationKind {
    match value {
        ActionIntent::Browser(value) => match value {
            BrowserIntent::Navigate { .. } => wire::TaskActionOperationKind::Navigate,
            BrowserIntent::Search { .. } => wire::TaskActionOperationKind::Search,
            BrowserIntent::HistoryBack { .. } => wire::TaskActionOperationKind::HistoryBack,
            BrowserIntent::HistoryForward { .. } => wire::TaskActionOperationKind::HistoryForward,
            BrowserIntent::Reload { .. } => wire::TaskActionOperationKind::Reload,
            BrowserIntent::StopLoading { .. } => wire::TaskActionOperationKind::StopLoading,
            BrowserIntent::TabsOpen { .. } => wire::TaskActionOperationKind::TabsOpen,
            BrowserIntent::TabsList { .. } => wire::TaskActionOperationKind::TabsList,
            BrowserIntent::TabsActivate { .. } => wire::TaskActionOperationKind::TabsActivate,
            BrowserIntent::TabsClose { .. } => wire::TaskActionOperationKind::TabsClose,
            BrowserIntent::DomQuery { .. } => wire::TaskActionOperationKind::DomQuery,
            BrowserIntent::DomRead { .. } => wire::TaskActionOperationKind::DomRead,
            BrowserIntent::DomClick { .. } => wire::TaskActionOperationKind::DomClick,
            BrowserIntent::DomFocus { .. } => wire::TaskActionOperationKind::DomFocus,
            BrowserIntent::DomScroll { .. } => wire::TaskActionOperationKind::DomScroll,
            BrowserIntent::FormInspect { .. } => wire::TaskActionOperationKind::FormInspect,
            BrowserIntent::FormFill { .. } => wire::TaskActionOperationKind::FormFill,
            BrowserIntent::FormSelect { .. } => wire::TaskActionOperationKind::FormSelect,
            BrowserIntent::FormToggle { .. } => wire::TaskActionOperationKind::FormToggle,
            BrowserIntent::FormSubmit { .. } => wire::TaskActionOperationKind::FormSubmit,
            BrowserIntent::DownloadStart { .. } | BrowserIntent::DownloadFromLink { .. } => wire::TaskActionOperationKind::DownloadStart,
            BrowserIntent::DownloadList { .. } => wire::TaskActionOperationKind::DownloadList,
            BrowserIntent::DownloadCancel { .. } => wire::TaskActionOperationKind::DownloadCancel,
            BrowserIntent::SelectionRead { .. } => wire::TaskActionOperationKind::SelectionRead,
            BrowserIntent::ImageDescribe { .. } => wire::TaskActionOperationKind::ImageDescribe,
            BrowserIntent::ImageReadText { .. } => wire::TaskActionOperationKind::ImageReadText,
            BrowserIntent::VideoInspect { .. } => wire::TaskActionOperationKind::VideoInspect,
            BrowserIntent::PdfInspect { .. } => wire::TaskActionOperationKind::PdfInspect,
            BrowserIntent::PageScreenshotInspect { .. } => {
                wire::TaskActionOperationKind::PageScreenshotInspect
            }
            BrowserIntent::LinkOpen { .. } => wire::TaskActionOperationKind::LinkOpen,
        },
        ActionIntent::MediaTool(_) | ActionIntent::ToolJob(_) => {
            wire::TaskActionOperationKind::ToolJob
        }
        ActionIntent::Library(value) => match value {
            LibraryIntent::Search { .. } => wire::TaskActionOperationKind::LibrarySearch,
            LibraryIntent::Save { .. } => wire::TaskActionOperationKind::LibrarySave,
            LibraryIntent::Remove { .. } => wire::TaskActionOperationKind::LibraryRemove,
        },
        ActionIntent::Memory(value) => match value {
            MemoryIntent::Search { .. } => wire::TaskActionOperationKind::MemorySearch,
            MemoryIntent::Save { .. } => wire::TaskActionOperationKind::MemorySave,
            MemoryIntent::Update { .. } => wire::TaskActionOperationKind::MemoryUpdate,
            MemoryIntent::Delete { .. } => wire::TaskActionOperationKind::MemoryDelete,
        },
        ActionIntent::Store(value) => match value {
            StoreIntent::HistorySearch { .. } => wire::TaskActionOperationKind::HistorySearch,
            StoreIntent::HistoryRecent { .. } => wire::TaskActionOperationKind::HistoryRecent,
            StoreIntent::BookmarksSearch { .. } => wire::TaskActionOperationKind::BookmarksSearch,
            StoreIntent::BookmarksList { .. } => wire::TaskActionOperationKind::BookmarksList,
            StoreIntent::OpenTabsList { .. } => wire::TaskActionOperationKind::OpenTabsList,
        },
    }
}

pub(super) const fn risk(value: ActionClass) -> wire::PolicyRiskClass {
    match value {
        // A tool job computes in an isolated runtime over material the task
        // already holds, so its floor is the read floor — the same verdict
        // policy-engine's `baseline_risk` reaches, which is the deciding copy.
        ActionClass::ObservePage
        | ActionClass::ScrollIntoView
        | ActionClass::ExecuteToolJob
        | ActionClass::LibraryRead => wire::PolicyRiskClass::LocalRead,
        // A store read stays on the phone and changes nothing there.
        ActionClass::MemoryRead | ActionClass::ProfileStoreRead => wire::PolicyRiskClass::LocalRead,
        ActionClass::OpenLink
        | ActionClass::CreateTaskTab
        | ActionClass::SyntheticClick
        | ActionClass::MoveFocus
        | ActionClass::ControlTab => wire::PolicyRiskClass::ReversibleDisclosure,
        ActionClass::FillField
        | ActionClass::SelectOption
        | ActionClass::ToggleControl
        | ActionClass::SubmitForm
        | ActionClass::StartDownload
        | ActionClass::UploadFile
        | ActionClass::LibraryWrite => wire::PolicyRiskClass::SensitiveDisclosure,
        ActionClass::MemoryWrite => wire::PolicyRiskClass::SensitiveDisclosure,
        ActionClass::SendMessage | ActionClass::Purchase => {
            wire::PolicyRiskClass::ExcludedCommitment
        }
        ActionClass::ExtractCredential | ActionClass::BypassAccessControl => {
            wire::PolicyRiskClass::ProhibitedAbuse
        }
    }
}

pub(super) const fn control(value: core_runtime::ControlMode) -> wire::TaskControlMode {
    match value {
        core_runtime::ControlMode::User => wire::TaskControlMode::User,
        core_runtime::ControlMode::Shared => wire::TaskControlMode::Shared,
        core_runtime::ControlMode::Assistant => wire::TaskControlMode::Assistant,
    }
}

pub(super) const fn revocation(
    value: core_runtime::RevocationReason,
) -> wire::TaskRevocationReason {
    match value {
        core_runtime::RevocationReason::UserTookOver => wire::TaskRevocationReason::UserTookOver,
        core_runtime::RevocationReason::DirectUserInput => {
            wire::TaskRevocationReason::DirectUserInput
        }
        core_runtime::RevocationReason::TaskCancelled => wire::TaskRevocationReason::TaskCancelled,
        core_runtime::RevocationReason::PolicyRevoked => wire::TaskRevocationReason::PolicyRevoked,
        core_runtime::RevocationReason::TabClosed => wire::TaskRevocationReason::TabClosed,
    }
}

pub(super) const fn recovery(value: core_runtime::RecoveryRule) -> wire::TaskRecoveryRule {
    match value {
        core_runtime::RecoveryRule::RetryWithinEpochAndBudget => {
            wire::TaskRecoveryRule::RetryWithinEpochAndBudget
        }
        core_runtime::RecoveryRule::RetryAfterStateCheck => {
            wire::TaskRecoveryRule::RetryAfterStateCheck
        }
        core_runtime::RecoveryRule::ReconcileFirst => wire::TaskRecoveryRule::ReconcileFirst,
        core_runtime::RecoveryRule::NeverAutomatically => {
            wire::TaskRecoveryRule::NeverAutomatically
        }
    }
}

pub(super) const fn artifact(value: core_runtime::ArtifactKind) -> wire::TaskArtifactKind {
    match value {
        core_runtime::ArtifactKind::Markdown => wire::TaskArtifactKind::Markdown,
        core_runtime::ArtifactKind::Csv => wire::TaskArtifactKind::Csv,
        core_runtime::ArtifactKind::Xlsx => wire::TaskArtifactKind::Xlsx,
        core_runtime::ArtifactKind::Pdf => wire::TaskArtifactKind::Pdf,
        core_runtime::ArtifactKind::Docx => wire::TaskArtifactKind::Docx,
        core_runtime::ArtifactKind::Pptx => wire::TaskArtifactKind::Pptx,
        core_runtime::ArtifactKind::WaveAudio => wire::TaskArtifactKind::WaveAudio,
        core_runtime::ArtifactKind::FrameArchive => wire::TaskArtifactKind::FrameArchive,
    }
}

pub(super) const fn platform_permission(
    value: core_runtime::PlatformPermission,
) -> wire::PlatformPermission {
    match value {
        core_runtime::PlatformPermission::Notifications => wire::PlatformPermission::Notifications,
        core_runtime::PlatformPermission::Microphone => wire::PlatformPermission::Microphone,
        core_runtime::PlatformPermission::Camera => wire::PlatformPermission::Camera,
        core_runtime::PlatformPermission::Location => wire::PlatformPermission::Location,
    }
}

pub(super) const fn postcondition(value: &ActionIntent) -> wire::TaskActionPostcondition {
    match value {
        ActionIntent::Browser(BrowserIntent::TabsList { .. }) => {
            wire::TaskActionPostcondition::TaskTabsListed
        }
        ActionIntent::Browser(BrowserIntent::TabsActivate { .. }) => {
            wire::TaskActionPostcondition::TaskTabActive
        }
        ActionIntent::Browser(BrowserIntent::TabsClose { .. }) => {
            wire::TaskActionPostcondition::TaskTabAbsent
        }
        ActionIntent::Browser(BrowserIntent::DownloadCancel { .. }) => {
            wire::TaskActionPostcondition::DownloadCancelled
        }
        ActionIntent::Browser(
            BrowserIntent::HistoryBack { .. } | BrowserIntent::HistoryForward { .. },
        ) => wire::TaskActionPostcondition::DocumentNavigated,
        ActionIntent::Browser(BrowserIntent::Reload { .. }) => {
            wire::TaskActionPostcondition::PageReloaded
        }
        ActionIntent::Browser(BrowserIntent::StopLoading { .. }) => {
            wire::TaskActionPostcondition::LoadingStopped
        }
        ActionIntent::Store(_) => wire::TaskActionPostcondition::StoreRowsListed,
        _ => postcondition_for_class(value.action_class()),
    }
}

const fn postcondition_for_class(value: ActionClass) -> wire::TaskActionPostcondition {
    match value {
        ActionClass::ObservePage => wire::TaskActionPostcondition::ObservationCaptured,
        ActionClass::OpenLink | ActionClass::CreateTaskTab => {
            wire::TaskActionPostcondition::DocumentNavigated
        }
        ActionClass::ScrollIntoView
        | ActionClass::SyntheticClick
        | ActionClass::MoveFocus
        | ActionClass::FillField
        | ActionClass::SelectOption
        | ActionClass::ToggleControl
        | ActionClass::SubmitForm => wire::TaskActionPostcondition::NodeStateChanged,
        ActionClass::StartDownload => wire::TaskActionPostcondition::DownloadStarted,
        // A tool job never reaches the dispatch projection that reads this —
        // it is dispatched as a tool-job binding, not a page action — but the
        // vocabulary is total, and the supervisor's report is the only
        // acknowledgment such a job could ever have.
        ActionClass::UploadFile
        | ActionClass::SendMessage
        | ActionClass::Purchase
        | ActionClass::ExtractCredential
        | ActionClass::BypassAccessControl
        | ActionClass::ExecuteToolJob
        | ActionClass::LibraryRead
        | ActionClass::LibraryWrite
        | ActionClass::MemoryRead
        | ActionClass::MemoryWrite
        | ActionClass::ControlTab
        // Every store intent is answered above; the class row keeps the
        // vocabulary total.
        | ActionClass::ProfileStoreRead => wire::TaskActionPostcondition::PlatformAcknowledged,
    }
}
