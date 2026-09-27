// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The frozen registry of everything a sandboxed worker may ever be asked to
//! do.
//!
//! Authoritative specification:
//! `docs/decisions/0064-a-worker-entrypoint-is-frozen-and-loses-to-a-native-path.md`.
//! Owning milestone: M7, with decision
//! `docs/decisions/0007-sandboxed-python-utility-process.md`.
//!
//! # What "frozen" means here
//!
//! Four things, and each is structural rather than remembered:
//!
//! 1. **First-party source, compiled in.** The rows are generated into
//!    [`generated`] from one committed file and compiled. Nothing in the
//!    product opens that file.
//! 2. **Never loaded from disk.** This crate has no reader, no parser and no
//!    file API. It cannot be pointed at a second registry because it cannot be
//!    pointed at a first one.
//! 3. **Never delivered.** An entrypoint is not an asset. The delivery plane
//!    fetches bytes an artifact catalog pins; it has no row shape that could
//!    carry a name a worker would then answer to.
//! 4. **Never supplied by a caller.** [`Entrypoint`] and [`NativePath`] have
//!    private fields and crate-private constructors, so the only values of
//!    those types that exist anywhere are the `static` ones the generator
//!    wrote. A caller may compare an identity and may not mint one.
//!
//! # The rule that refuses
//!
//! A row may name a `native_alternative`. When it does, [`admit`] refuses the
//! entrypoint and names the native owner instead of admitting the worker.
//!
//! That is the point of the registry rather than a detail of it. Starting a
//! sandboxed interpreter to do something the product already owns natively
//! buys nothing and costs a process, a budget, an opened resource and an
//! attack surface. Keeping the row — rather than leaving the name out — is
//! what makes the decision reviewable: an absent name reads as "not written
//! yet", and a named one reads as "considered, and answered somewhere else".
//!
//! # What this crate does not do
//!
//! It opens nothing, reads no clock, holds no bytes and has no state. It
//! answers one question about one string, which is why the browser and the
//! core can both ask it and get the same answer without sharing anything else.

#![forbid(unsafe_code)]

// The generator emits a table `rustfmt` would otherwise rewrap, and a rewrapped
// table no longer matches what `--check` renders. Without this attribute
// `cargo fmt --all` and `generate_entrypoints.py --check` contradict each other
// permanently on a tree nobody edited, and the failure names the generator
// rather than the formatter.
#[rustfmt::skip]
mod generated;
mod registry;

pub use generated::{ValueKind, ENTRYPOINTS, REGISTRY_FINGERPRINT, REGISTRY_VERSION};
pub use registry::{admit, find, Admission, Entrypoint, NativePath, NativeState, Port};
