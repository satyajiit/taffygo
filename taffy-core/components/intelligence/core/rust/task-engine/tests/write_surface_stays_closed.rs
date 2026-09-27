// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The protocol can express a write. The product still refuses one.
//!
//! Those two sentences are about different things, and the whole point of
//! this file is that they are both true at once and stay that way. BIP 0.8
//! stopped reserving `SET_TEXT`, `SELECT_OPTION`, `TOGGLE` and `SUBMIT_FORM`:
//! the contract now says what each one does and the renderer performs each
//! through the platform accessibility path. Nothing about that is permission.
//! The four `browser.form.*` mutations are implemented at milestone M5, but
//! generic production tasks do not receive them and the independent policy
//! class surface refuses them at every current milestone until the browser
//! supplies a closed consequence classification.
//!
//! The reason this is a file of its own rather than another case beside the
//! registry's unit tests is the failure it exists to catch. Un-reserving is
//! exactly the kind of change that arrives with a small, reasonable-looking
//! adjustment somewhere else — an allowlist widened "to match the protocol",
//! a milestone moved "now that it works". Both assertions live here together
//! so that a change which makes the first sentence stronger has to walk past
//! the second.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

use bip_types::action::{ActionInputKind, ActionType, PostconditionKind};
use bip_types::version::{ClosedEnum, ProtocolVersion};
use task_engine::tool::{resolve, EffectiveToolSet, Milestone, ToolLookup};

/// Every tool name that would carry one of the four writing operations.
const WRITE_TOOLS: &[&str] = &[
    "browser.form.fill",
    "browser.form.select",
    "browser.form.toggle",
    "browser.form.submit",
];

#[test]
fn the_protocol_can_name_every_writing_operation() {
    // The half that changed. A member decoding here is the mechanism existing;
    // it says nothing at all about whether anything may ask for it.
    for wire in ["SET_TEXT", "SELECT_OPTION", "TOGGLE", "SUBMIT_FORM"] {
        assert!(
            ActionType::decode_wire(wire).known().is_some(),
            "{wire} is not a member of ActionType"
        );
    }
    // A write that could not carry its input or declare its effect would not
    // be expressible, so these moved with it.
    for wire in ["TEXT", "OPTION", "TOGGLE_STATE"] {
        assert!(
            ActionInputKind::decode_wire(wire).known().is_some(),
            "{wire}"
        );
    }
    assert!(PostconditionKind::decode_wire("NODE_VALUE_CHANGED")
        .known()
        .is_some());

    // BROWSER_FLOW_STARTED is still a member of the enumeration — it always
    // was — and is still reserved in the schema. A download, a file chooser
    // and a permission prompt are consequences of their own, and going
    // through a page's submit control is not a licence to start one.
    assert!(PostconditionKind::decode_wire("BROWSER_FLOW_STARTED")
        .known()
        .is_some());
}

#[test]
fn the_protocol_step_that_opened_the_shape_was_a_minor_one() {
    // If un-reserving had cost a major version it would have been a different
    // change: a reader at the previous major refuses a message outright, and
    // every stored observation would have needed re-reading. It did not,
    // because the four members were on the wire from 0.1 — which is also why
    // no compatibility fixture claims an older reader refuses one.
    let current = ProtocolVersion::current().unwrap();
    assert!(current > ProtocolVersion::new(0, 7));
    assert!(current.is_compatible_with(ProtocolVersion::new(0, 7)));
    assert_eq!(current.major, 0);
}

#[test]
fn no_form_write_tool_is_available_before_its_implementation_milestone() {
    // Tool availability describes implementation, not authorization. This
    // assertion keeps the shape absent before M5; the policy suite separately
    // proves that reaching M5 does not open its consequence class.
    for name in WRITE_TOOLS {
        match resolve(name, Milestone::M3) {
            ToolLookup::Unavailable { available_from, .. } => {
                assert_eq!(available_from, Milestone::M5, "{name}");
            }
            other => unreachable!("{name} resolved to {other:?} at M3"),
        }
        assert!(!resolve(name, Milestone::M3).is_available(), "{name}");
    }
}

#[test]
fn a_write_tool_is_refused_at_every_milestone_before_the_one_that_owns_it() {
    // Walked rather than spot-checked, so that moving a milestone boundary
    // cannot leave a write quietly reachable from somewhere earlier than M5.
    for name in WRITE_TOOLS {
        for milestone in Milestone::ALL {
            let available = resolve(name, *milestone).is_available();
            assert_eq!(
                available,
                *milestone >= Milestone::M5,
                "{name} at {milestone:?}"
            );
        }
    }
}

#[test]
fn activating_fill_at_m3_does_not_offer_it() {
    let set = EffectiveToolSet::for_task(Milestone::M3, &[]).with_activated(&["browser.form.fill"]);
    assert!(!set
        .offered()
        .iter()
        .any(|entry| entry.name == "browser.form.fill"));
    assert!(!resolve("browser.form.fill", Milestone::M3).is_available());
}

#[test]
fn the_refusal_names_the_milestone_rather_than_denying_the_name_exists() {
    // A registered-but-later name and an unregistered name are different
    // answers on purpose. Reporting "no such tool" for a name the product
    // will one day have invites a model to go looking for a synonym, and a
    // synonym for filling a form is a worse path than the one being refused.
    assert!(matches!(
        resolve("browser.form.fill", Milestone::M3),
        ToolLookup::Unavailable { .. }
    ));
    assert!(matches!(
        resolve("browser.form.enchant", Milestone::M3),
        ToolLookup::Unknown
    ));
}
