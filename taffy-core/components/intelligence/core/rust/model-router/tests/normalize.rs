// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Request-side hygiene: minted identities, image placeholders, thinking replay.
//!
//! Decision 0069: identities are minted, never renamed. A sanitizer that maps
//! two ids onto one is a silent wrong result, so a string the core did not
//! mint is a defect rather than a rewrite. Images the target model cannot
//! accept become a compiled-in placeholder. Thinking blocks are kept only
//! when the same model can replay them.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

use model_router::normalize::{
    assert_call_id, call_id_is_taffy_mint, downgrade_images, placeholder, thinking_for_target,
    CallIdDefect, ContentPart, ImageSource, ThinkingBlock, ThinkingReplay, TOOL_IMAGE_PLACEHOLDER,
    USER_IMAGE_PLACEHOLDER,
};

#[test]
fn the_widest_mint_is_a_call_identity() {
    // Decision 0069 section 5: `turn-{u64::MAX}-call-{u32::MAX}` is 41
    // characters drawn from `[a-z0-9-]`, inside Anthropic's 64-character
    // `^[a-zA-Z0-9_-]+$` cap by construction. Asserted at the widest input
    // rather than a typical one, so a future change to the format fails here
    // rather than at a provider.
    let widest = format!("turn-{}-call-{}", u64::MAX, u32::MAX);
    assert_eq!(widest.len(), 41);
    assert!(call_id_is_taffy_mint(&widest));
    assert_eq!(assert_call_id(&widest), Ok(()));
}

#[test]
fn a_pipe_in_an_identity_is_a_defect_not_a_rewrite() {
    // OpenAI Responses mints identities that carry `|`. The reference
    // implementation rewrites that byte to `_`. Applied here the rewrite
    // would be a repair with nothing to repair, and a repair on this field
    // can map two distinct calls onto one string.
    let foreign = "resp_abc|tool_0";
    assert!(!call_id_is_taffy_mint(foreign));
    assert_eq!(
        assert_call_id(foreign),
        Err(CallIdDefect::InvalidByte { byte: b'|' })
    );
    assert_eq!(
        CallIdDefect::InvalidByte { byte: b'|' }.label(),
        "invalid-byte"
    );
}

#[test]
fn a_string_that_is_not_a_mint_is_refused() {
    assert_eq!(assert_call_id(""), Err(CallIdDefect::Empty));
    assert_eq!(assert_call_id("turn-1-call-"), Err(CallIdDefect::NotAMint));
    assert_eq!(assert_call_id("turn--call-0"), Err(CallIdDefect::NotAMint));
    assert_eq!(
        assert_call_id("turn-1-call-2 extra"),
        Err(CallIdDefect::InvalidByte { byte: b' ' })
    );
    let too_long = "a".repeat(65);
    assert_eq!(
        assert_call_id(&too_long),
        Err(CallIdDefect::TooLong { len: 65 })
    );
    assert!(!call_id_is_taffy_mint("resp_abc"));
    assert!(!call_id_is_taffy_mint("turn-1-call-"));
    assert!(!call_id_is_taffy_mint("turn--call-0"));
}

#[test]
fn unsupported_images_use_the_source_placeholder() {
    let parts = [ContentPart::Image];
    assert_eq!(
        downgrade_images(&parts, false, ImageSource::User),
        [ContentPart::Text(USER_IMAGE_PLACEHOLDER)]
    );
    assert_eq!(
        downgrade_images(&parts, false, ImageSource::ToolResult),
        [ContentPart::Text(TOOL_IMAGE_PLACEHOLDER)]
    );
    assert_eq!(placeholder(ImageSource::User), USER_IMAGE_PLACEHOLDER);
    assert_eq!(placeholder(ImageSource::ToolResult), TOOL_IMAGE_PLACEHOLDER);
}

#[test]
fn consecutive_images_collapse_to_one_placeholder() {
    let parts = [
        ContentPart::Text("before"),
        ContentPart::Image,
        ContentPart::Image,
        ContentPart::Text("after"),
    ];
    assert_eq!(
        downgrade_images(&parts, false, ImageSource::User),
        [
            ContentPart::Text("before"),
            ContentPart::Text(USER_IMAGE_PLACEHOLDER),
            ContentPart::Text("after"),
        ]
    );
}

#[test]
fn a_model_that_accepts_images_keeps_them() {
    let parts = [
        ContentPart::Text("hi"),
        ContentPart::Image,
        ContentPart::Image,
    ];
    assert_eq!(downgrade_images(&parts, true, ImageSource::User), parts);
}

#[test]
fn thinking_is_kept_only_when_the_same_model_can_replay_it() {
    let redacted = ThinkingBlock {
        text: "hidden",
        signature: Some("sig"),
        redacted: true,
    };
    assert_eq!(thinking_for_target(redacted, false), ThinkingReplay::Drop);
    assert_eq!(
        thinking_for_target(redacted, true),
        ThinkingReplay::Keep(redacted)
    );

    let empty_signed = ThinkingBlock {
        text: "",
        signature: Some("sig"),
        redacted: false,
    };
    assert_eq!(
        thinking_for_target(empty_signed, true),
        ThinkingReplay::Keep(empty_signed)
    );
    assert_eq!(
        thinking_for_target(empty_signed, false),
        ThinkingReplay::Drop
    );

    let empty_unsigned = ThinkingBlock {
        text: "",
        signature: None,
        redacted: false,
    };
    assert_eq!(
        thinking_for_target(empty_unsigned, true),
        ThinkingReplay::Drop
    );
    let empty_blank_signature = ThinkingBlock {
        text: "",
        signature: Some(""),
        redacted: false,
    };
    assert_eq!(
        thinking_for_target(empty_blank_signature, true),
        ThinkingReplay::Drop
    );
    assert_eq!(
        thinking_for_target(empty_unsigned, false),
        ThinkingReplay::Drop
    );

    let thought = ThinkingBlock {
        text: "because",
        signature: Some("sig"),
        redacted: false,
    };
    assert_eq!(
        thinking_for_target(thought, false),
        ThinkingReplay::AsText("because")
    );
    assert_eq!(
        thinking_for_target(thought, true),
        ThinkingReplay::Keep(thought)
    );

    let unsigned_thought = ThinkingBlock {
        text: "because",
        signature: None,
        redacted: false,
    };
    assert_eq!(
        thinking_for_target(unsigned_thought, true),
        ThinkingReplay::Keep(unsigned_thought)
    );
}
