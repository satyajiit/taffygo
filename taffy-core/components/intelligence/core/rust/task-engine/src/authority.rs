// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Neutral proposal and policy-result values at the task reducer boundary.
//!
//! The task engine owns no grant logic. It records a closed decision delivered
//! by the composition runtime, while `policy-engine` remains the only component
//! allowed to mint a grant. Keeping these values neutral prevents the reducer
//! from depending on the policy implementation or its lease ledger.

use core::fmt;

use bip_types::ActionResultCode;

/// A class of effect the task engine may propose.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ActionClass {
    /// Take a bounded semantic observation.
    ObservePage,
    /// Bring a target into view.
    ScrollIntoView,
    /// Activate an ordinary link.
    OpenLink,
    /// Open a task-owned tab.
    CreateTaskTab,
    /// Activate a non-link interactive node.
    SyntheticClick,
    /// Move focus.
    MoveFocus,
    /// Write text into a field.
    FillField,
    /// Choose an option.
    SelectOption,
    /// Change a checkbox, radio, or switch.
    ToggleControl,
    /// Submit a form.
    SubmitForm,
    /// Start a download.
    StartDownload,
    /// Upload a user-selected file.
    UploadFile,
    /// Send or publish a message.
    SendMessage,
    /// Complete a purchase or move funds.
    Purchase,
    /// Read a credential or secret.
    ExtractCredential,
    /// Defeat an access control.
    BypassAccessControl,
    /// Run one job in an isolated, capability-free runtime the core
    /// supervises. The job reaches no page and holds no lease; its input is
    /// task material and its output returns as one durable completion.
    ExecuteToolJob,
    /// Read the profile-local Library without page or network authority.
    LibraryRead,
    /// Save or remove one exact-revision profile-local Library entry.
    LibraryWrite,
    /// Read active standard Memory within the task's visible scope.
    MemoryRead,
    /// Save, update, or delete one exact-revision Memory record after the
    /// person approves the exact proposal.
    MemoryWrite,
    /// Control navigation lifecycle in one exact existing tab without naming
    /// a destination or renderer node.
    ControlTab,
    /// Read a person's own History, Bookmarks or open tabs, attached to the
    /// task as a store.
    ProfileStoreRead,
}

impl ActionClass {
    /// A compiled-in contract label.
    pub const fn label(self) -> &'static str {
        match self {
            Self::ObservePage => "observe_page",
            Self::ScrollIntoView => "scroll_into_view",
            Self::OpenLink => "open_link",
            Self::CreateTaskTab => "create_task_tab",
            Self::SyntheticClick => "synthetic_click",
            Self::MoveFocus => "move_focus",
            Self::FillField => "fill_field",
            Self::SelectOption => "select_option",
            Self::ToggleControl => "toggle_control",
            Self::SubmitForm => "submit_form",
            Self::StartDownload => "start_download",
            Self::UploadFile => "upload_file",
            Self::SendMessage => "send_message",
            Self::Purchase => "purchase",
            Self::ExtractCredential => "extract_credential",
            Self::BypassAccessControl => "bypass_access_control",
            Self::ExecuteToolJob => "execute_tool_job",
            Self::LibraryRead => "library_read",
            Self::LibraryWrite => "library_write",
            Self::MemoryRead => "memory_read",
            Self::MemoryWrite => "memory_write",
            Self::ControlTab => "control_tab",
            Self::ProfileStoreRead => "profile_store_read",
        }
    }

    /// Whether attempting the class can change page or browser state.
    ///
    /// A tool job runs in an isolated runtime with no page lease and no
    /// browser capability, so it cannot: whatever it computes returns as data
    /// through its one completion.
    pub const fn mutates(self) -> bool {
        !matches!(
            self,
            Self::ObservePage
                | Self::ScrollIntoView
                | Self::ExecuteToolJob
                | Self::LibraryRead
                | Self::MemoryRead
                | Self::ProfileStoreRead
        )
    }
}

/// Who is acting in a task's tab.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ControlMode {
    /// Only the person may act.
    User,
    /// The person reviews each assistant step.
    Shared,
    /// The assistant may propose actions under explicit policy grants.
    Assistant,
}

/// The policy bundle version frozen by a task.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct PolicyVersion(pub u32);

/// Opaque capability reference returned by the grant-minting policy component.
#[derive(Clone, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct CapabilityId(String);

impl CapabilityId {
    /// Wraps a reference minted outside the task engine.
    pub fn new(value: impl Into<String>) -> Self {
        Self(value.into())
    }

    /// The opaque value, for correlation only.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl fmt::Display for CapabilityId {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.0)
    }
}

/// Opaque actor-lease reference held by the browser process.
///
/// Beside [`CapabilityId`] and for the same reason: the value is minted
/// elsewhere, it is carried here only so a record can correlate, and this
/// crate holds no lease and can neither issue nor extend one. The registry
/// that does is `taffy-core/components/security/browser/action_authority.h`,
/// in the browser process, because preemption has to be synchronous with the
/// input event that causes it.
#[derive(Clone, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct ActorLeaseId(String);

impl ActorLeaseId {
    /// Wraps a lease identity minted outside the task engine.
    pub fn new(value: impl Into<String>) -> Self {
        Self(value.into())
    }

    /// The opaque value, for correlation only.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl fmt::Display for ActorLeaseId {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.0)
    }
}

/// The grant facts the reducer is allowed to retain.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Authorization {
    /// The opaque grant reference. Scope and spending remain browser-owned.
    pub capability_id: CapabilityId,
}

/// A closed refusal delivered by policy.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Denial {
    /// Protocol result recorded on the action.
    pub code: ActionResultCode,
}

impl Denial {
    /// Builds a refusal from the policy component's closed result code.
    pub const fn new(code: ActionResultCode) -> Self {
        Self { code }
    }
}

/// The result of asking the sole policy component about one proposal.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ProposalDecision {
    /// Policy minted a grant.
    Authorize(Authorization),
    /// An exact user decision is required; no grant exists.
    RequireApproval,
    /// Policy refused the proposal.
    Deny(Denial),
}

impl ProposalDecision {
    /// The capability reference, when policy minted one.
    pub const fn capability_id(&self) -> Option<&CapabilityId> {
        match self {
            Self::Authorize(authorization) => Some(&authorization.capability_id),
            Self::RequireApproval | Self::Deny(_) => None,
        }
    }

    /// The terminal protocol result, when this decision has one.
    pub const fn result_code(&self) -> Option<ActionResultCode> {
        match self {
            Self::Authorize(_) => None,
            Self::RequireApproval => Some(ActionResultCode::ApprovalRequired),
            Self::Deny(denial) => Some(denial.code),
        }
    }
}

/// Why the runtime must revoke outstanding authority.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum RevocationReason {
    /// The user pressed take over.
    UserTookOver,
    /// The user interacted directly with the tab.
    DirectUserInput,
    /// The task was cancelled.
    TaskCancelled,
    /// A policy change withdrew authority.
    PolicyRevoked,
    /// The tab or document went away.
    TabClosed,
}

impl RevocationReason {
    /// A compiled-in contract label.
    pub const fn label(self) -> &'static str {
        match self {
            Self::UserTookOver => "user_took_over",
            Self::DirectUserInput => "direct_user_input",
            Self::TaskCancelled => "task_cancelled",
            Self::PolicyRevoked => "policy_revoked",
            Self::TabClosed => "tab_closed",
        }
    }
}
