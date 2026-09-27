// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Scale regression for bounded request assembly on every direct family.

use model_router::catalog::WireApi;
use model_router::thinking::ThinkingPlan;
use model_router::wire::request::{Speaker, Turn, WireRefusal, WireRequest};
use model_router::wire::{write_request, MAX_BODY_BYTES};

#[test]
fn every_direct_writer_rolls_back_while_counting_an_oversized_body() {
    let oversized = "x".repeat(MAX_BODY_BYTES);
    let pieces = [oversized.as_str()];
    let turns = [Turn::Said {
        speaker: Speaker::User,
        text: &pieces,
    }];
    let thinking = ThinkingPlan::Disabled;
    let request = WireRequest {
        model_id: "model",
        tool_calling: false,
        system: None,
        system_preamble: None,
        credential_method: None,
        compat: None,
        turns: &turns,
        tools: &[],
        tool_cache_retention: model_router::request::CacheRetention::None,
        conversation_key: None,
        thinking: &thinking,
        answer_tokens: 512,
        stream: false,
    };

    for api in [
        WireApi::AnthropicMessages,
        WireApi::OpenAiResponses,
        WireApi::OpenAiCompletions,
        WireApi::GoogleGenerativeLanguage,
        WireApi::OpenAiCodexResponses,
    ] {
        let mut out = String::from("prefix-the-caller-owns");
        let result = write_request(api, &request, &mut out);
        let bytes = match result {
            Err(WireRefusal::BodyTooLarge { bytes }) => Some(bytes),
            _ => None,
        };
        assert!(
            bytes.is_some(),
            "{api:?} accepted an oversized body: {result:?}"
        );
        assert!(
            bytes.unwrap_or_default() > MAX_BODY_BYTES,
            "{api:?} lost byte accounting"
        );
        assert_eq!(out, "prefix-the-caller-owns", "{api:?} did not roll back");
    }
}
