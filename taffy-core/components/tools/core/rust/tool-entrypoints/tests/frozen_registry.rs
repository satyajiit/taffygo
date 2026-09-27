// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What the frozen registry promises, driven through its only public surface.
//!
//! Three properties are under test and they are not the same property:
//!
//! 1. **The refusal fires.** A row that names a native alternative is refused,
//!    with the native owner named. This is the rule the registry exists for.
//! 2. **The surface is exactly the compiled-in one.** Asserted as a vector
//!    rather than a count, so a row that appears without review fails here
//!    rather than sliding under a number that was also updated.
//! 3. **Nothing can be added.** There is no constructor, no reader and no
//!    mutable state reachable from outside, which is what makes 1 and 2 hold
//!    for the life of the process rather than until the first caller.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

use tool_entrypoints::{
    admit, find, Admission, Entrypoint, NativePath, NativeState, ValueKind, ENTRYPOINTS,
};

/// The rule this registry exists for.
///
/// `table.reshape` is a row and it is refused, because reshaping rows is
/// arithmetic the portable core owns. If this ever admits, a sandboxed
/// interpreter has been bought for something the product can already do, and
/// the whole cost argument in decision 0064 has quietly been reversed.
#[test]
fn a_row_with_a_native_alternative_is_refused_and_names_its_owner() {
    match admit("table.reshape") {
        Admission::RefusedForNativePath(native) => {
            assert_eq!(native.id(), "core.table.reshape");
            assert_eq!(native.state(), NativeState::Active);
            assert!(!native.description().is_empty());
        }
        Admission::Admitted(_) => panic!("table.reshape was admitted to a worker"),
        Admission::Unknown => panic!("table.reshape is not in the registry at all"),
    }
}

/// A refusal remains a refusal after its native owner becomes active.
///
/// Native state records implementation readiness; it never widens the worker
/// allowlist. The portable core handles this operation directly.
#[test]
fn an_active_native_owner_still_refuses_the_worker_path() {
    let refused: Vec<&str> = ENTRYPOINTS
        .iter()
        .filter_map(|row| row.native_alternative().map(NativePath::id))
        .collect();
    assert_eq!(refused, vec!["core.table.reshape"]);

    for row in ENTRYPOINTS {
        let Some(native) = row.native_alternative() else {
            continue;
        };
        assert_eq!(native.state(), NativeState::Active);
        assert!(matches!(
            admit(row.id()),
            Admission::RefusedForNativePath(_)
        ));
    }
}

/// The two admitted rows, which are the ones a worker may ever be started for.
#[test]
fn the_admitted_surface_is_exactly_two_rows() {
    let admitted: Vec<&str> = ENTRYPOINTS
        .iter()
        .filter(|row| row.native_alternative().is_none())
        .map(Entrypoint::id)
        .collect();
    assert_eq!(admitted, vec!["document.build", "spreadsheet.build"]);

    for id in &admitted {
        assert!(matches!(admit(id), Admission::Admitted(_)));
    }
}

/// The whole registry, as a vector rather than as a count.
///
/// A count passes when one row is added and another removed, which is the
/// review this assertion exists to force.
#[test]
fn the_registry_is_exactly_three_named_rows_in_order() {
    let ids: Vec<&str> = ENTRYPOINTS.iter().map(Entrypoint::id).collect();
    assert_eq!(
        ids,
        vec!["document.build", "spreadsheet.build", "table.reshape"]
    );
}

/// A name nobody compiled in has nowhere to be found.
///
/// The three near misses matter more than the nonsense one: a lookup that
/// trimmed, lowercased or prefix-matched would be a lookup a caller could
/// steer into a row it was not given.
#[test]
fn an_unregistered_name_is_unknown_and_no_near_miss_resolves() {
    for name in [
        "",
        "shell.run",
        "table",
        "table.reshap",
        "table.reshapee",
        "Table.reshape",
        " table.reshape",
        "table.reshape ",
        "table.reshape\0",
    ] {
        assert!(
            matches!(admit(name), Admission::Unknown),
            "{name:?} resolved to something"
        );
        assert!(find(name).is_none(), "{name:?} was found");
    }
}

/// Every declared port carries a closed kind and a reason for existing.
///
/// The kinds are the second half of the bound: an entrypoint that could
/// declare a port of an open kind could be handed anything, and the identity
/// gate above would be the only thing left.
#[test]
fn every_port_is_named_bounded_and_described() {
    for row in ENTRYPOINTS {
        assert!(!row.summary().is_empty(), "{} has no summary", row.id());
        assert!(!row.inputs().is_empty(), "{} declares no input", row.id());
        assert!(!row.outputs().is_empty(), "{} declares no output", row.id());
        for port in row.inputs().iter().chain(row.outputs()) {
            assert!(!port.name().is_empty());
            assert!(!port.description().is_empty());
            // A closed enumeration has no unknown arm to fall through.
            match port.kind() {
                ValueKind::DocumentDocx
                | ValueKind::JsonUtf8
                | ValueKind::SpreadsheetXlsx
                | ValueKind::TableCsvUtf8 => {}
            }
            assert!(!port.kind().wire_name().is_empty());
        }
    }
}

/// An identity appears once, so a lookup cannot depend on which copy it found.
#[test]
fn no_identity_appears_twice() {
    let mut seen: Vec<&str> = ENTRYPOINTS.iter().map(Entrypoint::id).collect();
    let before = seen.len();
    seen.sort_unstable();
    seen.dedup();
    assert_eq!(seen.len(), before);
}

/// The registry a build was compiled with is the one this build reports.
///
/// The fingerprint is derived from the identities, the refusals and the ports.
/// It is asserted here as a literal because the value of a fingerprint is that
/// changing the rows changes it: a test that recomputed it would agree with
/// any table at all.
#[test]
fn the_fingerprint_names_this_registry() {
    assert_eq!(tool_entrypoints::REGISTRY_FINGERPRINT, 1_879_083_464);
    assert_eq!(tool_entrypoints::REGISTRY_VERSION, 1);
}
