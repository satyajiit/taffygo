// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Counting identical refusals, because a prompt cannot count.
//!
//! # Why this is state and not an instruction
//!
//! A model asked in a system prompt not to loop will still loop. The request
//! that loops looks locally reasonable every single time: the page moved, the
//! handle went stale, try again — and each individual step is the right step,
//! taken against a world that is not changing. Counting is the only thing that
//! sees the pattern, and decision 0054 section 5 puts the counter in reducer
//! state for exactly that reason.
//!
//! # What is kept, and what is deliberately not
//!
//! The register holds three things per row: a number standing for the call, a
//! compiled-in result code, and a count. Nothing here is text a caller
//! supplied. The number is a [`CallFingerprint`] — a non-cryptographic digest
//! of the tool name and what it was asked for — and it exists so that "the
//! same call again" is a `u64` comparison rather than a retained copy of the
//! arguments. It is **not** a commitment: it must never be used to prove that
//! two calls were the same to anything that would act on the claim, only to
//! notice that a task is not making progress.
//!
//! # The ladder narrows and never loosens
//!
//! The count picks a recovery, and that recovery is combined with the one the
//! result code already implied using [`Recovery::stricter_of`]. A policy denial
//! seen a second time does not become a thing worth observing the page about;
//! it becomes, at most, something to stop doing.

use bip_types::ActionResultCode;

use super::arguments::{ArgumentValue, SuppliedArgument};
use super::recovery::{recovery_for, Recovery};

/// How many identical refusals end the attempt.
///
/// Three, per decision 0054 section 5: the first is answered by the code's own
/// recovery, the second is told to ask for less, and the third is abandoned
/// and told to report what it has.
pub const MAX_IDENTICAL_REFUSALS: u32 = 3;

/// How many distinct refusals one task's register holds.
///
/// A ceiling rather than an eviction policy: a task that has produced this
/// many *different* refusals is not making progress either, and dropping the
/// oldest row would be an invitation to vary one byte per call and never be
/// counted.
///
/// A call the full register cannot track makes the abandonment sticky for the
/// whole attempt. The reducer deliberately discards [`RefusalLedger::record`]'s
/// immediate verdict after recording an outcome, so the verdict alone cannot
/// guard the next proposal. Keeping the register-full reason here makes
/// [`RefusalLedger::is_abandoned`] fail closed on every later non-exit call and
/// makes journal replay derive the same terminal condition. Unconditional exit
/// tools remain available through the reducer guard's existing exception.
pub const MAX_TRACKED_REFUSALS: usize = 64;

const FNV_OFFSET_BASIS: u64 = 0xcbf2_9ce4_8422_2325;
const FNV_PRIME: u64 = 0x0000_0100_0000_01b3;

/// Separates one field from the next, so that two different splittings of the
/// same bytes cannot produce the same number.
const FIELD_SEPARATOR: u8 = 0x1f;

/// Tells the two ways of naming a call apart. Without it a proposal's target
/// and an argument list could agree by accident, and the register would count
/// two unrelated calls as repeats of each other — which is the delivery plane's
/// position-derived identifier collision wearing a different noun.
const DOMAIN_TARGET: u8 = 0x01;
const DOMAIN_ARGUMENTS: u8 = 0x02;

/// A number standing for one exact call.
///
/// Equal fingerprints mean the register treats two calls as the same. Unequal
/// ones mean it does not. That is the whole of its contract: it is not
/// reversible into what was asked, and it is not a proof that two calls were
/// identical.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct CallFingerprint(u64);

impl CallFingerprint {
    /// The fingerprint of a call named by its tool and the thing it targets.
    ///
    /// This is the shape a proposal has by the time it reaches the reducer: a
    /// tool name, a tab, and a node. Decision 0054 section 5 counts repeats of
    /// `(tool, target, refusal code)`, and for a browser proposal the target
    /// *is* the argument list.
    pub fn of_target(tool_name: &str, tab: Option<&str>, node: Option<&str>) -> Self {
        let mut hash = mix_byte(FNV_OFFSET_BASIS, DOMAIN_TARGET);
        hash = mix_field(hash, tool_name.as_bytes());
        hash = mix_optional(hash, tab);
        hash = mix_optional(hash, node);
        Self(hash)
    }

    /// The fingerprint of a call named by its tool and its supplied arguments.
    ///
    /// Order-insensitive: the arguments are folded in name order, so a model
    /// that lists the same arguments differently is still making the same call.
    pub fn of_arguments(tool_name: &str, arguments: &[SuppliedArgument]) -> Self {
        let mut ordered: Vec<(&str, &ArgumentValue)> = arguments
            .iter()
            .map(|argument| (argument.name.as_str(), &argument.value))
            .collect();
        ordered.sort_unstable_by(|left, right| left.0.cmp(right.0));
        let mut hash = mix_byte(FNV_OFFSET_BASIS, DOMAIN_ARGUMENTS);
        hash = mix_field(hash, tool_name.as_bytes());
        for (name, value) in ordered {
            hash = mix_field(hash, name.as_bytes());
            hash = mix_value(hash, value);
        }
        Self(hash)
    }

    /// The number, for a caller that records it.
    pub const fn value(self) -> u64 {
        self.0
    }
}

const fn mix_byte(hash: u64, byte: u8) -> u64 {
    (hash ^ (byte as u64)).wrapping_mul(FNV_PRIME)
}

fn mix_field(hash: u64, bytes: &[u8]) -> u64 {
    let separated = mix_byte(hash, FIELD_SEPARATOR);
    bytes
        .iter()
        .fold(separated, |folded, byte| mix_byte(folded, *byte))
}

fn mix_optional(hash: u64, value: Option<&str>) -> u64 {
    match value {
        Some(present) => mix_field(mix_byte(hash, 1), present.as_bytes()),
        None => mix_byte(hash, 0),
    }
}

fn mix_value(hash: u64, value: &ArgumentValue) -> u64 {
    // The type tag goes in before the bytes, so a count of 49 and the text
    // "1" cannot fold to the same number. This module refuses coercion for the
    // same reason `super::arguments` does.
    let tagged = mix_field(hash, value.type_label().as_bytes());
    match value {
        ArgumentValue::Handle(number) => mix_field(tagged, &number.to_be_bytes()),
        ArgumentValue::SuppliedValue(index) => mix_field(tagged, &index.to_be_bytes()),
        ArgumentValue::Count(number) => mix_field(tagged, &number.to_be_bytes()),
        ArgumentValue::Flag(flag) => mix_byte(tagged, u8::from(*flag)),
        ArgumentValue::Text(text) | ArgumentValue::Address(text) | ArgumentValue::Choice(text) => {
            mix_field(tagged, text.as_bytes())
        }
    }
}

/// Why an attempt was abandoned.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum AbandonReason {
    /// The same call was refused the same way [`MAX_IDENTICAL_REFUSALS`] times.
    IdenticalRefusals,
    /// The register is full of distinct refusals, so this task has stopped
    /// making progress in a way the identical-refusal count cannot see.
    RegisterFull,
}

impl AbandonReason {
    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::IdenticalRefusals => "identical_refusals",
            Self::RegisterFull => "register_full",
        }
    }
}

/// What the register says about one refusal.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RepeatVerdict {
    /// Counted, and the attempt may go on under this recovery.
    Counted {
        /// How many times this exact call has been refused this way,
        /// including now.
        count: u32,
        /// What the caller may do, already narrowed by the count.
        recovery: Recovery,
    },
    /// The attempt is over.
    Abandoned(AbandonReason),
}

impl RepeatVerdict {
    /// What the caller may do next.
    pub const fn recovery(self) -> Recovery {
        match self {
            Self::Counted { recovery, .. } => recovery,
            Self::Abandoned(_) => Recovery::Abandon,
        }
    }

    /// Whether the attempt is over.
    pub const fn is_abandoned(self) -> bool {
        matches!(self, Self::Abandoned(_))
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct CountedRefusal {
    call: CallFingerprint,
    code: ActionResultCode,
    count: u32,
}

/// Every refusal one task has repeated, as counts.
///
/// Part of the reducer's state, so a rebuild from the journal re-derives it by
/// re-applying the same commands rather than by restoring a snapshot of it.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct RefusalLedger {
    counted: Vec<CountedRefusal>,
    global_abandonment: Option<AbandonReason>,
}

impl RefusalLedger {
    /// An empty register.
    pub const fn new() -> Self {
        Self {
            counted: Vec::new(),
            global_abandonment: None,
        }
    }

    /// Counts one refusal and says what may follow it.
    pub fn record(&mut self, call: CallFingerprint, code: ActionResultCode) -> RepeatVerdict {
        if let Some(reason) = self.global_abandonment {
            return RepeatVerdict::Abandoned(reason);
        }
        let existing = self
            .counted
            .iter_mut()
            .find(|row| row.call == call && row.code == code);
        let count = if let Some(row) = existing {
            row.count = row.count.saturating_add(1);
            row.count
        } else {
            if self.counted.len() >= MAX_TRACKED_REFUSALS {
                self.global_abandonment = Some(AbandonReason::RegisterFull);
                return RepeatVerdict::Abandoned(AbandonReason::RegisterFull);
            }
            self.counted.push(CountedRefusal {
                call,
                code,
                count: 1,
            });
            1
        };
        if count >= MAX_IDENTICAL_REFUSALS {
            return RepeatVerdict::Abandoned(AbandonReason::IdenticalRefusals);
        }
        RepeatVerdict::Counted {
            count,
            recovery: recovery_for(code).stricter_of(ladder(count)),
        }
    }

    /// How many times this exact call has been refused this way.
    pub fn count_of(&self, call: CallFingerprint, code: ActionResultCode) -> u32 {
        self.counted
            .iter()
            .find(|row| row.call == call && row.code == code)
            .map_or(0, |row| row.count)
    }

    /// Whether this call has been abandoned, whatever code it was refused with.
    ///
    /// A caller that has spent its three refusals on one code does not get to
    /// spend three more on the next one for the same call. Once an untracked
    /// refusal filled the register, every call is abandoned: allowing a model
    /// to vary one byte per call would otherwise turn the ceiling into the
    /// point where counting stops.
    pub fn is_abandoned(&self, call: CallFingerprint) -> bool {
        self.global_abandonment.is_some()
            || self
                .counted
                .iter()
                .any(|row| row.call == call && row.count >= MAX_IDENTICAL_REFUSALS)
    }

    /// How many distinct refusals are tracked.
    pub fn distinct(&self) -> usize {
        self.counted.len()
    }

    /// How many refusals have been counted in total.
    pub fn total(&self) -> u64 {
        self.counted
            .iter()
            .fold(0_u64, |sum, row| sum.saturating_add(u64::from(row.count)))
    }

    /// Whether the register can track nothing further.
    pub fn is_full(&self) -> bool {
        self.counted.len() >= MAX_TRACKED_REFUSALS
    }
}

/// What the count alone asks for, before the result code narrows it.
const fn ladder(count: u32) -> Recovery {
    if count <= 1 {
        Recovery::ReobserveThenRetry
    } else {
        Recovery::NarrowAndRetry
    }
}

#[cfg(test)]
mod tests {
    use super::{
        AbandonReason, CallFingerprint, RefusalLedger, RepeatVerdict, MAX_IDENTICAL_REFUSALS,
        MAX_TRACKED_REFUSALS,
    };
    use crate::tool::arguments::{ArgumentValue, SuppliedArgument};
    use crate::tool::recovery::Recovery;
    use bip_types::ActionResultCode;

    fn call() -> CallFingerprint {
        CallFingerprint::of_target("browser.dom.click", Some("tab_1"), Some("node_9"))
    }

    #[test]
    fn the_third_identical_refusal_ends_the_attempt() {
        let mut ledger = RefusalLedger::new();
        let code = ActionResultCode::NodeGone;
        assert_eq!(
            ledger.record(call(), code),
            RepeatVerdict::Counted {
                count: 1,
                recovery: Recovery::ReobserveThenRetry
            }
        );
        assert_eq!(
            ledger.record(call(), code),
            RepeatVerdict::Counted {
                count: 2,
                recovery: Recovery::NarrowAndRetry
            }
        );
        let third = ledger.record(call(), code);
        assert_eq!(
            third,
            RepeatVerdict::Abandoned(AbandonReason::IdenticalRefusals)
        );
        assert_eq!(third.recovery(), Recovery::Abandon);
        assert!(ledger.is_abandoned(call()));
        assert_eq!(ledger.count_of(call(), code), MAX_IDENTICAL_REFUSALS);
    }

    #[test]
    fn the_ladder_narrows_a_refusal_and_never_loosens_one() {
        // A policy denial will deny again. Counting it does not turn it into
        // something worth observing the page about.
        let mut ledger = RefusalLedger::new();
        let verdict = ledger.record(call(), ActionResultCode::DeniedByPolicy);
        assert_eq!(verdict.recovery(), Recovery::DoNotRetry);
        let second = ledger.record(call(), ActionResultCode::DeniedByPolicy);
        assert_eq!(second.recovery(), Recovery::DoNotRetry);
    }

    #[test]
    fn a_different_call_or_a_different_code_is_not_a_repeat() {
        let mut ledger = RefusalLedger::new();
        let other = CallFingerprint::of_target("browser.dom.click", Some("tab_1"), Some("node_8"));
        ledger.record(call(), ActionResultCode::NodeGone);
        ledger.record(other, ActionResultCode::NodeGone);
        ledger.record(call(), ActionResultCode::NotVisible);
        assert_eq!(ledger.count_of(call(), ActionResultCode::NodeGone), 1);
        assert_eq!(ledger.count_of(other, ActionResultCode::NodeGone), 1);
        assert_eq!(ledger.count_of(call(), ActionResultCode::NotVisible), 1);
        assert_eq!(ledger.distinct(), 3);
        assert_eq!(ledger.total(), 3);
    }

    #[test]
    fn a_call_abandoned_under_one_code_is_abandoned_under_every_code() {
        let mut ledger = RefusalLedger::new();
        for _ in 0..MAX_IDENTICAL_REFUSALS {
            ledger.record(call(), ActionResultCode::NodeGone);
        }
        assert!(ledger.is_abandoned(call()));
        // Spending three more refusals on a second code for the same call is
        // the obvious way around a per-code count, so the guard is per call.
        assert_eq!(ledger.count_of(call(), ActionResultCode::NotVisible), 0);
        assert!(ledger.is_abandoned(call()));
    }

    #[test]
    fn a_register_full_of_distinct_refusals_ends_the_attempt_too() {
        // Varying one byte per call is how a task evades a count of identical
        // refusals, so the register's own ceiling is a refusal rather than an
        // eviction.
        let mut ledger = RefusalLedger::new();
        for index in 0..MAX_TRACKED_REFUSALS {
            let distinct = CallFingerprint::of_target(
                "browser.dom.click",
                Some("tab_1"),
                Some(&format!("n{index}")),
            );
            assert!(!ledger
                .record(distinct, ActionResultCode::NodeGone)
                .is_abandoned());
        }
        assert!(ledger.is_full());
        let one_more =
            CallFingerprint::of_target("browser.dom.click", Some("tab_1"), Some("n_last"));
        assert_eq!(
            ledger.record(one_more, ActionResultCode::NodeGone),
            RepeatVerdict::Abandoned(AbandonReason::RegisterFull)
        );
        assert_eq!(ledger.distinct(), MAX_TRACKED_REFUSALS);
        assert!(ledger.is_abandoned(one_more));
        assert!(ledger.is_abandoned(call()));
        assert_eq!(
            ledger.record(call(), ActionResultCode::NotVisible),
            RepeatVerdict::Abandoned(AbandonReason::RegisterFull)
        );
    }

    #[test]
    fn a_full_register_still_counts_the_calls_it_already_holds() {
        let mut ledger = RefusalLedger::new();
        for index in 0..MAX_TRACKED_REFUSALS {
            let distinct = CallFingerprint::of_target(
                "browser.dom.click",
                Some("tab_1"),
                Some(&format!("n{index}")),
            );
            ledger.record(distinct, ActionResultCode::NodeGone);
        }
        let known = CallFingerprint::of_target("browser.dom.click", Some("tab_1"), Some("n0"));
        assert_eq!(
            ledger.record(known, ActionResultCode::NodeGone),
            RepeatVerdict::Counted {
                count: 2,
                recovery: Recovery::NarrowAndRetry
            }
        );
    }

    #[test]
    fn arguments_fingerprint_the_same_call_the_same_way_in_any_order() {
        let first = vec![
            SuppliedArgument::new("direction", ArgumentValue::Choice("up".to_owned())),
            SuppliedArgument::new("node", ArgumentValue::Handle(4)),
        ];
        let reordered = vec![
            SuppliedArgument::new("node", ArgumentValue::Handle(4)),
            SuppliedArgument::new("direction", ArgumentValue::Choice("up".to_owned())),
        ];
        assert_eq!(
            CallFingerprint::of_arguments("browser.dom.scroll", &first),
            CallFingerprint::of_arguments("browser.dom.scroll", &reordered)
        );
    }

    #[test]
    fn changing_anything_about_a_call_changes_its_fingerprint() {
        let base = vec![SuppliedArgument::new("node", ArgumentValue::Handle(4))];
        let baseline = CallFingerprint::of_arguments("browser.dom.click", &base);
        let other_tool = CallFingerprint::of_arguments("browser.dom.read", &base);
        let other_value = CallFingerprint::of_arguments(
            "browser.dom.click",
            &[SuppliedArgument::new("node", ArgumentValue::Handle(5))],
        );
        let other_name = CallFingerprint::of_arguments(
            "browser.dom.click",
            &[SuppliedArgument::new("within", ArgumentValue::Handle(4))],
        );
        // A count of 4 is not a handle of 4, so the type tag has to be folded
        // in before the bytes.
        let other_type = CallFingerprint::of_arguments(
            "browser.dom.click",
            &[SuppliedArgument::new("node", ArgumentValue::Count(4))],
        );
        for different in [other_tool, other_value, other_name, other_type] {
            assert_ne!(baseline, different);
        }
    }

    #[test]
    fn field_boundaries_are_not_ambiguous() {
        // Without a separator between fields, "ab" then "c" and "a" then "bc"
        // fold to the same number, and two unrelated calls become repeats of
        // each other.
        assert_ne!(
            CallFingerprint::of_target("browser.tabs", Some("ab"), Some("c")),
            CallFingerprint::of_target("browser.tabs", Some("a"), Some("bc"))
        );
        assert_ne!(
            CallFingerprint::of_target("browser.tabs", Some("tab_1"), None),
            CallFingerprint::of_target("browser.tabs", None, Some("tab_1"))
        );
    }

    #[test]
    fn the_two_ways_of_naming_a_call_never_agree() {
        // One domain tag, for the same reason the delivery plane learned to
        // stop deriving an identifier from a position: two unrelated things
        // that mint one identity are silent in both directions.
        let by_target = CallFingerprint::of_target("browser.dom.click", None, None);
        let by_arguments = CallFingerprint::of_arguments("browser.dom.click", &[]);
        assert_ne!(by_target, by_arguments);
    }

    #[test]
    fn an_empty_register_says_nothing_is_abandoned() {
        let ledger = RefusalLedger::new();
        assert!(!ledger.is_abandoned(call()));
        assert_eq!(ledger.count_of(call(), ActionResultCode::NodeGone), 0);
        assert_eq!(ledger.distinct(), 0);
        assert_eq!(ledger.total(), 0);
        assert!(!ledger.is_full());
    }
}
