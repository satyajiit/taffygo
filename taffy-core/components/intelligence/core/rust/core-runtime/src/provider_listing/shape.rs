// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Turning one provider's own model list into a catalog document.
//!
//! This module reads and it writes, and it decides nothing. What a model's
//! window is, whether its price is sane, whether the entry may be routed to at
//! all — every one of those is the catalog decoder's answer, and the whole
//! point of decision 0098 is that there is not a second place that gives them.
//! What is left for here is translation: the provider's vocabulary into the
//! catalog's, and a row the catalog has no word for refused rather than
//! approximated.
//!
//! The vocabulary lives next door in [`dialect`], because vendors publish the
//! same facts under different names and at different scales. What arrives here
//! is a [`ListedModel`] and nothing else — an identity, a window, an
//! allowance, three capability facts and four rates — so the facts a listing
//! can put into the product are exactly the fields of that one struct, and
//! adding a dialect cannot widen them.
//!
//! A row that states no capabilities is refused, not defaulted. That is the
//! same rule [`Model::tool_calling`][model_router::catalog::Model::tool_calling]
//! states for a published entry, and for the same reason: a record that does
//! not say is a record nobody finished, which is a third answer rather than
//! either of the two. Defaulted to no it withdraws a working model; defaulted
//! to yes it hands tools to a model that answers a tool call as prose.
//!
//! **Nothing written here needs escaping, and that is a property rather than a
//! hope.** Every string that reaches the document is either a compile-time
//! literal or a value already validated against a catalog key's alphabet,
//! which excludes the quote, the backslash and every control character. The
//! model's shown name is its identity for the same reason: a rendered name the
//! provider chose is arbitrary text, and carrying it would mean a JSON escaper
//! in this crate to put remote bytes into a document — a door worth not
//! opening for a nicer label.

use model_router::json::{parse_provider, JsonValue};
use model_router::money::Currency;
use model_router::ModelId;

use crate::composition::user_catalog::{limits_for, roles_for, UNSTATED_CONTEXT_WINDOW};

use super::dialect::{self, ListedModel};
use super::MAX_LISTED_MODELS;

/// The price snapshot version a fetched listing is filed under.
const LISTING_SNAPSHOT: &str = "provider-listing";

/// One listing, shaped into the document the published catalog uses.
pub(super) struct ShapedListing {
    /// The document text, for the fail-closed decoder.
    pub document: String,
    /// How many rows the listing carried, before anything was refused.
    pub offered: usize,
}

/// Shapes one provider's listing body into a catalog document.
///
/// `None` means the body is not a listing at all — not JSON, or not the shape
/// this build reads. A body that *is* a listing and every row of which is
/// refused shapes to a document with no models, which the caller treats as the
/// empty answer it is rather than as an empty catalog.
pub(super) fn shape_listing(
    provider_id: &str,
    currency: Currency,
    text: &str,
) -> Option<ShapedListing> {
    let root = parse_provider(text).ok()?;
    let entries = root.field("data")?.as_array()?;
    let offered = entries.len();
    let mut rows: Vec<(ModelId, String)> = entries
        .iter()
        .filter_map(|entry| shape_model(provider_id, currency, entry))
        .collect();
    // Sorted before the bound is applied, so the rows this build keeps are the
    // identity-lowest rather than the ones the provider happened to send
    // first. The decoder applies the same cap in document order, so ordering
    // here is what makes the two agree.
    rows.sort_by(|left, right| left.0.cmp(&right.0));
    rows.truncate(MAX_LISTED_MODELS);
    let models = rows
        .into_iter()
        .map(|(_, row)| row)
        .collect::<Vec<_>>()
        .join(",");
    Some(ShapedListing {
        document: format!(
            r#"{{"schema_version":1,"catalog_version":"{LISTING_SNAPSHOT}",
               "generated_at":"0001-01-01T00:00:00Z","providers":[],
               "models":[{models}]}}"#
        ),
        offered,
    })
}

/// One listing row, as a catalog model entry.
fn shape_model(
    provider_id: &str,
    currency: Currency,
    entry: &JsonValue,
) -> Option<(ModelId, String)> {
    // A listing describes models, never where to reach them (decision 0098
    // section 3). A row that names an address is refused outright rather than
    // having the address removed: the rest of what it said is not more
    // trustworthy for having been trimmed. Any *other* address-shaped key a
    // provider invents is refused by never being read — no dialect has
    // anywhere to put one — so there is no key a listing can put an address in
    // and have it arrive.
    if entry.field("endpoint").is_some() {
        return None;
    }
    let listed = dialect::read(provider_id, entry)?;
    let model_id = ModelId::new(listed.id).ok()?;
    let row = write_model(provider_id, currency, &model_id, &listed)?;
    Some((model_id, row))
}

/// The catalog row one read listing entry writes.
fn write_model(
    provider_id: &str,
    currency: Currency,
    model_id: &ModelId,
    listed: &ListedModel<'_>,
) -> Option<String> {
    let roles = roles_for(listed.reasoning, listed.tool_calling, listed.vision)?;
    let roles = roles
        .iter()
        .map(|role| format!(r#""{}""#, role_name(*role)))
        .collect::<Vec<_>>()
        .join(",");
    let (context_window, max_output_tokens) = limits_for(
        listed.context_length.unwrap_or(UNSTATED_CONTEXT_WINDOW),
        listed.max_output_tokens,
    )?;
    // The image role and the image modality are written from one fact,
    // because the catalog refuses a vision model that does not accept images
    // and two reads of the same boolean is how that pair comes apart.
    let modalities = if listed.vision {
        r#""TEXT","IMAGE""#
    } else {
        r#""TEXT""#
    };
    // An unstated cache rate takes the input rate rather than zero. A cache
    // read is normally the cheapest thing a request does and a cache write the
    // dearest, so the input rate sits between them: it can be a little wrong
    // in either direction, where zero can only ever be wrong in the direction
    // that spends past a cap.
    let input = listed.input;
    let output = listed.output;
    let cache_read = listed.cache_read.unwrap_or(input);
    let cache_write = listed.cache_write.unwrap_or(input);
    let id = model_id.as_str();
    let currency = currency.as_str();
    Some(format!(
        r#"{{"schema_version":1,"model_id":"{id}","provider_id":"{provider_id}",
           "display_name":"{id}","roles":[{roles}],"input_modalities":[{modalities}],
           "reasoning":{reasoning},"tool_calling":{tool_calling},
           "context_window":{context_window},"max_output_tokens":{max_output_tokens},
           "cost":{{"snapshot_version":"{LISTING_SNAPSHOT}","currency":"{currency}",
           "basis":"METERED","input_micros_per_million":{input},
           "output_micros_per_million":{output},
           "cache_read_micros_per_million":{cache_read},
           "cache_write_micros_per_million":{cache_write}}},"enabled":true}}"#,
        reasoning = listed.reasoning,
        tool_calling = listed.tool_calling,
    ))
}

/// The published spelling of one role.
///
/// Exhaustive with no catch-all, so a role added to the catalog fails to
/// compile here rather than being written under whichever name came first.
const fn role_name(role: model_router::catalog::ModelRole) -> &'static str {
    match role {
        model_router::catalog::ModelRole::PrimaryReasoning => "PRIMARY_REASONING",
        model_router::catalog::ModelRole::FastBrowsing => "FAST_BROWSING",
        model_router::catalog::ModelRole::Vision => "VISION",
        model_router::catalog::ModelRole::Embedding => "EMBEDDING",
    }
}
