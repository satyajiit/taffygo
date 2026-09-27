// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The managed route's canonical request schema.
//!
//! Deliberately not a fifth [`super::dialect`] row. The four dialect tables
//! describe provider vocabularies this product does not control, and their
//! machinery — tool placement, replayed calls, stream folding — exists because
//! providers differ in ways a table can hold. The managed schema is the
//! opposite object: a vocabulary this repository owns on both ends, spoken
//! only to the product's own edge worker at its compiled origin, versioned by
//! an explicit `schema_version` field rather than by whichever provider
//! shipped last. Writing it as a dialect row would hand it machinery it must
//! not have.
//!
//! # Tools travel in this repository's own shape
//!
//! Decision 0092. Version 2 carries a tool vocabulary, and it is a first-party
//! one: three records named for what they are — a tool the call offers, a call
//! the model made, a result a tool returned — which are the wire spelling of
//! [`ToolDeclaration`], [`ToolCallReplay`] and [`ToolResultView`], the types
//! decision 0069 settled. Copying one upstream's tool block instead would have
//! re-imported into this product's own wire exactly the per-vendor variance
//! the wire exists to remove, and would have made every other upstream a
//! translation *from a shape that was never neutral*. The worker owns the
//! translation into each family, and owns the fidelity loss where a family has
//! nowhere to put a fact.
//!
//! What a tool-carrying transcript has to satisfy is not decided here either:
//! the same `transcript` and call-identity assertions the direct writer makes
//! are run over the same turns, so the direct route defines what a tool turn
//! means and a managed body that differed from it would be a defect in this
//! translation rather than a second opinion.
//!
//! The refusals that stood in front of that absence are not deleted. They are
//! read off one version predicate — [`tools_travel_at`] over
//! [`MANAGED_TOOL_SCHEMA_VERSION`] — which route selection reads as well, so
//! the two independent sites cannot drift apart, and what they say is a fact
//! about a schema version rather than a property of the route.
//!
//! The same borrowing discipline as [`super::request`] applies: every
//! content-bearing field is a borrow, the writer appends into a buffer the
//! caller owns, and nothing here holds page-derived material across a call.
//! That discipline is what answers "does the router retain page content?" from
//! the signatures rather than from an audit, and it is why a tool's arguments
//! arrive as a decoded [`JsonValue`] borrowed from the caller: a tool argument
//! is page-derived often enough to be the worst possible first owned field in
//! this crate, and a pre-rendered one would have to be spliced into the body as
//! bytes, which [`super::emit`] offers no door for.
//!
//! The worker's bounded canonical response stream is folded by
//! [`super::managed_stream`]. Keeping that state machine beside this writer,
//! rather than inside the provider stream machinery, preserves the important
//! distinction: the managed vocabulary is ours and is versioned as one
//! request/response protocol, while provider frames are dialects we do not
//! control.

use super::emit::Json;
use super::request::{
    assert_replayed_call_ids, validate_media, within_depth, Speaker, ToolCallReplay,
    ToolDeclaration, ToolResultView, Turn, WireMediaAttachment, WireRefusal, MAX_BODY_BYTES,
    MAX_TOOL_SCHEMA_DEPTH,
};
use crate::thinking::{ThinkingLevel, ThinkingPlan};

/// The canonical schema version this build writes and reads.
///
/// One number for both directions on purpose: the worker echoes the version it
/// answered in, and a device that wrote 1 and read 2 would be trusting a shape
/// nobody on this end has reviewed.
pub const MANAGED_SCHEMA_VERSION: u64 = 3;

/// The first canonical schema version whose vocabulary includes tools.
///
/// The one constant both version-dependent refusals are read from — the
/// writer's [`ManagedWireRefusal::ToolTurn`] and route selection's
/// `ManagedToolCallingUnsupported`. They are deliberately two independent
/// refusals, because the last place that can say no must not rely on the first
/// having said it; reading one predicate over one constant is what keeps two
/// independent sites from disagreeing about which builds carry tools.
pub const MANAGED_TOOL_SCHEMA_VERSION: u64 = 2;

/// Whether a build speaking `schema_version` may carry a tool vocabulary.
pub const fn tools_travel_at(schema_version: u64) -> bool {
    schema_version >= MANAGED_TOOL_SCHEMA_VERSION
}

/// Everything one managed request body is written from.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ManagedRequest<'a> {
    /// The device-minted request identity, as a lowercase hyphenated UUID.
    ///
    /// It becomes the worker's exactly-once metering key, so the caller mints
    /// it deterministically from durable call identity — a retry of the same
    /// call carries the same value, which is what keeps a lost response from
    /// being charged twice. One managed request is one turn of the errand and
    /// one hold, whatever the worker had to do upstream to serve it: retry and
    /// failover happen under this identity and mint no second one.
    pub request_id: &'a str,
    /// The catalog's own name for the model.
    pub model_id: &'a str,
    /// The standing instruction, written as the first message.
    pub system: Option<&'a str>,
    /// The conversation, oldest first. Spoken turns, the calls the model made
    /// and the results it was given back — the same three shapes the direct
    /// wire replays, written in this schema's own spelling.
    pub turns: &'a [Turn<'a>],
    /// The tools this call offers.
    ///
    /// The same [`ToolDeclaration`] the direct wire takes, borrowed like
    /// everything else here: the declaration's argument schema travels as a
    /// decoded value, never as text this crate would have to splice.
    pub tools: &'a [ToolDeclaration<'a>],
    /// The thinking control, already clamped to this model's ladder.
    pub thinking: &'a ThinkingPlan,
    /// Tokens the answer may use.
    pub answer_tokens: u64,
}

/// Why a managed body was not written.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ManagedWireRefusal {
    /// The conversation was empty. The worker refuses an empty `messages`
    /// array after the request has been sent, which costs a round trip to
    /// learn something knowable here.
    EmptyConversation,
    /// A turn replayed a tool call or a tool result, and the schema version
    /// this build writes has no field for one.
    ///
    /// Version-gated rather than absolute since decision 0092: it states a
    /// fact about [`MANAGED_TOOL_SCHEMA_VERSION`] and is read through
    /// [`tools_travel_at`], the same predicate route selection reads. Refused
    /// rather than dropped: a conversation with its tool exchanges removed
    /// reads as one in which nothing was ever done, and the model builds its
    /// next step on that fiction.
    ToolTurn {
        /// Position of the turn in the conversation it was passed in.
        turn_index: usize,
    },
    /// The call offered tools and the schema version this build writes has no
    /// field for them.
    ///
    /// The other half of [`Self::ToolTurn`] and the same version predicate.
    /// Writing the body without them would hand a model asked to act no way to
    /// act, and prose that reads like a plan is what comes back.
    ToolVocabulary,
    /// A spoken turn carried no text pieces. The worker refuses an empty
    /// `content` array, for the same round-trip reason as an empty
    /// conversation.
    EmptyTurn {
        /// Position of the turn in the conversation it was passed in.
        turn_index: usize,
    },
    /// A tool declaration or a replayed exchange failed an assertion the
    /// direct writer already makes, carried whole.
    ///
    /// Decision 0092 section 6: the direct route defines what a tool-carrying
    /// turn means, so this writer runs the direct writer's own checks over the
    /// same turns rather than keeping a second copy of the rules that could
    /// come to differ from them. An unanswered call, a result answering no
    /// call, an identity the core did not mint, arguments or a schema nested
    /// past [`MAX_TOOL_SCHEMA_DEPTH`], a turn replaying more calls than the
    /// reader would accept — every one of them is the same refusal on both
    /// routes.
    Direct(WireRefusal),
    /// The body passed [`MAX_BODY_BYTES`].
    BodyTooLarge {
        /// What it came to.
        bytes: usize,
    },
}

/// Appends one canonical request body to `out`.
///
/// On refusal, `out` is left exactly as it was handed over. The writer records
/// its starting length and rolls back an oversized body, avoiding a second
/// whole-body allocation and copy on the successful hot path.
pub fn write_managed_request(
    request: &ManagedRequest<'_>,
    out: &mut String,
) -> Result<(), ManagedWireRefusal> {
    write_managed_body(MANAGED_SCHEMA_VERSION, request, None, out)
}

/// Appends one canonical request carrying a browser-resident PNG.
///
/// The canonical image part carries the browser handle as its temporary data
/// value. The browser claims and replaces exactly that occurrence before the
/// managed Worker can receive the body.
pub fn write_managed_media_request(
    request: &ManagedRequest<'_>,
    media: WireMediaAttachment<'_>,
    out: &mut String,
) -> Result<(), ManagedWireRefusal> {
    write_managed_body(MANAGED_SCHEMA_VERSION, request, Some(media), out)
}

/// The writer, with the schema version it is writing named rather than assumed.
///
/// Private, and parameterized only so the version-dependent refusals can be
/// exercised at a version below [`MANAGED_TOOL_SCHEMA_VERSION`]. A public
/// entry point taking a version would let a caller write a body claiming a
/// version this build does not read.
fn write_managed_body(
    schema_version: u64,
    request: &ManagedRequest<'_>,
    media: Option<WireMediaAttachment<'_>>,
    out: &mut String,
) -> Result<(), ManagedWireRefusal> {
    validate_managed_request(schema_version, request, media)?;
    let original_len = out.len();
    let bytes = append_managed_json(schema_version, request, media, out);
    if bytes > MAX_BODY_BYTES {
        return Err(ManagedWireRefusal::BodyTooLarge { bytes });
    }
    if let Some(media) = media {
        let occurrences = out[original_len..].matches(media.handle).count();
        if occurrences != 1 {
            out.truncate(original_len);
            return Err(ManagedWireRefusal::Direct(
                WireRefusal::AmbiguousMediaHandle { occurrences },
            ));
        }
    }
    Ok(())
}

fn validate_managed_request(
    schema_version: u64,
    request: &ManagedRequest<'_>,
    media: Option<WireMediaAttachment<'_>>,
) -> Result<(), ManagedWireRefusal> {
    let carries_tools = tools_travel_at(schema_version);
    if request.turns.is_empty() {
        return Err(ManagedWireRefusal::EmptyConversation);
    }
    if let Some(media) = media {
        validate_media(media).map_err(ManagedWireRefusal::Direct)?;
        if !request.turns.iter().any(|turn| {
            matches!(
                turn,
                Turn::Said {
                    speaker: Speaker::User,
                    ..
                }
            )
        }) {
            return Err(ManagedWireRefusal::Direct(
                WireRefusal::MediaWithoutUserTurn,
            ));
        }
    }
    for (turn_index, turn) in request.turns.iter().enumerate() {
        match turn {
            Turn::Said { text, .. } => {
                if text.is_empty() {
                    return Err(ManagedWireRefusal::EmptyTurn { turn_index });
                }
            }
            Turn::Called { .. } | Turn::Returned { .. } => {
                if !carries_tools {
                    return Err(ManagedWireRefusal::ToolTurn { turn_index });
                }
            }
        }
    }
    if !request.tools.is_empty() && !carries_tools {
        return Err(ManagedWireRefusal::ToolVocabulary);
    }
    for (tool_index, tool) in request.tools.iter().enumerate() {
        if !within_depth(tool.parameters, MAX_TOOL_SCHEMA_DEPTH) {
            return Err(ManagedWireRefusal::Direct(WireRefusal::ToolSchemaTooDeep {
                tool_index,
            }));
        }
    }
    super::transcript::check(request.turns).map_err(ManagedWireRefusal::Direct)?;
    assert_replayed_call_ids(request.turns).map_err(ManagedWireRefusal::Direct)?;
    Ok(())
}

fn append_managed_json(
    schema_version: u64,
    request: &ManagedRequest<'_>,
    media: Option<WireMediaAttachment<'_>>,
    out: &mut String,
) -> usize {
    let mut json = Json::with_limit(out, MAX_BODY_BYTES);
    json.open_object();
    json.key("schema_version");
    json.number(schema_version);
    json.key("request_id");
    json.text(request.request_id);
    json.key("model");
    json.text(request.model_id);
    json.key("thinking_level");
    json.text(thinking_level_name(request.thinking));
    json.key("max_output_tokens");
    json.number(request.answer_tokens);
    json.key("stream");
    json.boolean(true);
    if !request.tools.is_empty() {
        write_tools(&mut json, request.tools);
    }
    json.key("messages");
    json.open_array();
    if let Some(instruction) = request.system {
        write_message(&mut json, "system", &[instruction], None);
    }
    let mut pending_media = media;
    for turn in request.turns {
        match turn {
            Turn::Said { speaker, text } => {
                let role = match speaker {
                    Speaker::User => "user",
                    Speaker::Assistant => "assistant",
                };
                let turn_media = if matches!(speaker, Speaker::User) {
                    pending_media.take()
                } else {
                    None
                };
                write_message(&mut json, role, text, turn_media);
            }
            // The managed schema is TaffyGo's own and seals no reasoning, so
            // a turn's sealed reasoning has no field here to travel in. It
            // belongs to the provider that sealed it, and this route never
            // reaches that provider directly.
            Turn::Called {
                text,
                calls,
                reasoning: _,
            } => write_called(&mut json, text, calls),
            Turn::Returned { results } => write_returned(&mut json, results),
        }
    }
    json.close_array();
    json.close_object();
    json.attempted_bytes()
}

fn write_message(
    json: &mut Json<'_>,
    role: &str,
    pieces: &[&str],
    media: Option<WireMediaAttachment<'_>>,
) {
    json.open_object();
    json.key("role");
    json.text(role);
    json.key("content");
    json.open_array();
    write_text_parts(json, pieces);
    if let Some(media) = media {
        json.open_object();
        json.key("type");
        json.text("image");
        json.key("mime_type");
        json.text(media.mime_type);
        json.key("data");
        json.text(media.handle);
        json.close_object();
    }
    json.close_array();
    json.close_object();
}

fn write_text_parts<T: AsRef<str>>(json: &mut Json<'_>, pieces: &[T]) {
    for piece in pieces {
        json.open_object();
        json.key("type");
        json.text("text");
        json.key("text");
        json.text(piece.as_ref());
        json.close_object();
    }
}

/// Writes the tools the call offers.
///
/// The declaration's own three fields and no vendor's envelope around them:
/// which upstream needs a wrapper, and what it calls the schema, is the
/// worker's translation to make.
fn write_tools(json: &mut Json<'_>, tools: &[ToolDeclaration<'_>]) {
    json.key("tools");
    json.open_array();
    for tool in tools {
        json.open_object();
        json.key("name");
        json.text(tool.name);
        json.key("description");
        json.text(tool.description);
        json.key("parameters");
        // The schema arrived decoded and is written as a value. A rendered one
        // would have to be spliced in as bytes, which is how structure gets
        // injected into a request.
        json.value(tool.parameters);
        json.close_object();
    }
    json.close_array();
}

/// Writes the turn in which the model asked for tools.
///
/// One message with the assistant's role, whatever it said and the calls it
/// made in the order it made them. The four families place a call in four
/// structurally different positions; this schema has one, because it is not
/// describing anybody else's document.
fn write_called(json: &mut Json<'_>, text: &[&str], calls: &[ToolCallReplay<'_>]) {
    json.open_object();
    json.key("role");
    json.text("assistant");
    json.key("content");
    json.open_array();
    write_text_parts(json, text);
    for call in calls {
        json.open_object();
        json.key("type");
        json.text("tool_call");
        // The core's own identity, on the managed route exactly as on the four
        // families: `turn-{ordinal}-call-{sequence}` and never a provider's,
        // so a managed transcript's identities are a pure function of the turn
        // ordinal and the call sequence and the worker has nothing to remap.
        json.key("call_id");
        json.text(call.call_id);
        json.key("tool");
        json.text(call.tool);
        json.key("arguments");
        json.value(call.arguments);
        json.close_object();
    }
    json.close_array();
    json.close_object();
}

/// Writes the turn in which the results came back.
///
/// The role stays the user's and the result is content rather than a speaker,
/// which is decision 0069's finding applied to this schema: a tool result is
/// not a third speaker anywhere, and the part says what it is without needing
/// one.
fn write_returned(json: &mut Json<'_>, results: &[ToolResultView<'_>]) {
    json.open_object();
    json.key("role");
    json.text("user");
    json.key("content");
    json.open_array();
    for result in results {
        json.open_object();
        json.key("type");
        json.text("tool_result");
        json.key("call_id");
        json.text(result.call_id);
        json.key("tool");
        json.text(result.tool);
        // Always written, true or false. Two of the four families have nowhere
        // to put it and fold it into a sentence instead; that sentence is the
        // worker's to write from this boolean, so no two upstreams can end up
        // disagreeing about what a failure looks like.
        json.key("is_error");
        json.boolean(result.is_error);
        json.key("content");
        json.open_array();
        write_text_parts(json, result.text);
        json.close_array();
        json.close_object();
    }
    json.close_array();
    json.close_object();
}

/// The schema's spelling of one thinking rung.
///
/// The plan's rung and not its catalog `value`: the value maps a rung into one
/// provider's vocabulary, and the party that resolves a provider on this route
/// is the gateway, against its own catalog. Sending it the rung is sending the
/// decision; sending a provider spelling would be guessing which provider it
/// picks.
const fn thinking_level_name(plan: &ThinkingPlan) -> &'static str {
    let level = match plan {
        ThinkingPlan::Disabled => ThinkingLevel::Off,
        ThinkingPlan::Effort { level, .. } | ThinkingPlan::Budget { level, .. } => *level,
    };
    match level {
        ThinkingLevel::Off => "off",
        ThinkingLevel::Minimal => "minimal",
        ThinkingLevel::Low => "low",
        ThinkingLevel::Medium => "medium",
        ThinkingLevel::High => "high",
        ThinkingLevel::XHigh => "xhigh",
        ThinkingLevel::Max => "max",
    }
}

#[cfg(test)]
mod tests {
    #![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

    use super::{
        tools_travel_at, write_managed_body, ManagedRequest, ManagedWireRefusal,
        MANAGED_SCHEMA_VERSION, MANAGED_TOOL_SCHEMA_VERSION,
    };
    use crate::json::JsonValue;
    use crate::thinking::ThinkingPlan;
    use crate::wire::request::{Speaker, ToolCallReplay, ToolDeclaration, ToolResultView, Turn};

    /// The version below the one that first carried tools.
    ///
    /// Named rather than written as `1`, so that this exercises "a build below
    /// the tool version" even after the constant moves.
    const BELOW: u64 = MANAGED_TOOL_SCHEMA_VERSION - 1;

    #[test]
    fn the_version_predicate_answers_for_the_version_it_is_asked_about() {
        assert!(tools_travel_at(MANAGED_SCHEMA_VERSION));
        assert!(tools_travel_at(MANAGED_TOOL_SCHEMA_VERSION));
        assert!(!tools_travel_at(BELOW));
    }

    #[test]
    fn a_build_below_the_tool_version_refuses_a_tool_turn_and_writes_nothing() {
        let plan = ThinkingPlan::Disabled;
        let arguments = JsonValue::Object(std::collections::BTreeMap::new());
        let calls = [ToolCallReplay {
            call_id: "turn-1-call-0",
            tool: "read_page",
            arguments: &arguments,
        }];
        let results = [ToolResultView {
            call_id: "turn-1-call-0",
            tool: "read_page",
            is_error: false,
            text: &["a headline".to_owned()],
        }];
        let turns = [
            Turn::Said {
                speaker: Speaker::User,
                text: &["go"],
            },
            Turn::Called {
                text: &[],
                calls: &calls,
                reasoning: &[],
            },
            Turn::Returned { results: &results },
        ];
        let tools = [ToolDeclaration {
            name: "read_page",
            description: "Read the page.",
            parameters: &arguments,
        }];
        let request = ManagedRequest {
            request_id: "0f95e8a2-77c4-41d3-8b6e-2a9c41d37f00",
            model_id: "reasoner-one",
            system: None,
            turns: &turns,
            tools: &tools,
            thinking: &plan,
            answer_tokens: 512,
        };

        let mut untouched = String::from("prefix-the-caller-owns");
        assert_eq!(
            write_managed_body(BELOW, &request, None, &mut untouched),
            Err(ManagedWireRefusal::ToolTurn { turn_index: 1 })
        );

        // The other half of the same version predicate: tools offered to a
        // build that has no field for them are refused rather than dropped.
        let spoken = [Turn::Said {
            speaker: Speaker::User,
            text: &["go"],
        }];
        let offering = ManagedRequest {
            turns: &spoken,
            ..request
        };
        assert_eq!(
            write_managed_body(BELOW, &offering, None, &mut untouched),
            Err(ManagedWireRefusal::ToolVocabulary)
        );
        // Both refusals left the caller's buffer exactly as handed over.
        assert_eq!(untouched, "prefix-the-caller-owns");

        // And at the version this build speaks, the same transcript writes.
        let mut body = String::new();
        assert!(write_managed_body(MANAGED_SCHEMA_VERSION, &request, None, &mut body).is_ok());
        assert!(body.contains("tool_call"));
    }
}
