// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Sealed reasoning, and why it is a type rather than a `&str`.
//!
//! One family returns its reasoning as encrypted content it will not accept a
//! next turn without. The only correct thing to do with it is hand it back
//! exactly as it arrived, and every other thing that could be done with a
//! string — read it, join it into a turn, write it to a record, print it while
//! debugging — is a disclosure of material the provider deliberately sealed.
//!
//! Intending not to do those is not a control. This module is the control: the
//! value is reachable through one module-private method, so every site that
//! can read it is inside `wire` and there is exactly one, the writer that puts
//! it back. It is the same shape
//! [`SecretMaterial`][crate::credential::SecretMaterial] takes for the same
//! reason, minus the erasure, because this is the provider's secret rather
//! than the person's.

use crate::json::JsonValue;

/// Reasoning a family sealed, on its way back to the conversation it came from.
///
/// One family returns its reasoning as encrypted content it will not accept a
/// next turn without, and the *only* correct thing to do with it is hand it
/// back exactly as it arrived. Everything else — reading it, joining it into a
/// turn's text, writing it to a record, printing it while debugging — is a
/// disclosure of material the provider deliberately sealed, and none of those
/// is prevented by intending not to do them.
///
/// So this type prevents them instead. It has no serde implementation, no
/// [`Display`][core::fmt::Display], and a [`Debug`][core::fmt::Debug] that
/// prints a fixed marker; the value inside it is reachable only through a
/// module-private method, so every site that can read it is inside `wire` and
/// there is exactly one — the writer that puts it back. A transcript
/// projection or an audit record cannot obtain the bytes from a value of this
/// type at all, which is what makes "it never reaches one" a fact about the
/// signature rather than a claim about the code.
///
/// It borrows, like every other view here, so it cannot outlive the reply it
/// came from. Carrying reasoning across a process restart would mean storing
/// it, and storing it is a durable-record decision this crate does not get to
/// make on its own.
#[derive(Clone, Copy, PartialEq, Eq)]
pub struct OpaqueReasoning<'a> {
    item: &'a JsonValue,
}

impl<'a> OpaqueReasoning<'a> {
    /// Wraps the item a reply carried.
    ///
    /// Module-private, so the only way to have one is to have read it out of a
    /// reply through [`super::reply`]: nothing above `wire` can mint one, and
    /// therefore nothing above `wire` can put material of its own choosing
    /// into a place the model reads as its own sealed thinking.
    pub(super) const fn new(item: &'a JsonValue) -> Self {
        Self { item }
    }

    /// The item, for the one writer that hands it back.
    pub(super) const fn for_replay(self) -> &'a JsonValue {
        self.item
    }
}

impl core::fmt::Debug for OpaqueReasoning<'_> {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.write_str("OpaqueReasoning(sealed)")
    }
}
