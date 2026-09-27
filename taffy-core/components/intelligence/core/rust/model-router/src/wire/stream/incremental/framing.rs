// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Bounded byte-to-frame decoding for NDJSON and SSE.

use crate::json;

const SSE_PREFIX_BYTES: usize = 6;
/// Prevents a byte-small newline flood from amplifying into millions of
/// transient line/frame allocations below the response byte ceiling.
const MAX_STREAM_LINES: usize = 32_768;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(super) enum FramingDefect {
    InputTooLarge,
    FrameTooLarge,
    InvalidUtf8,
    MalformedFrame,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Framing {
    Auto,
    Ndjson,
    Sse,
    ManagedNdjson,
}

#[derive(Debug)]
pub(super) struct FrameDecoder {
    framing: Framing,
    pending: Vec<u8>,
    scanned: usize,
    lines: usize,
    total_bytes: usize,
    max_total_bytes: usize,
}

impl FrameDecoder {
    pub(super) fn direct(max_total_bytes: usize) -> Self {
        Self {
            framing: Framing::Auto,
            pending: Vec::new(),
            scanned: 0,
            lines: 0,
            total_bytes: 0,
            max_total_bytes,
        }
    }

    pub(super) fn managed(max_total_bytes: usize) -> Self {
        Self {
            framing: Framing::ManagedNdjson,
            pending: Vec::new(),
            scanned: 0,
            lines: 0,
            total_bytes: 0,
            max_total_bytes,
        }
    }

    pub(super) fn push(&mut self, chunk: &[u8]) -> Result<Vec<String>, FramingDefect> {
        self.total_bytes = self
            .total_bytes
            .checked_add(chunk.len())
            .ok_or(FramingDefect::InputTooLarge)?;
        if self.total_bytes > self.max_total_bytes {
            return Err(FramingDefect::InputTooLarge);
        }
        self.pending.extend_from_slice(chunk);
        self.take_complete_lines()
    }

    pub(super) fn finish(&mut self) -> Result<Vec<String>, FramingDefect> {
        let mut frames = self.take_complete_lines()?;
        if self.pending.is_empty() {
            return Ok(frames);
        }
        if self.framing == Framing::ManagedNdjson {
            return Err(FramingDefect::MalformedFrame);
        }
        let pending = std::mem::take(&mut self.pending);
        self.scanned = 0;
        self.note_line()?;
        let line = String::from_utf8(pending).map_err(|_| FramingDefect::InvalidUtf8)?;
        self.process_line(line, &mut frames)?;
        Ok(frames)
    }

    fn take_complete_lines(&mut self) -> Result<Vec<String>, FramingDefect> {
        let unscanned = self
            .pending
            .get(self.scanned..)
            .ok_or(FramingDefect::MalformedFrame)?;
        if !unscanned.contains(&b'\n') {
            self.scanned = self.pending.len();
            if self.pending.len() > json::MAX_INPUT_BYTES + SSE_PREFIX_BYTES {
                return Err(FramingDefect::FrameTooLarge);
            }
            return Ok(Vec::new());
        }

        let bytes = std::mem::take(&mut self.pending);
        let mut frames = Vec::new();
        let mut line_start = 0usize;
        for (index, byte) in bytes.iter().enumerate() {
            if *byte != b'\n' {
                continue;
            }
            self.note_line()?;
            let line_bytes = bytes
                .get(line_start..index)
                .ok_or(FramingDefect::MalformedFrame)?;
            let line = std::str::from_utf8(line_bytes).map_err(|_| FramingDefect::InvalidUtf8)?;
            self.process_line(line.to_owned(), &mut frames)?;
            line_start = index + 1;
        }
        let remainder = bytes
            .get(line_start..)
            .ok_or(FramingDefect::MalformedFrame)?;
        self.pending.extend_from_slice(remainder);
        self.scanned = self.pending.len();
        if self.pending.len() > json::MAX_INPUT_BYTES + SSE_PREFIX_BYTES {
            return Err(FramingDefect::FrameTooLarge);
        }
        Ok(frames)
    }

    fn note_line(&mut self) -> Result<(), FramingDefect> {
        self.lines = self
            .lines
            .checked_add(1)
            .ok_or(FramingDefect::InputTooLarge)?;
        if self.lines > MAX_STREAM_LINES {
            return Err(FramingDefect::InputTooLarge);
        }
        Ok(())
    }

    fn process_line(
        &mut self,
        mut line: String,
        frames: &mut Vec<String>,
    ) -> Result<(), FramingDefect> {
        if line.ends_with('\r') {
            line.pop();
        }
        match self.framing {
            Framing::ManagedNdjson => Self::push_managed_line(line, frames),
            Framing::Ndjson => Self::push_ndjson_line(line, frames),
            Framing::Sse => Self::push_sse_line(&line, frames),
            Framing::Auto => {
                let leading = line.trim_start();
                if leading.is_empty() {
                    return Ok(());
                }
                if is_sse_field(leading) {
                    self.framing = Framing::Sse;
                    Self::push_sse_line(&line, frames)
                } else if leading.starts_with('{') {
                    self.framing = Framing::Ndjson;
                    Self::push_ndjson_line(line, frames)
                } else {
                    // Preserve the whole-body adapter's treatment of transport
                    // preambles: only JSON objects and SSE fields are frames.
                    Ok(())
                }
            }
        }
    }

    fn push_managed_line(line: String, frames: &mut Vec<String>) -> Result<(), FramingDefect> {
        if line.trim().is_empty() {
            return Err(FramingDefect::MalformedFrame);
        }
        push_bounded(line, frames)
    }

    fn push_ndjson_line(line: String, frames: &mut Vec<String>) -> Result<(), FramingDefect> {
        if line.trim_start().starts_with('{') {
            push_bounded(line, frames)?;
        }
        Ok(())
    }

    fn push_sse_line(line: &str, frames: &mut Vec<String>) -> Result<(), FramingDefect> {
        let leading = line.trim_start();
        let Some(data) = leading.strip_prefix("data:") else {
            return Ok(());
        };
        let data = data.strip_prefix(' ').unwrap_or(data).trim();
        if data.is_empty() || data == "[DONE]" {
            return Ok(());
        }
        push_bounded(data.to_owned(), frames)
    }
}

fn push_bounded(frame: String, frames: &mut Vec<String>) -> Result<(), FramingDefect> {
    if frame.len() > json::MAX_INPUT_BYTES {
        return Err(FramingDefect::FrameTooLarge);
    }
    frames.push(frame);
    Ok(())
}

fn is_sse_field(line: &str) -> bool {
    line.starts_with("data:")
        || line.starts_with("event:")
        || line.starts_with("id:")
        || line.starts_with("retry:")
        || line.starts_with(':')
}
