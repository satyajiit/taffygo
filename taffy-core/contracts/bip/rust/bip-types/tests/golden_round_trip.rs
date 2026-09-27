// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Every golden document in `taffy-core/contracts/bip/golden/` survives a serde round trip
//! with its semantics intact.
//!
//! "Semantics intact" is checked two ways, because either alone is weak. The
//! re-serialized document must agree with the original JSON at every path,
//! which proves the Rust types drop no field the contract carries and invent
//! none it does not. Decoding the re-serialized document again must produce an
//! equal value, which proves the encoding is stable rather than merely lossless
//! once.
//!
//! Agreement is by value, not by spelling. JSON has one number type, so a
//! coordinate written `64` and re-encoded as `64.0` is the same number and the
//! comparison says so; every other kind of difference, including a key present
//! on one side only, is a failure.
//!
//! The list of documents is not written here. It is read from
//! `taffy-core/contracts/bip/golden/index.json`, so a golden document added to the
//! contract without a Rust type to decode it fails this test instead of being
//! quietly skipped.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

use std::fmt::Debug;
use std::fs;
use std::path::{Path, PathBuf};

use bip_types::{action, delta, protocol_info, snapshot};
use serde::de::DeserializeOwned;
use serde::Serialize;
use serde_json::Value;

fn golden_dir() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../../golden")
}

/// The first path at which two documents disagree, or `None` when they agree
/// everywhere.
///
/// Numbers agree when they are the same number, so an integer literal and its
/// floating-point re-encoding are not a difference. Everything else is compared
/// structurally: a missing key, an extra key, a reordered array, or a changed
/// scalar all report.
fn first_difference(path: &str, original: &Value, reencoded: &Value) -> Option<String> {
    match (original, reencoded) {
        (Value::Number(left), Value::Number(right)) => {
            let agree = match (left.as_f64(), right.as_f64()) {
                (Some(left_value), Some(right_value)) => left_value.total_cmp(&right_value).is_eq(),
                _ => left == right,
            };
            if agree {
                None
            } else {
                Some(format!("{path}: {left} became {right}"))
            }
        }
        (Value::Array(left), Value::Array(right)) => {
            if left.len() != right.len() {
                return Some(format!(
                    "{path}: {} elements became {}",
                    left.len(),
                    right.len()
                ));
            }
            left.iter().zip(right.iter()).enumerate().find_map(
                |(index, (left_item, right_item))| {
                    first_difference(&format!("{path}[{index}]"), left_item, right_item)
                },
            )
        }
        (Value::Object(left), Value::Object(right)) => {
            if let Some(key) = left.keys().find(|key| !right.contains_key(*key)) {
                return Some(format!("{path}.{key}: dropped by the round trip"));
            }
            if let Some(key) = right.keys().find(|key| !left.contains_key(*key)) {
                return Some(format!("{path}.{key}: invented by the round trip"));
            }
            left.iter().find_map(|(key, left_value)| {
                right.get(key).and_then(|right_value| {
                    first_difference(&format!("{path}.{key}"), left_value, right_value)
                })
            })
        }
        _ => {
            if original == reencoded {
                None
            } else {
                Some(format!("{path}: {original} became {reencoded}"))
            }
        }
    }
}

/// Decodes `json` as `T`, re-encodes it, and returns a description of the first
/// way the round trip lost or changed meaning.
fn round_trip<T>(json: &str) -> Result<(), String>
where
    T: DeserializeOwned + Serialize + PartialEq + Debug,
{
    let original: Value =
        serde_json::from_str(json).map_err(|error| format!("golden is not JSON: {error}"))?;

    let decoded: T = serde_json::from_str(json)
        .map_err(|error| format!("golden does not decode into the generated type: {error}"))?;

    let reencoded = serde_json::to_value(&decoded)
        .map_err(|error| format!("decoded value does not re-encode: {error}"))?;

    if let Some(difference) = first_difference("$", &original, &reencoded) {
        return Err(format!("re-encoding changed the document at {difference}"));
    }

    let decoded_again: T = serde_json::from_value(reencoded)
        .map_err(|error| format!("re-encoded document does not decode: {error}"))?;

    if decoded_again != decoded {
        return Err("decoding the re-encoded document produced a different value".to_owned());
    }

    Ok(())
}

/// Dispatches a golden document to the Rust type its schema definition names.
///
/// An unhandled definition is an error, never a skip: a new message family in
/// the contract must arrive with the type that decodes it.
fn round_trip_definition(definition: &str, json: &str) -> Result<(), String> {
    match definition {
        "ProtocolInfo" => round_trip::<protocol_info::ProtocolInfo>(json),
        "SnapshotRequest" => round_trip::<snapshot::SnapshotRequest>(json),
        "PageSnapshot" => round_trip::<snapshot::PageSnapshot>(json),
        "PageDelta" => round_trip::<delta::PageDelta>(json),
        "PageInvalidation" => round_trip::<delta::PageInvalidation>(json),
        "BackpressureNotice" => round_trip::<delta::BackpressureNotice>(json),
        "ActionProposal" => round_trip::<action::ActionProposal>(json),
        "AuthorizedActionEnvelope" => round_trip::<action::AuthorizedActionEnvelope>(json),
        "RendererActionCommand" => round_trip::<action::RendererActionCommand>(json),
        "ActionResult" => round_trip::<action::ActionResult>(json),
        other => Err(format!(
            "no Rust type is wired up for schema definition {other}"
        )),
    }
}

struct GoldenEntry {
    file: String,
    definition: String,
}

fn golden_index() -> Vec<GoldenEntry> {
    let path = golden_dir().join("index.json");
    let text = fs::read_to_string(&path)
        .unwrap_or_else(|error| panic!("cannot read {}: {error}", path.display()));
    let index: Value = serde_json::from_str(&text).expect("golden index is not JSON");

    index["messages"]
        .as_array()
        .expect("golden index has no messages array")
        .iter()
        .map(|entry| GoldenEntry {
            file: entry["file"]
                .as_str()
                .expect("golden index entry has no file")
                .to_owned(),
            definition: entry["definition"]
                .as_str()
                .expect("golden index entry has no definition")
                .to_owned(),
        })
        .collect()
}

#[test]
fn every_golden_document_round_trips_with_its_semantics_preserved() {
    let entries = golden_index();
    assert!(
        !entries.is_empty(),
        "the golden index lists no documents, so this test proves nothing"
    );

    let mut failures = Vec::new();
    for entry in &entries {
        let path = golden_dir().join(&entry.file);
        let text = match fs::read_to_string(&path) {
            Ok(text) => text,
            Err(error) => {
                failures.push(format!("{}: cannot read: {error}", entry.file));
                continue;
            }
        };

        if let Err(reason) = round_trip_definition(&entry.definition, &text) {
            failures.push(format!("{}: {reason}", entry.file));
        }
    }

    assert!(
        failures.is_empty(),
        "{} of {} golden documents failed the round trip:\n{}",
        failures.len(),
        entries.len(),
        failures.join("\n")
    );
}

#[test]
fn every_golden_file_on_disk_is_listed_in_the_index() {
    let listed: Vec<String> = golden_index().into_iter().map(|entry| entry.file).collect();

    let mut unlisted = Vec::new();
    for entry in fs::read_dir(golden_dir()).expect("cannot read the golden directory") {
        let entry = entry.expect("cannot read a golden directory entry");
        let name = entry.file_name().to_string_lossy().into_owned();
        let is_json = std::path::Path::new(&name)
            .extension()
            .is_some_and(|extension| extension.eq_ignore_ascii_case("json"));
        if name == "index.json" || !is_json {
            continue;
        }
        if !listed.contains(&name) {
            unlisted.push(name);
        }
    }

    assert!(
        unlisted.is_empty(),
        "golden documents exist that the index does not list, so nothing decodes them: {unlisted:?}"
    );
}
