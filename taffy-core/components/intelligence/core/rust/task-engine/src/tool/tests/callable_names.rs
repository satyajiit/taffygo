// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Exact names the registry may expose to a model or durable proposal.

use crate::tool::{resolve, Milestone, NameMatch, REGISTRY};

#[test]
fn every_callable_name_is_compiled_in_unique_and_resolves_to_its_row() {
    let mut seen = Vec::new();
    for entry in REGISTRY {
        let names: Vec<&str> = entry.callable_names().collect();
        assert!(
            !names.is_empty(),
            "{} declares no callable name",
            entry.name
        );
        for name in names {
            assert!(!seen.contains(&name), "{name} is declared twice");
            seen.push(name);
            assert!(name.len() <= 128, "{name} exceeds the reply name bound");
            assert!(
                name.chars()
                    .all(|character| character.is_ascii_lowercase()
                        || matches!(character, '.' | '_')),
                "{name} is not a lowercase dotted name"
            );
            if matches!(entry.matching, NameMatch::Namespace { .. }) {
                let Some(member) = name
                    .strip_prefix(entry.name)
                    .and_then(|rest| rest.strip_prefix('.'))
                else {
                    panic!("{name} is not beneath namespace {}", entry.name);
                };
                assert!(!member.is_empty(), "{} has an empty member", entry.name);
            }
            assert_eq!(
                resolve(name, Milestone::M8).entry(),
                Some(entry),
                "{name} does not resolve to {}",
                entry.name
            );
        }
    }
}
