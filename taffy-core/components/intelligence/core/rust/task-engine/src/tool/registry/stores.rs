// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Rows over a person's own stores handed to the task (decision 0133).
//!
//! Each row is a browser-executed local read of class `ProfileStoreRead`.
//! A start request attaches a store by naming its allowlist group, so a row
//! whose store was not attached is simply not on the effective set and a
//! call on it is refused by name.

use super::super::entry::{
    IdempotencyClass, NameMatch, ToolAvailability, ToolDispatch, ToolEntry, ToolLoading,
};
use super::super::milestone::Milestone;
use super::super::parameters;
use crate::authority::ActionClass;

pub(super) const ROWS: &[ToolEntry] = &[
    ToolEntry {
        name: "history.search",
        matching: NameMatch::Exact,
        availability: ToolAvailability::From(Milestone::M3),
        loading: ToolLoading::Immediate,
        idempotency: IdempotencyClass::PureRead,
        dispatch: ToolDispatch::BrowserAction(ActionClass::ProfileStoreRead),
        terminal_safe: false,
        purpose: "Visits in the person's history matching words",
        parameters: parameters::STORE_SEARCH,
    },
    ToolEntry {
        name: "history.recent",
        matching: NameMatch::Exact,
        availability: ToolAvailability::From(Milestone::M3),
        loading: ToolLoading::Immediate,
        idempotency: IdempotencyClass::PureRead,
        dispatch: ToolDispatch::BrowserAction(ActionClass::ProfileStoreRead),
        terminal_safe: false,
        purpose: "The person's latest visits",
        parameters: parameters::STORE_LIST,
    },
    ToolEntry {
        name: "bookmarks.search",
        matching: NameMatch::Exact,
        availability: ToolAvailability::From(Milestone::M3),
        loading: ToolLoading::Immediate,
        idempotency: IdempotencyClass::PureRead,
        dispatch: ToolDispatch::BrowserAction(ActionClass::ProfileStoreRead),
        terminal_safe: false,
        purpose: "The person's bookmarks matching words",
        parameters: parameters::STORE_SEARCH,
    },
    ToolEntry {
        name: "bookmarks.list",
        matching: NameMatch::Exact,
        availability: ToolAvailability::From(Milestone::M3),
        loading: ToolLoading::Immediate,
        idempotency: IdempotencyClass::PureRead,
        dispatch: ToolDispatch::BrowserAction(ActionClass::ProfileStoreRead),
        terminal_safe: false,
        purpose: "The person's bookmarks, folders flattened",
        parameters: parameters::STORE_LIST,
    },
    ToolEntry {
        name: "open_tabs.list",
        matching: NameMatch::Exact,
        availability: ToolAvailability::From(Milestone::M3),
        loading: ToolLoading::Immediate,
        idempotency: IdempotencyClass::PureRead,
        dispatch: ToolDispatch::BrowserAction(ActionClass::ProfileStoreRead),
        terminal_safe: false,
        purpose: "The person's own open tabs",
        parameters: parameters::NONE,
    },
];
