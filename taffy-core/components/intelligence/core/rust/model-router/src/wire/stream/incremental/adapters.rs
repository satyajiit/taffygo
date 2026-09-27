// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Whole-body compatibility adapters over the incremental decoder.

use crate::catalog::WireApi;
use crate::wire::managed_stream::{ManagedReading, ManagedReplyDefect};
use crate::wire::reply::ReplyContext;

use super::{ModelStreamDecoder, ModelStreamDefect, RawReading};
use crate::wire::stream::{FoldedReply, StreamDefect};

pub(super) fn fold_direct(
    api: WireApi,
    body: &str,
    context: &ReplyContext,
) -> Result<FoldedReply, StreamDefect> {
    let mut decoder = ModelStreamDecoder::direct(api, *context);
    for chunk in body.as_bytes().split_inclusive(|byte| *byte == b'\n') {
        decoder.push(chunk, &mut |_| {}).map_err(direct_defect)?;
        if decoder.terminal_ready() {
            return match decoder.finish_raw(&mut |_| {}).map_err(direct_defect)? {
                RawReading::Direct(reading) => Ok(reading),
                RawReading::Managed(_) => Err(StreamDefect::MalformedFrame),
            };
        }
    }
    match decoder.finish_raw(&mut |_| {}).map_err(direct_defect)? {
        RawReading::Direct(reading) => Ok(reading),
        RawReading::Managed(_) => Err(StreamDefect::MalformedFrame),
    }
}

pub(crate) fn fold_managed(
    body: &str,
    expected_request_id: &str,
    context: &ReplyContext,
) -> Result<ManagedReading, ManagedReplyDefect> {
    let mut decoder = ModelStreamDecoder::managed(expected_request_id, *context);
    decoder
        .push(body.as_bytes(), &mut |_| {})
        .map_err(|defect| managed_defect(&defect))?;
    match decoder
        .finish_raw(&mut |_| {})
        .map_err(|defect| managed_defect(&defect))?
    {
        RawReading::Managed(reading) => Ok(reading),
        RawReading::Direct(_) => Err(ManagedReplyDefect::MalformedFrame),
    }
}

fn direct_defect(defect: ModelStreamDefect) -> StreamDefect {
    match defect {
        ModelStreamDefect::Direct(defect) => defect,
        ModelStreamDefect::AlreadyFinished => StreamDefect::AlreadyTerminated,
        ModelStreamDefect::InputTooLarge
        | ModelStreamDefect::FrameTooLarge
        | ModelStreamDefect::InvalidUtf8
        | ModelStreamDefect::MalformedFraming
        | ModelStreamDefect::Managed(_) => StreamDefect::MalformedFrame,
    }
}

const fn managed_defect(defect: &ModelStreamDefect) -> ManagedReplyDefect {
    match defect {
        ModelStreamDefect::Managed(defect) => *defect,
        ModelStreamDefect::InputTooLarge => ManagedReplyDefect::StreamTooLarge,
        ModelStreamDefect::AlreadyFinished => ManagedReplyDefect::EventAfterTerminal,
        ModelStreamDefect::FrameTooLarge
        | ModelStreamDefect::InvalidUtf8
        | ModelStreamDefect::MalformedFraming
        | ModelStreamDefect::Direct(_) => ManagedReplyDefect::MalformedFrame,
    }
}
