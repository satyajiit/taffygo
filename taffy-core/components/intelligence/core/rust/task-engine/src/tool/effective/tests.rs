// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::EffectiveToolSet;
use crate::tool::milestone::Milestone;

fn list(names: &[&str]) -> Vec<String> {
    names.iter().map(|name| (*name).to_owned()).collect()
}

#[test]
fn an_empty_allowlist_is_the_whole_milestone_surface() {
    let set = EffectiveToolSet::for_task(Milestone::M3, &[]);
    assert_eq!(
        set.names(),
        crate::tool::available_at(Milestone::M3)
            .iter()
            .map(|entry| entry.name)
            .collect::<Vec<&str>>()
    );
    assert_eq!(set.milestone(), Milestone::M3);
}

#[test]
fn the_milestone_filter_has_no_exception() {
    // Not even the unconditional name survives a milestone that has not
    // reached it. An allowlist cannot grant what the build does not have.
    let set = EffectiveToolSet::for_task(Milestone::M2, &list(&["user.handover"]));
    assert!(set.is_empty());
    assert!(!set.admits("user.handover"));
}

#[test]
fn a_later_milestone_tool_is_absent_however_the_allowlist_reads() {
    let set = EffectiveToolSet::for_task(Milestone::M3, &list(&["browser.form.fill"]));
    assert!(!set.admits("browser.form.fill"));
    assert_eq!(set.names(), vec!["user.handover"]);
}

#[test]
fn an_excluded_name_is_absent_at_every_milestone() {
    for milestone in Milestone::ALL {
        let set = EffectiveToolSet::for_task(*milestone, &list(&["device.clipboard.read"]));
        assert!(!set.admits("device.clipboard.read"), "{milestone}");
    }
}

#[test]
fn a_narrowed_allowlist_keeps_the_way_out_and_nothing_else_it_omitted() {
    let set = EffectiveToolSet::for_task(Milestone::M3, &list(&["browser.dom.read"]));
    assert_eq!(set.names(), vec!["browser.dom.read", "user.handover"]);
    // Asking is a capability, so narrowing it costs a capability. Only the
    // terminal step is protected, because only it has nothing after it.
    assert!(!set.admits("user.ask"));
    assert!(set.admits("user.handover"));
}

#[test]
fn narrowing_can_only_take_names_away() {
    let full = EffectiveToolSet::for_task(Milestone::M3, &[]);
    let narrowed = full.narrow_by(&list(&["browser.dom.read"]));
    for name in narrowed.names() {
        assert!(full.names().contains(&name), "{name} was not in the set");
    }
    assert!(narrowed.len() < full.len());
    // A list naming something the set never had adds nothing.
    let widened = narrowed.narrow_by(&list(&["browser.dom.read", "python.execute"]));
    assert!(!widened.admits("python.execute"));
    assert_eq!(widened.names(), narrowed.names());
}

#[test]
fn narrowing_twice_is_the_intersection_and_never_a_restoration() {
    let full = EffectiveToolSet::for_task(Milestone::M3, &[]);
    let once = full.narrow_by(&list(&["browser.dom.read", "browser.navigate"]));
    let twice = once.narrow_by(&list(&["browser.navigate", "browser.search"]));
    assert_eq!(twice.names(), vec!["browser.navigate", "user.handover"]);
}

#[test]
fn a_namespace_row_is_admitted_whole_or_not_at_all() {
    let whole = EffectiveToolSet::for_task(Milestone::M3, &list(&["browser.tabs"]));
    for served in [
        "browser.tabs.open",
        "browser.tabs.list",
        "browser.tabs.activate",
        "browser.tabs.close",
    ] {
        assert!(whole.admits(served), "{served}");
    }
    // Naming one member admits nothing, because admitting the row would
    // admit the siblings the list did not ask for.
    let member_only = EffectiveToolSet::for_task(Milestone::M3, &list(&["browser.tabs.open"]));
    assert!(!member_only.admits("browser.tabs.open"));
    assert_eq!(member_only.names(), vec!["user.handover"]);
}

/// What `Guard::ToolAvailable` would answer for `name` under `allowlist`,
/// spelled out here rather than called, because the guard needs a whole
/// reducer and what is under test is the *rule* the two share.
///
/// The milestone half is `tool::resolve(..).is_available()`, which this
/// set applies identically through `available_at`; the clause reproduced
/// here is the canonical-row allowlist half.
fn guard_would_admit(allowlist: &[String], name: &str) -> bool {
    let lookup = crate::tool::resolve(name, Milestone::M3);
    lookup.is_available()
        && lookup.entry().is_some_and(|entry| {
            crate::tool::is_unconditional(entry.name)
                || allowlist.is_empty()
                || allowlist
                    .iter()
                    .any(|allowed| allowed == crate::tool::allowlist_name(entry.name))
        })
}

#[test]
fn the_namespace_row_has_the_same_answer_here_and_at_the_reducers_guard() {
    // The row named, a member proposed: both admit the row's member.
    let by_row = list(&["browser.tabs"]);
    assert!(EffectiveToolSet::for_task(Milestone::M3, &by_row).admits("browser.tabs.open"));
    assert!(guard_would_admit(&by_row, "browser.tabs.open"));

    // A member named, the same member proposed: both refuse because the
    // allowlist did not name the row.
    let by_member = list(&["browser.tabs.open"]);
    assert!(!EffectiveToolSet::for_task(Milestone::M3, &by_member).admits("browser.tabs.open"));
    assert!(!guard_would_admit(&by_member, "browser.tabs.open"));

    // Exact rows use the same canonical name and therefore agree too.
    for name in ["browser.dom.read", "browser.navigate", "user.handover"] {
        let narrowed = list(&[name]);
        let set = EffectiveToolSet::for_task(Milestone::M3, &narrowed);
        assert_eq!(
            set.admits(name),
            guard_would_admit(&narrowed, name),
            "{name}"
        );
        assert_eq!(
            set.admits("browser.search"),
            guard_would_admit(&narrowed, "browser.search"),
            "{name}"
        );
    }
}

#[test]
fn a_name_that_merely_shares_a_prefix_is_not_admitted() {
    let set = EffectiveToolSet::for_task(Milestone::M3, &[]);
    assert!(!set.admits("browser.tabsx.open"));
    assert!(!set.admits("browser.tabs"));
    assert!(!set.admits("user"));
    assert!(!set.admits(""));
}

#[test]
fn every_definition_the_set_shows_is_well_formed() {
    let base = EffectiveToolSet::for_task(Milestone::M8, &[]);
    let activated: Vec<&str> = base
        .entries()
        .iter()
        .filter(|entry| entry.loading == crate::tool::ToolLoading::Deferred)
        .flat_map(|entry| entry.callable_names())
        .collect();
    let set = base.with_activated(&activated);
    for definition in set.definitions() {
        assert!(definition.is_well_formed(), "{}", definition.name);
        assert!(
            crate::tool::resolve(definition.name, Milestone::M8).is_available(),
            "declared name {} does not resolve",
            definition.name
        );
    }
    let callable_count: usize = set
        .offered()
        .iter()
        .map(|entry| entry.callable_names().count())
        .sum();
    assert_eq!(set.definitions().len(), callable_count);
}

#[test]
fn offered_excludes_deferred_later_milestone_tools() {
    let m3 = EffectiveToolSet::for_task(Milestone::M3, &[]);
    assert!(!m3
        .offered()
        .iter()
        .any(|entry| entry.name == "browser.form.fill"));
    assert!(!m3
        .offered()
        .iter()
        .any(|entry| entry.name == "page.pdf.inspect"));
    assert!(m3
        .offered()
        .iter()
        .any(|entry| entry.name == crate::tool::SEARCH_TOOLS));

    let m4 = EffectiveToolSet::for_task(Milestone::M4, &[]);
    assert!(m4.admits("page.pdf.inspect"));
    assert!(m4
        .deferred()
        .iter()
        .any(|entry| entry.name == "page.pdf.inspect"));
    assert!(!m4
        .offered()
        .iter()
        .any(|entry| entry.name == "page.pdf.inspect"));
}

#[test]
fn search_finds_a_deferred_name_by_purpose_and_does_not_find_fill_at_m3() {
    let m4 = EffectiveToolSet::for_task(Milestone::M4, &[]);
    let hits: Vec<&str> = m4
        .search("document")
        .iter()
        .map(|entry| entry.name)
        .collect();
    assert_eq!(hits, vec!["page.pdf.inspect"]);
    assert_eq!(
        m4.search("  DoCuMeNt  ")
            .iter()
            .map(|entry| entry.name)
            .collect::<Vec<_>>(),
        vec!["page.pdf.inspect"]
    );

    let m3 = EffectiveToolSet::for_task(Milestone::M3, &[]);
    assert!(m3.search("fill").is_empty());
    assert!(m3.search("form assistance").is_empty());

    let m6 = EffectiveToolSet::for_task(Milestone::M6, &[]);
    assert_eq!(m6.search_names("read_text"), vec!["page.images.read_text"]);
    assert_eq!(m6.search_names("READ_TEXT"), vec!["page.images.read_text"]);
}

#[test]
fn with_activated_moves_an_available_deferred_name_to_offered() {
    let set = EffectiveToolSet::for_task(Milestone::M4, &[]).with_activated(&["page.pdf.inspect"]);
    assert!(set
        .offered()
        .iter()
        .any(|entry| entry.name == "page.pdf.inspect"));
    assert!(!set
        .deferred()
        .iter()
        .any(|entry| entry.name == "page.pdf.inspect"));
}

#[test]
fn with_activated_cannot_introduce_a_name_not_on_the_allowlist() {
    let set = EffectiveToolSet::for_task(Milestone::M4, &list(&["browser.dom.read"]))
        .with_activated(&["page.pdf.inspect"]);
    assert!(!set.admits("page.pdf.inspect"));
    assert!(!set
        .offered()
        .iter()
        .any(|entry| entry.name == "page.pdf.inspect"));
    assert_eq!(
        set.names(),
        vec!["browser.dom.read", crate::tool::HANDOVER_TOOL]
    );
}

#[test]
fn with_activated_ignores_unknown_names_and_later_milestone_writes() {
    let set = EffectiveToolSet::for_task(Milestone::M3, &[])
        .with_activated(&["not-a-tool", "browser.form.fill"]);
    assert!(!set.admits("browser.form.fill"));
    assert!(!set
        .offered()
        .iter()
        .any(|entry| entry.name == "browser.form.fill"));
}

#[test]
fn activating_one_reviewed_image_member_exposes_only_exact_callable_names() {
    let allowlist = list(&[
        "page.images",
        crate::tool::SEARCH_TOOLS,
        crate::tool::ACTIVATE_TOOL,
    ]);
    let base = EffectiveToolSet::for_task(Milestone::M6, &allowlist);
    assert_eq!(
        base.search_names("image"),
        vec!["page.images.describe", "page.images.read_text"]
    );
    assert_eq!(
        base.activatable_name("page.images.describe"),
        Some("page.images.describe")
    );
    assert_eq!(base.activatable_name("page.images.caption"), None);

    let activated = base.with_activated(&["page.images.describe"]);
    let names: Vec<&str> = activated
        .definitions()
        .iter()
        .map(|definition| definition.name)
        .collect();
    assert!(names.contains(&"page.images.describe"));
    assert!(names.contains(&"page.images.read_text"));
    assert!(!names.contains(&"page.images"));
    assert!(!names.contains(&"page.images.caption"));
}

#[test]
fn an_errand_is_shown_its_download_form_and_tab_rows_before_any_activation() {
    use crate::task::TaskTemplateId;
    let errand = EffectiveToolSet::for_template(TaskTemplateId::WebErrand, Milestone::M8, &[]);
    let offered: Vec<&str> = errand.offered().iter().map(|entry| entry.name).collect();
    for name in [
        "browser.download.start",
        "browser.download.list",
        "browser.download.cancel",
        "browser.form.fill",
        "browser.tabs.list",
        "page.pdf.inspect",
    ] {
        assert!(offered.contains(&name), "{name} is offered on turn one");
        assert!(
            !errand.deferred().iter().any(|entry| entry.name == name),
            "{name} is no longer deferred"
        );
        // Promotion is a loading change, not a new row: the set admits exactly
        // what the plain set admits.
        assert!(EffectiveToolSet::for_task(Milestone::M8, &[]).admits(name));
    }
    // The three form names no template allowlist admits are not promoted.
    // Promoting them advertised a capability the product withholds — the
    // browser's own comment argues the refusal, and OD-130 records a second,
    // independently measured blocker under it — so the promotion was a no-op
    // that read like an offer.
    for withheld in [
        "browser.form.select",
        "browser.form.toggle",
        "browser.form.submit",
    ] {
        assert!(
            !offered.contains(&withheld),
            "{withheld} is not offered on turn one"
        );
    }
    // Idempotent activation of a promoted row still answers its name, so a
    // model that activates it anyway is not refused.
    assert_eq!(
        errand.activatable_name("browser.download.start"),
        Some("browser.download.start")
    );
    // A promoted row is offered, so searching for it finds nothing to load;
    // the media rows that read a completed download still answer, because
    // they stay deferred.
    assert!(!errand
        .search_names("download")
        .iter()
        .any(|name| name.starts_with("browser.download.")));
    assert_eq!(
        errand.names(),
        EffectiveToolSet::for_task(Milestone::M8, &[]).names()
    );
}

#[test]
fn a_research_template_keeps_every_registered_loading() {
    use crate::task::TaskTemplateId;
    for template in [
        TaskTemplateId::CompareProducts,
        TaskTemplateId::SummarizeEvidence,
        TaskTemplateId::BuildSourceTable,
    ] {
        let set = EffectiveToolSet::for_template(template, Milestone::M8, &[]);
        assert_eq!(
            set,
            EffectiveToolSet::for_task(Milestone::M8, &[]),
            "{template:?}"
        );
        assert!(set
            .deferred()
            .iter()
            .any(|entry| entry.name == "browser.download.start"));
    }
}

#[test]
fn promotion_cannot_reach_past_the_milestone_or_the_allowlist() {
    use crate::task::TaskTemplateId;
    let early = EffectiveToolSet::for_template(TaskTemplateId::WebErrand, Milestone::M3, &[]);
    assert!(!early.admits("browser.download.start"));
    let narrowed = EffectiveToolSet::for_template(
        TaskTemplateId::WebErrand,
        Milestone::M8,
        &list(&["browser.dom.read"]),
    );
    assert_eq!(narrowed.names(), vec!["browser.dom.read", "user.handover"]);
}
