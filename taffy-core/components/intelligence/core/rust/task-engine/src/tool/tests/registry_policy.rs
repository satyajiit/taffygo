// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::super::{available_at, Milestone, ToolAvailability, ToolDispatch, REGISTRY};
use crate::authority::ActionClass;
use policy_engine::{ActionClass as PolicyActionClass, PolicyMilestone};

/// Tool rows whose implementation is available at a milestone whose policy
/// surface has not authorized their class. The two functions part company on
/// purpose: availability says which milestone reserved a class, the allowlist
/// says which milestone actually took it.
///
/// `browser.form.fill` left this set when M5 took `FillField`; the assertion
/// below is what noticed, and it is the only thing that would. The other three
/// stay because `authorized_classes(PolicyMilestone::M5)` still names neither
/// `SelectOption`, `ToggleControl` nor `SubmitForm`, which the policy engine's
/// own surface test asserts deliberately. Decision 0089 argues for ratifying
/// all four together and is still `Proposed`; until it is accepted and the
/// allowlist widened, this set is the honest description of the tree.
const UNCLASSIFIED_PAGE_WRITES: &[&str] = &[
    "browser.form.select",
    "browser.form.toggle",
    "browser.form.submit",
];

const fn policy_class(class: ActionClass) -> PolicyActionClass {
    match class {
        ActionClass::ObservePage => PolicyActionClass::ObservePage,
        ActionClass::ScrollIntoView => PolicyActionClass::ScrollIntoView,
        ActionClass::OpenLink => PolicyActionClass::OpenLink,
        ActionClass::CreateTaskTab => PolicyActionClass::CreateTaskTab,
        ActionClass::SyntheticClick => PolicyActionClass::SyntheticClick,
        ActionClass::MoveFocus => PolicyActionClass::MoveFocus,
        ActionClass::FillField => PolicyActionClass::FillField,
        ActionClass::SelectOption => PolicyActionClass::SelectOption,
        ActionClass::ToggleControl => PolicyActionClass::ToggleControl,
        ActionClass::SubmitForm => PolicyActionClass::SubmitForm,
        ActionClass::StartDownload => PolicyActionClass::StartDownload,
        ActionClass::UploadFile => PolicyActionClass::UploadFile,
        ActionClass::SendMessage => PolicyActionClass::SendMessage,
        ActionClass::Purchase => PolicyActionClass::Purchase,
        ActionClass::ExtractCredential => PolicyActionClass::ExtractCredential,
        ActionClass::BypassAccessControl => PolicyActionClass::BypassAccessControl,
        ActionClass::ExecuteToolJob => PolicyActionClass::ExecuteToolJob,
        ActionClass::LibraryRead => PolicyActionClass::LibraryRead,
        ActionClass::LibraryWrite => PolicyActionClass::LibraryWrite,
        ActionClass::MemoryRead => PolicyActionClass::MemoryRead,
        ActionClass::MemoryWrite => PolicyActionClass::MemoryWrite,
        ActionClass::ControlTab => PolicyActionClass::ControlTab,
        ActionClass::ProfileStoreRead => PolicyActionClass::ProfileStoreRead,
    }
}

const fn policy_surface_at(owner: Milestone) -> PolicyMilestone {
    match owner {
        Milestone::M0 | Milestone::M1 | Milestone::M2 => PolicyMilestone::M2,
        Milestone::M3 | Milestone::M4 => PolicyMilestone::M3,
        Milestone::M5 => PolicyMilestone::M5,
        Milestone::M6 => PolicyMilestone::M6,
        Milestone::M7 | Milestone::M8 => PolicyMilestone::M7,
    }
}

#[test]
fn the_milestone_m3_surface_is_exactly_the_read_oriented_tools() {
    let names: Vec<&str> = available_at(Milestone::M3)
        .iter()
        .map(|entry| entry.name)
        .collect();
    assert_eq!(
        names,
        vec![
            "browser.navigate",
            "browser.search",
            "browser.back",
            "browser.forward",
            "browser.reload",
            "browser.stop_loading",
            "browser.tabs.open",
            "browser.tabs.list",
            "browser.tabs.activate",
            "browser.tabs.close",
            "browser.dom.query",
            "browser.dom.read",
            "browser.dom.click",
            "browser.dom.focus",
            "browser.link.open",
            "browser.dom.scroll",
            "browser.form.inspect",
            "browser.selection.read",
            "artifact.markdown.create",
            "artifact.csv.create",
            "user.handover",
            "user.ask",
            "user.request_values",
            "tool.search",
            "tool.activate",
            "run.spawn",
            "history.search",
            "history.recent",
            "bookmarks.search",
            "bookmarks.list",
            "open_tabs.list",
        ]
    );
}

#[test]
fn tool_implementation_availability_never_opens_an_unclassified_page_write() {
    let mut unclassified_rows = 0usize;
    for entry in REGISTRY {
        if entry.dispatch == ToolDispatch::Unserved {
            continue;
        }
        let Some(class) = entry.dispatch.action_class() else {
            // Artifact, person and loop settlements deliberately spend no
            // capability, so policy agreement does not apply to them.
            continue;
        };
        let ToolAvailability::From(owner) = entry.availability else {
            unreachable!("served policy-gated row {} is excluded", entry.name)
        };
        let policy_milestone = policy_surface_at(owner);
        if UNCLASSIFIED_PAGE_WRITES.contains(&entry.name) {
            unclassified_rows += 1;
            assert!(
                !policy_class(class).is_authorized_at(policy_milestone),
                "{} gained authority from implementation availability",
                entry.name
            );
            continue;
        }
        assert!(
            policy_class(class).is_authorized_at(policy_milestone),
            "{} becomes available at {} but class {} is closed on {}",
            entry.name,
            owner,
            class.label(),
            policy_milestone.label()
        );
    }
    assert_eq!(
        unclassified_rows,
        UNCLASSIFIED_PAGE_WRITES.len(),
        "the explicit typed-but-unauthorized set drifted"
    );
}
