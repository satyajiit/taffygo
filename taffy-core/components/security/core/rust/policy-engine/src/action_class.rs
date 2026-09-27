// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The action-class taxonomy and the ratified surface of each milestone
//! (protocol specification section 11.1, roadmap sections 5 and 8).
//!
//! A node's advertised `actions[]` says what a page adapter believes could be
//! attempted. It grants nothing. This module holds the other half of that
//! sentence: which classes of effect the product has actually authorized, at
//! which milestone, and why each of the others is refused.
//!
//! Every class exists as a value, including the ones that are denied. That is
//! deliberate. A denial that is spelled out in the taxonomy is compile-checked,
//! enumerable by a test, and visible in review; a denial expressed by leaving a
//! value out is invisible until somebody adds the value back. Protocol
//! representation never implies product authorization.
//!
//! The surface is an allowlist, never a blocklist. [`authorized_classes`] lists
//! what a milestone permits and [`ActionClass::is_authorized_at`] consults that
//! list, so a class added to this enumeration is denied until a milestone
//! explicitly adds it.

use bip_types::action::ActionType;
use bip_types::snapshot::SemanticRole;

use crate::phase::ActionPhase;
use crate::risk::RiskClass;

/// The milestone whose ratified action surface a decision runs against.
///
/// Only the milestones whose surface has been ratified appear here. M2 and M3
/// share one surface (protocol specification section 11.1); M5 widens it
/// (decision
/// `docs/decisions/0089-the-write-surface-is-ratified-as-one-milestone.md`),
/// and M6 adds only the profile-local Library surface.
/// Each widening adds its own member together with its own allowlist, which is
/// why [`authorized_classes`] is a total function over this enumeration rather
/// than a comparison against a milestone number.
///
/// Deliberately unordered. There is no `PartialOrd`, so nothing anywhere can
/// ask whether a milestone is "at least" another one and get an answer that
/// widens a surface without naming what it widened.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum PolicyMilestone {
    /// M2 — page intelligence.
    M2,
    /// M3 — the assistant and workspaces.
    M3,
    /// M5 — browser-owned field fill and download initiation. Selection,
    /// toggle, submit, and upload remain reserved and fail closed.
    M5,
    /// M6 — the Library and its explicit local retrieval/mutation tools.
    M6,
    /// M7 — sandboxed computation and file-generation tools.
    M7,
}

impl PolicyMilestone {
    /// Every ratified milestone surface, in order.
    pub const ALL: &'static [Self] = &[Self::M2, Self::M3, Self::M5, Self::M6, Self::M7];

    /// A short, compiled-in name for the milestone.
    pub const fn label(self) -> &'static str {
        match self {
            Self::M2 => "M2",
            Self::M3 => "M3",
            Self::M5 => "M5",
            Self::M6 => "M6",
            Self::M7 => "M7",
        }
    }
}

/// A class of effect a task may propose.
///
/// Classes are about consequence, not about which protocol message carries
/// them: `ACTIVATE` on a link leaves the document and `ACTIVATE` on a button
/// does not, so they are separate classes even though the wire value is the
/// same.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ActionClass {
    /// Take a bounded semantic observation of a document.
    ObservePage,
    /// Bring a target into view without activating it.
    ScrollIntoView,
    /// Activate an ordinary link.
    OpenLink,
    /// Open a task-owned tab through a browser-owned command.
    CreateTaskTab,
    /// Activate an interactive node that is not a link.
    SyntheticClick,
    /// Move focus to a node.
    MoveFocus,
    /// Write text into a field.
    FillField,
    /// Choose an option in a selection control.
    SelectOption,
    /// Change a checkbox, radio, or switch.
    ToggleControl,
    /// Submit a form.
    SubmitForm,
    /// Start a download.
    StartDownload,
    /// Upload a file the user selected.
    UploadFile,
    /// Send or publish a message on the user's behalf.
    SendMessage,
    /// Complete a purchase or move funds.
    Purchase,
    /// Read a credential, secret, or authentication value out of a page.
    ExtractCredential,
    /// Defeat a bot check or an access control.
    BypassAccessControl,
    /// Run one job in an isolated, capability-free runtime the core
    /// supervises. The job reaches no page and holds no lease; its input is
    /// task material the loop already carries and its output returns as one
    /// durable completion.
    ExecuteToolJob,
    /// Read the profile-local Library without page or network authority.
    LibraryRead,
    /// Save or remove one exact-revision profile-local Library entry.
    LibraryWrite,
    /// Read active standard Memory inside the task's visible scope.
    MemoryRead,
    /// Save, update, or delete one exact-revision Memory record after exact
    /// person approval.
    MemoryWrite,
    /// Control one exact tab's browser-owned navigation lifecycle without
    /// selecting a destination or touching a renderer node.
    ControlTab,
    /// Read a person's own History, Bookmarks or open tabs, attached to the
    /// task as a store. Local and non-mutating: the browser answers bounded
    /// rows and nothing is staged or disclosed beyond the task.
    ProfileStoreRead,
}

/// Why a class is or is not on the ratified surface.
///
/// Ordered from permissive to strict so a caller can compare two classes
/// without a table of its own.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum ClassAvailability {
    /// On the read-oriented surface of milestones M2 and M3 (roadmap section
    /// 5): observation, scrolling, ordinary link activation, task tabs, and
    /// synthetic click.
    ReadOriented,
    /// Attemptable under the protocol, but no milestone has authorized it yet.
    /// A page adapter may advertise it; the decision function refuses it.
    NotYetAuthorized,
    /// Reserved by milestone M5 (form assistance, downloads, uploads).
    ///
    /// Reserved is not taken. Download initiation and exact-value field fill
    /// have browser-owned typed operations, confirmation, and verifiers, so M5
    /// names them. Select, toggle and submit cannot yet distinguish a search or
    /// filter from a message, purchase, legal acceptance or account change, so
    /// no milestone names them.
    /// [`ActionClass::UploadFile`] also stays reserved with no milestone naming
    /// it, because no assistant-controlled file-selection surface exists. This
    /// value says which milestone reserved a class, never that the class is
    /// authorized; the remaining form-assistance design is `[Open (OD-056)]`.
    WriteMilestone,
    /// Reserved for the M6 profile-local Library surface. Keeping this
    /// distinct prevents a local read from leaking into M2/M3 and a Library
    /// write from leaking into the unrelated M5 page-write allowlist.
    LibraryMilestone,
    /// Reserved for the M6 profile-local Memory surface. It is distinct from
    /// Library so either aggregate can never inherit the other's authority by
    /// sharing a broad local-storage classification.
    MemoryMilestone,
    /// Reserved for M7's isolated computation surface. A tool job remains an
    /// ordinary policy-gated action even though its worker has no page or
    /// profile authority of its own.
    ComputationMilestone,
    /// Outside the single public release until a separate explicit decision
    /// (roadmap section 8): purchase, financial transfer, message send, publish, destructive
    /// delete, account or security change.
    ExcludedFromRelease,
    /// A permanent product prohibition (threat model section 8.4, highest risk
    /// class). No milestone and no approval enables it.
    Prohibited,
}

impl ClassAvailability {
    /// Whether the read-oriented surface authorizes a class with this
    /// availability.
    ///
    /// Only [`Self::ReadOriented`] does, which is the whole surface M2 and M3
    /// share.
    ///
    /// **This is not the production gate and must not be used as one.** Since
    /// [`PolicyMilestone::M5`] exists, availability and authorization are two
    /// different questions: availability says which milestone *reserved* a
    /// class, and [`authorized_classes`] says which milestone actually *took*
    /// it. Field fill and download initiation are on the M5 allowlist; the
    /// three unclassified form mutations and [`ActionClass::UploadFile`] are not, so
    /// no predicate over availability alone can answer for that milestone.
    /// [`ActionClass::is_authorized_at`] is the answer; this is a statement
    /// about a surface, kept because two tests state what the surface used to
    /// be.
    pub const fn can_be_authorized_today(self) -> bool {
        matches!(self, Self::ReadOriented)
    }
}

impl ActionClass {
    /// Every class, in declaration order.
    ///
    /// A test walks this list, so a class added without a milestone entry
    /// fails rather than being skipped.
    pub const ALL: &'static [Self] = &[
        Self::ObservePage,
        Self::ScrollIntoView,
        Self::OpenLink,
        Self::CreateTaskTab,
        Self::SyntheticClick,
        Self::MoveFocus,
        Self::FillField,
        Self::SelectOption,
        Self::ToggleControl,
        Self::SubmitForm,
        Self::StartDownload,
        Self::UploadFile,
        Self::SendMessage,
        Self::Purchase,
        Self::ExtractCredential,
        Self::BypassAccessControl,
        Self::ExecuteToolJob,
        Self::LibraryRead,
        Self::LibraryWrite,
        Self::MemoryRead,
        Self::MemoryWrite,
        Self::ControlTab,
        Self::ProfileStoreRead,
    ];

    /// The classes milestone M5 reserves: fill, select, toggle, submit,
    /// download, upload.
    ///
    /// This list is not a synonym for "the classes M5 opens". Field fill and
    /// download initiation are on the M5 surface. Select, toggle, and submit
    /// are withheld until the browser owns a closed consequence classification,
    /// and upload has no assistant-controlled selection surface. A caller that wants the
    /// classes a milestone authorizes reads [`authorized_classes`]; this list
    /// records only the reservation.
    pub const WRITE_MILESTONE: &'static [Self] = &[
        Self::FillField,
        Self::SelectOption,
        Self::ToggleControl,
        Self::SubmitForm,
        Self::StartDownload,
        Self::UploadFile,
    ];

    /// The two classes introduced by the M6 Library surface.
    pub const LIBRARY_MILESTONE: &'static [Self] = &[Self::LibraryRead, Self::LibraryWrite];

    /// The two classes introduced by the M6 Memory surface.
    pub const MEMORY_MILESTONE: &'static [Self] = &[Self::MemoryRead, Self::MemoryWrite];

    /// How many classes there are.
    ///
    /// Paired with [`Self::ordinal`] by the compile-time check below, so a
    /// class added to the enumeration and forgotten in [`Self::ALL`] fails the
    /// build rather than escaping every table that walks it.
    pub const COUNT: usize = 23;

    /// The class's position in [`Self::ALL`].
    ///
    /// The match is exhaustive on purpose. Adding a class without deciding
    /// where it sits does not compile, and the const assertion below then
    /// refuses a class that has a position but no entry in the list.
    pub const fn ordinal(self) -> usize {
        match self {
            Self::ObservePage => 0,
            Self::ScrollIntoView => 1,
            Self::OpenLink => 2,
            Self::CreateTaskTab => 3,
            Self::SyntheticClick => 4,
            Self::MoveFocus => 5,
            Self::FillField => 6,
            Self::SelectOption => 7,
            Self::ToggleControl => 8,
            Self::SubmitForm => 9,
            Self::StartDownload => 10,
            Self::UploadFile => 11,
            Self::SendMessage => 12,
            Self::Purchase => 13,
            Self::ExtractCredential => 14,
            Self::BypassAccessControl => 15,
            Self::ExecuteToolJob => 16,
            Self::LibraryRead => 17,
            Self::LibraryWrite => 18,
            Self::MemoryRead => 19,
            Self::MemoryWrite => 20,
            Self::ControlTab => 21,
            Self::ProfileStoreRead => 22,
        }
    }

    /// The lowest risk this class can carry, before context raises it.
    ///
    /// A baseline, never a verdict. Content, destination, ambiguity, and state
    /// each raise the effective class through [`RiskClass::join`], and nothing
    /// lowers it. The milestone surface and the risk lattice are two
    /// independent gates over the same decision: a class has to pass both.
    ///
    /// This is the reading for an effect that is actually caused, which is the
    /// [`ActionPhase::Commit`] column of [`Self::baseline_risk_in`]. A caller
    /// that has not decided which phase it is asking about gets the more
    /// consequential answer.
    pub const fn baseline_risk(self) -> RiskClass {
        self.baseline_risk_in(ActionPhase::Commit)
    }

    /// The lowest risk this class can carry in one phase of its authorization
    /// (decision 0022).
    ///
    /// A total table over class and phase, and the only place a prepare is
    /// cheaper than the commit it stages. Nothing here takes a risk class and
    /// returns a lower one; the table simply states what each phase is worth,
    /// and [`RiskClass::join`] with context runs over it exactly as before.
    ///
    /// Two rows lower and the rest repeat, and each is a claim about
    /// consequence rather than about convenience:
    ///
    /// - **A staged send or purchase is a draft.** What makes those two
    ///   commitments is the irreversible act, and a prepare does not perform
    ///   it, so staging one lands where the threat model already puts a drafted
    ///   message. It is still a class no ratified milestone authorizes, which
    ///   is the point: the phase widens nothing.
    /// - **A staged write is still handling the value.** For a fill, a
    ///   selection, a toggle, a submission, a download, or an upload, the
    ///   sensitive part is the value the assistant has already chosen and
    ///   holds, not the moment it reaches the page. The phase does not make it
    ///   less sensitive, so the row does not move.
    /// - **A read has one phase.** Nothing is staged, so the reading repeats
    ///   rather than inventing a cheaper one for a request that could only ever
    ///   have been a commit.
    /// - **A prohibition has no cheap half.** Staging a credential extraction
    ///   means reading the credential, so the row does not move for it either.
    pub const fn baseline_risk_in(self, phase: ActionPhase) -> RiskClass {
        match self {
            // A tool job reads and computes over material the loop already
            // holds, inside an isolated runtime with no page lease: a local
            // read in both phases, because nothing is staged and nothing is
            // disclosed that was not already in the task.
            Self::ObservePage
            | Self::ScrollIntoView
            | Self::ExecuteToolJob
            | Self::LibraryRead
            | Self::MemoryRead
            | Self::ProfileStoreRead => RiskClass::LocalRead,
            Self::OpenLink
            | Self::CreateTaskTab
            | Self::SyntheticClick
            | Self::MoveFocus
            | Self::ControlTab => RiskClass::ReversibleDisclosure,
            Self::FillField
            | Self::SelectOption
            | Self::ToggleControl
            | Self::SubmitForm
            | Self::StartDownload
            | Self::UploadFile
            | Self::LibraryWrite
            | Self::MemoryWrite => RiskClass::SensitiveDisclosure,
            Self::SendMessage | Self::Purchase => match phase {
                ActionPhase::Prepare => RiskClass::SensitiveDisclosure,
                ActionPhase::Commit => RiskClass::ExcludedCommitment,
            },
            Self::ExtractCredential | Self::BypassAccessControl => RiskClass::ProhibitedAbuse,
        }
    }

    /// Whether an effect of this class is prepared and committed separately
    /// (decision 0022).
    ///
    /// Exactly the classes whose committed consequence needs an exact approval.
    /// A class a person is asked about is a class whose staged effect can be
    /// put in front of them first; a class nobody is ever asked about has
    /// nothing to stage and no second question to answer.
    ///
    /// Derived rather than tabulated, because a second table would be a second
    /// place for the answer to drift. The two prohibited classes are included
    /// and it costs nothing: no milestone authorizes either phase of them.
    pub const fn is_two_phase(self) -> bool {
        self.baseline_risk_in(ActionPhase::Commit)
            .requires_exact_approval()
    }

    /// A short, compiled-in name, safe to record in an audit event.
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

    /// Why this class is or is not on the ratified surface.
    pub const fn availability(self) -> ClassAvailability {
        match self {
            Self::ObservePage
            | Self::ScrollIntoView
            | Self::OpenLink
            | Self::CreateTaskTab
            | Self::SyntheticClick
            | Self::MoveFocus
            | Self::ControlTab
            | Self::ProfileStoreRead => ClassAvailability::ReadOriented,
            Self::ExecuteToolJob => ClassAvailability::ComputationMilestone,
            Self::FillField
            | Self::SelectOption
            | Self::ToggleControl
            | Self::SubmitForm
            | Self::StartDownload
            | Self::UploadFile => ClassAvailability::WriteMilestone,
            Self::LibraryRead | Self::LibraryWrite => ClassAvailability::LibraryMilestone,
            Self::MemoryRead | Self::MemoryWrite => ClassAvailability::MemoryMilestone,
            Self::SendMessage | Self::Purchase => ClassAvailability::ExcludedFromRelease,
            Self::ExtractCredential | Self::BypassAccessControl => ClassAvailability::Prohibited,
        }
    }

    /// Whether the browser already gives this class a closed meaning that
    /// cannot hide a more consequential page operation.
    ///
    /// A fill is the narrow exception: the browser binds a person's visible
    /// exact-value confirmation to one field, one supplied position, one live
    /// document and one expiry, then verifies the set-text postcondition.
    /// Select, toggle, and submit are typed mechanically but not semantically:
    /// a submit-looking control can search, send, buy, subscribe or change an
    /// account. Their protocol support therefore cannot become authorization
    /// until the browser owns and binds that closed consequence. This match is
    /// exhaustive so a new class must make the same decision explicitly.
    const fn page_consequence_is_classified(self) -> bool {
        match self {
            Self::SelectOption | Self::ToggleControl | Self::SubmitForm => false,
            Self::ObservePage
            | Self::ScrollIntoView
            | Self::OpenLink
            | Self::CreateTaskTab
            | Self::SyntheticClick
            | Self::MoveFocus
            | Self::FillField
            | Self::StartDownload
            | Self::UploadFile
            | Self::SendMessage
            | Self::Purchase
            | Self::ExtractCredential
            | Self::BypassAccessControl
            | Self::ExecuteToolJob
            | Self::LibraryRead
            | Self::LibraryWrite
            | Self::MemoryRead
            | Self::MemoryWrite
            | Self::ControlTab
            | Self::ProfileStoreRead => true,
        }
    }

    /// Whether this class changes the page or the world.
    ///
    /// A mutating class needs a current actor lease as well as a capability
    /// (threat model invariant I-04). Observation and scrolling do not mutate,
    /// but scrolling still needs a capability, because scope and budget still
    /// apply.
    pub const fn mutates(self) -> bool {
        !matches!(
            self,
            Self::ObservePage
                | Self::ScrollIntoView
                | Self::LibraryRead
                | Self::MemoryRead
                | Self::ProfileStoreRead
        )
    }

    /// Whether this class can put the browser on a new destination.
    ///
    /// The question the destination-class table asks. A click is included:
    /// activating a link is how most navigation actually happens, and a class
    /// that excluded it would be a table nobody consults.
    pub const fn navigates(self) -> bool {
        matches!(
            self,
            Self::OpenLink | Self::CreateTaskTab | Self::SyntheticClick
        )
    }

    /// Whether the capability scope names everything this class acts on.
    ///
    /// Observation and creating a tab are not node-targeted. Opening a link
    /// is either a node (clicking one on the page) or a destination origin
    /// (typing an address). Every other class needs a node.
    pub const fn scope_is_complete(self, has_node: bool, has_destination: bool) -> bool {
        match self {
            Self::ObservePage
            | Self::CreateTaskTab
            | Self::MoveFocus
            | Self::ControlTab
            | Self::LibraryRead
            | Self::LibraryWrite
            | Self::MemoryRead
            | Self::MemoryWrite
            | Self::ExecuteToolJob
            | Self::ProfileStoreRead => true,
            Self::OpenLink => has_node || has_destination,
            _ => has_node,
        }
    }

    /// Whether `milestone` authorizes this class.
    ///
    /// Membership of [`authorized_classes`] is the whole answer: there is no
    /// second path that can widen it, and no parameter that relaxes it.
    pub fn is_authorized_at(self, milestone: PolicyMilestone) -> bool {
        authorized_classes(milestone).contains(&self)
    }

    /// The class an advertised action type carries on a node with `role`.
    ///
    /// `None` means the protocol value has no product meaning on that role —
    /// activating a paragraph, for instance. The caller fails closed; it never
    /// substitutes a neighbouring class.
    ///
    /// `ACTIVATE` splits by role because its consequence splits by role: on a
    /// link it leaves the document, and on anything else it is a synthetic
    /// click within the document.
    pub const fn resolve(action_type: ActionType, role: SemanticRole) -> Option<Self> {
        match action_type {
            ActionType::Activate => match role {
                SemanticRole::Link | SemanticRole::Citation => Some(Self::OpenLink),
                SemanticRole::Button
                | SemanticRole::Checkbox
                | SemanticRole::Radio
                | SemanticRole::Option
                | SemanticRole::UnknownInteractive => Some(Self::SyntheticClick),
                _ => None,
            },
            ActionType::ScrollIntoView => Some(Self::ScrollIntoView),
            ActionType::Focus => Some(Self::MoveFocus),
            ActionType::SetText => Some(Self::FillField),
            ActionType::SelectOption => Some(Self::SelectOption),
            ActionType::Toggle => Some(Self::ToggleControl),
            ActionType::SubmitForm => Some(Self::SubmitForm),
        }
    }
}

/// The classes `milestone` authorizes.
///
/// M2 and M3 share the read-oriented surface of roadmap section 5. M5 adds
/// browser-owned exact-value fill and download initiation. The protocol also
/// represents select, toggle and submit, but representation is not
/// authorization: those classes stay absent until the browser binds a closed consequence that can
/// distinguish preparation or search from a message, purchase, legal
/// acceptance, subscription, or account change.
///
/// The list is the single place the surface is written down; a decision
/// function that wanted to widen it would have to edit this table, which is
/// exactly the review a widening deserves.
///
/// Select, toggle, submit, and `UploadFile` are absent on purpose while their
/// availability still says `WriteMilestone`.
/// `SendMessage` and `Purchase` stay outside the release, and
/// `ExtractCredential` and `BypassAccessControl` are prohibited permanently —
/// none of the four may ever appear in any arm of this function, and
/// [`invariants::no_allowlist_names_an_unavailable_class`] refuses the build if one does.
pub const fn authorized_classes(milestone: PolicyMilestone) -> &'static [ActionClass] {
    const READ_ORIENTED: &[ActionClass] = &[
        ActionClass::ObservePage,
        ActionClass::ScrollIntoView,
        ActionClass::OpenLink,
        ActionClass::CreateTaskTab,
        ActionClass::SyntheticClick,
        ActionClass::MoveFocus,
        ActionClass::ControlTab,
        ActionClass::ProfileStoreRead,
    ];
    const READ_ORIENTED_FILL_AND_DOWNLOAD: &[ActionClass] = &[
        ActionClass::ObservePage,
        ActionClass::ScrollIntoView,
        ActionClass::OpenLink,
        ActionClass::CreateTaskTab,
        ActionClass::SyntheticClick,
        ActionClass::MoveFocus,
        ActionClass::ControlTab,
        ActionClass::FillField,
        ActionClass::StartDownload,
        ActionClass::ProfileStoreRead,
    ];
    const READ_DOWNLOAD_AND_LIBRARY: &[ActionClass] = &[
        ActionClass::ObservePage,
        ActionClass::ScrollIntoView,
        ActionClass::OpenLink,
        ActionClass::CreateTaskTab,
        ActionClass::SyntheticClick,
        ActionClass::MoveFocus,
        ActionClass::ControlTab,
        ActionClass::FillField,
        ActionClass::StartDownload,
        ActionClass::LibraryRead,
        ActionClass::LibraryWrite,
        ActionClass::MemoryRead,
        ActionClass::MemoryWrite,
        ActionClass::ProfileStoreRead,
    ];
    const READ_DOWNLOAD_LIBRARY_AND_COMPUTATION: &[ActionClass] = &[
        ActionClass::ObservePage,
        ActionClass::ScrollIntoView,
        ActionClass::OpenLink,
        ActionClass::CreateTaskTab,
        ActionClass::SyntheticClick,
        ActionClass::MoveFocus,
        ActionClass::ControlTab,
        ActionClass::FillField,
        ActionClass::StartDownload,
        ActionClass::LibraryRead,
        ActionClass::LibraryWrite,
        ActionClass::MemoryRead,
        ActionClass::MemoryWrite,
        ActionClass::ExecuteToolJob,
        ActionClass::ProfileStoreRead,
    ];

    match milestone {
        PolicyMilestone::M2 | PolicyMilestone::M3 => READ_ORIENTED,
        PolicyMilestone::M5 => READ_ORIENTED_FILL_AND_DOWNLOAD,
        PolicyMilestone::M6 => READ_DOWNLOAD_AND_LIBRARY,
        PolicyMilestone::M7 => READ_DOWNLOAD_LIBRARY_AND_COMPUTATION,
    }
}

mod invariants;

#[cfg(test)]
mod tests;
