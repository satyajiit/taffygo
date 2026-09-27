// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::{write, Entry, Package};
use crate::{FileError, Limits, PackageIssue};

#[test]
fn writer_refuses_traversal_and_duplicate_paths() {
    assert_eq!(
        write(
            vec![Entry::new("../escape.xml", b"x".to_vec())],
            Limits::default()
        ),
        Err(FileError::InvalidPackage(PackageIssue::UnsafePath))
    );
    assert_eq!(
        write(
            vec![
                Entry::new("same.xml", b"one".to_vec()),
                Entry::new("same.xml", b"two".to_vec()),
            ],
            Limits::default()
        ),
        Err(FileError::InvalidPackage(PackageIssue::UnsafePath))
    );
}

#[test]
fn parser_refuses_a_duplicate_central_name_even_before_local_comparison() {
    let Ok(mut bytes) = write(
        vec![
            Entry::new("a.xml", b"one".to_vec()),
            Entry::new("b.xml", b"two".to_vec()),
        ],
        Limits::default(),
    ) else {
        unreachable!("fixture zip writes");
    };
    let occurrences = occurrences(&bytes, b"b.xml");
    let Some(central) = occurrences.get(1).copied() else {
        unreachable!("central name exists");
    };
    let Some(central_name) = bytes.get_mut(central..central.saturating_add(5)) else {
        unreachable!("central name range exists");
    };
    central_name.copy_from_slice(b"a.xml");
    assert_eq!(
        Package::parse(&bytes, Limits::default()).map(|_| ()),
        Err(FileError::InvalidPackage(PackageIssue::UnsafePath))
    );
}

#[test]
fn parser_refuses_timestamp_compression_checksum_and_truncation_mutations() {
    let Ok(original) = write(
        vec![Entry::new("safe.xml", b"payload".to_vec())],
        Limits::default(),
    ) else {
        unreachable!("fixture zip writes");
    };

    let mut timestamp = original.clone();
    let Some(timestamp_byte) = timestamp.get_mut(12) else {
        unreachable!("local date byte exists");
    };
    *timestamp_byte ^= 1;
    assert!(Package::parse(&timestamp, Limits::default()).is_err());

    let mut compression = original.clone();
    let Some(method) = compression.get_mut(8) else {
        unreachable!("local method byte exists");
    };
    *method = 8;
    assert!(Package::parse(&compression, Limits::default()).is_err());

    let Some(payload_offset) = occurrences(&original, b"payload").first().copied() else {
        unreachable!("payload exists");
    };
    let mut checksum = original.clone();
    let Some(byte) = checksum.get_mut(payload_offset) else {
        unreachable!("payload byte exists");
    };
    *byte ^= 1;
    assert_eq!(
        Package::parse(&checksum, Limits::default()).map(|_| ()),
        Err(FileError::InvalidPackage(PackageIssue::ChecksumMismatch))
    );

    let mut truncated = original;
    truncated.pop();
    assert!(Package::parse(&truncated, Limits::default()).is_err());
}

#[test]
fn package_lookup_finds_sorted_boundaries_and_refuses_missing_names() {
    let Ok(bytes) = write(
        vec![
            Entry::new("middle.xml", b"middle".to_vec()),
            Entry::new("z-last.xml", b"last".to_vec()),
            Entry::new("a-first.xml", b"first".to_vec()),
        ],
        Limits::default(),
    ) else {
        unreachable!("fixture zip writes");
    };
    let Ok(package) = Package::parse(&bytes, Limits::default()) else {
        unreachable!("fixture zip parses");
    };

    assert_eq!(package.find("a-first.xml"), Some(b"first".as_slice()));
    assert_eq!(package.find("middle.xml"), Some(b"middle".as_slice()));
    assert_eq!(package.find("z-last.xml"), Some(b"last".as_slice()));
    assert_eq!(package.find("before.xml"), None);
    assert_eq!(package.find("zz-missing.xml"), None);
}

fn occurrences(bytes: &[u8], needle: &[u8]) -> Vec<usize> {
    bytes
        .windows(needle.len())
        .enumerate()
        .filter_map(|(index, window)| (window == needle).then_some(index))
        .collect()
}
