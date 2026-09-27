// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Bounded lookup over compiled definitions already admitted to one task.

use std::cmp::Reverse;

use crate::tool::{ToolDefinition, ToolEntry};

const MAX_QUERY_BYTES: usize = 512;
const MAX_QUERY_TERMS: usize = 16;

struct Query<'a> {
    literal: &'a str,
    terms: Vec<&'a str>,
}

impl<'a> Query<'a> {
    fn parse(value: &'a str) -> Option<Self> {
        let literal = value.trim();
        if literal.is_empty()
            || literal.len() > MAX_QUERY_BYTES
            || literal.chars().any(char::is_control)
        {
            return None;
        }
        let terms: Vec<_> = literal
            .split_whitespace()
            .take(MAX_QUERY_TERMS + 1)
            .collect();
        (terms.len() <= MAX_QUERY_TERMS).then_some(Self { literal, terms })
    }

    fn rank(&self, definition: &ToolDefinition) -> Option<u8> {
        if definition.name.eq_ignore_ascii_case(self.literal) {
            return Some(4);
        }
        if contains(definition.name, self.literal) {
            return Some(3);
        }
        if contains(definition.description, self.literal) {
            return Some(2);
        }
        self.terms
            .iter()
            .all(|term| {
                contains(definition.name, term)
                    || contains(definition.description, term)
                    || definition.parameters.iter().any(|parameter| {
                        contains(parameter.name, term) || contains(parameter.description, term)
                    })
            })
            .then_some(1)
    }
}

pub(super) fn rows(
    entries: impl Iterator<Item = &'static ToolEntry>,
    query: &str,
) -> Vec<&'static ToolEntry> {
    let Some(query) = Query::parse(query) else {
        return Vec::new();
    };
    let mut matches: Vec<_> = entries
        .filter_map(|entry| {
            entry
                .definitions()
                .filter_map(|definition| query.rank(&definition))
                .max()
                .map(|rank| (rank, entry))
        })
        .collect();
    matches.sort_by_key(|(rank, _)| Reverse(*rank));
    matches.into_iter().map(|(_, entry)| entry).collect()
}

pub(super) fn names(
    entries: impl Iterator<Item = &'static ToolEntry>,
    query: &str,
) -> Vec<&'static str> {
    let Some(query) = Query::parse(query) else {
        return Vec::new();
    };
    let mut matches: Vec<_> = entries
        .flat_map(ToolEntry::definitions)
        .filter_map(|definition| query.rank(&definition).map(|rank| (rank, definition.name)))
        .collect();
    // Stable sorting retains registry/member order for equally relevant names.
    matches.sort_by_key(|(rank, _)| Reverse(*rank));
    matches.into_iter().map(|(_, name)| name).collect()
}

/// Registry strings are ASCII; avoid lowercasing and copying each definition.
fn contains(haystack: &str, needle: &str) -> bool {
    !needle.is_empty()
        && haystack
            .as_bytes()
            .windows(needle.len())
            .any(|window| window.eq_ignore_ascii_case(needle.as_bytes()))
}

#[cfg(test)]
mod tests;
