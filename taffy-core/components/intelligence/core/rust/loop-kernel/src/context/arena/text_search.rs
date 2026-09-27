// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Allocation-free matching for the common page-query text path.

/// Whether `haystack` contains a needle that its caller already lowercased.
///
/// Page queries and most page labels are ASCII. Searching those bytes directly
/// avoids allocating and copying every candidate name and text run for every
/// query. Unicode retains the previous full lowercase behavior: changing that
/// behavior for speed would change which node handle a model receives.
pub(super) fn contains_folded(haystack: &str, folded_needle: &str) -> bool {
    if folded_needle.is_empty() {
        return true;
    }
    if haystack.is_ascii() && folded_needle.is_ascii() {
        return haystack
            .as_bytes()
            .windows(folded_needle.len())
            .any(|candidate| candidate.eq_ignore_ascii_case(folded_needle.as_bytes()));
    }
    haystack.to_lowercase().contains(folded_needle)
}

#[cfg(test)]
mod tests {
    use super::contains_folded;

    #[test]
    fn ascii_search_is_case_insensitive_and_keeps_substring_semantics() {
        assert!(contains_folded("Download quarterly REPORT", "report"));
        assert!(!contains_folded("Download quarterly REPORT", "receipt"));
        assert!(contains_folded("anything", ""));
    }

    #[test]
    fn unicode_search_keeps_full_lowercase_behavior() {
        assert!(contains_folded("CAFÉ STRAẞE", "café straße"));
    }
}
