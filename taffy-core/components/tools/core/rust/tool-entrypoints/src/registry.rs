// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The registry's own types, and the one decision it makes.
//!
//! The rows live in [`crate::generated`], which is written by
//! `entrypoints/tools/generate_entrypoints.py`. This module holds what the
//! rows *are* and what the core does with them, because a decision belongs in
//! a file a person wrote.

use crate::generated::{ValueKind, ENTRYPOINTS};

/// Whether the native owner of a refused capability is built today.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum NativeState {
    /// The native path exists and answers requests.
    Active,
    /// The native path is specified and not yet built.
    ///
    /// A refusal against a planned owner still stands. The registry answers
    /// *which layer owns a capability*, not which layer has finished it, and
    /// admitting an interpreter in the meantime would make "temporary" the
    /// reason the sandbox was widened.
    Planned,
}

/// The native owner of a capability the Python runtime is refused.
#[derive(Debug)]
pub struct NativePath {
    id: &'static str,
    state: NativeState,
    description: &'static str,
}

impl NativePath {
    /// Only the generated table may mint one, which is what stops a caller
    /// from presenting a native path of its own invention.
    pub(crate) const fn new(
        id: &'static str,
        state: NativeState,
        description: &'static str,
    ) -> Self {
        Self {
            id,
            state,
            description,
        }
    }

    /// The native owner's identity.
    pub const fn id(&self) -> &'static str {
        self.id
    }

    /// Whether that owner is built today.
    pub const fn state(&self) -> NativeState {
        self.state
    }

    /// Why this layer owns the capability.
    pub const fn description(&self) -> &'static str {
        self.description
    }
}

/// One declared input or output of an entrypoint.
#[derive(Debug)]
pub struct Port {
    name: &'static str,
    kind: ValueKind,
    required: bool,
    description: &'static str,
}

impl Port {
    pub(crate) const fn new(
        name: &'static str,
        kind: ValueKind,
        required: bool,
        description: &'static str,
    ) -> Self {
        Self {
            name,
            kind,
            required,
            description,
        }
    }

    /// The port's name within its own entrypoint.
    pub const fn name(&self) -> &'static str {
        self.name
    }

    /// The closed value kind this port carries.
    pub const fn kind(&self) -> ValueKind {
        self.kind
    }

    /// Whether a job must supply this port. Always true for an output.
    pub const fn required(&self) -> bool {
        self.required
    }

    /// What the port is, in one sentence.
    pub const fn description(&self) -> &'static str {
        self.description
    }
}

/// One registry row: everything the product knows about one entrypoint.
#[derive(Debug)]
pub struct Entrypoint {
    id: &'static str,
    summary: &'static str,
    native_alternative: Option<&'static NativePath>,
    inputs: &'static [Port],
    outputs: &'static [Port],
}

impl Entrypoint {
    /// Only the generated table may mint one.
    ///
    /// This is the whole of what "frozen" means in Rust: the constructor is
    /// crate-private, every field is private, and the only values of this type
    /// that exist are the `static` ones the generator wrote. A caller cannot
    /// build an `Entrypoint`, cannot mutate one, and cannot obtain one this
    /// build was not compiled with.
    pub(crate) const fn new(
        id: &'static str,
        summary: &'static str,
        native_alternative: Option<&'static NativePath>,
        inputs: &'static [Port],
        outputs: &'static [Port],
    ) -> Self {
        Self {
            id,
            summary,
            native_alternative,
            inputs,
            outputs,
        }
    }

    /// The entrypoint's identity, as a job names it on the wire.
    pub const fn id(&self) -> &'static str {
        self.id
    }

    /// What the entrypoint does, in one sentence.
    pub const fn summary(&self) -> &'static str {
        self.summary
    }

    /// The native owner that refuses this entrypoint, when there is one.
    pub const fn native_alternative(&self) -> Option<&'static NativePath> {
        self.native_alternative
    }

    /// What a job must supply.
    pub const fn inputs(&self) -> &'static [Port] {
        self.inputs
    }

    /// What the worker returns.
    pub const fn outputs(&self) -> &'static [Port] {
        self.outputs
    }
}

/// What the core answers when it is asked for an entrypoint by name.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Admission {
    /// The registry has this row and no native owner claims it.
    Admitted(&'static Entrypoint),
    /// The registry has this row and a native path owns the capability, so no
    /// worker is started for it. The native owner travels with the refusal so
    /// the caller can be told where the answer actually comes from.
    RefusedForNativePath(&'static NativePath),
    /// No row carries this identity. There is no second place to look.
    Unknown,
}

impl PartialEq for Entrypoint {
    /// Identity comparison, because every row is a distinct `static` and the
    /// registry holds exactly one row per identity.
    fn eq(&self, other: &Self) -> bool {
        core::ptr::eq(self, other)
    }
}

impl Eq for Entrypoint {}

impl PartialEq for NativePath {
    fn eq(&self, other: &Self) -> bool {
        core::ptr::eq(self, other)
    }
}

impl Eq for NativePath {}

/// The row with this identity, if the build was compiled with one.
///
/// There is no fallback, no prefix match and no normalisation: an identity
/// either is one of the compiled-in names or it is not. A lookup that repaired
/// a near miss would be a lookup a caller could steer.
pub fn find(entrypoint_id: &str) -> Option<&'static Entrypoint> {
    ENTRYPOINTS.iter().find(|row| row.id == entrypoint_id)
}

/// Whether a worker may be asked for this entrypoint, and why not when it may
/// not be.
///
/// This is the whole gate. Both refusals are silent about each other on
/// purpose: an unknown name and a natively-owned name are different facts, and
/// collapsing them would hide the second, which is the one a reviewer needs to
/// see.
pub fn admit(entrypoint_id: &str) -> Admission {
    match find(entrypoint_id) {
        None => Admission::Unknown,
        Some(row) => match row.native_alternative {
            Some(native) => Admission::RefusedForNativePath(native),
            None => Admission::Admitted(row),
        },
    }
}
