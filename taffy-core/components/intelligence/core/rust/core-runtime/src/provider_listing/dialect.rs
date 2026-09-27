// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The spelling one provider publishes its own model list in.
//!
//! Decision 0098 settled that a provider's list is read by the catalog's own
//! decoder and by no second one. That is about *judgement* — what a window may
//! be, whether a price is sane, whether a row may be routed to at all — and it
//! is unchanged here. What this module owns is the step before it: the
//! provider's vocabulary, turned into the facts a catalog row is written from.
//! Nothing in this file decides anything. It reads, or it refuses.
//!
//! **A provider is read in the dialect it was compiled for, and the dialect is
//! chosen by name rather than guessed from the body.** Guessing is not a
//! smaller version of this; it is a different and much worse thing, and one
//! pair of vendors shows why. Chutes and `OpenRouter` both publish
//! `pricing.prompt` as a bare number. `OpenRouter` means dollars per
//! *token*;
//! Chutes means dollars per *million*. The two spellings are
//! indistinguishable, they differ by a factor of a million, and reading either
//! one in the other's dialect produces a perfectly well-formed catalog row
//! whose price is wrong by six orders of magnitude — in the direction that
//! spends past a cap, half the time. So the table below is exhaustive by
//! provider id, and a provider that is not in it is read in the listing
//! dialect that has always been here, which refuses what it cannot read rather
//! than approximating it.
//!
//! | Provider | Rates quoted per | Capability facts read from |
//! |---|---|---|
//! | *(anything else)* | token | `supported_parameters[]` |
//! | `chutes` | million | `supported_features[]`, `input_modalities[]` |
//! | `novita` | million | `features[]`, `input_modalities[]` |
//! | `venice` | million | `model_spec.capabilities` |
//!
//! No dialect reads an address, and that is a property of the list of facts
//! below rather than a rule applied to it: [`ListedModel`] has nowhere to put
//! one, so there is no key a provider can invent that arrives anywhere
//! (decision 0098 section 3).

use model_router::json::JsonValue;

/// The scale between micro-units per million tokens and a rate quoted for one
/// token: a million tokens times a million micro-units.
const PER_TOKEN_SCALE: u32 = 12;

/// The scale between micro-units per million tokens and a rate quoted for a
/// million of them: the micro-units alone.
const PER_MILLION_SCALE: u32 = 6;

/// One optional fact a listing may or may not have stated.
///
/// Two variants and not a bare [`Option`], because the reader that produces
/// one has a third answer to give: it returns `None` when the provider stated
/// something this build cannot read, which refuses the whole row. Nesting the
/// two `Option`s would have said the same thing in a shape where silence and
/// unreadability are one careless `unwrap_or` apart, and they are the two ends
/// of what may be done about a price.
#[derive(Clone, Copy)]
enum Stated {
    /// The provider stated it, and this build read it.
    Read(u64),
    /// The provider said nothing, so the caller applies its own default.
    Silent,
}

impl Stated {
    /// The value, or `None` for silence.
    const fn value(self) -> Option<u64> {
        match self {
            Self::Read(value) => Some(value),
            Self::Silent => None,
        }
    }
}

/// One listing row, in the catalog's own terms.
///
/// Every rate is already in micro-units per million tokens, because the scale
/// is the one thing that differs between dialects and leaving it for the
/// caller would put the conversion outside the only place that knows which
/// vendor it is reading.
pub(super) struct ListedModel<'a> {
    /// The model identity, unvalidated: the caller runs it past [`ModelId`].
    ///
    /// [`ModelId`]: model_router::ModelId
    pub id: &'a str,
    /// The window, or `None` when the listing stated none.
    pub context_length: Option<u64>,
    /// The answer allowance; `0` when the listing stated none.
    pub max_output_tokens: u64,
    /// Whether the provider says the model thinks.
    pub reasoning: bool,
    /// Whether the provider says the model calls tools.
    pub tool_calling: bool,
    /// Whether the provider says the model accepts images.
    pub vision: bool,
    /// The input rate.
    pub input: u64,
    /// The output rate.
    pub output: u64,
    /// The cache-read rate, or `None` when the listing stated none.
    pub cache_read: Option<u64>,
    /// The cache-write rate, or `None` when the listing stated none.
    pub cache_write: Option<u64>,
}

/// Reads one listing row in the dialect this provider publishes.
///
/// `None` refuses the row. Every refusal here is the same kind: the provider
/// described the model in terms this build does not read, and a row it cannot
/// read is one it cannot price or bound.
pub(super) fn read<'a>(provider_id: &str, entry: &'a JsonValue) -> Option<ListedModel<'a>> {
    match provider_id {
        "chutes" => chutes(entry),
        "novita" => novita(entry),
        "venice" => venice(entry),
        _ => openai_listing(entry),
    }
}

/// The dialect `OpenRouter` publishes and the gateways shaped after it share.
///
/// Three of its facts sit one level down, and two of them used to be missed.
/// **No row in either of the two bodies read on 2026-09-03 carried a top-level
/// `max_completion_tokens` at all** — 419 of `OpenRouter`'s 425 and 361 of
/// Kilo's 367 state it under `top_provider` and none states it beside `id` —
/// so every model this dialect has ever produced was published with the
/// standing default allowance of a couple of thousand tokens while the vendor
/// was saying six figures. That is not a refusal anybody sees: the model
/// works, and its answers stop early.
fn openai_listing(entry: &JsonValue) -> Option<ListedModel<'_>> {
    let parameters = entry.field("supported_parameters")?.as_array()?;
    let pricing = entry.field("pricing")?;
    // Absent on a row that states its limits only at the top, which is why
    // this is an empty object rather than a refusal.
    let served = entry.field("top_provider").unwrap_or(&JsonValue::Null);
    let window = match count(entry, "context_length")?.value() {
        Some(stated) => Some(stated),
        None => count(served, "context_length")?.value(),
    };
    let allowance = match count(entry, "max_completion_tokens")?.value() {
        Some(stated) => stated,
        None => count(served, "max_completion_tokens")?.value().unwrap_or(0),
    };
    // The modality is stated in a nested `architecture` object, and it is the
    // same fact the other three dialects read from their own vendors: a list
    // of what the model accepts.
    let architecture = entry.field("architecture").unwrap_or(&JsonValue::Null);
    let modalities = architecture
        .field("input_modalities")
        .and_then(JsonValue::as_array)
        .unwrap_or(&[]);
    Some(ListedModel {
        id: entry.field("id")?.as_str()?,
        context_length: window,
        max_output_tokens: allowance,
        reasoning: names(parameters, "reasoning"),
        tool_calling: names(parameters, "tools"),
        vision: names(modalities, "image"),
        input: rate(pricing.field("prompt")?, PER_TOKEN_SCALE)?,
        output: rate(pricing.field("completion")?, PER_TOKEN_SCALE)?,
        cache_read: optional_rate(pricing, "input_cache_read", PER_TOKEN_SCALE)?.value(),
        cache_write: optional_rate(pricing, "input_cache_write", PER_TOKEN_SCALE)?.value(),
    })
}

/// Chutes, which quotes per million under `OpenRouter`'s key names.
fn chutes(entry: &JsonValue) -> Option<ListedModel<'_>> {
    let features = entry.field("supported_features")?.as_array()?;
    let modalities = entry.field("input_modalities")?.as_array()?;
    let pricing = entry.field("pricing")?;
    Some(ListedModel {
        id: entry.field("id")?.as_str()?,
        context_length: count(entry, "context_length")?.value(),
        max_output_tokens: count(entry, "max_output_length")?.value().unwrap_or(0),
        reasoning: names(features, "reasoning"),
        tool_calling: names(features, "tools"),
        vision: names(modalities, "image"),
        input: rate(pricing.field("prompt")?, PER_MILLION_SCALE)?,
        output: rate(pricing.field("completion")?, PER_MILLION_SCALE)?,
        cache_read: optional_rate(pricing, "input_cache_read", PER_MILLION_SCALE)?.value(),
        cache_write: optional_rate(pricing, "input_cache_write", PER_MILLION_SCALE)?.value(),
    })
}

/// Novita, which serves one list for every kind of model it hosts.
fn novita(entry: &JsonValue) -> Option<ListedModel<'_>> {
    // The list carries image and speech models beside the chat ones. A model
    // this product cannot route a turn to is not a row to refuse loudly; it
    // is not one of the rows being asked for.
    if entry.field("model_type")?.as_str()? != "chat" {
        return None;
    }
    let features = entry.field("features")?.as_array()?;
    let modalities = entry.field("input_modalities")?.as_array()?;
    let pricing = entry.field("pricing")?;
    Some(ListedModel {
        id: entry.field("id")?.as_str()?,
        context_length: count(entry, "context_size")?.value(),
        max_output_tokens: count(entry, "max_output_tokens")?.value().unwrap_or(0),
        reasoning: names(features, "reasoning"),
        tool_calling: names(features, "function-calling"),
        vision: names(modalities, "image"),
        // Two refusals and both are load-bearing: the `?` on the reader
        // refuses a row whose price is unreadable, and the one on `value`
        // refuses a row that states no input or output rate at all. A cache
        // rate below may be absent, so it keeps only the first.
        input: novita_rate(pricing, "prompt")?.value()?,
        output: novita_rate(pricing, "completion")?.value()?,
        cache_read: novita_rate(pricing, "input_cache_read")?.value(),
        cache_write: novita_rate(pricing, "input_cache_write")?.value(),
    })
}

/// One Novita rate, at the dearer of the two prices it publishes.
///
/// Novita quotes each rate twice: a standing price and a discounted one. The
/// discount is a condition — it ends, and the standing price is what the
/// account is charged the day it does — so decision 0094 section 2's rule for
/// a conditional price applies unchanged and the dearer of the two is
/// recorded. Taking the cheaper one would set every spend cap on a number the
/// vendor is free to stop honouring, and a cap is the one thing that must not
/// be surprised. Written as a maximum rather than as "the standing one" so it
/// stays the dearer even if the two ever swap places.
fn novita_rate(pricing: &JsonValue, name: &str) -> Option<Stated> {
    let Some(quoted) = pricing.field(name).filter(|value| !value.is_null()) else {
        return Some(Stated::Silent);
    };
    let mut dearest: Option<u64> = None;
    for spelling in ["origin_price_per_m_decimal", "price_per_m_decimal"] {
        let Some(value) = quoted.field(spelling).filter(|value| !value.is_null()) else {
            continue;
        };
        let read = rate(value, PER_MILLION_SCALE)?;
        dearest = Some(dearest.map_or(read, |held: u64| held.max(read)));
    }
    Some(dearest.map_or(Stated::Silent, Stated::Read))
}

/// Venice, which states its facts under a `model_spec` of its own.
fn venice(entry: &JsonValue) -> Option<ListedModel<'_>> {
    // Venice serves images and speech from the same address under the same
    // list, and says which is which here.
    if entry.field("type")?.as_str()? != "text" {
        return None;
    }
    let spec = entry.field("model_spec")?;
    let capabilities = spec.field("capabilities")?;
    let pricing = spec.field("pricing")?;
    let window = match count(spec, "availableContextTokens")?.value() {
        Some(stated) => Some(stated),
        None => count(entry, "context_length")?.value(),
    };
    Some(ListedModel {
        id: entry.field("id")?.as_str()?,
        context_length: window,
        max_output_tokens: count(spec, "maxCompletionTokens")?.value().unwrap_or(0),
        reasoning: flag(capabilities, "supportsReasoning"),
        tool_calling: flag(capabilities, "supportsFunctionCalling"),
        vision: flag(capabilities, "supportsVision"),
        input: rate(usd(pricing, "input")?, PER_MILLION_SCALE)?,
        output: rate(usd(pricing, "output")?, PER_MILLION_SCALE)?,
        cache_read: venice_cache(pricing, "cache_input")?.value(),
        cache_write: None,
    })
}

/// One optional Venice cache rate, which is quoted the same way as the rest.
fn venice_cache(pricing: &JsonValue, name: &str) -> Option<Stated> {
    match pricing.field(name).filter(|value| !value.is_null()) {
        None => Some(Stated::Silent),
        Some(quoted) => Some(Stated::Read(rate(quoted.field("usd")?, PER_MILLION_SCALE)?)),
    }
}

/// The dollar amount of one Venice rate, which it also quotes in its own unit.
fn usd<'a>(pricing: &'a JsonValue, name: &str) -> Option<&'a JsonValue> {
    pricing.field(name)?.field("usd")
}

/// Whether a list of strings names this one.
fn names(values: &[JsonValue], wanted: &str) -> bool {
    values
        .iter()
        .filter_map(JsonValue::as_str)
        .any(|value| value == wanted)
}

/// One boolean a provider states, absent meaning no.
///
/// A capability object states what it states; a key it omits is a capability
/// this build does not have from the provider, and treating that as no is the
/// same fail-closed answer a missing key gets everywhere else here.
fn flag(capabilities: &JsonValue, name: &str) -> bool {
    capabilities
        .field(name)
        .and_then(JsonValue::as_bool)
        .unwrap_or(false)
}

/// One whole count the listing may have stated.
///
/// Three answers, and the returned `Option` carries the third: `None` refuses
/// the whole row. They are kept apart because they mean opposite things. A
/// provider that answered "minus one" about a window has said something no
/// reading makes true, and quietly substituting a default would hide it; a
/// provider that said nothing has left the caller to apply the standing
/// default, which is what a default is for.
fn count(parent: &JsonValue, field: &str) -> Option<Stated> {
    match parent.field(field) {
        None => Some(Stated::Silent),
        Some(value) if value.is_null() => Some(Stated::Silent),
        Some(value) => Some(Stated::Read(u64::try_from(value.as_i64()?).ok()?)),
    }
}

/// One rate the listing may have stated.
///
/// Same three answers as [`count`], for the same reason: an omitted rate
/// leaves the caller to decide, and a rate that is present and unreadable is a
/// provider describing a price in terms the catalog does not have.
fn optional_rate(pricing: &JsonValue, field: &str, scale: u32) -> Option<Stated> {
    match pricing.field(field) {
        None => Some(Stated::Silent),
        Some(value) if value.is_null() => Some(Stated::Silent),
        Some(value) => Some(Stated::Read(rate(value, scale)?)),
    }
}

/// The catalog's rate, from the price this dialect's provider states.
fn rate(value: &JsonValue, scale: u32) -> Option<u64> {
    match value {
        JsonValue::Integer(units) => u64::try_from(*units)
            .ok()?
            .checked_mul(10_u64.checked_pow(scale)?),
        JsonValue::Decimal(spelling) | JsonValue::Text(spelling) => scaled(spelling, scale),
        _ => None,
    }
}

/// A decimal spelling, shifted `scale` places and read as a whole number.
///
/// Nothing but digits and one point: a sign, an exponent or a space is a
/// spelling this build does not read, and reading one wrongly is a price.
///
/// **A digit past the last place this build can express rounds the last one
/// up.** It used to refuse the row instead, and that rule cost more than it
/// bought: a vendor that computes a rate and serialises it through a binary
/// float publishes `0.0000000416666666666667`, which is a repeating third of a
/// cent and is not a number anybody chose. Refusing it dropped sixteen live
/// Gemini rows out of one aggregator's list, on every device, silently. One
/// micro-unit per million tokens is at most a millionth of a cent dearer than
/// the vendor said, and *dearer* is the direction that matters: a rate rounded
/// up can only ever make a spend cap fire early, where rounding down or
/// refusing lets a request through on a price nobody stated. The refusal that
/// remains is for a spelling this build cannot read at all, which is a
/// different thing from one it can read too precisely.
fn scaled(spelling: &str, scale: u32) -> Option<u64> {
    let places = usize::try_from(scale).ok()?;
    let (whole, fraction) = spelling.split_once('.').unwrap_or((spelling, ""));
    if whole.is_empty() && fraction.is_empty() {
        return None;
    }
    if !whole
        .bytes()
        .chain(fraction.bytes())
        .all(|b| b.is_ascii_digit())
    {
        return None;
    }
    let mut digits = String::with_capacity(whole.len().saturating_add(places));
    digits.push_str(whole);
    let mut past_the_last_place = false;
    for (place, byte) in fraction.bytes().enumerate() {
        if place < places {
            digits.push(char::from(byte));
        } else if byte != b'0' {
            past_the_last_place = true;
        }
    }
    for _ in fraction.len().min(places)..places {
        digits.push('0');
    }
    let value: u64 = digits.parse().ok()?;
    if past_the_last_place {
        value.checked_add(1)
    } else {
        Some(value)
    }
}
