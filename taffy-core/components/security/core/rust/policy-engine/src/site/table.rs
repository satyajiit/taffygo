// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The destination-class tables and the one parser that reads them.
//!
//! Two tables, one parser. The shipping table is the product's own; the corpus
//! table names the fixture origins the adversarial suite needs. They are read
//! by the same code on purpose — a test-only override would mean the shipping
//! classifier is never the thing under test, which is the exact failure this
//! layer exists to prevent. Only the data differs, and a test asserts the two
//! never name the same host.
//!
//! Parsing happens once, on first use, into a sorted slice. Lookup is a binary
//! search over borrowed static strings and allocates nothing, because it sits
//! on the decision path.

use crate::origin::NormalizedOrigin;
use crate::site::SiteClass;

/// The product's own table.
const SHIPPING_SOURCE: &str =
    include_str!("../../../../../../../common/policy/restricted_destination_classes.tsv");

/// The fixture-corpus table.
const CORPUS_SOURCE: &str = include_str!("corpus_classes.tsv");

/// One parsed row.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct SiteRow {
    /// The host, lowercase, without scheme or port.
    pub host: &'static str,
    /// What kind of destination it is.
    pub class: SiteClass,
}

/// A parsed table, sorted by host so lookup is a binary search.
#[derive(Clone, Debug)]
pub struct SiteTable {
    rows: Vec<SiteRow>,
}

impl SiteTable {
    /// Parses a table, refusing a row it cannot read rather than skipping it.
    ///
    /// A skipped row is a missing control that looks like an absent one, so an
    /// unreadable table is an error and never a partial table.
    pub fn parse(source: &'static str) -> Result<Self, TableError> {
        let mut rows = Vec::new();
        for (number, line) in source.lines().enumerate() {
            let line_number = number + 1;
            let trimmed = line.trim_end();
            if trimmed.is_empty() || trimmed.starts_with('#') {
                continue;
            }
            let mut fields = trimmed.split('\t');
            let (Some(host), Some(class), Some(reason)) =
                (fields.next(), fields.next(), fields.next())
            else {
                return Err(TableError::MalformedRow { line_number });
            };
            if host.is_empty() || reason.trim().is_empty() {
                return Err(TableError::MalformedRow { line_number });
            }
            if host != host.to_ascii_lowercase() {
                return Err(TableError::HostNotLowercase { line_number });
            }
            let Some(class) = SiteClass::from_token(class) else {
                return Err(TableError::UnknownClass { line_number });
            };
            rows.push(SiteRow { host, class });
        }
        rows.sort_unstable_by_key(|row| row.host);
        if rows
            .iter()
            .zip(rows.iter().skip(1))
            .any(|(left, right)| left.host == right.host)
        {
            return Err(TableError::DuplicateHost);
        }
        Ok(Self { rows })
    }

    /// The class for a host, or `None` when this table has nothing to say.
    #[must_use]
    pub fn class_of(&self, host: &str) -> Option<SiteClass> {
        self.rows
            .binary_search_by_key(&host, |row| row.host)
            .ok()
            .and_then(|index| self.rows.get(index))
            .map(|row| row.class)
    }

    /// Every row, sorted by host.
    #[must_use]
    pub fn rows(&self) -> &[SiteRow] {
        &self.rows
    }
}

/// Why a table could not be read.
///
/// Every variant is a build-time defect in a committed file, not a runtime
/// condition, which is why the parser refuses rather than degrading.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TableError {
    /// A row does not carry three tab-separated fields.
    MalformedRow {
        /// The one-based line the row is on.
        line_number: usize,
    },
    /// A host is not already lowercase, so it could never match.
    HostNotLowercase {
        /// The one-based line the row is on.
        line_number: usize,
    },
    /// A row names a class outside the closed list.
    UnknownClass {
        /// The one-based line the row is on.
        line_number: usize,
    },
    /// Two rows name the same host.
    DuplicateHost,
}

/// The shipping table, parsed once.
///
/// A table that cannot be parsed is a defect in a committed file, and the
/// classifier answers `None` for everything rather than pretending to a
/// coverage it does not have. The parse tests below are what stop that state
/// from ever reaching a build.
pub fn shipping_table() -> &'static SiteTable {
    static TABLE: std::sync::OnceLock<SiteTable> = std::sync::OnceLock::new();
    TABLE
        .get_or_init(|| SiteTable::parse(SHIPPING_SOURCE).unwrap_or(SiteTable { rows: Vec::new() }))
}

/// The shipping table's source, for the suites that read it directly.
pub const SHIPPING_TABLE: &str = SHIPPING_SOURCE;

/// The corpus table's source, for the suites that read it directly.
///
/// Exposed beside the shipping one so an adversarial suite reaches the fixture
/// origins through the same parser the product uses, rather than through a
/// second list nobody keeps in step.
pub const CORPUS_TABLE: &str = CORPUS_SOURCE;

/// What kind of destination `origin` is, according to the shipping table.
///
/// `None` means the table has nothing to say. It does **not** mean the
/// destination is safe: every other check still applies, and the table narrows
/// a gap rather than closing one.
#[must_use]
pub fn classify_site(origin: &NormalizedOrigin) -> Option<SiteClass> {
    origin
        .host()
        .and_then(|host| shipping_table().class_of(host))
}

#[cfg(test)]
mod tests {
    use super::{classify_site, SiteTable, TableError, CORPUS_SOURCE, SHIPPING_SOURCE};

    use crate::origin::{normalize, NormalizedOrigin};
    use bip_types::identity::{Origin, OriginKind};

    fn tuple(serialization: &str) -> NormalizedOrigin {
        normalize(&Origin {
            kind: OriginKind::Tuple,
            serialization: Some(serialization.to_owned()),
            opaque_id: None,
        })
        .expect("a tuple origin normalizes")
    }

    #[test]
    fn the_shipping_table_parses_and_holds_no_duplicate_host() {
        let table = SiteTable::parse(SHIPPING_SOURCE).expect("the shipping table parses");
        assert!(!table.rows().is_empty());
        for pair in table.rows().windows(2) {
            assert!(
                pair[0].host < pair[1].host,
                "rows must be sorted and unique"
            );
        }
    }

    #[test]
    fn the_corpus_table_parses_through_the_same_parser() {
        let table = SiteTable::parse(CORPUS_SOURCE).expect("the corpus table parses");
        assert!(!table.rows().is_empty());
    }

    /// The one that keeps the two tables honest.
    ///
    /// A fixture host leaking into the shipping table would classify a real
    /// origin nobody reviewed; a shipping host in the corpus table would let a
    /// test pass against data the product does not ship.
    #[test]
    fn the_two_tables_never_name_the_same_host() {
        let shipping = SiteTable::parse(SHIPPING_SOURCE).expect("parses");
        let corpus = SiteTable::parse(CORPUS_SOURCE).expect("parses");
        for row in corpus.rows() {
            assert_eq!(
                shipping.class_of(row.host),
                None,
                "{} is in both tables",
                row.host
            );
            assert_eq!(
                row.host.rsplit('.').next(),
                Some("test"),
                "{} is a corpus row and must be a .test host",
                row.host
            );
        }
        for row in shipping.rows() {
            assert_ne!(
                row.host.rsplit('.').next(),
                Some("test"),
                "{} is a shipping row and must not be a .test host",
                row.host
            );
        }
    }

    #[test]
    fn a_row_that_cannot_be_read_refuses_the_whole_table() {
        assert_eq!(
            SiteTable::parse("only-two\tfields\n").unwrap_err(),
            TableError::MalformedRow { line_number: 1 }
        );
        assert_eq!(
            SiteTable::parse("host.example\tnot_a_class\treason\n").unwrap_err(),
            TableError::UnknownClass { line_number: 1 }
        );
        assert_eq!(
            SiteTable::parse("Host.Example\tbanking\treason\n").unwrap_err(),
            TableError::HostNotLowercase { line_number: 1 }
        );
        assert_eq!(
            SiteTable::parse("a.example\tbanking\tone\na.example\temail\ttwo\n").unwrap_err(),
            TableError::DuplicateHost
        );
    }

    #[test]
    fn a_space_separated_row_is_a_refusal_and_never_a_silent_skip() {
        // The failure mode the allow-terms table has: a row that looks right
        // and does nothing. Here it stops the build instead.
        assert_eq!(
            SiteTable::parse("host.example banking reason\n").unwrap_err(),
            TableError::MalformedRow { line_number: 1 }
        );
    }

    #[test]
    fn comments_and_blank_lines_are_not_rows() {
        let table =
            SiteTable::parse("# a comment\n\nhost.example\tbanking\tbecause\n").expect("parses");
        assert_eq!(table.rows().len(), 1);
    }

    #[test]
    fn a_classified_host_is_found_and_an_ordinary_one_is_not() {
        assert!(classify_site(&tuple("https://mail.google.com")).is_some());
        assert_eq!(classify_site(&tuple("https://example.test")), None);
    }

    #[test]
    fn a_subdomain_of_a_listed_host_is_not_the_listed_host() {
        // Deliberate: matching is exact. A suffix rule would classify
        // attacker-controlled subdomains of anything listed, and a prefix rule
        // would miss nothing while claiming more than the table knows.
        assert_eq!(classify_site(&tuple("https://evil.mail.google.com")), None);
    }
}
