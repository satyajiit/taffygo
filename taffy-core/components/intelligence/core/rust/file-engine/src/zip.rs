// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use crate::sink::ByteSink;
use crate::{FileError, LimitKind, Limits, PackageIssue};

const LOCAL_SIGNATURE: u32 = 0x0403_4b50;
const CENTRAL_SIGNATURE: u32 = 0x0201_4b50;
const END_SIGNATURE: u32 = 0x0605_4b50;
const VERSION: u16 = 20;
const FIXED_DATE: u16 = 0x0021;

pub(crate) struct Entry {
    pub(crate) name: String,
    pub(crate) data: Vec<u8>,
}

impl Entry {
    pub(crate) fn new(name: impl Into<String>, data: Vec<u8>) -> Self {
        Self {
            name: name.into(),
            data,
        }
    }
}

struct CentralRecord {
    name: String,
    crc: u32,
    size: u32,
    offset: u32,
}

pub(crate) fn write(mut entries: Vec<Entry>, limits: Limits) -> Result<Vec<u8>, FileError> {
    if entries.len() > limits.max_package_parts || entries.len() > u16::MAX.into() {
        return Err(FileError::LimitExceeded {
            kind: LimitKind::PackageParts,
            maximum: limits.max_package_parts,
        });
    }
    entries.sort_by(|left, right| left.name.cmp(&right.name));
    let mut previous: Option<&str> = None;
    for entry in &entries {
        safe_path(&entry.name)?;
        if previous == Some(entry.name.as_str()) {
            return Err(package(PackageIssue::UnsafePath));
        }
        previous = Some(&entry.name);
    }

    let mut sink = ByteSink::new(limits.max_output_bytes);
    let mut records = Vec::with_capacity(entries.len());
    for entry in entries {
        let name = entry.name.as_bytes();
        let name_len = u16::try_from(name.len()).map_err(|_| package(PackageIssue::UnsafePath))?;
        let size = u32::try_from(entry.data.len()).map_err(|_| output_limit(limits))?;
        let offset = u32::try_from(sink.position()).map_err(|_| output_limit(limits))?;
        let checksum = crc32(&entry.data);

        sink.le_u32(LOCAL_SIGNATURE)?;
        sink.le_u16(VERSION)?;
        sink.le_u16(0)?;
        sink.le_u16(0)?;
        sink.le_u16(0)?;
        sink.le_u16(FIXED_DATE)?;
        sink.le_u32(checksum)?;
        sink.le_u32(size)?;
        sink.le_u32(size)?;
        sink.le_u16(name_len)?;
        sink.le_u16(0)?;
        sink.extend(name)?;
        sink.extend(&entry.data)?;
        records.push(CentralRecord {
            name: entry.name,
            crc: checksum,
            size,
            offset,
        });
    }

    let central_offset = u32::try_from(sink.position()).map_err(|_| output_limit(limits))?;
    for record in &records {
        let name = record.name.as_bytes();
        let name_len = u16::try_from(name.len()).map_err(|_| package(PackageIssue::UnsafePath))?;
        sink.le_u32(CENTRAL_SIGNATURE)?;
        sink.le_u16(VERSION)?;
        sink.le_u16(VERSION)?;
        sink.le_u16(0)?;
        sink.le_u16(0)?;
        sink.le_u16(0)?;
        sink.le_u16(FIXED_DATE)?;
        sink.le_u32(record.crc)?;
        sink.le_u32(record.size)?;
        sink.le_u32(record.size)?;
        sink.le_u16(name_len)?;
        sink.le_u16(0)?;
        sink.le_u16(0)?;
        sink.le_u16(0)?;
        sink.le_u16(0)?;
        sink.le_u32(0)?;
        sink.le_u32(record.offset)?;
        sink.extend(name)?;
    }
    let central_size = u32::try_from(sink.position())
        .ok()
        .and_then(|position| position.checked_sub(central_offset))
        .ok_or_else(|| output_limit(limits))?;
    let count = u16::try_from(records.len()).map_err(|_| output_limit(limits))?;
    sink.le_u32(END_SIGNATURE)?;
    sink.le_u16(0)?;
    sink.le_u16(0)?;
    sink.le_u16(count)?;
    sink.le_u16(count)?;
    sink.le_u32(central_size)?;
    sink.le_u32(central_offset)?;
    sink.le_u16(0)?;
    Ok(sink.into_bytes())
}

pub(crate) struct Part<'a> {
    pub(crate) name: &'a str,
    pub(crate) data: &'a [u8],
}

pub(crate) struct Package<'a> {
    pub(crate) parts: Vec<Part<'a>>,
}

impl<'a> Package<'a> {
    pub(crate) fn parse(bytes: &'a [u8], limits: Limits) -> Result<Self, FileError> {
        let layout = archive_layout(bytes, limits)?;
        let mut central_cursor = layout.central_offset;
        let mut local_cursor = 0usize;
        let mut parts = Vec::with_capacity(layout.count);
        let mut previous: Option<&str> = None;
        for _ in 0..layout.count {
            let record = central_record(bytes, &mut central_cursor)?;
            if previous.is_some_and(|prior| prior >= record.name) {
                return Err(package(PackageIssue::UnsafePath));
            }
            previous = Some(record.name);
            if record.offset != local_cursor {
                return Err(package(PackageIssue::Malformed));
            }
            let local = take(bytes, record.offset, 30)?;
            if read_u32(local, 0)? != LOCAL_SIGNATURE
                || read_u16(local, 4)? != VERSION
                || read_u16(local, 6)? != 0
                || read_u16(local, 8)? != 0
                || read_u16(local, 10)? != 0
                || read_u16(local, 12)? != FIXED_DATE
                || read_u16(local, 26)?
                    != u16::try_from(record.name_bytes.len()).unwrap_or(u16::MAX)
                || read_u16(local, 28)? != 0
                || read_u32(local, 14)? != record.checksum
                || read_u32(local, 18)? != record.size
                || read_u32(local, 22)? != record.size
            {
                return Err(package(PackageIssue::Malformed));
            }
            let local_name = take(
                bytes,
                record.offset.saturating_add(30),
                record.name_bytes.len(),
            )?;
            if local_name != record.name_bytes {
                return Err(package(PackageIssue::Malformed));
            }
            let data_offset = record
                .offset
                .checked_add(30)
                .and_then(|value| value.checked_add(record.name_bytes.len()))
                .ok_or_else(|| package(PackageIssue::Malformed))?;
            let data_len =
                usize::try_from(record.size).map_err(|_| package(PackageIssue::Malformed))?;
            let data = take(bytes, data_offset, data_len)?;
            if crc32(data) != record.checksum {
                return Err(package(PackageIssue::ChecksumMismatch));
            }
            local_cursor = data_offset
                .checked_add(data_len)
                .ok_or_else(|| package(PackageIssue::Malformed))?;
            parts.push(Part {
                name: record.name,
                data,
            });
        }
        if central_cursor != layout.end_offset || local_cursor != layout.central_offset {
            return Err(package(PackageIssue::Malformed));
        }
        Ok(Self { parts })
    }

    pub(crate) fn find(&self, name: &str) -> Option<&'a [u8]> {
        self.parts
            .binary_search_by(|part| part.name.cmp(name))
            .ok()
            .and_then(|index| self.parts.get(index))
            .map(|part| part.data)
    }
}

struct ArchiveLayout {
    count: usize,
    central_offset: usize,
    end_offset: usize,
}

fn archive_layout(bytes: &[u8], limits: Limits) -> Result<ArchiveLayout, FileError> {
    if bytes.len() > limits.max_output_bytes {
        return Err(output_limit(limits));
    }
    let end_offset = bytes.len().checked_sub(22).ok_or_else(truncated)?;
    let end = bytes.get(end_offset..).ok_or_else(truncated)?;
    if read_u32(end, 0)? != END_SIGNATURE
        || read_u16(end, 4)? != 0
        || read_u16(end, 6)? != 0
        || read_u16(end, 8)? != read_u16(end, 10)?
        || read_u16(end, 20)? != 0
    {
        return Err(package(PackageIssue::Malformed));
    }
    let count = usize::from(read_u16(end, 10)?);
    if count == 0 || count > limits.max_package_parts {
        return Err(FileError::LimitExceeded {
            kind: LimitKind::PackageParts,
            maximum: limits.max_package_parts,
        });
    }
    let central_size =
        usize::try_from(read_u32(end, 12)?).map_err(|_| package(PackageIssue::Malformed))?;
    let central_offset =
        usize::try_from(read_u32(end, 16)?).map_err(|_| package(PackageIssue::Malformed))?;
    if central_offset.checked_add(central_size) != Some(end_offset) {
        return Err(package(PackageIssue::Malformed));
    }
    Ok(ArchiveLayout {
        count,
        central_offset,
        end_offset,
    })
}

struct CentralView<'a> {
    name: &'a str,
    name_bytes: &'a [u8],
    checksum: u32,
    size: u32,
    offset: usize,
}

fn central_record<'a>(bytes: &'a [u8], cursor: &mut usize) -> Result<CentralView<'a>, FileError> {
    let fixed = take(bytes, *cursor, 46)?;
    if read_u32(fixed, 0)? != CENTRAL_SIGNATURE
        || read_u16(fixed, 4)? != VERSION
        || read_u16(fixed, 6)? != VERSION
        || read_u16(fixed, 8)? != 0
        || read_u16(fixed, 10)? != 0
        || read_u16(fixed, 12)? != 0
        || read_u16(fixed, 14)? != FIXED_DATE
        || read_u16(fixed, 30)? != 0
        || read_u16(fixed, 32)? != 0
        || read_u16(fixed, 34)? != 0
        || read_u16(fixed, 36)? != 0
        || read_u32(fixed, 38)? != 0
    {
        return Err(package(PackageIssue::UnsupportedFeature));
    }
    let compressed = read_u32(fixed, 20)?;
    let size = read_u32(fixed, 24)?;
    if compressed != size {
        return Err(package(PackageIssue::UnsupportedFeature));
    }
    let name_len = usize::from(read_u16(fixed, 28)?);
    *cursor = cursor
        .checked_add(46)
        .ok_or_else(|| package(PackageIssue::Malformed))?;
    let name_bytes = take(bytes, *cursor, name_len)?;
    let name = std::str::from_utf8(name_bytes).map_err(|_| package(PackageIssue::UnsafePath))?;
    safe_path(name)?;
    *cursor = cursor
        .checked_add(name_len)
        .ok_or_else(|| package(PackageIssue::Malformed))?;
    Ok(CentralView {
        name,
        name_bytes,
        checksum: read_u32(fixed, 16)?,
        size,
        offset: usize::try_from(read_u32(fixed, 42)?)
            .map_err(|_| package(PackageIssue::Malformed))?,
    })
}

pub(crate) fn crc32(bytes: &[u8]) -> u32 {
    let mut crc = u32::MAX;
    for byte in bytes {
        crc ^= u32::from(*byte);
        for _ in 0..8 {
            let mask = 0u32.wrapping_sub(crc & 1);
            crc = (crc >> 1) ^ (0xedb8_8320 & mask);
        }
    }
    !crc
}

fn safe_path(path: &str) -> Result<(), FileError> {
    if path.is_empty()
        || !path.is_ascii()
        || path.starts_with('/')
        || path.ends_with('/')
        || path.contains('\\')
        || path.contains(':')
        || path
            .split('/')
            .any(|part| part.is_empty() || matches!(part, "." | ".."))
        || !path.bytes().all(|byte| {
            byte.is_ascii_alphanumeric() || matches!(byte, b'/' | b'_' | b'-' | b'.' | b'[' | b']')
        })
    {
        return Err(package(PackageIssue::UnsafePath));
    }
    Ok(())
}

fn take(bytes: &[u8], offset: usize, length: usize) -> Result<&[u8], FileError> {
    let end = offset.checked_add(length).ok_or_else(truncated)?;
    bytes.get(offset..end).ok_or_else(truncated)
}

fn read_u16(bytes: &[u8], offset: usize) -> Result<u16, FileError> {
    let value = take(bytes, offset, 2)?;
    let array: [u8; 2] = value.try_into().map_err(|_| truncated())?;
    Ok(u16::from_le_bytes(array))
}

fn read_u32(bytes: &[u8], offset: usize) -> Result<u32, FileError> {
    let value = take(bytes, offset, 4)?;
    let array: [u8; 4] = value.try_into().map_err(|_| truncated())?;
    Ok(u32::from_le_bytes(array))
}

fn truncated() -> FileError {
    package(PackageIssue::Truncated)
}

fn package(issue: PackageIssue) -> FileError {
    FileError::InvalidPackage(issue)
}

fn output_limit(limits: Limits) -> FileError {
    FileError::LimitExceeded {
        kind: LimitKind::OutputBytes,
        maximum: limits.max_output_bytes,
    }
}

#[cfg(test)]
mod tests;
