// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use core::cmp::Ordering;

use crate::Error;

const MAX_SCALE: u8 = 18;

/// A normalized exact base-ten value. No binary floating point enters a table.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct Decimal {
    negative: bool,
    coefficient: u128,
    scale: u8,
}

impl Decimal {
    pub(crate) fn zero() -> Self {
        Self {
            negative: false,
            coefficient: 0,
            scale: 0,
        }
    }

    pub(crate) fn parse(value: &str) -> Result<Self, Error> {
        let bytes = value.as_bytes();
        if bytes.is_empty() {
            return Err(Error::InvalidDecimal);
        }
        let (negative, digits) = match bytes.first() {
            Some(b'-') => (true, bytes.get(1..).ok_or(Error::InvalidDecimal)?),
            Some(b'+') => (false, bytes.get(1..).ok_or(Error::InvalidDecimal)?),
            Some(_) => (false, bytes),
            None => return Err(Error::InvalidDecimal),
        };
        if digits.is_empty() {
            return Err(Error::InvalidDecimal);
        }
        let mut coefficient = 0_u128;
        let mut scale = 0_u8;
        let mut seen_dot = false;
        let mut has_digit = false;
        for byte in digits {
            match *byte {
                b'0'..=b'9' => {
                    has_digit = true;
                    coefficient = coefficient
                        .checked_mul(10)
                        .and_then(|number| number.checked_add(u128::from(*byte - b'0')))
                        .ok_or(Error::DecimalOverflow)?;
                    if seen_dot {
                        scale = scale.checked_add(1).ok_or(Error::DecimalOverflow)?;
                        if scale > MAX_SCALE {
                            return Err(Error::DecimalOverflow);
                        }
                    }
                }
                b'.' if !seen_dot => seen_dot = true,
                _ => return Err(Error::InvalidDecimal),
            }
        }
        if !has_digit {
            return Err(Error::InvalidDecimal);
        }
        let mut value = Self {
            negative: negative && coefficient != 0,
            coefficient,
            scale,
        };
        value.normalize();
        Ok(value)
    }

    pub(crate) fn checked_add(self, other: Self) -> Result<Self, Error> {
        let scale = self.scale.max(other.scale);
        let left = self.scaled_coefficient(scale)?;
        let right = other.scaled_coefficient(scale)?;
        let (negative, coefficient) = match (self.negative, other.negative) {
            (false, false) => (
                false,
                left.checked_add(right).ok_or(Error::DecimalOverflow)?,
            ),
            (true, true) => (true, left.checked_add(right).ok_or(Error::DecimalOverflow)?),
            (false, true) if left >= right => (false, left - right),
            (false, true) => (true, right - left),
            (true, false) if right >= left => (false, right - left),
            (true, false) => (true, left - right),
        };
        let mut value = Self {
            negative: negative && coefficient != 0,
            coefficient,
            scale,
        };
        value.normalize();
        Ok(value)
    }

    fn scaled_coefficient(self, scale: u8) -> Result<u128, Error> {
        let difference = scale.saturating_sub(self.scale);
        let factor = 10_u128
            .checked_pow(u32::from(difference))
            .ok_or(Error::DecimalOverflow)?;
        self.coefficient
            .checked_mul(factor)
            .ok_or(Error::DecimalOverflow)
    }

    fn normalize(&mut self) {
        while self.scale > 0 && self.coefficient.is_multiple_of(10) {
            self.coefficient /= 10;
            self.scale -= 1;
        }
        if self.coefficient == 0 {
            self.negative = false;
        }
    }

    pub(crate) fn canonical(self) -> String {
        let mut digits = self.coefficient.to_string();
        if self.scale > 0 {
            let scale = usize::from(self.scale);
            if digits.len() <= scale {
                let zeroes = scale.saturating_add(1).saturating_sub(digits.len());
                digits = format!("{}{}", "0".repeat(zeroes), digits);
            }
            let split = digits.len().saturating_sub(scale);
            digits.insert(split, '.');
        }
        if self.negative {
            digits.insert(0, '-');
        }
        digits
    }
}

impl Ord for Decimal {
    fn cmp(&self, other: &Self) -> Ordering {
        if self.negative != other.negative {
            return if self.negative {
                Ordering::Less
            } else {
                Ordering::Greater
            };
        }
        let scale = self.scale.max(other.scale);
        let left = self.scaled_coefficient(scale);
        let right = other.scaled_coefficient(scale);
        let order = match (left, right) {
            (Ok(left), Ok(right)) => left.cmp(&right),
            // Both inputs were already bounded u128 decimals. If scaling one
            // overflows, its non-zero digit count makes it larger in magnitude.
            (Err(_), Ok(_)) => Ordering::Greater,
            (Ok(_), Err(_)) => Ordering::Less,
            (Err(_), Err(_)) => self
                .coefficient
                .to_string()
                .len()
                .saturating_add(usize::from(scale.saturating_sub(self.scale)))
                .cmp(
                    &other
                        .coefficient
                        .to_string()
                        .len()
                        .saturating_add(usize::from(scale.saturating_sub(other.scale))),
                ),
        };
        if self.negative {
            order.reverse()
        } else {
            order
        }
    }
}

impl PartialOrd for Decimal {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

#[cfg(test)]
mod tests {
    use core::cmp::Ordering;

    use super::Decimal;
    use crate::Error;

    fn decimal(value: &str) -> Decimal {
        let Ok(value) = Decimal::parse(value) else {
            unreachable!("the decimal fixture is valid")
        };
        value
    }

    #[test]
    fn parsing_normalizes_sign_zero_and_fractional_zeroes() {
        for (input, canonical) in [
            ("+001.2300", "1.23"),
            ("-0.000", "0"),
            (".5", "0.5"),
            ("5.", "5"),
        ] {
            assert_eq!(decimal(input).canonical(), canonical);
        }
        let padded = format!("{}1", "0".repeat(1_024));
        assert_eq!(decimal(&padded).canonical(), "1");
    }

    #[test]
    fn grammar_and_scale_fail_closed() {
        for input in ["", "+", "-", ".", "1e2", " 1", "1 ", "1.2.3"] {
            assert_eq!(
                Decimal::parse(input),
                Err(Error::InvalidDecimal),
                "{input:?}"
            );
        }
        assert_eq!(
            Decimal::parse("0.0000000000000000001"),
            Err(Error::DecimalOverflow)
        );
    }

    #[test]
    fn comparison_remains_exact_when_aligning_scales_would_overflow() {
        let integer = decimal("340282366920938463463374607431768211455");
        let fraction = decimal("34028236692093846346337460743176821145.5");
        assert_eq!(integer.cmp(&fraction), Ordering::Greater);
        assert_eq!(decimal("-0.1").cmp(&decimal("-0.01")), Ordering::Less);
    }

    #[test]
    fn addition_is_exact_and_checked() {
        let sum = decimal("0.1").checked_add(decimal("0.2"));
        assert_eq!(sum.map(Decimal::canonical), Ok("0.3".to_owned()));
        assert_eq!(
            decimal("340282366920938463463374607431768211455").checked_add(decimal("1")),
            Err(Error::DecimalOverflow)
        );
    }
}
