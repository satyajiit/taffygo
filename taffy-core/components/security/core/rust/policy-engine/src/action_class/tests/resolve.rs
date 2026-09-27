// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! From a protocol action and a role to a class, and whether its scope is complete.

use crate::action_class::{ActionClass, ClassAvailability, PolicyMilestone};
use bip_types::action::ActionType;
use bip_types::snapshot::SemanticRole;

#[test]
fn activation_splits_by_role_and_refuses_a_role_it_has_no_meaning_on() {
    assert_eq!(
        ActionClass::resolve(ActionType::Activate, SemanticRole::Link),
        Some(ActionClass::OpenLink)
    );
    assert_eq!(
        ActionClass::resolve(ActionType::Activate, SemanticRole::Button),
        Some(ActionClass::SyntheticClick)
    );
    assert_eq!(
        ActionClass::resolve(ActionType::Activate, SemanticRole::Paragraph),
        None
    );
}

#[test]
fn form_action_types_resolve_but_only_exact_fill_is_authorized() {
    // Protocol 0.8 stopped calling these reserved: SET_TEXT, SELECT_OPTION,
    // TOGGLE and SUBMIT_FORM are specified operations that a renderer can
    // perform. This test is the reason that change costs nothing. It used
    // to walk `ActionType::RESERVED`, a list the generator emitted from the
    // schema's own reservation, so un-reserving would have emptied the list
    // and left the assertion passing over nothing — the strongest test in
    // this module quietly proving that no member of an empty set is
    // authorized. Naming the four is what keeps the claim about them.
    for (action_type, expected) in [
        (ActionType::SetText, ActionClass::FillField),
        (ActionType::SelectOption, ActionClass::SelectOption),
        (ActionType::Toggle, ActionClass::ToggleControl),
        (ActionType::SubmitForm, ActionClass::SubmitForm),
    ] {
        let class = ActionClass::resolve(action_type, SemanticRole::TextField)
            .unwrap_or_else(|| unreachable!("{action_type:?} must carry a class"));
        assert_eq!(class, expected, "{action_type:?}");
        assert_eq!(
            class.availability(),
            ClassAvailability::WriteMilestone,
            "{action_type:?}"
        );
        assert!(!class.availability().can_be_authorized_today());
        // Protocol support and class resolution remain intact. Only exact
        // fill has the browser-owned confirmation and consequence binding M5
        // requires; the other operations stay closed.
        for milestone in PolicyMilestone::ALL {
            assert_eq!(
                class.is_authorized_at(*milestone),
                class == ActionClass::FillField
                    && matches!(
                        *milestone,
                        PolicyMilestone::M5 | PolicyMilestone::M6 | PolicyMilestone::M7
                    ),
                "{action_type:?} at {milestone:?}"
            );
        }
    }
}

#[test]
fn the_role_a_write_lands_on_does_not_change_the_class_it_carries() {
    // ACTIVATE splits by role because its consequence does. A write does
    // not: filling a field is filling a field wherever the adapter thinks
    // the node sits, and a class that softened on an unexpected role would
    // be a way to ask for the same effect under a cheaper name.
    for role in [
        SemanticRole::TextField,
        SemanticRole::Button,
        SemanticRole::Paragraph,
        SemanticRole::Link,
    ] {
        assert_eq!(
            ActionClass::resolve(ActionType::SubmitForm, role),
            Some(ActionClass::SubmitForm),
            "{role:?}"
        );
    }
}

#[test]
fn open_link_is_complete_with_a_node_or_a_destination() {
    assert!(ActionClass::OpenLink.scope_is_complete(true, false));
    assert!(ActionClass::OpenLink.scope_is_complete(false, true));
    assert!(ActionClass::OpenLink.scope_is_complete(true, true));
    assert!(!ActionClass::OpenLink.scope_is_complete(false, false));
    assert!(ActionClass::ObservePage.scope_is_complete(false, false));
    assert!(ActionClass::MoveFocus.scope_is_complete(false, false));
    assert!(!ActionClass::SyntheticClick.scope_is_complete(false, true));
}
