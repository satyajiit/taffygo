// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Composition of one descriptor-backed media job.

use core_runtime::ports::ActionEffectFacts;
use core_runtime::ActionIntent;

use crate::service_bridge_runtime::ServiceBridge;

pub(super) struct MediaJobBody {
    pub(super) operation: u8,
    pub(super) source_id: String,
    pub(super) source_browser_session_id: String,
    pub(super) source_bytes: u64,
    pub(super) max_frames: u32,
}

pub(super) fn compose(
    bridge: &ServiceBridge,
    facts: &ActionEffectFacts,
) -> Result<MediaJobBody, ()> {
    let ActionIntent::MediaTool(intent) = facts.proposal.intent() else {
        return Err(());
    };
    if intent.tool_name != intent.operation.tool_name()
        || intent.source_id.is_empty()
        || !(1..=16 * 1024 * 1024).contains(&intent.source_bytes)
        || intent.source_browser_session_id.as_str()
            != bridge
                .runtime
                .as_ref()
                .ok_or(())?
                .browser_session_id()
                .as_str()
        || (intent.operation == core_runtime::MediaOperation::SampleFrames
            && !(1..=12).contains(&intent.max_frames))
        || (intent.operation != core_runtime::MediaOperation::SampleFrames
            && intent.max_frames != 0)
    {
        return Err(());
    }
    Ok(MediaJobBody {
        operation: intent.operation.wire_tag(),
        source_id: intent.source_id.clone(),
        source_browser_session_id: intent.source_browser_session_id.as_str().to_owned(),
        source_bytes: intent.source_bytes,
        max_frames: intent.max_frames,
    })
}
