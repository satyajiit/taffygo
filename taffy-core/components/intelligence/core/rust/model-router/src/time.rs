// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Time values the router reads but never reads a clock for.
//!
//! Nothing in this crate calls a system clock. Catalog generation times arrive
//! as data, and deadlines arrive as monotonic values the caller supplies, so
//! every routing decision is reproducible from its inputs. That is the
//! deterministic-by-construction rule of the testing contract, applied to the
//! one place a router is most tempted to break it.

use core::fmt;

/// Why a timestamp was refused.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct TimestampError;

impl fmt::Display for TimestampError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("expected a UTC timestamp of the form YYYY-MM-DDTHH:MM:SSZ")
    }
}

/// A fixed-width UTC timestamp.
///
/// The canonical form is fixed width and zero padded, so lexicographic order
/// and chronological order are the same relation. That is what lets the
/// catalog merge compare two generation times without a date library.
#[derive(
    Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, serde::Serialize, serde::Deserialize,
)]
#[serde(try_from = "String", into = "String")]
pub struct Timestamp(String);

impl Timestamp {
    /// Validates and wraps `YYYY-MM-DDTHH:MM:SSZ`.
    pub fn new(raw: &str) -> Result<Self, TimestampError> {
        validate_utc_instant(raw)?;
        Ok(Self(raw.to_owned()))
    }

    /// The canonical string.
    pub fn as_str(&self) -> &str {
        &self.0
    }

    /// The ordering floor.
    ///
    /// A document whose generation time is missing or malformed takes this
    /// value, so it can never win a merge against a real one.
    pub fn floor() -> Self {
        Self("0001-01-01T00:00:00Z".to_owned())
    }
}

impl TryFrom<String> for Timestamp {
    type Error = TimestampError;

    fn try_from(raw: String) -> Result<Self, TimestampError> {
        Self::new(&raw)
    }
}

impl From<Timestamp> for String {
    fn from(value: Timestamp) -> Self {
        value.0
    }
}

impl fmt::Display for Timestamp {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.0)
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
/// `taffy_storage::clock::Timestamp` and `task_engine::Timestamp` carry the same
/// wire format and enforce exactly this rule. The
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

/// A deadline on a monotonic timeline the caller owns.
///
/// The router never asks what time it is; the host supplies both the reading
/// and the deadline, so a test can put either wherever it needs them.
#[derive(
    Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, serde::Serialize, serde::Deserialize,
)]
#[serde(transparent)]
pub struct MonotonicMillis(u64);

impl MonotonicMillis {
    /// Wraps a reading.
    pub fn new(value: u64) -> Self {
        Self(value)
    }

    /// The raw reading.
    pub fn get(self) -> u64 {
        self.0
    }

    /// Milliseconds from `self` until `later`, or zero if `later` has passed.
    pub fn until(self, later: Self) -> u64 {
        later.0.saturating_sub(self.0)
    }
}

/// A monotonic time source.
///
/// Library code takes this rather than reading a clock, so retry planning and
/// deadline checks are reproducible.
pub trait MonotonicClock {
    /// The current reading.
    fn now(&self) -> MonotonicMillis;
}

#[cfg(test)]
mod tests {
    use super::Timestamp;

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

    #[test]
    fn the_ordering_floor_is_itself_a_valid_instant() {
        assert_eq!(Timestamp::floor().as_str(), "0001-01-01T00:00:00Z");
        assert!(Timestamp::new(Timestamp::floor().as_str()).is_ok());
    }
}
