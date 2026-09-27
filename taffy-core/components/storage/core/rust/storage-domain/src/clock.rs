// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Time, injected.
//!
//! No function in this crate reads a system clock. Migration records, source
//! timestamps, and deletion receipts all take their time from a [`Clock`] the
//! caller supplies, so a test can pin every timestamp in a database and compare
//! two runs byte for byte.

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
/// Fixed width and zero padded, so the text `SQLite` sorts is also the order
/// events happened in — no date functions, no locale, no surprises when the
/// same column is read by C++ and by a test.
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Timestamp(String);

impl Timestamp {
    /// Validates and wraps `YYYY-MM-DDTHH:MM:SSZ`.
    ///
    /// This is a decode boundary as well as a constructor: rows read back from
    /// the browser-owned database arrive here, so a value that is not a real
    /// UTC instant must fail closed rather than become an ordering key.
    pub fn new(raw: &str) -> Result<Self, TimestampError> {
        validate_utc_instant(raw)?;
        Ok(Self(raw.to_owned()))
    }

    /// The stored representation.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl fmt::Display for Timestamp {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.0)
    }
}

/// A wall-clock source.
pub trait Clock {
    /// The current time.
    fn now_utc(&self) -> Timestamp;
}

/// A clock that never moves, for tests and for deterministic fixtures.
#[derive(Clone, Debug)]
pub struct FixedClock(Timestamp);

impl FixedClock {
    /// Builds a clock stuck at `at`.
    pub fn new(at: Timestamp) -> Self {
        Self(at)
    }
}

impl Clock for FixedClock {
    fn now_utc(&self) -> Timestamp {
        self.0.clone()
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
/// `model_router::time::Timestamp` and `task_engine::Timestamp` carry the same
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
}
