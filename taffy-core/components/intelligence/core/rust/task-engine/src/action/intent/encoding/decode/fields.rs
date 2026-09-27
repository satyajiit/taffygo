// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::super::super::ActionIntentCodecError;

pub(super) const MAX_ID_BYTES: usize = 256;
pub(super) const MAX_ADDRESS_BYTES: usize = crate::tool::MAX_ARGUMENT_VALUE_BYTES;
pub(super) const MAX_TOOL_NAME_BYTES: usize = 128;
const MAX_ORIGIN_BYTES: usize = 2_048;

pub(super) struct Field<'a> {
    pub tag: u8,
    pub value: &'a [u8],
}

pub(super) fn parse(mut input: &[u8]) -> Result<Vec<Field<'_>>, ActionIntentCodecError> {
    let mut fields = Vec::new();
    let mut last = None;
    while !input.is_empty() {
        if input.len() < 9 {
            return Err(ActionIntentCodecError::Truncated);
        }
        let tag = *input.first().ok_or(ActionIntentCodecError::Truncated)?;
        if last.is_some_and(|prior| tag <= prior) {
            return Err(ActionIntentCodecError::DuplicateOrOutOfOrderTag);
        }
        let length_bytes = input.get(1..9).ok_or(ActionIntentCodecError::Truncated)?;
        let length = u64::from_le_bytes(
            length_bytes
                .try_into()
                .map_err(|_| ActionIntentCodecError::Truncated)?,
        );
        let length = usize::try_from(length).map_err(|_| ActionIntentCodecError::TooLarge)?;
        input = input.get(9..).ok_or(ActionIntentCodecError::Truncated)?;
        if input.len() < length {
            return Err(ActionIntentCodecError::Truncated);
        }
        let (value, rest) = input.split_at(length);
        fields.push(Field { tag, value });
        input = rest;
        last = Some(tag);
    }
    Ok(fields)
}

pub(super) fn exact<'a, const N: usize>(
    fields: &'a [Field<'a>],
    tags: [u8; N],
) -> Result<[&'a [u8]; N], ActionIntentCodecError> {
    if fields.len() < N {
        return Err(ActionIntentCodecError::MissingField);
    }
    if fields.len() > N {
        return Err(ActionIntentCodecError::UnknownTag);
    }
    for (field, expected) in fields.iter().zip(tags) {
        if field.tag != expected {
            return Err(ActionIntentCodecError::UnknownTag);
        }
    }
    let mut values = fields.iter();
    Ok(std::array::from_fn(|_| {
        values.next().map_or(&[] as &[u8], |field| field.value)
    }))
}

pub(super) fn byte(value: &[u8]) -> Result<u8, ActionIntentCodecError> {
    match value {
        [value] => Ok(*value),
        _ => Err(ActionIntentCodecError::InvalidValue),
    }
}

pub(super) fn boolean(value: &[u8]) -> Result<bool, ActionIntentCodecError> {
    match value {
        [0] => Ok(false),
        [1] => Ok(true),
        _ => Err(ActionIntentCodecError::InvalidValue),
    }
}

pub(super) fn u32_value(value: &[u8]) -> Result<u32, ActionIntentCodecError> {
    Ok(u32::from_le_bytes(
        value
            .try_into()
            .map_err(|_| ActionIntentCodecError::InvalidValue)?,
    ))
}

pub(super) fn u64_value(value: &[u8]) -> Result<u64, ActionIntentCodecError> {
    Ok(u64::from_le_bytes(
        value
            .try_into()
            .map_err(|_| ActionIntentCodecError::InvalidValue)?,
    ))
}

pub(super) fn optional_u64(value: &[u8]) -> Result<Option<u64>, ActionIntentCodecError> {
    match value {
        [0] => Ok(None),
        [1, bytes @ ..] if bytes.len() == 8 => Ok(Some(u64::from_le_bytes(
            bytes
                .try_into()
                .map_err(|_| ActionIntentCodecError::InvalidValue)?,
        ))),
        _ => Err(ActionIntentCodecError::InvalidValue),
    }
}

pub(super) fn optional_byte(value: &[u8]) -> Result<Option<u8>, ActionIntentCodecError> {
    match value {
        [0] => Ok(None),
        [1, value] => Ok(Some(*value)),
        _ => Err(ActionIntentCodecError::InvalidValue),
    }
}

pub(super) fn text(value: &[u8], max: usize) -> Result<String, ActionIntentCodecError> {
    let value = std::str::from_utf8(value).map_err(|_| ActionIntentCodecError::InvalidUtf8)?;
    if value.is_empty() || value.len() > max || value.chars().any(forbidden_character) {
        return Err(ActionIntentCodecError::InvalidValue);
    }
    Ok(value.to_owned())
}

pub(super) fn optional_text(
    value: &[u8],
    max: usize,
) -> Result<Option<String>, ActionIntentCodecError> {
    match value {
        [0] => Ok(None),
        [1, bytes @ ..] => text(bytes, max).map(Some),
        _ => Err(ActionIntentCodecError::InvalidValue),
    }
}

pub(super) fn id(value: &[u8]) -> Result<String, ActionIntentCodecError> {
    let value = text(value, MAX_ID_BYTES)?;
    if value.chars().any(char::is_whitespace) {
        Err(ActionIntentCodecError::InvalidValue)
    } else {
        Ok(value)
    }
}

pub(super) fn address(value: &[u8]) -> Result<String, ActionIntentCodecError> {
    let value = text(value, MAX_ADDRESS_BYTES)?;
    if crate::tool::semantic::is_https_address(&value) {
        Ok(value)
    } else {
        Err(ActionIntentCodecError::InvalidValue)
    }
}

pub(super) fn origin_value(value: &[u8]) -> Result<String, ActionIntentCodecError> {
    let value = text(value, MAX_ORIGIN_BYTES)?;
    if value.chars().any(char::is_whitespace) {
        Err(ActionIntentCodecError::InvalidValue)
    } else {
        Ok(value)
    }
}

pub(super) fn optional_address(value: &[u8]) -> Result<Option<String>, ActionIntentCodecError> {
    match value {
        [0] => Ok(None),
        [1, bytes @ ..] => address(bytes).map(Some),
        _ => Err(ActionIntentCodecError::InvalidValue),
    }
}

pub(super) fn tool_name(value: &[u8]) -> Result<String, ActionIntentCodecError> {
    let name = text(value, MAX_TOOL_NAME_BYTES)?;
    let valid = name.split('.').all(|part| {
        !part.is_empty()
            && part
                .bytes()
                .all(|byte| byte.is_ascii_lowercase() || byte.is_ascii_digit() || byte == b'_')
    });
    if valid {
        Ok(name)
    } else {
        Err(ActionIntentCodecError::InvalidValue)
    }
}

fn forbidden_character(character: char) -> bool {
    character.is_control()
        || matches!(
            character,
            '\u{061c}'
                | '\u{200e}'
                | '\u{200f}'
                | '\u{202a}'..='\u{202e}'
                | '\u{2066}'..='\u{2069}'
        )
}
