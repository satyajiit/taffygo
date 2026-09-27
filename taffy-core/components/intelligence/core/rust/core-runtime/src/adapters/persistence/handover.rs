// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The durable projection of one handover.
//!
//! A handover is the assistant stopping and giving the page to the person, and
//! the two directions here are what makes that replayable. It sits beside
//! [`super::turn`] and not inside [`super::command`] for the reason that file
//! states about itself: `command` and `uncommand` are the index of the
//! persisted command surface, and a body decoded inside one of them is a body
//! a reader has to step over to read the index.
//!
//! Nothing here names a challenge, a one-time code or a password, and that is
//! the design rather than an omission — see `task_engine::handover`.

use core_service_types as wire;
use task_engine::Command;

use super::ConversionError;

/// The three handover bodies, written.
///
/// One helper rather than three arms in [`super::command`], so that match stays an
/// index of the command surface rather than a place where a body is encoded.
/// The `_` arm is unreachable through the one call site, which names exactly
/// these three, and refusing rather than defaulting is what keeps it that way.
pub(super) fn persisted_handover(
    value: &Command,
) -> Result<wire::PersistedCommand, ConversionError> {
    Ok(match value {
        Command::RequestHandover { handover_id } => wire::PersistedCommand::RequestHandover {
            handover_id: handover_id.as_str().to_owned(),
        },
        Command::ExpireHandover { handover_id } => wire::PersistedCommand::ExpireHandover {
            handover_id: handover_id.as_str().to_owned(),
        },
        Command::CompleteHandover(completion) => wire::PersistedCommand::CompleteHandover {
            completion: wire::PersistedHandoverCompletion {
                handover_id: completion.handover_id().as_str().to_owned(),
                lease_before: completion.lease_before().as_str().to_owned(),
                resumed_with: completion.resumed_with().as_str().to_owned(),
                person_input: completion.person_input().count().into(),
            },
        },
        _ => return Err(ConversionError::InvalidValue),
    })
}

/// The three handover bodies, restored.
///
/// Split out of [`super::command`]'s decode index so that match stays an index of the command
/// surface rather than a place where a body is decoded. Every identity is
/// re-validated on the way in: a journal is a file on a device, and a
/// restored identity that was never checked is an identity an editor chose.
pub(super) fn restored_handover(value: wire::PersistedCommand) -> Result<Command, ConversionError> {
    Ok(match value {
        wire::PersistedCommand::RequestHandover { handover_id } => Command::RequestHandover {
            handover_id: handover(handover_id)?,
        },
        wire::PersistedCommand::CompleteHandover { completion } => {
            restored_handover_completion(completion)?
        }
        wire::PersistedCommand::ExpireHandover { handover_id } => Command::ExpireHandover {
            handover_id: handover(handover_id)?,
        },
        _ => return Err(ConversionError::InvalidValue),
    })
}

fn handover(value: String) -> Result<task_engine::HandoverId, ConversionError> {
    task_engine::HandoverId::new(value).map_err(|_| ConversionError::InvalidIdentifier)
}

/// One completion, restored with its evidence clamped again on the way in.
///
/// `PersonInput::observed` saturates, so a record that somehow carried a
/// larger number — a hand-edited journal, a writer from a build that changed
/// the ceiling — comes back as the ceiling rather than as the number it
/// claimed. The clamp is the promise, and a promise that only held on the way
/// out would not be one.
fn restored_handover_completion(
    completion: wire::PersistedHandoverCompletion,
) -> Result<Command, ConversionError> {
    Ok(Command::CompleteHandover(
        task_engine::HandoverCompletion::new(
            handover(completion.handover_id)?,
            task_engine::ActorLeaseId::new(completion.lease_before),
            task_engine::ActorLeaseId::new(completion.resumed_with),
            task_engine::PersonInput::observed(completion.person_input),
        )
        .map_err(|_| ConversionError::InvalidIdentifier)?,
    ))
}
