// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Output accounting that stops retaining bytes as soon as a budget is crossed.

use core::fmt;

use super::error::ArtifactError;

pub(super) struct ArtifactWriter {
    content: String,
    bytes: u64,
    limit: u64,
    overflowed: bool,
}

impl ArtifactWriter {
    pub(super) const fn new(limit: u64) -> Self {
        Self {
            content: String::new(),
            bytes: 0,
            limit,
            overflowed: false,
        }
    }

    pub(super) fn push_str(&mut self, value: &str) {
        let _ = fmt::Write::write_str(self, value);
    }

    pub(super) fn finish(self) -> Result<String, ArtifactError> {
        if self.overflowed {
            return Err(ArtifactError::TooLarge {
                bytes: self.bytes,
                limit: self.limit,
            });
        }
        Ok(self.content)
    }
}

impl fmt::Write for ArtifactWriter {
    fn write_str(&mut self, value: &str) -> fmt::Result {
        self.bytes = self
            .bytes
            .saturating_add(u64::try_from(value.len()).unwrap_or(u64::MAX));
        if self.bytes > self.limit {
            if !self.overflowed {
                // Release the at-most-limit allocation immediately. Rendering
                // continues only to preserve the exact `TooLarge.bytes`
                // diagnostic; no rejected artifact body is retained.
                self.content = String::new();
                self.overflowed = true;
            }
        } else if !self.overflowed {
            self.content.push_str(value);
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::ArtifactWriter;

    #[test]
    fn overflow_counts_the_full_document_without_retaining_it() {
        let mut writer = ArtifactWriter::new(4);
        writer.push_str("1234");
        writer.push_str("56789");
        assert_eq!(
            writer.finish().err(),
            Some(super::ArtifactError::TooLarge { bytes: 9, limit: 4 })
        );
    }
}
