// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use crate::tool::{EffectiveToolSet, Milestone};

#[test]
fn purpose_terms_can_cross_name_and_description_without_being_adjacent() {
    let set = EffectiveToolSet::for_task(Milestone::M8, &[]);
    for (query, expected) in [
        ("memory save", "memory.save"),
        ("memory save scope", "memory.save"),
        ("approved preference update", "memory.update"),
        ("pdf document", "page.pdf.inspect"),
        ("duration download", "media.probe"),
    ] {
        assert!(set.search_names(query).contains(&expected), "{query}");
        assert!(
            set.search(query).iter().any(|row| row.matches(expected)),
            "{query}"
        );
    }
    assert_eq!(set.search_names("MeMoRy SaVe"), vec!["memory.save"]);
}

#[test]
fn exact_names_rank_first_and_namespace_members_stay_callable() {
    let set = EffectiveToolSet::for_task(Milestone::M8, &[]);
    assert_eq!(
        set.search_names("memory.save").first(),
        Some(&"memory.save")
    );
    assert_eq!(
        set.search_names("page.images"),
        vec!["page.images.describe", "page.images.read_text"]
    );
    assert_eq!(set.search_names("read_text"), vec!["page.images.read_text"]);
    assert!(!set.search_names("page.images").contains(&"page.images"));
}

#[test]
fn term_discovery_cannot_cross_scope_loading_or_milestone() {
    let restricted = EffectiveToolSet::for_task(Milestone::M8, &["memory.search".to_owned()]);
    assert!(restricted.search_names("memory save").is_empty());
    let early = EffectiveToolSet::for_task(Milestone::M3, &[]);
    assert!(early.search_names("memory save").is_empty());
    let loaded = EffectiveToolSet::for_task(Milestone::M8, &[]).with_activated(&["memory.save"]);
    assert!(loaded.search_names("memory save").is_empty());
    assert!(loaded
        .definitions()
        .iter()
        .any(|tool| tool.name == "memory.save"));
}

#[test]
fn queries_are_bounded_and_every_term_must_match_the_same_definition() {
    let set = EffectiveToolSet::for_task(Milestone::M8, &[]);
    for invalid in [
        " ".to_owned(),
        "a".repeat(513),
        "a ".repeat(17),
        "memory\0save".to_owned(),
    ] {
        assert!(set.search_names(&invalid).is_empty());
        assert!(set.search(&invalid).is_empty());
    }
    assert!(set.search_names("memory impossibleoperand").is_empty());
    assert!(set.search_names("memory download").is_empty());
}
