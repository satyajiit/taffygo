// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Bounded redacted facts retained from one exact page observation.

use super::{
    LivePage, MediaFact, MediaObservationKind, RedactedPageContent, MAX_WORKSPACE_PAGE_FACTS,
    MAX_WORKSPACE_PAGE_FACT_BYTES,
};
use crate::context::arena::{PageIdentity, Readability};
use task_engine::PageObservationEvidence;

impl LivePage {
    /// Projects one exact, complete observation into bounded workspace text.
    ///
    /// Only bytes that already crossed the BIP sensitivity gate are retained.
    /// Empty, unreadable, incomplete, and superseded arenas produce nothing.
    pub fn redacted_workspace_content(
        &self,
        evidence: &PageObservationEvidence,
    ) -> Option<RedactedPageContent> {
        let identity = PageIdentity::from_evidence(evidence).ok()?;
        let page = self
            .pages
            .iter()
            .find(|page| page.identity == identity && page.evidence == *evidence)?;
        if !evidence.supports_complete_result()
            || !matches!(page.arena.readability(), Readability::Readable)
        {
            return None;
        }
        let mut values = Vec::with_capacity(MAX_WORKSPACE_PAGE_FACTS);
        let mut current = String::new();
        for node in page.arena.nodes() {
            let atoms = node
                .name
                .iter()
                .map(String::as_str)
                .chain(node.text.iter().map(|run| run.text.as_str()));
            for atom in atoms {
                append_workspace_atom(&mut values, &mut current, atom);
                if values.len() == MAX_WORKSPACE_PAGE_FACTS {
                    return Some(RedactedPageContent { values });
                }
            }
        }
        if !current.is_empty() {
            values.push(current);
        }
        (!values.is_empty()).then_some(RedactedPageContent { values })
    }

    /// Returns bounded, already-redacted media facts for one exact action.
    ///
    /// Attachment handles and raw bytes stay resident in the browser and are
    /// deliberately not part of this view. The caller receives only the fact
    /// text and the browser-verified coordinates that can be persisted.
    pub fn redacted_workspace_media_facts(
        &self,
        evidence: &PageObservationEvidence,
        expected_kind: MediaObservationKind,
    ) -> Option<&[MediaFact]> {
        let identity = PageIdentity::from_evidence(evidence).ok()?;
        let page = self
            .pages
            .iter()
            .find(|page| page.identity == identity && page.evidence == *evidence)?;
        let media = page
            .media
            .as_ref()
            .filter(|media| media.kind == expected_kind)?;
        (!media.facts.is_empty()).then_some(media.facts.as_slice())
    }
}

fn append_workspace_atom(values: &mut Vec<String>, current: &mut String, atom: &str) {
    let atom = atom.trim();
    if atom.is_empty() || values.len() == MAX_WORKSPACE_PAGE_FACTS {
        return;
    }
    let separator = usize::from(!current.is_empty());
    if current
        .len()
        .saturating_add(separator)
        .saturating_add(atom.len())
        <= MAX_WORKSPACE_PAGE_FACT_BYTES
    {
        if separator == 1 {
            current.push('\n');
        }
        current.push_str(atom);
        return;
    }
    if !current.is_empty() {
        values.push(core::mem::take(current));
        if values.len() == MAX_WORKSPACE_PAGE_FACTS {
            return;
        }
    }
    current.push_str(utf8_prefix(atom, MAX_WORKSPACE_PAGE_FACT_BYTES));
}

fn utf8_prefix(value: &str, maximum: usize) -> &str {
    if value.len() <= maximum {
        return value;
    }
    let mut end = maximum;
    while !value.is_char_boundary(end) {
        end = end.saturating_sub(1);
    }
    &value[..end]
}
