// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The Rust view of the Browser Intelligence Protocol.
//!
//! Authoritative specification:
//! `docs/architecture/browser-intelligence-protocol.md`. Wire contract, code
//! generator, golden documents, and compatibility fixtures: `taffy-core/contracts/bip/`.
//! Owning milestone: M2 (page intelligence).
//!
//! # What this crate is
//!
//! Types and lifetime rules, nothing else. It carries no authority: whether an
//! observation may be taken, whether an action may be dispatched, and what a
//! destination is allowed to receive are decisions owned by `policy-engine`.
//! This crate gives that decision a vocabulary it cannot misspell.
//!
//! # Generated versus hand-written
//!
//! [`generated`] is produced by `taffy-core/contracts/bip/codegen/generate.py` from the
//! JSON Schema documents in `taffy-core/contracts/bip/schema/`. Every message shape, every
//! closed enumeration, and the protocol version constant live there, and none
//! of it is edited by hand: change the schema and regenerate.
//!
//! Everything else in this crate is hand-written and adds what a schema cannot
//! express — the lifetime rules of specification section 5, the compatibility
//! rules of section 6.2, the result taxonomy classifiers of section 11.7, and
//! the sensitivity lattice of section 9. Hand-written modules re-export the
//! generated types they extend, so a consumer imports one name per concept:
//!
//! | Module | Adds |
//! |---|---|
//! | [`identity`] | Handle construction, scoping, and the ordering rules of section 5 |
//! | [`version`] | Parsed protocol versions, compatibility verdicts, closed-enumeration decoding |
//! | [`result_code`] | Classifiers over the action result taxonomy |
//! | [`sensitivity`] | The sensitivity lattice and the never-extract value classes |
//!
//! # Two rules that hold everywhere
//!
//! **Every enumeration is closed.** A wire value outside the list is
//! unsupported. It is never coerced to the nearest known member, never treated
//! as the least restrictive member, and never dropped. Generated `from_wire`
//! returns `None`; [`version::ClosedEnum::decode_wire`] turns that `None` into
//! an explicit [`version::Decoded::Unsupported`] so the refusal is a value the
//! caller has to handle rather than a branch it can forget.
//!
//! **Handles are equality-compared, never ordered.** A page epoch says which
//! document a handle belongs to and nothing about which document came first, so
//! [`identity::PageEpoch`] deliberately implements no ordering at all. See the
//! [`identity`] module documentation for the full rule.
#![doc(html_no_source)]
#![cfg_attr(
    test,
    allow(
        clippy::unwrap_used,
        clippy::expect_used,
        clippy::panic,
        clippy::indexing_slicing
    )
)]

// The generator owns the exact bytes of everything under `generated`, and
// `generate.py --check` compares them byte for byte. Without this attribute
// `cargo fmt --all` rewraps the generated files and the codegen gate fails on a
// tree nobody edited. Marking the module declaration stops rustfmt descending
// into the whole subtree, so the two checks stop contradicting each other.
#[rustfmt::skip]
pub mod generated;

pub mod identity;
pub mod result_code;
pub mod sensitivity;
pub mod trust;
pub mod version;

pub use crate::generated::{action, delta, protocol_info, snapshot};
pub use crate::identity::NodeHandle;
pub use crate::result_code::ActionResultCode;
pub use crate::sensitivity::Sensitivity;
pub use crate::trust::{ContentTrust, TrustSet};
pub use crate::version::{ProtocolVersion, PROTOCOL_STATUS, PROTOCOL_VERSION};
