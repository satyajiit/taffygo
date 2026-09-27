// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The shape of one protocol family, as data.
//!
//! Nothing in this module knows any provider. It declares the vocabulary a
//! family is described *in* — where a field sits, what it is called, and which
//! of a handful of structural choices it makes — so that the four descriptions
//! in the sibling modules are tables rather than code.
//!
//! Two properties are the point of writing it this way.
//!
//! **A missing case is a compile error.** Every structural choice is a closed
//! enumeration and every reader matches it exhaustively with no catch-all arm.
//! Four hand-written encoders would instead have four independent chances to
//! quietly omit a branch — the failure mode being a request that is accepted
//! and answered wrongly, which no test that only checks for an error will see.
//!
//! **A difference is visible where it is decided.** Reading the four tables
//! side by side is how one learns that a Google turn calls the assistant
//! `model`, that only Anthropic reports uncached input separately from cache
//! reads, that Google mints no identity for a tool call at all, and that of
//! the four only Anthropic has a field for saying a tool result failed. Those
//! facts are load-bearing and each of them is one field here.

use crate::catalog::WireApi;
use crate::request::{ErrorClass, StopReason};

/// One move along a decoded reply.
///
/// Reply shapes are not all objects: two families wrap the whole answer in a
/// single-element array. Making the array hop a step of the path keeps every
/// location in the table a path, rather than a path plus a rule about when to
/// index.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Step {
    /// Enter the named field of an object.
    Key(&'static str),
    /// Enter the first element of an array.
    ///
    /// Only the first: both families that use one declare that they return a
    /// single candidate, and reading past it would be reading an answer nobody
    /// asked for.
    FirstItem,
}

/// Where one written field sits in a request body.
///
/// `parents` is the chain of objects to nest it under, outermost first, and is
/// empty for a field at the top of the body. Nesting is data rather than
/// control flow because two families put the answer allowance at the top level
/// and one puts it two objects deep.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct FieldPath {
    /// Objects to nest under, outermost first.
    pub parents: &'static [&'static str],
    /// The field's own name.
    pub key: &'static str,
}

/// A constant key and value a family requires in a fixed position.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Flag {
    /// The field name.
    pub key: &'static str,
    /// The only value it ever takes.
    pub value: &'static str,
}

/// A value a family requires at a fixed place in every body it is sent.
///
/// Three shapes, because the two a family actually asks for are a boolean and
/// a list of strings and the third is what a `Flag` already spells everywhere
/// else. It is a column of the table rather than a branch in the writer: the
/// subscription responses endpoint refuses a request that stores its answer
/// and refuses one that does not ask for the reasoning back, and both of those
/// are facts about that family, not about request building.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ConstantValue {
    /// A boolean.
    Boolean(bool),
    /// A string.
    Text(&'static str),
    /// An array of strings.
    TextList(&'static [&'static str]),
}

/// One constant and where it sits.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct BodyConstant {
    /// Where it goes.
    pub path: FieldPath,
    /// What it is.
    pub value: ConstantValue,
}

/// How a family carries a standing instruction of more than one piece.
///
/// Reached only where a subscription credential requires a caller-identifying
/// block ahead of the instruction itself. The ordinary one-piece case stays
/// the string [`SystemPlacement::TopLevelText`] describes, so a body written
/// on a key credential is byte for byte what it was before this existed.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct SystemBlocks {
    /// Field each block announces its kind in.
    pub type_key: &'static str,
    /// The kind a block of text is.
    pub tag: &'static str,
    /// Field the text sits in.
    pub text_key: &'static str,
}

/// Where a self-hosted server's compatibility overrides go.
///
/// `None` on a family no such server speaks. The names are here rather than in
/// the writer for the same reason every other field name is: a body field
/// belongs to the row of the family that reads it.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct CompatPlacement {
    /// The field the chat template's extra arguments go in.
    pub template_kwargs_key: &'static str,
}

/// Where a family returns reasoning it will want handed back.
///
/// One family does, and what it returns is opaque encrypted content rather
/// than words: it is carried back verbatim on the next turn of the same
/// conversation and is never read, rendered or recorded. The table says where
/// to find it; [`OpaqueReasoning`][crate::wire::OpaqueReasoning] is what makes
/// carrying it safe.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ReasoningCarriage {
    /// Path from the reply root to the array the produced items are in.
    pub path: &'static [Step],
    /// Field an element announces its kind in.
    pub type_key: &'static str,
    /// The kind a reasoning item is.
    pub tag: &'static str,
    /// Field carrying the sealed payload. An item without it carries nothing
    /// worth handing back and is not carried at all.
    pub payload_key: &'static str,
}

/// How one turn carries what was said.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ContentShape {
    /// The content field is the text itself.
    ///
    /// A family in this shape has no way to say "two blocks", so several
    /// pieces of text are written as one string.
    PlainText,
    /// The content field is an array of blocks, each announcing its kind.
    ///
    /// The tag differs by speaker in at least one family, which is why there
    /// are two of them rather than one.
    TaggedBlocks {
        /// Field each block announces its kind in.
        type_key: &'static str,
        /// Kind a block of the user's text is.
        user_tag: &'static str,
        /// Kind a block of the model's text is.
        assistant_tag: &'static str,
        /// Field the text sits in.
        text_key: &'static str,
    },
    /// The content field is an array of untagged objects carrying text.
    UntaggedParts {
        /// Field the text sits in.
        text_key: &'static str,
    },
}

/// The envelope a family puts around the model's own replayed message.
///
/// `None` on a family that replays an assistant turn as a bare role and
/// content, which is three of the five. One family reads the conversation
/// back as its own **output** items rather than as input messages, so a
/// replayed assistant turn has to look like the item that family emitted: it
/// announces a kind, carries a status, and is identified.
///
/// A row rather than a branch in the writer, because what the fields are
/// called is a fact about the family that reads them — the same reason every
/// other key in this file lives here.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct AssistantEnvelope {
    /// Field the element announces its kind in, and the kind it is.
    pub type_key: &'static str,
    /// The kind value.
    pub type_value: &'static str,
    /// Field carrying how the message ended, and the value for one that did.
    ///
    /// Always "it completed": a replayed turn is one the model finished and
    /// this process recorded. There is no shape here for a turn that did not,
    /// because such a turn never reached the transcript.
    pub status_key: &'static str,
    /// The completed value.
    pub status_value: &'static str,
    /// Field carrying the element's identity, and the prefix its value takes.
    ///
    /// The provider's own identity for the message is not kept — a durable
    /// transcript carries what was said, never the item id a vendor minted —
    /// so the value is this writer's, built from the element's position in
    /// the array it is being written into. That is a position inside one
    /// body, not an identity anything is keyed on, and it is unique within
    /// the only scope the field has to be unique in.
    pub id_key: &'static str,
    /// The prefix, which the position follows.
    pub id_prefix: &'static str,
    /// Field each text block carries its (always empty) annotation list in.
    ///
    /// Empty because a replayed message carries no citations: what this
    /// process kept is the text. The key is written all the same, because
    /// the family emits it and a replay that omits it is a different shape
    /// from the one it is replaying.
    pub annotations_key: &'static str,
}

/// Where the standing instruction goes.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SystemPlacement {
    /// A string at the top of the body.
    TopLevelText {
        /// The field name.
        key: &'static str,
    },
    /// An object at the top of the body holding the same content shape a turn
    /// uses, so the content and part keys are read from the dialect rather
    /// than repeated here.
    TopLevelParts {
        /// The field name.
        key: &'static str,
        /// The role that object announces, on a family that wants one.
        ///
        /// `None` where the family reads the object's position as enough. One
        /// family requires the instruction to arrive as the *user's*, which is
        /// a fact about that endpoint rather than about who is speaking — the
        /// spelling it does not accept is the one the other row omits
        /// entirely.
        role: Option<&'static str>,
    },
    /// A turn of its own at the front of the conversation.
    LeadingTurn {
        /// The role that turn speaks in.
        role: &'static str,
    },
}

/// How the family is told how hard to think.
///
/// Which of the two a family takes is already decided by
/// [`WireApi::is_effort_mapped`][crate::catalog::WireApi::is_effort_mapped],
/// and [`crate::thinking::plan`] produces the matching plan. This says *where*
/// the value goes once it exists.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ThinkingControl {
    /// A named effort string.
    Effort {
        /// Where the string goes.
        path: FieldPath,
    },
    /// A token budget.
    Budget {
        /// Where the number goes.
        path: FieldPath,
        /// A flag the family requires beside the budget to switch thinking on
        /// at all.
        enable: Option<Flag>,
    },
}

/// How a tool declaration is wrapped inside the tool list.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ToolWrapping {
    /// The declaration is the list element.
    Direct,
    /// A typed envelope wraps it. When `inner_key` is set the declaration is
    /// nested under it; when it is not, the declaration's fields sit beside
    /// the tag in the same object.
    Envelope {
        /// The constant the envelope announces itself with.
        tag: Flag,
        /// Field the declaration nests under, when it nests.
        inner_key: Option<&'static str>,
    },
    /// One list element groups every declaration under a key.
    Grouped {
        /// The grouping field.
        group_key: &'static str,
    },
}

/// Where and how the tool vocabulary is written.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ToolPlacement {
    /// The list at the top of the body.
    pub list_key: &'static str,
    /// How each declaration is wrapped.
    pub wrapping: ToolWrapping,
    /// Field carrying the tool's name.
    pub name_key: &'static str,
    /// Field carrying the sentence the model reads.
    pub description_key: &'static str,
    /// Field carrying the argument schema.
    pub schema_key: &'static str,
    /// Field the family expects a schema-strictness flag in, when it expects
    /// one at all.
    ///
    /// `None` on every family that reads no such field. Where it is named,
    /// the value written is JSON `null` rather than `false`, and the two are
    /// not the same answer: `false` asks the family not to constrain
    /// sampling, and `null` says the caller has no opinion and leaves the
    /// family's own default in place. Omitting the key entirely is a third
    /// answer again, and it is the one this product has been giving by
    /// accident.
    pub strict_key: Option<&'static str>,
    /// Explicit retention of the tool prefix, where the family supports it.
    /// Other prompt blocks are outside this placement.
    pub prefix_cache: Option<ToolPrefixCache>,
}

/// A cache marker on the last tool declaration, covering tools only.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ToolPrefixCache {
    /// Field containing the marker object.
    pub key: &'static str,
    /// Cache kind. Its default lifetime is the family's short retention.
    pub kind: Flag,
    /// Optional longer lifetime, written only for an explicit long request.
    pub extended_retention: Flag,
}

/// How one element of a content array carries text.
///
/// The projection of [`ContentShape`] a writer needs once it has more than
/// text to put in that array: on two of the four families a replayed tool call
/// is a further element of the same array the text blocks are in.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct TextPart {
    /// Field the element announces its kind in, when the family tags them.
    pub type_key: Option<&'static str>,
    /// Kind a block of the user's text is, when the family tags them.
    pub user_tag: Option<&'static str>,
    /// Kind a block of the model's text is, when the family tags them.
    pub assistant_tag: Option<&'static str>,
    /// Field the text sits in.
    pub text_key: &'static str,
}

/// How one browser-resident PNG is represented inside a user's content.
///
/// The value written into the data field is an opaque browser handle, not
/// image bytes. The browser requires that handle to occur exactly once,
/// claims the corresponding one-use attachment, and replaces it with base64
/// immediately before dispatch. Keeping the provider spellings in the table
/// makes a fifth shape a compile-time widening rather than a quiet fallback.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MediaShape {
    /// A tagged block whose base64 fields live below a source object.
    Base64Source {
        type_key: &'static str,
        type_value: &'static str,
        source_key: &'static str,
        source_type_key: &'static str,
        source_type_value: &'static str,
        mime_key: &'static str,
        data_key: &'static str,
    },
    /// A tagged block carrying a data URL, directly or below one object.
    DataUrl {
        type_key: &'static str,
        type_value: &'static str,
        url_key: &'static str,
        nested_key: Option<&'static str>,
    },
    /// An untagged block carrying inline base64 below one object.
    InlineBase64 {
        nested_key: &'static str,
        mime_key: &'static str,
        data_key: &'static str,
    },
}

/// The media-specific part of one protocol-family row.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct MediaPlacement {
    /// Text-block shape used when text and an image share one content array.
    ///
    /// Explicit on every row: Chat Completions changes a user's `content`
    /// from a string into an array here, while the other rows repeat their
    /// ordinary part shape so this mixed-content writer has no fallback.
    pub text_when_mixed: TextPart,
    /// Shape of the image part itself.
    pub shape: MediaShape,
}

impl ContentShape {
    /// How one element of the content array is written, and `None` for the
    /// family whose content is a string rather than an array.
    ///
    /// Asking rather than matching is what keeps one pairing honest without a
    /// rule written down beside the table. A tool call can only be a *further
    /// element of an array*, so a writer that wants to place one asks for the
    /// array and is answered — instead of reaching a `PlainText` case that the
    /// four rows do not contain and that it would have to fake a verdict for.
    pub const fn parts(&self) -> Option<TextPart> {
        match *self {
            Self::PlainText => None,
            Self::TaggedBlocks {
                type_key,
                user_tag,
                assistant_tag,
                text_key,
            } => Some(TextPart {
                type_key: Some(type_key),
                user_tag: Some(user_tag),
                assistant_tag: Some(assistant_tag),
                text_key,
            }),
            Self::UntaggedParts { text_key } => Some(TextPart {
                type_key: None,
                user_tag: None,
                assistant_tag: None,
                text_key,
            }),
        }
    }
}

/// How a family delivers the arguments of a tool call.
///
/// It answers for both directions, which is why it sits with the request-side
/// vocabulary rather than with the reply-side one: a family that hands the
/// arguments over inside a string wants them handed back the same way.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ArgumentsShape {
    /// A JSON value in place.
    Value,
    /// A JSON document inside a string.
    ///
    /// Reading, the string is passed on unparsed: the bound a second parse
    /// runs under belongs to whoever owns the arena the text lives in, not to
    /// this crate. Writing, the decoded value is *escaped* into the string
    /// rather than spliced into it — which is why a replayed call carries a
    /// `JsonValue` and never the text the reply delivered.
    EmbeddedText,
}

/// How one tool-call or tool-result object announces itself.
///
/// One enumeration for both, because the four families make the same four
/// choices about each and two enumerations spelling one set of choices is how
/// the two halves come to disagree.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PartWrapping {
    /// The fields sit directly on the object.
    Direct,
    /// A constant announces the kind and the fields sit beside it.
    Tagged {
        /// The constant.
        tag: Flag,
    },
    /// The name and the payload nest under a key. An identity, on a family
    /// that carries one, stays outside it — the same place the reply reader
    /// finds it.
    Nested {
        /// The nesting field.
        key: &'static str,
    },
    /// Both: a constant announces the kind and the rest nests under a key.
    TaggedAndNested {
        /// The constant.
        tag: Flag,
        /// The nesting field.
        key: &'static str,
    },
}

/// Where a call's identity is written, relative to the family's own wrapper.
///
/// Only meaningful on a family that both mints an identity and nests the name
/// and arguments under a key. Four of the six carry the identity on the
/// element itself, beside whatever announces the call; one carries no identity
/// at all; and one wants it among the name and the arguments, because the
/// endpoint that reads it translates the call into another vendor's shape
/// where the identity belongs to the call rather than to the block containing
/// it. Written in the wrong one of the two places it is silently absent: the
/// endpoint accepts the request, and the conversation fails several turns
/// later when a result first has to be paired with the call it answers.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum IdentityPosition {
    /// On the element carrying the wrapper, beside it.
    OnElement,
    /// Inside the wrapper, among the name and the arguments.
    InWrapper,
}

/// Where a replayed tool call sits relative to the turn that made it.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CallPosition {
    /// A further element of the turn's own content array, after its text.
    InContent,
    /// A list of its own beside the turn's content.
    BesideContent {
        /// The list's field name.
        key: &'static str,
    },
    /// An element of the conversation list in its own right, following the
    /// element that carries the turn's text.
    OwnElement,
}

/// How a replayed tool call is written back into a request body.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct CallPlacement {
    /// Where it sits relative to the turn that made it.
    pub position: CallPosition,
    /// How the object announces itself.
    pub wrapping: PartWrapping,
    /// Field carrying the call's identity.
    ///
    /// `None` on the family that carries none and pairs a result to a call by
    /// the tool's name. The core mints an identity for every call on every
    /// family — this says whether the family has anywhere to put it.
    pub call_id_key: Option<&'static str>,
    /// Where that field sits relative to [`Self::wrapping`].
    pub call_id_position: IdentityPosition,
    /// Field carrying the tool's name.
    pub name_key: &'static str,
    /// Field carrying the arguments.
    pub arguments_key: &'static str,
    /// How the arguments are delivered.
    pub arguments: ArgumentsShape,
}

/// How the results of tool calls become elements of the conversation.
///
/// This is the member [`Speaker`][crate::wire::Speaker] deliberately does not
/// have. Two families put a result in a turn whose role is the *user's*, one
/// gives it a role of its own, and one gives it no role at all — so "who is
/// speaking" is the wrong question to ask about a tool result, and a third
/// speaker would have been a false answer in whichever row it was read in.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ResultGrouping {
    /// Every result is a block in the content array of one turn, which speaks
    /// in `role`.
    SharedTurn {
        /// The role that turn speaks in.
        role: &'static str,
    },
    /// Each result is its own element of the conversation list.
    OwnElement {
        /// The role that element speaks in, and `None` on the family whose
        /// items carry no role at all.
        role: Option<&'static str>,
    },
}

/// Where the text of a tool result goes.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ResultPayload {
    /// A string at this key.
    Text {
        /// The field name.
        key: &'static str,
    },
    /// A string inside an object at this key.
    ///
    /// One family requires a JSON object where the result goes and rejects a
    /// bare string, so the text is given a field of its own inside it.
    Object {
        /// The object's field name.
        key: &'static str,
        /// The field the text sits in inside it.
        text_key: &'static str,
    },
}

/// How a failed tool result is told apart from one that worked.
///
/// Read the four rows together and "every family has somewhere to say so" does
/// not survive: one carries a boolean, one changes the field the text arrives
/// under, and two have nowhere at all. A failure delivered as an ordinary
/// result reads to the model as a tool that worked, so the last case folds the
/// fact into the text rather than dropping it.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FailureSignal {
    /// A boolean beside the payload, written only when the result failed.
    Flag {
        /// The field name.
        key: &'static str,
    },
    /// The payload's own field changes: a failure is delivered under this key
    /// instead of the one a success uses.
    PayloadKey {
        /// The field a failure arrives under.
        key: &'static str,
    },
    /// The family has nowhere to say it.
    FoldedIntoText,
}

/// Where the result of a tool call is written in a request body.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ResultPlacement {
    /// How results become elements of the conversation.
    pub grouping: ResultGrouping,
    /// How one result object announces itself.
    pub wrapping: PartWrapping,
    /// Field carrying the identity of the call being answered, or `None` on
    /// the family that pairs by the tool's name.
    pub call_id_key: Option<&'static str>,
    /// Where that field sits relative to [`Self::wrapping`].
    ///
    /// The same answer as the call row's on every family: a result written
    /// somewhere the call was not is a result the endpoint pairs with nothing.
    pub call_id_position: IdentityPosition,
    /// Field carrying the tool's name, on the family that pairs by it.
    pub name_key: Option<&'static str>,
    /// Where the result text goes.
    pub payload: ResultPayload,
    /// How a failure is told apart from a success.
    pub failure: FailureSignal,
}

/// Where the answer text is in a reply.
///
/// One walk covers all four families: follow the path; take the value if it is
/// a string; otherwise treat it as an array and take each element that carries
/// text, descending exactly one level into an element that nests its own
/// blocks.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct TextLocation {
    /// Path from the reply root.
    pub path: &'static [Step],
    /// Field an element announces its kind in, when the family tags elements.
    pub type_key: Option<&'static str>,
    /// Kinds that carry text. Empty means the family does not tag, and an
    /// element carries text when it has the text field.
    pub tags: &'static [&'static str],
    /// Field the text sits in.
    pub text_key: &'static str,
    /// Field an outer element nests its own text blocks under.
    pub nested_key: Option<&'static str>,
    /// Kinds whose payload is that nested array rather than text.
    pub nested_tags: &'static [&'static str],
}

/// Where the tool calls are in a reply.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ToolCallLocation {
    /// Path from the reply root to the array that may hold them.
    pub path: &'static [Step],
    /// Field an element announces its kind in, when the family tags elements.
    pub type_key: Option<&'static str>,
    /// Kinds that are a tool call. Empty means the family does not tag, and an
    /// element is a tool call when it has `inner_key`.
    pub tags: &'static [&'static str],
    /// Field the name and arguments nest under, when they nest.
    pub inner_key: Option<&'static str>,
    /// Field on the outer element carrying the identity the provider minted.
    ///
    /// `None` for a family that mints none. That is not a detail: a caller
    /// that has to pair a result with a call has to mint the identity itself,
    /// and a table that pretended every family supplied one would leave it
    /// reading an absent field as an empty string.
    ///
    /// That caller is the core, and it mints one for *every* family rather
    /// than for the one that needs it — a replay-derived identity is stable
    /// across a restart, which a provider's own is not. What each family does
    /// with it on the way back is [`CallPlacement::call_id_key`], and the row
    /// that reads `None` here reads `None` there for the same reason.
    pub id_key: Option<&'static str>,
    /// Field carrying the tool's name.
    pub name_key: &'static str,
    /// Field carrying the arguments.
    pub arguments_key: &'static str,
    /// How the arguments are delivered.
    pub arguments: ArgumentsShape,
}

/// Where the token counts are, and what the input count already includes.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct UsageFields {
    /// Path from the reply root to the object holding the counts.
    pub object: &'static [Step],
    /// Path within it to the input count.
    pub input: &'static [Step],
    /// Path within it to the output count.
    pub output: &'static [Step],
    /// Path within it to the cache-read count, when the family reports one.
    pub cache_read: Option<&'static [Step]>,
    /// Path within it to the cache-write count, when the family reports one.
    pub cache_write: Option<&'static [Step]>,
    /// Whether the input count already contains the cached counts.
    ///
    /// Three of the four families report one prompt total with the cached
    /// tokens inside it, and one reports uncached input separately. A reader
    /// that ignored the difference would add the cached tokens twice on three
    /// providers out of four — and it would do so in
    /// [`TokenUsage::total_input`][crate::cost::TokenUsage::total_input],
    /// which is what prices the request, so the error would be an
    /// overcharge nobody could see in the reply.
    pub input_includes_cache: bool,
}

/// Where the stop token is, and what its words mean.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct StopFields {
    /// Paths to try in order; the first one present wins.
    ///
    /// One family says "incomplete" at the top and why underneath, so the
    /// specific path is listed before the general one.
    pub paths: &'static [&'static [Step]],
    /// The family's own words and what each one means.
    ///
    /// A word absent from this list is a refusal rather than a guess: a stop
    /// reason nobody mapped is a reason nobody has decided how to treat, and
    /// treating it as an ordinary finish would report a truncated or filtered
    /// answer as a complete one.
    pub vocabulary: &'static [(&'static str, StopReason)],
}

/// Where a failing reply says what went wrong.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ErrorFields {
    /// Path to the object a failing reply carries instead of an answer.
    pub object: &'static [Step],
    /// Path within it to the provider's own class token.
    pub token: &'static [Step],
    /// Path within it to the sentence kept, bounded, for diagnosis.
    pub message: &'static [Step],
    /// The family's own words and the class each one normalizes to.
    ///
    /// An unlisted word normalizes to [`ErrorClass::Unknown`], which is the
    /// taxonomy's own "nothing above matched" and is terminal — so an
    /// unrecognized failure stops rather than being retried into a bill.
    pub vocabulary: &'static [(&'static str, ErrorClass)],
}

/// One protocol family, described end to end.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Dialect {
    /// The family this row describes.
    pub family: WireApi,
    /// Objects the request itself is nested inside, outermost first.
    ///
    /// Empty on a family whose body *is* the request, which is four of the
    /// five. One family wraps the request it understands inside an envelope
    /// carrying routing fields the endpoint reads and the model never sees, so
    /// the conversation, the instruction, the tools and the generation
    /// settings all sit one object down while the model name and the calling
    /// client sit at the top.
    ///
    /// It is a column rather than a branch in the writer because which fields
    /// are inside and which are outside is a fact about the family, and
    /// because the alternative — re-parenting every key that already carries a
    /// [`FieldPath`] — cannot express it: the body's root-level writers run in
    /// sequence, so each would open an envelope of its own and the last one
    /// would be the only one a parser kept.
    ///
    /// A scalar whose [`FieldPath::parents`] begins with this chain is written
    /// inside the envelope, and one that does not is written outside it. An
    /// empty chain is a prefix of every path, so a row that declares none puts
    /// everything inside a zero-deep envelope, which is the body itself.
    pub body_root: &'static [&'static str],
    /// Where the model name goes, or `None` when the family carries it in the
    /// URL instead — which is a fact about the request the browser builds, not
    /// about the body this crate writes.
    pub model: Option<FieldPath>,
    /// Field holding the conversation.
    pub turns_key: &'static str,
    /// Field a turn names its speaker in.
    pub role_key: &'static str,
    /// The user's role name.
    pub user_role: &'static str,
    /// The model's role name.
    pub assistant_role: &'static str,
    /// Field a turn carries its content in.
    pub content_key: &'static str,
    /// How that content is shaped.
    pub content: ContentShape,
    /// How a browser-resident image is mixed into a user's content.
    pub media: MediaPlacement,
    /// Where the standing instruction goes.
    pub system: SystemPlacement,
    /// Text every request carries ahead of the standing instruction.
    ///
    /// Empty on four of the five rows, and it is not product copy on the
    /// fifth: it is a compatibility token one endpoint inspects to decide
    /// which client is calling, sent whether or not the caller supplied an
    /// instruction of their own, because the endpoint reads its absence rather
    /// than the caller reading its presence.
    ///
    /// Distinct from [`SystemBlocks`], which carries a *per-request* preamble
    /// naming who is calling and exists only on a row whose instruction field
    /// takes a string. This is constant, it is part of the instruction rather
    /// than a block ahead of it, and it goes where the family's own parts go.
    ///
    /// A row that means the model to disregard the token writes the
    /// neutralising piece here too, beside it: both pieces are one fact about
    /// the endpoint and separating them would leave a second assistant
    /// identity standing in the instruction.
    pub system_prelude: &'static [&'static str],
    /// How the instruction is carried when it has a preamble ahead of it, and
    /// `None` on a family with nowhere to put one.
    pub system_blocks: Option<SystemBlocks>,
    /// The envelope around a replayed assistant message, where the family
    /// wants one.
    pub assistant_envelope: Option<AssistantEnvelope>,
    /// Values this family requires in every body, wherever they sit.
    pub constants: &'static [BodyConstant],
    /// Where this family carries the caller's own name for the conversation,
    /// and `None` on a family that has nowhere to put one.
    ///
    /// A per-request value rather than a [`BodyConstant`], which is the whole
    /// reason it is a field of its own: the constants are what a family
    /// requires of every body it ever sees, and this is one value that differs
    /// per conversation and is the same across the turns of one. A family that
    /// names no path is sent no key, and a request that carries no key writes
    /// nothing even where the path exists — an endpoint reading an empty
    /// conversation name is worse than one reading none.
    pub conversation_key: Option<FieldPath>,
    /// Where a self-hosted server's overrides go, on the dialect such servers
    /// speak.
    pub compat: Option<CompatPlacement>,
    /// Where this family returns reasoning it wants handed back.
    pub reasoning: Option<ReasoningCarriage>,
    /// Where the answer allowance goes.
    pub answer_tokens: FieldPath,
    /// Where the thinking control goes.
    pub thinking: ThinkingControl,
    /// Where the tool vocabulary goes.
    pub tools: ToolPlacement,
    /// Where a replayed tool call goes in a request body.
    pub calls: CallPlacement,
    /// Where the result of one goes.
    pub results: ResultPlacement,
    /// Where the answer text is in a reply.
    pub text: TextLocation,
    /// Where the tool calls are in a reply.
    pub tool_calls: ToolCallLocation,
    /// Where the token counts are in a reply.
    pub usage: UsageFields,
    /// Where the stop token is in a reply.
    pub stop: StopFields,
    /// Where a failure says what went wrong.
    pub error: ErrorFields,
    /// Body field that asks the family to stream, when the family puts that
    /// flag in the body. `None` when streaming is a URL path the browser
    /// owns — Google's generateContent versus streamGenerateContent — so this
    /// crate cannot name it without naming a path.
    pub stream: Option<&'static str>,
    /// Boolean that asks a streamed reply to carry its token counts, when the
    /// family sends none unless asked. Written as `true`, and only beside
    /// [`Self::stream`].
    ///
    /// The incremental reader refuses a stream that ends without usage,
    /// because every turn is charged from those counts. A family that omits
    /// them by default therefore needs the request to say so, or every reply
    /// it streams — however complete — is read as unreadable and re-asked.
    pub stream_usage: Option<FieldPath>,
}

impl Dialect {
    /// Where a preamble goes and what a block of it looks like, on the row
    /// that has room for one.
    ///
    /// The two fields it reads are separate and nothing in the type system
    /// pairs them, so the pairing is asked here rather than assumed in two
    /// places — the writer that places a preamble and the refusal that says a
    /// family cannot carry one. Two independent opinions about that would
    /// produce a body whose preamble had quietly vanished, which is exactly
    /// the failure the refusal exists to prevent.
    pub const fn preamble_shape(&self) -> Option<(&'static str, SystemBlocks)> {
        match (self.system, self.system_blocks) {
            (SystemPlacement::TopLevelText { key }, Some(blocks)) => Some((key, blocks)),
            _ => None,
        }
    }
}
