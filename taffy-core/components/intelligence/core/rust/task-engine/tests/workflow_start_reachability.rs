// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Cross-language pin for the browser's reviewed workflow-start bundles.
//!
//! The browser chooses the durable task allowlist before the Rust core sees a
//! start. A Rust registry row that is implemented but absent there is therefore
//! dead production code, while a browser spelling absent from the registry is
//! an authority-shaped no-op. This test reads the exact compiled C++ table and
//! checks both directions at the production M7 pin. The table deliberately
//! uses one machine-readable macro shape so this test does not approximate C++
//! control flow.

#![allow(
    clippy::expect_used,
    clippy::indexing_slicing,
    clippy::panic,
    clippy::unwrap_used
)]

use std::collections::{BTreeMap, BTreeSet};

use task_engine::tool::allowlist_name;
use task_engine::{available_at, EffectiveToolSet, Milestone};

const WORKFLOW_SOURCE: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../../../../../browser/core_api/task_workflow_tools.cc"
));
const ROW_PREFIX: &str = "TAFFY_WORKFLOW_TOOL(";
const NO_MODEL_ROW_PREFIX: &str = "TAFFY_NO_MODEL_WORKFLOW_TOOL(";
const STORE_ROW_PREFIX: &str = "TAFFY_STORE_WORKFLOW_TOOL(";
/// The three stores a start may attach, with the one group each selects and
/// the exact rows that group admits (decision 0133).
const ATTACHED_STORES: &[(&str, &str, &[&str])] = &[
    (
        "kHistory",
        "person.history",
        &["history.search", "history.recent"],
    ),
    (
        "kBookmarks",
        "person.bookmarks",
        &["bookmarks.search", "bookmarks.list"],
    ),
    ("kOpenTabs", "person.open_tabs", &["open_tabs.list"]),
];
const WITHHELD_PENDING_CONSEQUENCE_CLASSIFICATION: &[&str] = &[
    "browser.form.select",
    "browser.form.toggle",
    "browser.form.submit",
];

#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
enum Template {
    CompareProducts,
    SummarizeEvidence,
    BuildSourceTable,
    WebErrand,
}

impl Template {
    const ALL: [Self; 4] = [
        Self::CompareProducts,
        Self::SummarizeEvidence,
        Self::BuildSourceTable,
        Self::WebErrand,
    ];

    fn parse(value: &str) -> Option<Self> {
        match value {
            "kCompareProducts" => Some(Self::CompareProducts),
            "kSummarizeEvidence" => Some(Self::SummarizeEvidence),
            "kBuildSourceTable" => Some(Self::BuildSourceTable),
            "kWebErrand" => Some(Self::WebErrand),
            _ => None,
        }
    }
}

fn parse_bundles(prefix: &str) -> BTreeMap<Template, Vec<String>> {
    let mut bundles = BTreeMap::new();
    for line in WORKFLOW_SOURCE.lines() {
        let line = line.trim();
        let Some(body) = line
            .strip_prefix(prefix)
            .and_then(|body| body.strip_suffix(')'))
        else {
            continue;
        };
        let (template, quoted_name) = body
            .split_once(", ")
            .expect("a workflow row has exactly a template and quoted name");
        let template = Template::parse(template).expect("the C++ row names one closed template");
        let name = quoted_name
            .strip_prefix('"')
            .and_then(|name| name.strip_suffix('"'))
            .expect("the C++ row carries one string literal");
        bundles
            .entry(template)
            .or_insert_with(Vec::new)
            .push(name.to_owned());
    }
    bundles
}

fn reviewed_bundles() -> BTreeMap<Template, Vec<String>> {
    parse_bundles(ROW_PREFIX)
}

fn reviewed_no_model_bundles() -> BTreeMap<Template, Vec<String>> {
    parse_bundles(NO_MODEL_ROW_PREFIX)
}

/// The attached-store rows as (template, store, group), in table order.
fn reviewed_store_rows() -> Vec<(Template, String, String)> {
    let mut rows = Vec::new();
    for line in WORKFLOW_SOURCE.lines() {
        let line = line.trim();
        let Some(body) = line
            .strip_prefix(STORE_ROW_PREFIX)
            .and_then(|body| body.strip_suffix(')'))
        else {
            continue;
        };
        let mut fields = body.split(", ");
        let template = fields
            .next()
            .and_then(Template::parse)
            .expect("a store row names one closed template");
        let store = fields.next().expect("a store row names one store");
        let name = fields
            .next()
            .and_then(|name| name.strip_prefix('"'))
            .and_then(|name| name.strip_suffix('"'))
            .expect("a store row carries one string literal");
        assert!(fields.next().is_none(), "a store row has three fields");
        rows.push((template, store.to_owned(), name.to_owned()));
    }
    rows
}

fn effective(
    bundles: &BTreeMap<Template, Vec<String>>,
    template: Template,
    milestone: Milestone,
) -> EffectiveToolSet {
    EffectiveToolSet::for_task(
        milestone,
        bundles
            .get(&template)
            .expect("every reviewed template has a model bundle"),
    )
}

#[test]
fn every_cpp_name_is_registered_and_only_classified_m7_rows_are_reachable() {
    let bundles = reviewed_bundles();
    let no_model = reviewed_no_model_bundles();
    let store_rows = reviewed_store_rows();
    assert_eq!(bundles.len(), Template::ALL.len());
    assert_eq!(no_model.len(), 2);
    let saved_replay = no_model
        .get(&Template::WebErrand)
        .expect("an exact saved flow has a local replay bundle");
    assert!(saved_replay.iter().any(|tool| tool == "browser.navigate"));
    assert!(saved_replay.iter().any(|tool| tool == "user.handover"));
    assert!(saved_replay
        .iter()
        .any(|tool| tool == "browser.download.from_link"));
    assert!(!saved_replay.iter().any(|tool| tool == "browser.search"
        || tool.starts_with("person.")
        || tool == "run.spawn"));
    let [only_no_model_tool] = no_model
        .get(&Template::BuildSourceTable)
        .expect("BuildSourceTable has the one reviewed no-model bundle")
        .as_slice()
    else {
        panic!("BuildSourceTable no-model bundle must have exactly one tool");
    };
    assert_eq!(only_no_model_tool, "browser.dom.read");

    let registered: BTreeSet<&str> = available_at(Milestone::M7)
        .into_iter()
        .map(|entry| allowlist_name(entry.name))
        .collect();
    let mut declared = BTreeSet::new();
    for (_, _, name) in &store_rows {
        assert!(
            registered.contains(name.as_str()),
            "a store row admits C++ name {name} with no shipping Rust group"
        );
        declared.insert(name.as_str());
    }
    for template in Template::ALL {
        let names = bundles
            .get(&template)
            .expect("every closed template has a model bundle");
        assert!(!names.is_empty(), "{template:?}");
        let unique: BTreeSet<&str> = names.iter().map(String::as_str).collect();
        assert_eq!(unique.len(), names.len(), "duplicate in {template:?}");
        for name in unique {
            assert!(
                registered.contains(name),
                "{template:?} admits C++ name {name} with no shipping Rust row/group"
            );
            declared.insert(name);
        }
    }
    for (template, names) in no_model {
        for name in names {
            assert!(
                registered.contains(name.as_str()),
                "{template:?} no-model bundle admits C++ name {name} with no shipping Rust row/group"
            );
        }
    }
    let withheld: BTreeSet<&str> = WITHHELD_PENDING_CONSEQUENCE_CLASSIFICATION
        .iter()
        .copied()
        .collect();
    assert!(
        withheld.is_subset(&registered),
        "every deliberately withheld name remains a registered, typed refusal"
    );
    assert!(
        declared.is_disjoint(&withheld),
        "an unclassified page mutation reached a generic production template"
    );
    assert!(
        bundles
            .get(&Template::WebErrand)
            .expect("WebErrand has a reviewed model bundle")
            .iter()
            .any(|name| name == "browser.form.fill"),
        "the exact-value FillField path must be reachable only from WebErrand"
    );
    for template in [
        Template::CompareProducts,
        Template::SummarizeEvidence,
        Template::BuildSourceTable,
    ] {
        assert!(
            !bundles
                .get(&template)
                .expect("every template has a reviewed model bundle")
                .iter()
                .any(|name| name == "browser.form.fill"),
            "FillField escaped the WebErrand template into {template:?}"
        );
    }
    assert_eq!(
        declared,
        registered.difference(&withheld).copied().collect(),
        "every other shipping Rust allowlist identity needs a reviewed production template"
    );
}

#[test]
fn an_attached_store_row_admits_exactly_its_own_tools() {
    let bundles = reviewed_bundles();
    let store_rows = reviewed_store_rows();
    let known: BTreeSet<&str> = ATTACHED_STORES.iter().map(|(store, _, _)| *store).collect();
    for (template, store, name) in &store_rows {
        let (_, group, _) = ATTACHED_STORES
            .iter()
            .find(|(candidate, _, _)| candidate == store)
            .unwrap_or_else(|| panic!("{template:?} names an unknown store {store}"));
        assert_eq!(
            name, group,
            "{template:?} pairs {store} with the wrong group"
        );
    }
    for template in Template::ALL {
        let rows: BTreeSet<&str> = store_rows
            .iter()
            .filter(|(candidate, _, _)| *candidate == template)
            .map(|(_, store, _)| store.as_str())
            .collect();
        assert_eq!(
            rows, known,
            "{template:?} must offer every store or say why"
        );
        let bundle = bundles
            .get(&template)
            .expect("every reviewed template has a model bundle");
        let bare = EffectiveToolSet::for_task(Milestone::M7, bundle);
        for (_, group, tools) in ATTACHED_STORES {
            let mut with_store = bundle.clone();
            with_store.push((*group).to_owned());
            let attached = EffectiveToolSet::for_task(Milestone::M7, &with_store);
            for tool in *tools {
                assert!(!bare.admits(tool), "{template:?} admits {tool} unattached");
                assert!(
                    attached.admits(tool),
                    "{template:?} refuses {tool} attached"
                );
            }
            for (_, other_group, other_tools) in ATTACHED_STORES {
                if other_group == group {
                    continue;
                }
                for tool in *other_tools {
                    assert!(!attached.admits(tool), "{group} admitted {tool}");
                }
            }
        }
    }
}

#[test]
fn deferred_high_value_tools_follow_template_and_milestone_review() {
    let bundles = reviewed_bundles();

    for template in Template::ALL {
        let m3 = effective(&bundles, template, Milestone::M3);
        assert!(!m3.admits("page.pdf.inspect"), "{template:?}");
        assert!(!m3.admits("page.images.describe"), "{template:?}");
        assert!(!m3.admits("media.probe"), "{template:?}");
        assert!(!m3.admits("core.table.reshape"), "{template:?}");
        assert!(!m3.admits("artifact.xlsx.create"), "{template:?}");
        assert!(!m3.admits("python.execute"), "{template:?}");

        let m4 = effective(&bundles, template, Milestone::M4);
        assert_eq!(
            m4.activatable_name("page.pdf.inspect"),
            Some("page.pdf.inspect"),
            "{template:?}"
        );
        assert!(!m4.admits("page.images.describe"), "{template:?}");

        let m6 = effective(&bundles, template, Milestone::M6);
        assert_eq!(
            m6.activatable_name("page.images.describe"),
            Some("page.images.describe"),
            "{template:?}"
        );
        assert!(!m6.admits("page.images"), "{template:?}");
    }

    for template in [Template::CompareProducts, Template::SummarizeEvidence] {
        let m7 = effective(&bundles, template, Milestone::M7);
        assert!(m7.admits("page.video.inspect"), "{template:?}");
        assert!(m7.admits("media.probe"), "{template:?}");
        assert!(m7.admits("artifact.pdf.create"), "{template:?}");
        assert!(m7.admits("python.execute"), "{template:?}");
    }

    let compare = effective(&bundles, Template::CompareProducts, Milestone::M7);
    assert!(compare.admits("core.table.reshape"));
    assert!(compare.admits("artifact.xlsx.create"));

    let summary = effective(&bundles, Template::SummarizeEvidence, Milestone::M7);
    assert!(!summary.admits("core.table.reshape"));
    assert!(!summary.admits("artifact.xlsx.create"));

    let table = effective(&bundles, Template::BuildSourceTable, Milestone::M7);
    assert!(table.admits("core.table.reshape"));
    assert!(table.admits("artifact.xlsx.create"));
    assert!(table.admits("python.execute"));
    assert!(!table.admits("page.video.inspect"));
    assert!(!table.admits("media.probe"));
    assert!(!table.admits("artifact.pdf.create"));

    let errand = effective(&bundles, Template::WebErrand, Milestone::M7);
    assert!(errand.admits("browser.download.start"));
    for name in WITHHELD_PENDING_CONSEQUENCE_CLASSIFICATION {
        assert!(!errand.admits(name), "{name}");
    }
    for name in [
        "page.video.inspect",
        "media.probe",
        "core.table.reshape",
        "artifact.xlsx.create",
        "artifact.pdf.create",
        "python.execute",
    ] {
        assert!(!errand.admits(name), "{name}");
    }
}
