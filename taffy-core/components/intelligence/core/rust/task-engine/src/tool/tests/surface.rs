// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::super::{
    available_at, is_unconditional, EffectiveToolSet, IdempotencyClass, Milestone, NameMatch,
    ToolAvailability, ToolDispatch, ToolLoading, ACTIVATE_TOOL, REGISTRY, SEARCH_TOOLS, SPAWN_RUN,
    UNCONDITIONAL_TOOLS,
};
use crate::authority::ActionClass;

#[test]
fn a_consequential_tool_is_never_retried_unattended() {
    for entry in REGISTRY {
        let unattended = entry.idempotency.recovery_rule().permits_unattended_retry();
        assert_eq!(
            unattended,
            entry.idempotency == IdempotencyClass::PureRead,
            "{} is {}",
            entry.name,
            entry.idempotency.label()
        );
    }
    assert!(!IdempotencyClass::Consequential
        .recovery_rule()
        .permits_unattended_retry());
}

#[test]
fn the_read_oriented_surface_begins_at_m3() {
    assert!(!available_at(Milestone::M3).is_empty());
    assert!(available_at(Milestone::M2).is_empty());
}

#[test]
fn every_unconditional_name_is_registered() {
    // A list beside the table can name something the table dropped, and
    // the failure would be silent in the worst direction: the guard would
    // go on admitting a name nothing resolves.
    for name in UNCONDITIONAL_TOOLS {
        assert!(
            REGISTRY.iter().any(|entry| entry.name == *name),
            "{name} is unconditional and not registered"
        );
    }
}

#[test]
fn the_escape_is_unconditional_and_asking_is_not() {
    assert!(is_unconditional("user.handover"));
    // Narrowing `user.ask` costs a capability. Narrowing the handover
    // would cost the way out, which is a different act wearing the same
    // word.
    assert!(!is_unconditional("user.ask"));
    assert!(!is_unconditional("browser.dom.read"));
}

#[test]
fn the_effective_set_at_m3_is_the_milestone_surface_itself() {
    // Pinned against the vector above rather than against a second literal
    // list, so that the two filters and the exact-count test cannot drift
    // apart: the milestone half of `EffectiveToolSet` *is* `available_at`.
    let surface: Vec<&str> = available_at(Milestone::M3)
        .iter()
        .map(|entry| entry.name)
        .collect();
    let effective = EffectiveToolSet::for_task(Milestone::M3, &[]);
    assert_eq!(effective.names(), surface);
}

#[test]
fn every_registered_row_offers_a_well_formed_definition() {
    for entry in REGISTRY {
        let definition = entry.definition();
        assert!(definition.is_well_formed(), "{}", entry.name);
        // The projection and not a second copy. A row whose definition
        // disagreed with it would be a reviewer reading one description and
        // a model reading another.
        assert_eq!(definition.name, entry.name);
        assert_eq!(definition.description, entry.purpose);
    }
}

#[test]
fn navigation_exposes_only_the_in_tab_address_operand() {
    let navigate = REGISTRY
        .iter()
        .find(|entry| entry.name == "browser.navigate")
        .unwrap_or_else(|| unreachable!("browser.navigate is registered"));
    let parameters: Vec<(&str, bool)> = navigate
        .definition()
        .parameters
        .iter()
        .map(|parameter| (parameter.name, parameter.required))
        .collect();

    assert_eq!(parameters, vec![("address", true)]);
    assert!(REGISTRY
        .iter()
        .any(|entry| entry.name == "browser.tabs.open"));
}

#[test]
fn a_name_excluded_by_requirement_describes_no_arguments() {
    // The row exists so the refusal is enumerable, not so the call can be
    // made. A schema for it would be describing the arguments of something
    // the product never does.
    for entry in REGISTRY {
        if entry.availability == ToolAvailability::ExcludedByRequirement {
            assert!(entry.parameters.is_empty(), "{}", entry.name);
        }
    }
}

#[test]
fn a_name_excluded_by_requirement_dispatches_nowhere() {
    // The row exists so the refusal is enumerable. A dispatch column
    // naming an action class for one of them would be a compiled-in join
    // from a name the product never calls to a permission `policy-engine`
    // reads, sitting in the table waiting for somebody to make the name
    // available.
    for entry in REGISTRY {
        if entry.availability == ToolAvailability::ExcludedByRequirement {
            assert_eq!(entry.dispatch, ToolDispatch::Unserved, "{}", entry.name);
        }
    }
}

#[test]
fn exactly_the_three_names_that_reach_a_person_dispatch_to_one() {
    let person: Vec<&str> = REGISTRY
        .iter()
        .filter(|entry| entry.dispatch == ToolDispatch::Person)
        .map(|entry| entry.name)
        .collect();
    // Three ways to involve a person, and they carry different things.
    // `user.ask` returns text the assistant reads; `user.request_values`
    // returns nothing the assistant can read at all, because the answer is
    // minted in the browser's vault (decision 0088); `user.handover` hands
    // over the page and returns only that it is over.
    assert_eq!(
        person,
        vec!["user.handover", "user.ask", "user.request_values"]
    );
}

/// One tool takes a CAPTCHA answer and the other does not say it does.
///
/// A purpose line is the copy the model reads at the moment it picks a tool —
/// it sits in the function schema beside the call, while the errand preamble
/// is prose at the top of the prompt. When the two disagree, this wins.
///
/// Decision 0188 took the CAPTCHA out of the preamble's handover sentence and
/// left it in this row, so the contradiction survived the fix it was the
/// subject of. A phone paid for it twice: on 2026-09-18 a model planned to ask
/// for the eAadhaar number, read that a CAPTCHA is handed over, and handed
/// over the whole form; on 2026-09-19 errand `7b367bd8` reached the same form
/// with the CAPTCHA on screen and called `user.handover` without ever trying
/// `user.request_values` (decision 0209).
#[test]
fn only_the_tool_that_takes_a_captcha_answer_mentions_one() {
    let purpose = |name: &str| {
        let Some(entry) = REGISTRY.iter().find(|entry| entry.name == name) else {
            panic!("{name} is a registered row")
        };
        entry.purpose.to_lowercase()
    };
    let handover = purpose("user.handover");
    let request_values = purpose("user.request_values");
    assert!(
        !handover.contains("captcha") || handover.contains("user.request_values"),
        "user.handover must not offer itself for a CAPTCHA without naming the tool that \
         does take one: {handover}"
    );
    assert!(
        request_values.contains("otp") || request_values.contains("captcha"),
        "user.request_values must say what kind of value it is for: {request_values}"
    );
}

#[test]
fn exactly_the_four_loop_tools_dispatch_locally() {
    let loop_tools: Vec<&str> = REGISTRY
        .iter()
        .filter(|entry| entry.dispatch == ToolDispatch::Loop)
        .map(|entry| entry.name)
        .collect();
    assert_eq!(
        loop_tools,
        vec![
            crate::tool::TABLE_RESHAPE_TOOL,
            SEARCH_TOOLS,
            ACTIVATE_TOOL,
            SPAWN_RUN
        ]
    );
    for name in [SEARCH_TOOLS, ACTIVATE_TOOL, SPAWN_RUN] {
        let Some(entry) = REGISTRY.iter().find(|entry| entry.name == name) else {
            unreachable!("{name} is registered");
        };
        assert_eq!(entry.loading, ToolLoading::Immediate, "{name}");
        assert_eq!(entry.matching, NameMatch::Exact, "{name}");
        assert_eq!(
            entry.availability,
            ToolAvailability::From(Milestone::M3),
            "{name}"
        );
    }
    let table = REGISTRY
        .iter()
        .find(|entry| entry.name == crate::tool::TABLE_RESHAPE_TOOL)
        .unwrap_or_else(|| unreachable!("the native table tool is registered"));
    assert_eq!(table.loading, ToolLoading::Deferred);
    assert_eq!(table.availability, ToolAvailability::From(Milestone::M7));
}

#[test]
fn loading_is_immediate_except_for_exact_deferred_rows() {
    for entry in REGISTRY {
        let expected = if matches!(
            entry.name,
            "browser.tabs.list" | "browser.tabs.activate" | "browser.tabs.close"
        ) || !matches!(entry.availability, ToolAvailability::From(Milestone::M3))
        {
            ToolLoading::Deferred
        } else {
            ToolLoading::Immediate
        };
        assert_eq!(entry.loading, expected, "{}", entry.name);
    }
}

#[test]
fn a_read_only_class_never_sits_beside_a_consequential_row() {
    // The two columns are read together at dispatch: the class decides
    // what `policy-engine` is asked, and the idempotency class decides
    // what recovery may do about an ambiguous attempt. A row that observed
    // a page and was marked consequential, or one that submitted a form
    // and was marked a pure read, would be a single-line typo that reads
    // as deliberate — and the pure-read half is the dangerous direction,
    // because its recovery rule is the only one permitting an unattended
    // retry.
    for entry in REGISTRY {
        let Some(class) = entry.dispatch.action_class() else {
            continue;
        };
        let reads_only = matches!(
            class,
            ActionClass::ObservePage | ActionClass::ScrollIntoView
        );
        assert!(
            !reads_only || entry.idempotency != IdempotencyClass::Consequential,
            "{} observes and is consequential",
            entry.name
        );
        let writes = matches!(
            class,
            ActionClass::FillField
                | ActionClass::SelectOption
                | ActionClass::ToggleControl
                | ActionClass::SubmitForm
                | ActionClass::StartDownload
                | ActionClass::UploadFile
                | ActionClass::SendMessage
                | ActionClass::Purchase
        );
        assert!(
            !writes || entry.idempotency != IdempotencyClass::PureRead,
            "{} writes and is a pure read",
            entry.name
        );
    }
}

#[test]
fn the_escape_takes_no_text_from_the_model() {
    // What a person is shown at a handover is composed from a trusted local
    // template. A free-text parameter here would be model text rendered to
    // a person at the exact moment the product is admitting it cannot
    // proceed.
    let Some(handover) = REGISTRY.iter().find(|entry| entry.name == "user.handover") else {
        unreachable!("the handover is registered")
    };
    for parameter in handover.parameters {
        assert!(
            !parameter.value_type.choices().is_empty(),
            "{} takes free text",
            parameter.name
        );
    }
}
