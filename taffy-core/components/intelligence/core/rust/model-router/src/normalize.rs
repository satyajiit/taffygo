// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Request-side hygiene for a replayed transcript.
//!
//! Decision 0069: identities are minted, never renamed. Images the target
//! model cannot accept become a compiled-in placeholder rather than a failed
//! request. Thinking blocks are kept only when the same model can replay them.
//!
//! The conversation views this crate writes today are text, tool calls and
//! tool results. Image parts and thinking blocks have no field on [`Turn`] to
//! travel in yet. The functions here are still the real transform: they take
//! borrowed views, hold no page bytes, and do not invent a result for a call
//! that has none. Call-identity assertion is on the write path; the rest is
//! ready for the views that will carry those parts.
//!
//! [`Turn`]: crate::wire::Turn

use core::str::FromStr;

/// Longest call identity the strictest family accepts, in bytes.
///
/// Anthropic requires `^[a-zA-Z0-9_-]+$` and a 64-character cap. A Taffy mint
/// is at most 41 characters (decision 0069 section 5), so a mint that reaches
/// this bound is already a defect in whatever produced it. The bound is still
/// checked: asserting a property that is true by construction is how it stays
/// true.
const MAX_CALL_ID_BYTES: usize = 64;

/// Prefix of a minted call identity.
const MINT_PREFIX: &str = "turn-";

/// Separator between the turn ordinal and the call sequence.
const MINT_SEPARATOR: &str = "-call-";

/// Placeholder written in place of a user image the target model cannot accept.
pub const USER_IMAGE_PLACEHOLDER: &str = "(image omitted: model does not support images)";

/// Placeholder written in place of a tool-result image the target model cannot
/// accept.
pub const TOOL_IMAGE_PLACEHOLDER: &str = "(tool image omitted: model does not support images)";

/// Why a call identity was refused.
///
/// A closed enumeration rather than a rewritten string. Decision 0069
/// sections 4–5: a sanitizer that maps two ids onto one is a silent wrong
/// result, so the only answer for a string the core did not mint is a named
/// defect.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CallIdDefect {
    /// Nothing was supplied.
    Empty,
    /// Longer than the 64 bytes the strictest family accepts.
    TooLong {
        /// Length of the offending identity in bytes.
        len: usize,
    },
    /// A byte the strictest family would rewrite.
    InvalidByte {
        /// The offending byte.
        byte: u8,
    },
    /// The string is not `turn-{ordinal}-call-{sequence}`.
    NotAMint,
}

impl CallIdDefect {
    /// A short, compiled-in name for the defect.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Empty => "empty",
            Self::TooLong { .. } => "too-long",
            Self::InvalidByte { .. } => "invalid-byte",
            Self::NotAMint => "not-a-mint",
        }
    }
}

impl core::fmt::Display for CallIdDefect {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        match self {
            Self::Empty => f.write_str("call identity is empty"),
            Self::TooLong { len } => {
                write!(f, "call identity is {len} bytes, over the limit")
            }
            Self::InvalidByte { byte } => {
                write!(f, "call identity contains byte 0x{byte:02x}")
            }
            Self::NotAMint => f.write_str("call identity is not a minted identity"),
        }
    }
}

/// One piece of content in a turn the wire does not yet carry as a view.
///
/// [`ContentPart::Image`] has no data field: this crate must remain
/// structurally unable to hold page-derived bytes, and a placeholder does not
/// need the bytes it is replacing.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ContentPart<'a> {
    /// Ordinary text.
    Text(&'a str),
    /// An image the caller already holds. The bytes stay with the caller.
    Image,
}

/// Whose image is being replaced, which picks the compiled-in sentence.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ImageSource {
    /// An image the person supplied.
    User,
    /// An image a tool result carried.
    ToolResult,
}

/// One thinking block as it arrived, borrowed.
///
/// A signature is opaque to this crate and meaningful only to the model that
/// minted it. Redacted thinking is encrypted content for that model and is
/// not a sentence anyone else can read.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ThinkingBlock<'a> {
    /// The thinking text, when it is text.
    pub text: &'a str,
    /// The model's own signature, when it produced one.
    pub signature: Option<&'a str>,
    /// Whether the block is opaque encrypted content rather than text.
    pub redacted: bool,
}

/// What to do with one thinking block when replaying it to a target model.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ThinkingReplay<'a> {
    /// The target can replay it as thinking.
    Keep(ThinkingBlock<'a>),
    /// The target cannot replay it as thinking; write the text as ordinary
    /// prose so the next model still sees that reasoning happened.
    AsText(&'a str),
    /// Drop the block. There is nothing a different model can do with
    /// redacted content, and an empty unsigned block is not a thought.
    Drop,
}

/// Asserts that `id` is a Taffy mint the strictest family will accept unaltered.
///
/// The format is `turn-{ordinal}-call-{sequence}` with a decimal [`u64`]
/// ordinal and a decimal [`u32`] sequence — the inverse of `turn_call_of`,
/// applied to the string the core already minted. Both halves must parse
/// whole: `from_str` on each, so leading junk, a missing sequence, or an
/// empty ordinal is a defect rather than a turn that never happened.
///
/// The string must also match `^[a-zA-Z0-9_-]+$` and fit in 64 bytes. A mint
/// satisfies both by construction; they are still checked, because a
/// sanitizer that rewrote `|` to `_` and sliced to 64 characters is the
/// exact repair this function exists not to perform.
pub fn assert_call_id(id: &str) -> Result<(), CallIdDefect> {
    if id.is_empty() {
        return Err(CallIdDefect::Empty);
    }
    if id.len() > MAX_CALL_ID_BYTES {
        return Err(CallIdDefect::TooLong { len: id.len() });
    }
    if let Some(byte) = id.bytes().find(|&byte| !accepted_call_id_byte(byte)) {
        return Err(CallIdDefect::InvalidByte { byte });
    }
    if !call_id_is_taffy_mint(id) {
        return Err(CallIdDefect::NotAMint);
    }
    Ok(())
}

/// Whether `id` is `turn-{ordinal}-call-{sequence}` with both numbers parsed
/// whole.
///
/// Strict rather than lenient, matching `turn_call_of`. A provider-minted
/// identity such as `resp_…` is not a mint, even when it happens to fit the
/// character set the strictest family accepts.
pub fn call_id_is_taffy_mint(id: &str) -> bool {
    let Some(rest) = id.strip_prefix(MINT_PREFIX) else {
        return false;
    };
    let Some((ordinal, sequence)) = rest.split_once(MINT_SEPARATOR) else {
        return false;
    };
    u64::from_str(ordinal).is_ok() && u32::from_str(sequence).is_ok()
}

/// Bytes Anthropic will accept in a tool-use identity: `^[a-zA-Z0-9_-]+$`.
const fn accepted_call_id_byte(byte: u8) -> bool {
    byte.is_ascii_alphanumeric() || byte == b'_' || byte == b'-'
}

/// The compiled-in sentence that replaces an image `source` produced.
pub const fn placeholder(source: ImageSource) -> &'static str {
    match source {
        ImageSource::User => USER_IMAGE_PLACEHOLDER,
        ImageSource::ToolResult => TOOL_IMAGE_PLACEHOLDER,
    }
}

/// Replaces images the target model cannot accept with one compiled-in
/// placeholder per run of consecutive images.
///
/// When `accepts_image` is true the parts are returned unchanged, images
/// included. When it is false, consecutive [`ContentPart::Image`] parts
/// collapse to a single [`ContentPart::Text`] carrying [`placeholder`] for
/// `source`; text parts pass through. Two images with text between them
/// become two placeholders, because they were two images the person or the
/// tool actually supplied.
pub fn downgrade_images<'a>(
    parts: &[ContentPart<'a>],
    accepts_image: bool,
    source: ImageSource,
) -> Vec<ContentPart<'a>> {
    if accepts_image {
        return parts.to_vec();
    }
    let omitted = placeholder(source);
    let mut out = Vec::with_capacity(parts.len());
    let mut pending_images = false;
    for part in parts.iter().copied() {
        match part {
            ContentPart::Image => pending_images = true,
            ContentPart::Text(_) => {
                if pending_images {
                    out.push(ContentPart::Text(omitted));
                    pending_images = false;
                }
                out.push(part);
            }
        }
    }
    if pending_images {
        out.push(ContentPart::Text(omitted));
    }
    out
}

/// What to do with one thinking block when the next request targets a model.
///
/// Redacted thinking is opaque encrypted content and is only valid for the
/// model that produced it: kept when `same_model`, dropped otherwise. Empty
/// thinking is kept only when the same model can replay it *and* a signature
/// is present (encrypted reasoning with no plaintext). Empty unsigned
/// thinking is dropped. Non-empty thinking stays a thinking block for the
/// same model and becomes ordinary text across models, signature or not —
/// a signature minted by one model is not a signature another can verify.
pub fn thinking_for_target(block: ThinkingBlock<'_>, same_model: bool) -> ThinkingReplay<'_> {
    if block.redacted {
        return if same_model {
            ThinkingReplay::Keep(block)
        } else {
            ThinkingReplay::Drop
        };
    }
    let empty = block.text.trim().is_empty();
    // A present-but-empty signature is not a signature: the reference
    // implementation treats `""` as unsigned, and keeping unsigned empty
    // thinking would replay a block nothing can verify.
    let signed = block
        .signature
        .is_some_and(|signature| !signature.is_empty());
    if empty {
        return if same_model && signed {
            ThinkingReplay::Keep(block)
        } else {
            ThinkingReplay::Drop
        };
    }
    if same_model {
        ThinkingReplay::Keep(block)
    } else {
        ThinkingReplay::AsText(block.text)
    }
}
