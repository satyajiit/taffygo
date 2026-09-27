// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Allocation-free exact-integer recognition for provider number tokens.

use core::ops::Range;

const I64_MIN_MAGNITUDE: u64 = (i64::MAX as u64) + 1;

/// An `i64` when `spelling` is exactly that integer, including `1.0` and `1e3`.
///
/// The grammar has already been validated by the reader. This pass never
/// concatenates coefficient digits: it indexes the integer and fractional
/// slices in place, removes only verified zero places, then accumulates at
/// most the 19 significant digits an `i64` can hold.
pub(super) fn exact_i64(spelling: &str) -> Option<i64> {
    let number = NumberParts::read(spelling)?;
    let digit_count = number.digit_count()?;
    let scale = i32::try_from(number.fraction.len()).ok()?;
    let net_scale = scale.checked_sub(number.exponent)?;
    let (kept_digits, appended_zeros) = if net_scale > 0 {
        let places = usize::try_from(net_scale).ok()?;
        if places >= digit_count {
            return (0..digit_count)
                .all(|position| number.digit_at(position) == Some(b'0'))
                .then_some(0);
        }
        let kept = digit_count.checked_sub(places)?;
        if !(kept..digit_count).all(|position| number.digit_at(position) == Some(b'0')) {
            return None;
        }
        (kept, 0)
    } else {
        let zeros = usize::try_from(net_scale.checked_neg()?).ok()?;
        // Preserve the reader's historical reconstruction ceiling before
        // stripping leading zeroes: even a zero coefficient with an enormous
        // positive exponent remains a decimal spelling rather than an integer.
        if digit_count.checked_add(zeros)? > 19 {
            return None;
        }
        (digit_count, zeros)
    };

    let first_nonzero = (0..kept_digits).find(|position| number.digit_at(*position) != Some(b'0'));
    let Some(first_nonzero) = first_nonzero else {
        return Some(0);
    };
    let significant_digits = kept_digits
        .checked_sub(first_nonzero)?
        .checked_add(appended_zeros)?;
    if significant_digits > 19 {
        return None;
    }

    let mut magnitude = 0u64;
    for position in first_nonzero..kept_digits {
        magnitude = magnitude
            .checked_mul(10)?
            .checked_add(u64::from(number.digit_at(position)?.checked_sub(b'0')?))?;
    }
    for _ in 0..appended_zeros {
        magnitude = magnitude.checked_mul(10)?;
    }
    signed_magnitude(magnitude, number.negative)
}

fn signed_magnitude(magnitude: u64, negative: bool) -> Option<i64> {
    if !negative {
        return i64::try_from(magnitude).ok();
    }
    if magnitude == I64_MIN_MAGNITUDE {
        Some(i64::MIN)
    } else {
        i64::try_from(magnitude).ok().and_then(i64::checked_neg)
    }
}

struct NumberParts<'a> {
    bytes: &'a [u8],
    negative: bool,
    integer: Range<usize>,
    fraction: Range<usize>,
    exponent: i32,
}

impl<'a> NumberParts<'a> {
    fn read(spelling: &'a str) -> Option<Self> {
        let bytes = spelling.as_bytes();
        let negative = bytes.first().copied() == Some(b'-');
        let mut index = usize::from(negative);
        let integer_start = index;
        while bytes.get(index).is_some_and(u8::is_ascii_digit) {
            index = index.checked_add(1)?;
        }
        if index == integer_start {
            return None;
        }
        let integer = integer_start..index;

        let mut fraction = index..index;
        if bytes.get(index).copied() == Some(b'.') {
            index = index.checked_add(1)?;
            let start = index;
            while bytes.get(index).is_some_and(u8::is_ascii_digit) {
                index = index.checked_add(1)?;
            }
            if index == start {
                return None;
            }
            fraction = start..index;
        }
        let (exponent, end) = read_exponent(spelling, index)?;
        if end != bytes.len() {
            return None;
        }
        Some(Self {
            bytes,
            negative,
            integer,
            fraction,
            exponent,
        })
    }

    fn digit_count(&self) -> Option<usize> {
        self.integer.len().checked_add(self.fraction.len())
    }

    fn digit_at(&self, position: usize) -> Option<u8> {
        if position < self.integer.len() {
            self.bytes
                .get(self.integer.start.checked_add(position)?)
                .copied()
        } else {
            self.bytes
                .get(
                    self.fraction
                        .start
                        .checked_add(position.checked_sub(self.integer.len())?)?,
                )
                .copied()
        }
    }
}

fn read_exponent(spelling: &str, mut index: usize) -> Option<(i32, usize)> {
    let bytes = spelling.as_bytes();
    if !bytes
        .get(index)
        .is_some_and(|byte| matches!(*byte, b'e' | b'E'))
    {
        return Some((0, index));
    }
    index = index.checked_add(1)?;
    let negative = match bytes.get(index).copied() {
        Some(b'-') => {
            index = index.checked_add(1)?;
            true
        }
        Some(b'+') => {
            index = index.checked_add(1)?;
            false
        }
        _ => false,
    };
    let start = index;
    while bytes.get(index).is_some_and(u8::is_ascii_digit) {
        index = index.checked_add(1)?;
    }
    if index == start {
        return None;
    }
    let absolute: i32 = spelling.get(start..index)?.parse().ok()?;
    let exponent = if negative {
        absolute.checked_neg()?
    } else {
        absolute
    };
    Some((exponent, index))
}
