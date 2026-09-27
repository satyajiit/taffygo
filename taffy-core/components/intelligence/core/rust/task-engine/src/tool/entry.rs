// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What one registered tool name says about itself.
//!
//! Six closed enumerations and the record that carries them. They are one
//! module because they are one thing — a tool definition — and the registry
//! table beside them is nothing but a list of these values.
//!
//! None of them grants anything. The idempotency class decides what recovery
//! may do after an ambiguous dispatch, and it belongs to the definition —
//! trusted, versioned application data — never to a model's proposal.

use super::definition::{Parameter, ToolDefinition};
use super::milestone::Milestone;
/// The idempotency class of a tool (domain model section 18.2).
///
/// The class decides what recovery is allowed to do after an ambiguous
/// dispatch, so it belongs to the tool definition — trusted, versioned
/// application data — and never to a model's proposal.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum IdempotencyClass {
    /// Pure or read: a snapshot, a local retrieval.
    PureRead,
    /// Idempotent write: sets a local projection to an exact revision.
    IdempotentWrite,
    /// Conditionally idempotent: opening a known address in one task tab.
    ConditionallyIdempotent,
    /// Non-idempotent and consequential: submit, send, purchase.
    Consequential,
}

impl IdempotencyClass {
    /// Every class, from the most to the least retryable.
    pub const ALL: &'static [Self] = &[
        Self::PureRead,
        Self::IdempotentWrite,
        Self::ConditionallyIdempotent,
        Self::Consequential,
    ];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::PureRead => "pure_read",
            Self::IdempotentWrite => "idempotent_write",
            Self::ConditionallyIdempotent => "conditionally_idempotent",
            Self::Consequential => "consequential",
        }
    }

    /// What recovery may do with an attempt whose outcome is unknown.
    pub const fn recovery_rule(self) -> RecoveryRule {
        match self {
            Self::PureRead => RecoveryRule::RetryWithinEpochAndBudget,
            Self::IdempotentWrite => RecoveryRule::RetryAfterStateCheck,
            Self::ConditionallyIdempotent => RecoveryRule::ReconcileFirst,
            Self::Consequential => RecoveryRule::NeverAutomatically,
        }
    }
}

/// What may happen to an attempt whose outcome could not be confirmed.
///
/// No variant says "retry" without a qualifier, because none of them is
/// unconditional (domain model section 18.2).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum RecoveryRule {
    /// Safe to retry inside the current page epoch and the task's budgets.
    RetryWithinEpochAndBudget,
    /// Retry with the same key once the target state has been checked.
    RetryAfterStateCheck,
    /// Reconcile the tab and navigation state before deciding anything.
    ReconcileFirst,
    /// Never automatically. Reconciliation or a user decision is required.
    NeverAutomatically,
}

impl RecoveryRule {
    /// Whether the runtime may act on this rule without asking anybody.
    pub const fn permits_unattended_retry(self) -> bool {
        matches!(self, Self::RetryWithinEpochAndBudget)
    }

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::RetryWithinEpochAndBudget => "retry_within_epoch_and_budget",
            Self::RetryAfterStateCheck => "retry_after_state_check",
            Self::ReconcileFirst => "reconcile_first",
            Self::NeverAutomatically => "never_automatically",
        }
    }
}

/// What a call to one registered name actually becomes.
///
/// The tool name is what joins a model's suggestion to the authority decision
/// that follows it, so the join has to be a compiled-in table rather than
/// anything the reply carried. Decision 0052 section 3 states it as a rule and
/// this column is that rule: an `ActionClass` reaches `policy-engine` from
/// here and from nowhere else.
///
/// Four answers rather than an optional class. "No action class" is three
/// different facts — the call reaches the person instead of the page, the
/// runtime settles it inside the sandbox with no browser effect, or no
/// runtime in this build serves it at all — and a single `None` would let a
/// reviewer read any of those as the others. The first two are refusals a
/// test can enumerate; the loop case is a settlement the journal never sees;
/// none of them is a name that quietly does nothing.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ToolDispatch {
    /// One deterministic artifact rendered from accepted cited workspace facts.
    Artifact(crate::artifact::ArtifactKind),
    /// One job for an isolated, capability-free runtime the core supervises.
    /// The call becomes an ordinary proposal of class
    /// [`crate::authority::ActionClass::ExecuteToolJob`], decided by
    /// `policy-engine` exactly as every other proposal is; the effect reaches
    /// the broker, never a page.
    ToolJob(ToolRuntime),
    /// One operation over the profile-local durable Library.
    Library(LibraryTool),
    /// One operation over profile-local durable Memory.
    Memory(MemoryTool),
    /// One browser action proposal of this class, decided per action by
    /// `policy-engine` exactly as every other proposal is.
    BrowserAction(crate::authority::ActionClass),
    /// The person. The call reaches no page and spends no capability; the task
    /// stops and waits, and what the person is shown is composed from a
    /// trusted local template rather than from anything the model wrote.
    Person,
    /// Settled inside the sandbox by the runtime. No browser effect, no
    /// capability, no page lease; the walk waits for a residency outcome
    /// rather than proposing an action.
    Loop,
    /// Nothing in this build. A name registered so its refusal is enumerable —
    /// a later milestone's runtime, or one the product excludes by
    /// requirement.
    Unserved,
}

/// The closed set of isolated runtime families a tool job may name.
///
/// First-party on purpose: this crate must not depend on the generated
/// service contract, so the contract's `ToolRuntimeKind` is mapped from this
/// enum at the composition boundary — the same move the refused-call path
/// makes — and the two are kept in step by that mapping's exhaustiveness.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ToolRuntime {
    /// Media probing, extraction, sampling and transcoding.
    Media,
    /// The bundled Python interpreter.
    Python,
    /// A local model runtime.
    LocalModel,
    /// A signed WebAssembly module.
    Wasm,
}

/// The closed Library operation family exposed to the model.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum LibraryTool {
    Search,
    Save,
    Remove,
}

/// The closed Memory operation family exposed to the model.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum MemoryTool {
    Search,
    Save,
    Update,
    Delete,
}

impl MemoryTool {
    pub const fn label(self) -> &'static str {
        match self {
            Self::Search => "memory_search",
            Self::Save => "memory_save",
            Self::Update => "memory_update",
            Self::Delete => "memory_delete",
        }
    }

    pub const fn action_class(self) -> crate::authority::ActionClass {
        match self {
            Self::Search => crate::authority::ActionClass::MemoryRead,
            Self::Save | Self::Update | Self::Delete => crate::authority::ActionClass::MemoryWrite,
        }
    }
}

impl LibraryTool {
    pub const fn label(self) -> &'static str {
        match self {
            Self::Search => "library_search",
            Self::Save => "library_save",
            Self::Remove => "library_remove",
        }
    }

    pub const fn action_class(self) -> crate::authority::ActionClass {
        match self {
            Self::Search => crate::authority::ActionClass::LibraryRead,
            Self::Save | Self::Remove => crate::authority::ActionClass::LibraryWrite,
        }
    }
}

impl ToolRuntime {
    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Media => "media",
            Self::Python => "python",
            Self::LocalModel => "local_model",
            Self::Wasm => "wasm",
        }
    }
}

impl ToolDispatch {
    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Artifact(kind) => kind.label(),
            Self::BrowserAction(_) => "browser_action",
            Self::ToolJob(runtime) => runtime.label(),
            Self::Library(tool) => tool.label(),
            Self::Memory(tool) => tool.label(),
            Self::Person => "person",
            Self::Loop => "loop",
            Self::Unserved => "unserved",
        }
    }

    /// The class a proposal of this dispatch would carry, when it makes one.
    pub const fn action_class(self) -> Option<crate::authority::ActionClass> {
        match self {
            Self::BrowserAction(class) => Some(class),
            Self::ToolJob(_) => Some(crate::authority::ActionClass::ExecuteToolJob),
            Self::Library(tool) => Some(tool.action_class()),
            Self::Memory(tool) => Some(tool.action_class()),
            Self::Artifact(_) | Self::Person | Self::Loop | Self::Unserved => None,
        }
    }
}

/// When a registered name is placed on the model-facing tool list.
///
/// Immediate names are offered on compose. Deferred names stay registered and
/// findable through `tool.search`, and they join the offered set only after
/// `tool.activate` names them. The milestone filter is independent: a deferred
/// write tool the build has not reached is not findable and not activatable.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ToolLoading {
    /// Offered on compose for any task that admits the row.
    Immediate,
    /// Registered and searchable; not offered until this task activates it.
    Deferred,
}

impl ToolLoading {
    /// Every loading, in declaration order.
    pub const ALL: &'static [Self] = &[Self::Immediate, Self::Deferred];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Immediate => "immediate",
            Self::Deferred => "deferred",
        }
    }
}

/// When a name becomes usable, if ever.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ToolAvailability {
    /// Owned by a milestone, and usable from it onwards.
    From(Milestone),
    /// Excluded by requirement. No milestone and no approval enables it: no
    /// tool reads the clipboard or sends shares on the user's behalf.
    ExcludedByRequirement,
}

/// How a name is matched.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum NameMatch {
    /// The whole dotted name, exactly.
    Exact,
    /// The dotted prefix of a namespace, whose closed `members` are the full
    /// callable names this row resolves. Used where review grants one
    /// namespace while the model must still receive exact callable names.
    ///
    /// **The member list is not documentation.** This variant used to accept
    /// anything at all after the prefix and a dot, and the tool name a model
    /// asked for was recorded verbatim in the durable journal. So
    /// `browser.tabs.` followed by a hundred bytes the model wrote — a
    /// one-time code, a fragment of a page, a sentence of prose — resolved to
    /// a real row, was dispatched, and was written permanently into a record
    /// that is supposed to retain counts and closed enumerations and nothing
    /// a page or a model authored. Naming the members closes the set: a call
    /// is either one of these or it is not a call.
    Namespace {
        /// Every full callable member name this prefix claims, and no others.
        members: &'static [&'static str],
    },
}

/// One registered tool name.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ToolEntry {
    /// The internal dotted name. It never appears in user-facing copy.
    pub name: &'static str,
    /// Whether the name is exact or a namespace prefix.
    pub matching: NameMatch,
    /// When the name becomes usable, if ever.
    pub availability: ToolAvailability,
    /// Whether the name is offered on compose or only after activation.
    pub loading: ToolLoading,
    /// What recovery may do after an ambiguous dispatch.
    pub idempotency: IdempotencyClass,
    /// What a call to this name becomes: a browser proposal of one class, a
    /// stop that reaches the person, a loop settlement inside the sandbox, or
    /// nothing this build serves.
    pub dispatch: ToolDispatch,
    /// Whether a settled call of this name may end the turn without another model
    /// request, when it was the only kind of call and the reply already carried
    /// visible answer text (`answer_segments > 0`).
    ///
    /// Display-only: the tool still runs (its side effect must happen), and its
    /// result still pairs the transcript. The skip is the follow-up *model* call
    /// that would only exist so the model can read an informational ack. An
    /// ordinary tool, a mix with an ordinary tool, or a display-only tool with no
    /// visible answer still requests another turn (aster-one `terminal_safe_turn`).
    pub terminal_safe: bool,
    /// What the tool is for, in the appendix's words. This is also the line a
    /// model is shown, through [`Self::definition`], so the description has
    /// exactly one source.
    pub purpose: &'static str,
    /// The arguments the tool takes, as a compiled-in table.
    ///
    /// A `&'static [Parameter]` and not a document, because a schema the
    /// product parses is a schema something could hand it — and a tool name
    /// joins at dispatch to the `ActionClass` that `policy-engine` reads
    /// (decision 0054). The tables live in one `parameters` module, named for the
    /// shape rather than for the tool, because several rows take the same
    /// arguments.
    pub parameters: &'static [Parameter],
}

impl ToolEntry {
    /// The row-level schema used to validate a resolved call.
    ///
    /// A projection and not a second copy: the name and the description come
    /// straight off the row. What it deliberately leaves out is the milestone,
    /// the idempotency class and the matching rule — those are what the
    /// *product* reads about a call, and decision 0054 section 1 is that they
    /// come from here and never from a proposal. Model-facing enumeration uses
    /// [`Self::definitions`], which expands a namespace into exact callable
    /// names while retaining this schema.
    pub const fn definition(&self) -> ToolDefinition {
        ToolDefinition {
            name: self.name,
            description: self.purpose,
            parameters: self.parameters,
        }
    }

    /// The exact callable definitions a model may receive for this row.
    ///
    /// An exact row contributes itself. A namespace row contributes its
    /// closed, compiled-in member names rather than the uncallable namespace
    /// prefix. The description and parameter table still come from the one
    /// registry row, so expansion cannot introduce a second schema.
    pub fn definitions(&self) -> impl Iterator<Item = ToolDefinition> + '_ {
        self.callable_names().map(|name| ToolDefinition {
            name,
            description: self.purpose,
            parameters: self.parameters,
        })
    }

    /// The exact compiled-in callable names this row declares.
    pub fn callable_names(&self) -> impl Iterator<Item = &'static str> + '_ {
        let exact = match self.matching {
            NameMatch::Exact => Some(self.name),
            NameMatch::Namespace { .. } => None,
        };
        let members = match self.matching {
            NameMatch::Exact => &[][..],
            NameMatch::Namespace { members } => members,
        };
        exact.into_iter().chain(members.iter().copied())
    }

    /// Returns the registry-owned callable spelling of `requested`.
    ///
    /// This borrowed form is used by transient activation so the loop stores
    /// only a `&'static str` from the table. [`Self::canonical_name`] is the
    /// owned form used by durable proposals.
    pub fn callable_name(&self, requested: &str) -> Option<&'static str> {
        match self.matching {
            NameMatch::Exact => (self.name == requested).then_some(self.name),
            NameMatch::Namespace { members } => {
                members.iter().copied().find(|member| *member == requested)
            }
        }
    }

    /// Whether `milestone` has reached this name.
    pub fn is_available_at(&self, milestone: Milestone) -> bool {
        match self.availability {
            ToolAvailability::From(owner) => milestone >= owner,
            ToolAvailability::ExcludedByRequirement => false,
        }
    }

    /// The milestone that owns the name, when one does.
    pub const fn owning_milestone(&self) -> Option<Milestone> {
        match self.availability {
            ToolAvailability::From(owner) => Some(owner),
            ToolAvailability::ExcludedByRequirement => None,
        }
    }

    /// The compiled-in name for `requested`, or `None` when this row does not
    /// claim it.
    ///
    /// Every value this returns is assembled from `&'static str`s in the
    /// registry. That is the whole point: the caller has a string a model
    /// wrote, and what reaches an `ActionProposal` — and through it the
    /// durable journal — must be the table's own name for the thing, not the
    /// author's spelling of it. It is the same rule `idempotency` and
    /// `action_class` already follow, applied to the one field that used to
    /// be copied straight across.
    ///
    /// With [`NameMatch::Namespace`] closed to named members the two strings
    /// are necessarily equal, so this changes no behaviour today. It is here
    /// so that the property is visible where the value is produced instead of
    /// resting on `matches` staying airtight forever.
    pub fn canonical_name(&self, requested: &str) -> Option<String> {
        self.callable_name(requested).map(str::to_owned)
    }

    pub(super) fn matches(&self, name: &str) -> bool {
        self.callable_name(name).is_some()
    }
}

#[cfg(test)]
mod tests {
    use crate::tool::REGISTRY;

    #[test]
    fn every_registered_row_is_not_terminal_safe() {
        assert!(!REGISTRY.is_empty());
        for entry in REGISTRY {
            assert!(!entry.terminal_safe, "{} is terminal_safe", entry.name);
        }
    }
}
