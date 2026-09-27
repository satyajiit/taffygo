// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The typed tool registry (domain model appendix "Tool namespace").
//!
//! # Registration is not authorization
//!
//! A name in this table says the product has a definition for it and knows
//! which milestone owns it. It says nothing about whether a task may use it:
//! that is `policy-engine`'s decision, per action, after capability and risk
//! checks. A tool that resolves to [`ToolLookup::Available`] here is still
//! refused at dispatch if its action class is off the ratified surface.
//!
//! # Every name exists, including the refused ones
//!
//! Later-milestone tools and the two the product excludes by requirement are
//! present as values. A refusal spelled out in the table is enumerable by a
//! test and visible in review; a refusal expressed by leaving a name out is
//! invisible until somebody adds it back. That is the same rule
//! `policy-engine` applies to action classes.
//!
//! # How this module is laid out
//!
//! | Module | Owns |
//! |---|---|
//! | [`milestone`] | The build ladder a name is owned by |
//! | [`entry`] | What one tool definition says about itself |
//! | [`registry`] | The table of every registered name |
//! | [`definition`] | The row a model is shown: name, description, parameters |
//! | [`parameters`] | The compiled-in parameter tables the rows point at |
//! | [`effective`] | What one task may actually call |
//! | [`arguments`] | Whether a set of supplied arguments matches a schema |
//! | [`outcome`] | What a call produced: something, nothing, or a refusal |
//! | [`recovery`] | What may follow a refusal, and never an unqualified retry |
//! | [`repetition`] | Counting identical refusals, because a prompt cannot |
//!
//! The two functions here are the only rules the *registry* has: resolving a
//! name against the milestone the build has reached, and listing what that
//! milestone has.
//!
//! # One name that is not re-exported at the crate root
//!
//! [`recovery::Recovery`] is reachable as `tool::Recovery` and nowhere else.
//! `crate::Recovery` is [`crate::reducer::Recovery`], which is what a rebuild
//! from the journal concluded — a different subject that has held the name
//! since before this module existed. Two types called `Recovery` at the crate
//! root would be one import away from a caller that read the wrong one and
//! compiled.

mod arguments;
mod definition;
mod effective;
mod entry;
mod job;
mod milestone;
mod outcome;
mod parameters;
mod recovery;
mod registry;
mod repetition;
pub(crate) mod semantic;

pub use self::arguments::{
    validate, ArgumentRefusal, ArgumentRefusalReason, ArgumentValue, SuppliedArgument,
    MAX_ARGUMENT_VALUE_BYTES, MAX_SUPPLIED_ARGUMENTS,
};
pub use self::definition::{Parameter, ParameterType, ToolDefinition};
pub use self::effective::EffectiveToolSet;
pub use self::entry::{
    IdempotencyClass, LibraryTool, MemoryTool, NameMatch, RecoveryRule, ToolAvailability,
    ToolDispatch, ToolEntry, ToolLoading, ToolRuntime,
};
pub use self::job::{job_id_for_action, ToolJobOutcome, ToolJobStatus};
pub use self::milestone::Milestone;
pub use self::outcome::{EmptyReason, ToolOutcome};
pub use self::recovery::{permits_unattended_attempt, recovery_for, Recovery};
pub use self::registry::REGISTRY;
pub use self::repetition::{
    AbandonReason, CallFingerprint, RefusalLedger, RepeatVerdict, MAX_IDENTICAL_REFUSALS,
    MAX_TRACKED_REFUSALS,
};
pub(crate) use self::semantic::call_operands_are_valid;
pub use self::semantic::MAX_DOM_QUERY_RESULTS;

/// What the registry says about a name.
///
/// There is no variant that means "probably fine". An unregistered name is
/// [`Self::Unknown`] and the caller fails closed; it never resolves to the
/// nearest registered neighbour.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ToolLookup {
    /// The current milestone has reached this name. It is still subject to
    /// every per-action capability and risk check.
    Available(&'static ToolEntry),
    /// Registered, but owned by a later milestone.
    Unavailable {
        /// The registered entry.
        entry: &'static ToolEntry,
        /// The milestone that owns it.
        available_from: Milestone,
    },
    /// Registered and excluded by requirement. No milestone enables it.
    Excluded(&'static ToolEntry),
    /// Not a registered name.
    Unknown,
}

impl ToolLookup {
    /// Whether the name may be proposed at all.
    pub const fn is_available(self) -> bool {
        matches!(self, Self::Available(_))
    }

    /// The registered entry, when the name is registered.
    pub const fn entry(self) -> Option<&'static ToolEntry> {
        match self {
            Self::Available(entry) | Self::Excluded(entry) | Self::Unavailable { entry, .. } => {
                Some(entry)
            }
            Self::Unknown => None,
        }
    }

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Available(_) => "available",
            Self::Unavailable { .. } => "unavailable",
            Self::Excluded(_) => "excluded",
            Self::Unknown => "unknown",
        }
    }
}

/// Resolves an internal tool name against the milestone the build has reached.
///
/// Being [`ToolLookup::Available`] is necessary and not sufficient: the action
/// a tool proposes still goes to `policy-engine`, which decides it per action
/// against the ratified action surface.
pub fn resolve(name: &str, milestone: Milestone) -> ToolLookup {
    let Some(entry) = REGISTRY.iter().find(|entry| entry.matches(name)) else {
        return ToolLookup::Unknown;
    };
    match entry.availability {
        ToolAvailability::ExcludedByRequirement => ToolLookup::Excluded(entry),
        ToolAvailability::From(owner) if milestone >= owner => ToolLookup::Available(entry),
        ToolAvailability::From(owner) => ToolLookup::Unavailable {
            entry,
            available_from: owner,
        },
    }
}

/// The names a narrowed allowlist may not remove.
///
/// A skill or a learned procedure narrows the tool surface (decision 0055
/// section 3), and narrowing is safe precisely because it can only take
/// authority away. That argument fails for exactly one name. `user.handover`
/// is not a capability the task spends; it is the way the task *stops* and
/// gives the page back to the person, and decision 0054 section 6 makes it the
/// thing that remains once everything else has been refused. Taffy never
/// learns to recognise a challenge meant to prove a person is present —
/// `BypassAccessControl` and `ExtractCredential` are prohibited by class, so
/// the challenge, the one-time code and the password are all refused before
/// anything looks at them. Refusal by exhaustion has no false negatives, but
/// it only works while something is left at the end of the exhaustion.
///
/// A procedure that simply forgot to list `user.handover` would therefore
/// strand every task it ran on: the model would be refused each remaining
/// tool, reach for the escape, and be refused that too — with no way to say so
/// to the person whose page it is holding. Removing an escape is not a
/// narrowing, and this is where the two are told apart.
///
/// `user.ask` is deliberately **not** here. Narrowing it costs a capability
/// and not the exit: a task that may not ask can still hand back. Only the
/// terminal step is unconditional, because only the terminal step has nothing
/// after it.
///
/// A list rather than a column on [`ToolEntry`]. Thirty-one rows saying "no"
/// to describe one row is worse to review than one line and this paragraph,
/// and `every_unconditional_name_is_registered` keeps the list from naming
/// something the table does not.
pub const UNCONDITIONAL_TOOLS: &[&str] = &[HANDOVER_TOOL];

/// The one registered name that stops the task and gives the page to a person.
///
/// A constant rather than a literal at each site because two places now depend
/// on the exact spelling for different reasons: this list, and the agent loop,
/// which turns this row and no other into `Command::RequestHandover`. Reading
/// the name off [`ToolEntry::name`] rather than off the model's reply is what
/// makes that safe — the registry's `&'static str` is the only spelling that
/// can reach the comparison.
pub const HANDOVER_TOOL: &str = "user.handover";

/// The registered name that waits for a value from the person.
pub const ASK_TOOL: &str = "user.ask";

/// The registered name that finds deferred tools by name or purpose.
pub const SEARCH_TOOLS: &str = "tool.search";

/// The registered name that loads one deferred tool into this task.
pub const ACTIVATE_TOOL: &str = "tool.activate";

/// The registered name that starts a nested pass under this task's identity.
pub const SPAWN_RUN: &str = "run.spawn";

/// The registered native table transform executed inside the portable core.
pub const TABLE_RESHAPE_TOOL: &str = "core.table.reshape";

/// How many nested passes one task may hold at once.
///
/// One, because nested work is bounded parallel work under the same task
/// identity (decision 0009 point 4), not a second assistant. A further spawn
/// is refused rather than stacked.
pub const MAX_NESTED_DEPTH: u32 = 1;

/// The registered name for in-tab navigation on the consented origin.
pub const NAVIGATE_TOOL: &str = "browser.navigate";

/// The allowlist group shared by the four independently typed tab rows.
pub const TABS_ALLOWLIST_GROUP: &str = "browser.tabs";

/// The allowlist group a start request names to attach the person's History.
pub const HISTORY_ALLOWLIST_GROUP: &str = "person.history";
/// The allowlist group a start request names to attach the person's Bookmarks.
pub const BOOKMARKS_ALLOWLIST_GROUP: &str = "person.bookmarks";
/// The allowlist group a start request names to attach the person's open tabs.
pub const OPEN_TABS_ALLOWLIST_GROUP: &str = "person.open_tabs";

/// The stable allowlist identity of one exact registry row.
///
/// Tab members have different schemas and authority classes, but existing
/// task templates consent to their reviewed group as one surface. This maps
/// only those compiled-in names; an invented namespace member resolves
/// nowhere before this function can matter.
///
/// A person's store is attached by its group (decision 0133): the two
/// History rows share one, the two Bookmarks rows another, and the open-tabs
/// row its own, so a start request that did not attach a store leaves its
/// rows off the effective set.
pub const fn allowlist_name(tool_name: &str) -> &str {
    match tool_name.as_bytes() {
        b"browser.tabs.open"
        | b"browser.tabs.list"
        | b"browser.tabs.activate"
        | b"browser.tabs.close" => TABS_ALLOWLIST_GROUP,
        b"history.search" | b"history.recent" => HISTORY_ALLOWLIST_GROUP,
        b"bookmarks.search" | b"bookmarks.list" => BOOKMARKS_ALLOWLIST_GROUP,
        b"open_tabs.list" => OPEN_TABS_ALLOWLIST_GROUP,
        _ => tool_name,
    }
}

/// Whether `tool_name` survives every narrowing.
pub fn is_unconditional(tool_name: &str) -> bool {
    UNCONDITIONAL_TOOLS.contains(&tool_name)
}

/// Every name `milestone` has reached, in registration order.
pub fn available_at(milestone: Milestone) -> Vec<&'static ToolEntry> {
    REGISTRY
        .iter()
        .filter(|entry| entry.is_available_at(milestone))
        .collect()
}

#[cfg(test)]
mod tests;
