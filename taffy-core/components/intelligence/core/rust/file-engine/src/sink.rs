// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use crate::{FileError, LimitKind};

pub(crate) struct ByteSink {
    bytes: Vec<u8>,
    maximum: usize,
}

impl ByteSink {
    pub(crate) fn new(maximum: usize) -> Self {
        Self {
            bytes: Vec::new(),
            maximum,
        }
    }

    pub(crate) fn position(&self) -> usize {
        self.bytes.len()
    }

    pub(crate) fn push(&mut self, byte: u8) -> Result<(), FileError> {
        self.reserve(1)?;
        self.bytes.push(byte);
        Ok(())
    }

    pub(crate) fn extend(&mut self, value: &[u8]) -> Result<(), FileError> {
        self.reserve(value.len())?;
        self.bytes.extend_from_slice(value);
        Ok(())
    }

    pub(crate) fn le_u16(&mut self, value: u16) -> Result<(), FileError> {
        self.extend(&value.to_le_bytes())
    }

    pub(crate) fn le_u32(&mut self, value: u32) -> Result<(), FileError> {
        self.extend(&value.to_le_bytes())
    }

    pub(crate) fn decimal(&mut self, value: usize) -> Result<(), FileError> {
        self.extend(value.to_string().as_bytes())
    }

    pub(crate) fn into_bytes(self) -> Vec<u8> {
        self.bytes
    }

    fn reserve(&mut self, additional: usize) -> Result<(), FileError> {
        let Some(required) = self.bytes.len().checked_add(additional) else {
            return Err(FileError::LimitExceeded {
                kind: LimitKind::OutputBytes,
                maximum: self.maximum,
            });
        };
        if required > self.maximum {
            return Err(FileError::LimitExceeded {
                kind: LimitKind::OutputBytes,
                maximum: self.maximum,
            });
        }
        self.bytes.reserve(additional);
        Ok(())
    }
}

pub(crate) struct TextSink {
    text: String,
    maximum: usize,
}

impl TextSink {
    pub(crate) fn new(maximum: usize) -> Self {
        Self {
            text: String::new(),
            maximum,
        }
    }

    pub(crate) fn push_str(&mut self, value: &str) -> Result<(), FileError> {
        let Some(required) = self.text.len().checked_add(value.len()) else {
            return Err(FileError::LimitExceeded {
                kind: LimitKind::OutputBytes,
                maximum: self.maximum,
            });
        };
        if required > self.maximum {
            return Err(FileError::LimitExceeded {
                kind: LimitKind::OutputBytes,
                maximum: self.maximum,
            });
        }
        self.text.push_str(value);
        Ok(())
    }

    pub(crate) fn decimal(&mut self, value: usize) -> Result<(), FileError> {
        self.push_str(&value.to_string())
    }

    pub(crate) fn into_bytes(self) -> Vec<u8> {
        self.text.into_bytes()
    }
}
