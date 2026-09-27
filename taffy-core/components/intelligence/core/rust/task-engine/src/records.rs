// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Portable record views consumed by task scope and artifact generation.
//!
//! These are reducer inputs, not physical storage rows. The browser storage
//! broker maps its schema-domain records into these bounded views before a
//! deterministic artifact is requested, so the task engine never imports the
//! storage implementation or gains filesystem/database access.

use core::fmt;

/// Why an opaque record identifier did not parse.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct IdError;

impl fmt::Display for IdError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str("expected 32 lowercase hexadecimal digits")
    }
}

fn hex_digit(byte: u8) -> Option<u8> {
    match byte {
        b'0'..=b'9' => Some(byte - b'0'),
        b'a'..=b'f' => Some(byte - b'a' + 10),
        _ => None,
    }
}

fn parse_128(raw: &str) -> Result<[u8; 16], IdError> {
    let bytes = raw.as_bytes();
    if bytes.len() != 32 {
        return Err(IdError);
    }
    let mut out = [0u8; 16];
    for (index, slot) in out.iter_mut().enumerate() {
        let high = bytes
            .get(index.saturating_mul(2))
            .copied()
            .and_then(hex_digit)
            .ok_or(IdError)?;
        let low = bytes
            .get(index.saturating_mul(2).saturating_add(1))
            .copied()
            .and_then(hex_digit)
            .ok_or(IdError)?;
        *slot = (high << 4) | low;
    }
    Ok(out)
}

fn render_128(value: &[u8; 16]) -> String {
    let mut out = String::with_capacity(32);
    for byte in value {
        out.push(char::from_digit(u32::from(byte >> 4), 16).unwrap_or('0'));
        out.push(char::from_digit(u32::from(byte & 0x0f), 16).unwrap_or('0'));
    }
    out
}

macro_rules! record_id {
    ($name:ident, $what:literal) => {
        #[doc = concat!("An opaque ", $what, ".")]
        #[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
        pub struct $name([u8; 16]);

        impl $name {
            /// Wraps 16 caller-supplied bytes.
            pub const fn from_bytes(bytes: [u8; 16]) -> Self {
                Self(bytes)
            }

            /// Parses the closed stored representation.
            pub fn parse(raw: &str) -> Result<Self, IdError> {
                parse_128(raw).map(Self)
            }

            /// The stable text representation.
            pub fn to_text(self) -> String {
                render_128(&self.0)
            }
        }

        impl fmt::Display for $name {
            fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
                formatter.write_str(&render_128(&self.0))
            }
        }

        impl fmt::Debug for $name {
            fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
                write!(
                    formatter,
                    concat!(stringify!($name), "({})"),
                    render_128(&self.0)
                )
            }
        }
    };
}

record_id!(WorkspaceId, "workspace identifier");
record_id!(SourceId, "source identifier");
record_id!(ObservationId, "observation identifier");
record_id!(FactId, "fact identifier");
record_id!(ProvenanceId, "provenance identifier");

/// Why a timestamp was refused.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct TimestampError;

impl fmt::Display for TimestampError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str("expected a UTC timestamp of the form YYYY-MM-DDTHH:MM:SSZ")
    }
}

/// A validated fixed-width UTC timestamp used only as artifact evidence.
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Timestamp(String);

impl Timestamp {
    /// Validates and wraps `YYYY-MM-DDTHH:MM:SSZ`.
    pub fn new(raw: &str) -> Result<Self, TimestampError> {
        validate_utc_instant(raw)?;
        Ok(Self(raw.to_owned()))
    }

    /// The validated timestamp.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl fmt::Display for Timestamp {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.0)
    }
}

/// The one shared rule for `YYYY-MM-DDTHH:MM:SSZ`: shape, then the calendar.
///
/// A value that is not a real UTC instant has no chronological position, so
/// admitting one breaks the only property this fixed-width format exists to
/// give — that the text an index sorts is the order the events happened in.
/// Shape alone does not establish that: `2026-02-31T00:00:00Z` and
/// `9999-99-99T99:99:99Z` are both well shaped and neither is a time.
///
/// `taffy_storage::clock::Timestamp` and `model_router::time::Timestamp` carry
/// the same wire format and enforce exactly this rule. The
/// three cannot share one implementation today because that needs a crate
/// beneath all of them, which the component graph does not have.
fn validate_utc_instant(raw: &str) -> Result<(), TimestampError> {
    let bytes = raw.as_bytes();
    if bytes.len() != 20 {
        return Err(TimestampError);
    }
    for (index, byte) in bytes.iter().enumerate() {
        let valid = match index {
            4 | 7 => *byte == b'-',
            10 => *byte == b'T',
            13 | 16 => *byte == b':',
            19 => *byte == b'Z',
            _ => byte.is_ascii_digit(),
        };
        if !valid {
            return Err(TimestampError);
        }
    }
    let field = |from: usize, to: usize| -> Option<u32> {
        raw.get(from..to).and_then(|text| text.parse::<u32>().ok())
    };
    let (Some(year), Some(month), Some(day), Some(hour), Some(minute), Some(second)) = (
        field(0, 4),
        field(5, 7),
        field(8, 10),
        field(11, 13),
        field(14, 16),
        field(17, 19),
    ) else {
        return Err(TimestampError);
    };
    // A month outside 1..=12 has zero days, so this one comparison closes both
    // the month and the day. A leap second is a real UTC reading, so 60 stays.
    if day == 0 || day > days_in_month(year, month) || hour > 23 || minute > 59 || second > 60 {
        return Err(TimestampError);
    }
    Ok(())
}

/// Days in one Gregorian month; zero for a month number that does not exist.
const fn days_in_month(year: u32, month: u32) -> u32 {
    match month {
        1 | 3 | 5 | 7 | 8 | 10 | 12 => 31,
        4 | 6 | 9 | 11 => 30,
        2 => {
            if is_leap_year(year) {
                29
            } else {
                28
            }
        }
        _ => 0,
    }
}

/// The Gregorian leap rule in full: 2100 is not a leap year, 2000 is.
const fn is_leap_year(year: u32) -> bool {
    year.is_multiple_of(4) && (!year.is_multiple_of(100) || year.is_multiple_of(400))
}

macro_rules! closed_enum {
    ($name:ident, $( $variant:ident => $text:literal ),+ $(,)?) => {
        #[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
        pub enum $name {
            $(
                #[doc = concat!("`", $text, "`")]
                $variant
            ),+
        }

        impl $name {
            /// The fixed contract spelling.
            pub const fn as_str(self) -> &'static str {
                match self {
                    $( Self::$variant => $text ),+
                }
            }
        }
    };
}

closed_enum!(
    DeletionState,
    Active => "ACTIVE",
    Deleting => "DELETING",
    Deleted => "DELETED",
    Blocked => "BLOCKED",
);
closed_enum!(
    SourceKind,
    WebPage => "WEB_PAGE",
    Pdf => "PDF",
    UserFile => "USER_FILE",
    UserText => "USER_TEXT",
    SearchResult => "SEARCH_RESULT",
);
closed_enum!(
    Ownership,
    External => "EXTERNAL",
    UserProvided => "USER_PROVIDED",
    Generated => "GENERATED",
);
closed_enum!(
    Sensitivity,
    Public => "PUBLIC",
    Personal => "PERSONAL",
    Sensitive => "SENSITIVE",
    HighlySensitive => "HIGHLY_SENSITIVE",
    Prohibited => "PROHIBITED",
);
closed_enum!(
    FactClassification,
    UserEntered => "USER_ENTERED",
    Extracted => "EXTRACTED",
    Summarized => "SUMMARIZED",
    Inferred => "INFERRED",
);
closed_enum!(
    FactStatus,
    Candidate => "CANDIDATE",
    Accepted => "ACCEPTED",
    Corrected => "CORRECTED",
    Superseded => "SUPERSEDED",
    Removed => "REMOVED",
);
closed_enum!(
    ProvenanceKind,
    Dom => "DOM",
    Accessibility => "AX",
    StructuredData => "STRUCTURED_DATA",
    User => "USER",
    Model => "MODEL",
    Tool => "TOOL",
);

/// A source view sufficient for deterministic artifact rendering.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Source {
    /// Identity.
    pub source_id: SourceId,
    /// Kind of material.
    pub kind: SourceKind,
    /// Canonical locator when retention permits it.
    pub canonical_locator: Option<String>,
    /// Display-safe locator.
    pub display_locator: String,
    /// Origin when it has one.
    pub origin: Option<String>,
    /// User-visible title.
    pub title: Option<String>,
    /// First observation.
    pub first_seen_at: Timestamp,
    /// Latest observation.
    pub last_observed_at: Option<Timestamp>,
    /// Provenance ownership.
    pub ownership: Ownership,
    /// Data classification.
    pub sensitivity: Sensitivity,
    /// Browser-owned retention class identifier.
    pub retention_class: String,
    /// Deletion lifecycle snapshot.
    pub deletion_state: DeletionState,
}

/// A fact view sufficient for deterministic artifact rendering.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Fact {
    /// Identity.
    pub fact_id: FactId,
    /// Workspace identity.
    pub workspace_id: WorkspaceId,
    /// Stable subject key.
    pub subject_key: String,
    /// Stable predicate.
    pub predicate: String,
    /// Caller-typed value.
    pub typed_value: String,
    /// Unit when one exists.
    pub unit: Option<String>,
    /// How the fact was produced.
    pub classification: FactClassification,
    /// Confidence in basis points.
    pub confidence_basis_points: Option<i64>,
    /// Observation evidence time.
    pub observation_time: Timestamp,
    /// Data classification.
    pub sensitivity: Sensitivity,
    /// Fact lifecycle snapshot.
    pub status: FactStatus,
    /// Corrected predecessor, when one exists.
    pub supersedes_fact_id: Option<FactId>,
    /// Browser-owned retention class identifier.
    pub retention_class: String,
}

/// One inspectable evidence locator for a fact.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ProvenanceLocator {
    /// Identity.
    pub provenance_id: ProvenanceId,
    /// Fact identity.
    pub fact_id: FactId,
    /// Source identity.
    pub source_id: SourceId,
    /// Observation identity when one exists.
    pub observation_id: Option<ObservationId>,
    /// Evidence kind.
    pub kind: ProvenanceKind,
    /// Human-inspectable location description.
    pub location_descriptor: Option<String>,
    /// Extraction rule version when one exists.
    pub extraction_rule_version: Option<String>,
    /// Closed transformation description.
    pub transformation_chain: String,
    /// Capture evidence time.
    pub captured_at: Timestamp,
}

#[cfg(test)]
mod tests {
    use super::{SourceId, Timestamp};

    #[test]
    fn identifiers_and_timestamps_refuse_noncanonical_input() {
        assert!(SourceId::parse("00112233445566778899aabbccddeeff").is_ok());
        assert!(SourceId::parse("00112233445566778899AABBCCDDEEFF").is_err());
        assert!(Timestamp::new("2026-08-22T10:20:30Z").is_ok());
        assert!(Timestamp::new("2026-08-22 10:20:30Z").is_err());
    }

    #[test]
    fn a_well_shaped_impossible_date_is_not_a_timestamp() {
        // The exact value the shape-only rule used to accept.
        assert!(Timestamp::new("2026-02-31T00:00:00Z").is_err());
        assert!(Timestamp::new("9999-99-99T99:99:99Z").is_err());
        assert!(Timestamp::new("2026-13-01T00:00:00Z").is_err());
        assert!(Timestamp::new("2026-00-01T00:00:00Z").is_err());
        assert!(Timestamp::new("2026-01-00T00:00:00Z").is_err());
        assert!(Timestamp::new("2026-04-31T00:00:00Z").is_err());
        assert!(Timestamp::new("2026-01-01T24:00:00Z").is_err());
        assert!(Timestamp::new("2026-01-01T00:60:00Z").is_err());
        assert!(Timestamp::new("2026-01-01T00:00:61Z").is_err());
    }

    #[test]
    fn the_calendar_boundary_is_the_gregorian_one() {
        assert!(Timestamp::new("2024-02-29T00:00:00Z").is_ok());
        assert!(Timestamp::new("2026-02-29T00:00:00Z").is_err());
        assert!(Timestamp::new("2000-02-29T00:00:00Z").is_ok());
        assert!(Timestamp::new("2100-02-29T00:00:00Z").is_err());
        assert!(Timestamp::new("2026-12-31T23:59:60Z").is_ok());
        assert!(Timestamp::new("0001-01-01T00:00:00Z").is_ok());
    }
}
