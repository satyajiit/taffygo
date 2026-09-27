// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Browser-resident visual attachments through every request vocabulary.

#![allow(clippy::expect_used, clippy::panic, clippy::unwrap_used)]

use model_router::catalog::WireApi;
use model_router::thinking::ThinkingPlan;
use model_router::wire::{
    write_managed_media_request, write_media_request, ManagedRequest, Speaker, Turn,
    WireMediaAttachment, WireRefusal, WireRequest, MEDIA_ATTACHMENT_MIME_TYPE,
};
use serde_json::{json, Value};

const HANDLE: &str = "media-03981b7d-5408-4b11-983e-a6fe63206c93";

fn media() -> WireMediaAttachment<'static> {
    WireMediaAttachment {
        handle: HANDLE,
        mime_type: MEDIA_ATTACHMENT_MIME_TYPE,
    }
}

fn direct(api: WireApi, text: &str) -> (String, Value) {
    let pieces = [text];
    let turns = [Turn::Said {
        speaker: Speaker::User,
        text: &pieces,
    }];
    let thinking = ThinkingPlan::Disabled;
    let request = WireRequest {
        model_id: "vision-model",
        tool_calling: true,
        system: Some("Read the image and page together."),
        system_preamble: None,
        credential_method: None,
        compat: None,
        turns: &turns,
        tools: &[],
        tool_cache_retention: model_router::request::CacheRetention::None,
        conversation_key: None,
        thinking: &thinking,
        answer_tokens: 1_024,
        stream: true,
    };
    let mut body = String::new();
    write_media_request(api, &request, media(), &mut body).expect("a visual turn writes");
    let parsed = serde_json::from_str(&body).expect("the provider body is JSON");
    (body, parsed)
}

#[test]
fn every_direct_family_places_the_same_handle_once_in_its_native_image_shape() {
    let cases = [
        (
            WireApi::AnthropicMessages,
            "/messages/0/content/1",
            json!({
                "type": "image",
                "source": {
                    "type": "base64",
                    "media_type": "image/png",
                    "data": HANDLE,
                },
            }),
        ),
        (
            WireApi::OpenAiResponses,
            "/input/0/content/1",
            json!({
                "type": "input_image",
                "image_url": format!("data:image/png;base64,{HANDLE}"),
            }),
        ),
        (
            WireApi::OpenAiCodexResponses,
            "/input/0/content/1",
            json!({
                "type": "input_image",
                "image_url": format!("data:image/png;base64,{HANDLE}"),
            }),
        ),
        (
            WireApi::OpenAiCompletions,
            "/messages/1/content/1",
            json!({
                "type": "image_url",
                "image_url": { "url": format!("data:image/png;base64,{HANDLE}") },
            }),
        ),
        (
            WireApi::GoogleGenerativeLanguage,
            "/contents/0/parts/1",
            json!({
                "inlineData": { "mimeType": "image/png", "data": HANDLE },
            }),
        ),
    ];

    for (api, pointer, expected) in cases {
        let (body, document) = direct(api, "describe this exact image");
        assert_eq!(body.matches(HANDLE).count(), 1, "{api:?}");
        assert_eq!(document.pointer(pointer), Some(&expected), "{api:?}");
    }
}

#[test]
fn the_managed_schema_carries_one_canonical_image_part() {
    let pieces = ["describe this exact image"];
    let turns = [Turn::Said {
        speaker: Speaker::User,
        text: &pieces,
    }];
    let thinking = ThinkingPlan::Disabled;
    let request = ManagedRequest {
        request_id: "0f95e8a2-77c4-41d3-8b6e-2a9c41d37f00",
        model_id: "vision-model",
        system: Some("Read the image and page together."),
        turns: &turns,
        tools: &[],
        thinking: &thinking,
        answer_tokens: 1_024,
    };
    let mut body = String::new();
    write_managed_media_request(&request, media(), &mut body).expect("managed visual body");
    let document: Value = serde_json::from_str(&body).expect("canonical JSON");

    assert_eq!(body.matches(HANDLE).count(), 1);
    assert_eq!(
        document.pointer("/messages/1/content/1"),
        Some(&json!({ "type": "image", "mime_type": "image/png", "data": HANDLE }))
    );
}

#[test]
fn content_that_repeats_the_handle_refuses_the_whole_write() {
    let mut out = String::from("caller-prefix");
    let pieces = [HANDLE];
    let turns = [Turn::Said {
        speaker: Speaker::User,
        text: &pieces,
    }];
    let thinking = ThinkingPlan::Disabled;
    let request = WireRequest {
        model_id: "vision-model",
        tool_calling: true,
        system: None,
        system_preamble: None,
        credential_method: None,
        compat: None,
        turns: &turns,
        tools: &[],
        tool_cache_retention: model_router::request::CacheRetention::None,
        conversation_key: None,
        thinking: &thinking,
        answer_tokens: 1_024,
        stream: true,
    };

    assert_eq!(
        write_media_request(WireApi::AnthropicMessages, &request, media(), &mut out),
        Err(WireRefusal::AmbiguousMediaHandle { occurrences: 2 })
    );
    assert_eq!(out, "caller-prefix");
}

#[test]
fn a_non_png_attachment_is_refused_before_any_body_is_written() {
    let mut out = String::from("caller-prefix");
    let pieces = ["look"];
    let turns = [Turn::Said {
        speaker: Speaker::User,
        text: &pieces,
    }];
    let thinking = ThinkingPlan::Disabled;
    let request = WireRequest {
        model_id: "vision-model",
        tool_calling: true,
        system: None,
        system_preamble: None,
        credential_method: None,
        compat: None,
        turns: &turns,
        tools: &[],
        tool_cache_retention: model_router::request::CacheRetention::None,
        conversation_key: None,
        thinking: &thinking,
        answer_tokens: 1_024,
        stream: true,
    };
    let invalid = WireMediaAttachment {
        handle: HANDLE,
        mime_type: "image/jpeg",
    };

    assert_eq!(
        write_media_request(WireApi::OpenAiResponses, &request, invalid, &mut out),
        Err(WireRefusal::InvalidMediaAttachment)
    );
    assert_eq!(out, "caller-prefix");
}
