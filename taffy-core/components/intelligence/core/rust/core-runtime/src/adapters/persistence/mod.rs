// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Canonical task-transaction encoding and committed replay.

mod action_value;
mod command;
mod command_body;
mod consent;
mod effect;
mod enum_action;
mod enum_journal;
mod enum_permission;
mod enum_task;
mod event;
mod handover;
mod journal;
mod restore;
mod turn;
mod value;

pub use self::journal::{decode_task_restore, ProductionStorage, RestoreDecodeError};
pub use self::restore::{DecodedTaskRestore, RestoredTaskEffectBatch};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum ConversionError {
    InvalidIdentifier,
    InvalidValue,
    NumericOverflow,
}
