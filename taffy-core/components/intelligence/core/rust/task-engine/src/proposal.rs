// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The identity of one proposal bound to one plan step over one consented
//! source.
//!
//! # Why this is one construction and not one per caller
//!
//! Two paths reach the same page for the same reason. [`crate::workflow`] runs
//! the reviewed local `BuildSourceTable` sequence from hard-coded logic, and
//! `procedure-engine` replays a stored procedure that reproduces it. Decision
//! 0055 section 7 requires those two to emit **identical** commands — that is
//! what the conformance oracle asserts, and what lets the hard-coded workflow
//! be deleted afterwards rather than kept beside a second implementation of
//! itself.
//!
//! Two spellings of one proposal's identity would put that out of reach for
//! reasons that have nothing to do with whether the two paths agree about
//! anything worth checking. The digest is the field a capability is bound to,
//! and the idempotency key is the effect identity the browser's journal
//! refuses a second time; a difference in either is a difference in the
//! `Command`, whatever the sequencing did. So the canonicalisation lives here
//! once and both callers assemble the rest of the proposal themselves — which
//! keeps the choices worth testing (which tool, which step, which attempt,
//! required or not) on each caller's own side of the comparison.
//!
//! This is the same argument [`hex_digest`] was already shared under: two
//! spellings of one thirty-two bytes would be two digests for one proposal.
//!
//! # The namespace names what the effect is about
//!
//! [`plan_step_key`] takes a namespace, the source, the plan step and the
//! attempt, and every one of those four is part of what the effect *is* rather
//! than where it landed in a batch. A procedure may carry several reads of one
//! source, so a key that named only the source would give two different
//! effects one identity — the browser journals an effect id before dispatching
//! it and refuses one it has already journaled, so the second read would never
//! reach the page and nothing would say so.
//!
//! The reviewed workflow passes [`crate::workflow::REVIEWED_EFFECT_NAMESPACE`];
//! a replayed procedure passes its own identifier. A built-in procedure whose
//! identifier equals the reviewed namespace therefore mints the *same* key for
//! the same read, which is what stops a task that started under the hard-coded
//! path and finished under the procedure path reading its source twice.
//! `procedure-engine`'s built-in asserts that equality rather than assuming it.

use crate::action::ActionIntent;
use crate::ids::{IdempotencyKey, PlanStepId};
use crate::task::ConsentedSource;

/// The domain separator every plan-step proposal's material starts with.
///
/// A proposal bound to a plan step is one kind of thing, and the separator
/// names that kind rather than the caller that built it. The agent loop's
/// per-turn calls are a different kind and carry their own separator, because
/// a turn call is identified by the reply it came from and has no plan step at
/// all.
pub const PLAN_STEP_PROPOSAL_DOMAIN: &[u8] = b"taffy.plan-step.proposal.v1";

/// The idempotency key of the `attempt`-th try at `plan_step_id` over
/// `source`.
///
/// `attempt` is one-based, so the first try is `1`. Every component names
/// something about the effect: what sequence it belongs to, which source it
/// reads, which step of that sequence it is, and which try.
pub fn plan_step_key(
    namespace: &str,
    source: &ConsentedSource,
    plan_step_id: &PlanStepId,
    attempt: usize,
) -> IdempotencyKey {
    IdempotencyKey::new(format!(
        "{namespace}-{}-{}-{attempt}",
        source.source_id.to_text(),
        plan_step_id.as_str()
    ))
}

/// The bounded canonical material one plan-step proposal's digest is taken
/// over.
///
/// Length-prefixed field by field, so two different splittings of the same
/// bytes cannot produce the same digest. It names the sequence, the task, the
/// registered tool, the source, the tab, the origin, the step and the key —
/// everything that makes this proposal this proposal, and no text a model
/// composed.
///
/// `None` means a field longer than `u32::MAX` bytes, which cannot be
/// length-prefixed. Each caller maps that to its own refusal rather than this
/// module inventing an error type two crates would have to name.
pub fn plan_step_material(
    namespace: &str,
    task_id: &str,
    tool_name: &str,
    source: &ConsentedSource,
    plan_step_id: &PlanStepId,
    key: &IdempotencyKey,
) -> Option<Vec<u8>> {
    let mut out = PLAN_STEP_PROPOSAL_DOMAIN.to_vec();
    let source_id = source.source_id.to_text();
    for value in [
        namespace,
        task_id,
        tool_name,
        source_id.as_str(),
        source.tab_id.as_str(),
        source.normalized_origin.as_str(),
        plan_step_id.as_str(),
        key.as_str(),
    ] {
        let length = u32::try_from(value.len()).ok()?;
        out.extend_from_slice(&length.to_le_bytes());
        out.extend_from_slice(value.as_bytes());
    }
    Some(out)
}

/// Plan-step proposal material bound to the full canonical action intent.
///
/// This is the digest seam shared by reviewed workflows and procedure replay.
/// The fixed suffix preserves the original v1 fields and adds the canonical
/// typed operation without either caller learning its encoding.
pub fn plan_action_material(
    namespace: &str,
    task_id: &str,
    source: &ConsentedSource,
    plan_step_id: &PlanStepId,
    key: &IdempotencyKey,
    intent: &ActionIntent,
) -> Option<Vec<u8>> {
    let mut out = plan_step_material(
        namespace,
        task_id,
        intent.tool_name(),
        source,
        plan_step_id,
        key,
    )?;
    let intent_bytes = intent.encode_canonical().ok()?;
    let intent_length = u64::try_from(intent_bytes.len()).ok()?;
    out.extend_from_slice(b"\x1fintent");
    out.extend_from_slice(&intent_length.to_le_bytes());
    out.extend_from_slice(&intent_bytes);
    Some(out)
}

/// Lowercase hexadecimal, for a digest that goes into a proposal.
///
/// Shared by every path that mints a proposal rather than written once each:
/// two spellings of the same thirty-two bytes would be two proposal digests
/// for one proposal, and a capability is bound to exactly one of them.
pub fn hex_digest(bytes: &[u8; 32]) -> String {
    const HEX: &[u8; 16] = b"0123456789abcdef";
    let mut output = String::with_capacity(64);
    for byte in bytes {
        let high = HEX.get(usize::from(byte >> 4)).copied().unwrap_or(b'0');
        let low = HEX.get(usize::from(byte & 0x0f)).copied().unwrap_or(b'0');
        output.push(char::from(high));
        output.push(char::from(low));
    }
    output
}

#[cfg(test)]
mod tests {
    use super::{
        hex_digest, plan_action_material, plan_step_key, plan_step_material,
        PLAN_STEP_PROPOSAL_DOMAIN,
    };
    use crate::action::{ActionIntent, BrowserIntent};
    use crate::ids::PlanStepId;
    use crate::records::SourceId;
    use crate::task::ConsentedSource;
    use bip_types::identity::TabId;

    fn source() -> ConsentedSource {
        ConsentedSource {
            source_id: SourceId::from_bytes([3; 16]),
            tab_id: TabId::new("tab-live"),
            normalized_origin: "https://example.test".to_owned(),
            canonical_locator: None,
        }
    }

    fn step() -> PlanStepId {
        PlanStepId::new("plan-step-1")
    }

    #[test]
    fn a_key_names_the_sequence_the_source_the_step_and_the_attempt() {
        let key = plan_step_key("source-table", &source(), &step(), 1);
        let text = key.as_str();
        assert!(text.starts_with("source-table-"), "{text}");
        assert!(text.contains(SourceId::from_bytes([3; 16]).to_text().as_str()));
        assert!(text.ends_with("-plan-step-1-1"), "{text}");
    }

    #[test]
    fn two_steps_of_one_sequence_over_one_source_never_share_a_key() {
        // The browser journals an effect id before dispatching and refuses one
        // it has already journaled, so a shared key is not a duplicate record
        // — it is a step that never reaches the page, with nothing saying so.
        let first = plan_step_key("proc", &source(), &PlanStepId::new("step-a"), 1);
        let second = plan_step_key("proc", &source(), &PlanStepId::new("step-b"), 1);
        assert_ne!(first, second);
        // And a retry of one step is a different effect from its first try.
        assert_ne!(first, plan_step_key("proc", &source(), &step(), 2));
    }

    #[test]
    fn two_sequences_over_one_source_never_share_a_key() {
        assert_ne!(
            plan_step_key("source-table", &source(), &step(), 1),
            plan_step_key("other-table", &source(), &step(), 1)
        );
    }

    #[test]
    fn the_material_is_length_prefixed_so_a_resplit_cannot_collide() {
        // Without the lengths, ("ab", "c") and ("a", "bc") would be one
        // message, and a digest is the only thing standing between a proposal
        // and the capability minted for it.
        let key = plan_step_key("ns", &source(), &step(), 1);
        let Some(first) =
            plan_step_material("ab", "c", "browser.dom.read", &source(), &step(), &key)
        else {
            unreachable!("bounded fixture fields are length-prefixable")
        };
        let Some(second) =
            plan_step_material("a", "bc", "browser.dom.read", &source(), &step(), &key)
        else {
            unreachable!("bounded fixture fields are length-prefixable")
        };
        assert_ne!(first, second);
        assert!(first.starts_with(PLAN_STEP_PROPOSAL_DOMAIN));
    }

    #[test]
    fn the_tool_name_is_part_of_what_a_proposal_is() {
        let key = plan_step_key("ns", &source(), &step(), 1);
        assert_ne!(
            plan_step_material("ns", "task", "browser.dom.read", &source(), &step(), &key),
            plan_step_material("ns", "task", "browser.dom.query", &source(), &step(), &key)
        );
    }

    #[test]
    fn the_full_typed_intent_is_part_of_plan_action_material() {
        let key = plan_step_key("ns", &source(), &step(), 1);
        let first = ActionIntent::Browser(BrowserIntent::DomRead {
            tab: source().tab_id,
            target: None,
        });
        let second = ActionIntent::Browser(BrowserIntent::SelectionRead {
            tab: source().tab_id,
        });
        assert_ne!(
            plan_action_material("ns", "task", &source(), &step(), &key, &first),
            plan_action_material("ns", "task", &source(), &step(), &key, &second)
        );
    }

    #[test]
    fn hexadecimal_is_lowercase_and_sixty_four_characters() {
        let mut bytes = [0_u8; 32];
        bytes[0] = 0xab;
        bytes[31] = 0x0f;
        let text = hex_digest(&bytes);
        assert_eq!(text.len(), 64);
        assert!(text.starts_with("ab"));
        assert!(text.ends_with("0f"));
        assert!(text
            .chars()
            .all(|c| c.is_ascii_hexdigit() && !c.is_uppercase()));
    }
}
