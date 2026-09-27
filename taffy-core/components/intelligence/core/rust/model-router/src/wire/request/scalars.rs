// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Scalar fields shared by every direct provider request.

use super::{Json, WireRequest};
use crate::thinking::ThinkingPlan;
use crate::wire::compat::ServerCompat;
use crate::wire::dialect::{CompatPlacement, ConstantValue, Dialect, ThinkingControl};

/// One scalar the dialect places somewhere in the body.
///
/// The key is borrowed rather than static because a self-hosted server may
/// rename the answer allowance field.
#[derive(Clone, Copy, Debug)]
pub(super) struct Placed<'a> {
    pub(super) parents: &'static [&'static str],
    key: &'a str,
    value: Scalar<'a>,
}

#[derive(Clone, Copy, Debug)]
enum Scalar<'a> {
    Text(&'a str),
    Number(u64),
    Boolean(bool),
    TextList(&'static [&'static str]),
}

/// Every scalar this body carries, each with the path it belongs at.
///
/// Collected rather than written, because where a scalar ends up is not
/// decided here: the caller partitions them on the family's
/// [`Dialect::body_root`] and writes the two groups either side of the
/// envelope it opens.
pub(super) fn collect<'a>(dialect: &Dialect, request: &WireRequest<'a>) -> Vec<Placed<'a>> {
    let mut placed: Vec<Placed<'a>> = Vec::new();
    if let Some(path) = dialect.model {
        placed.push(Placed {
            parents: path.parents,
            key: path.key,
            value: Scalar::Text(request.model_id),
        });
    }
    placed.push(Placed {
        parents: dialect.answer_tokens.parents,
        key: request
            .compat
            .map_or(dialect.answer_tokens.key, |compat| compat.answer_tokens_key),
        value: Scalar::Number(answer_allowance(dialect, request)),
    });
    if accepts_effort(request) {
        if let Some(entry) = thinking_scalar(dialect, request.thinking) {
            if let ThinkingControl::Budget {
                path,
                enable: Some(flag),
            } = dialect.thinking
            {
                placed.push(Placed {
                    parents: path.parents,
                    key: flag.key,
                    value: Scalar::Text(flag.value),
                });
            }
            placed.push(entry);
        }
    }
    for constant in dialect.constants {
        placed.push(Placed {
            parents: constant.path.parents,
            key: constant.path.key,
            value: match constant.value {
                ConstantValue::Boolean(flag) => Scalar::Boolean(flag),
                ConstantValue::Text(text) => Scalar::Text(text),
                ConstantValue::TextList(items) => Scalar::TextList(items),
            },
        });
    }
    // Only where the family names a place and the caller named a
    // conversation. Both halves are required: an endpoint reading an empty
    // conversation name is worse than one reading none, because it is a name
    // every conversation shares.
    if let (Some(path), Some(key)) = (dialect.conversation_key, request.conversation_key) {
        if !key.is_empty() {
            placed.push(Placed {
                parents: path.parents,
                key: path.key,
                value: Scalar::Text(key),
            });
        }
    }
    if let Some(compat) = request.compat {
        for (key, value) in compat.sampling {
            placed.push(Placed {
                parents: &[],
                key,
                value: Scalar::Text(value),
            });
        }
    }
    placed
}

/// Writes the extra arguments a self-hosted server's chat template needs.
pub(super) fn write_template_kwargs(
    json: &mut Json<'_>,
    placement: &CompatPlacement,
    compat: &ServerCompat<'_>,
) {
    if compat.template_kwargs.is_empty() {
        return;
    }
    json.key(placement.template_kwargs_key);
    json.open_object();
    for (key, value) in compat.template_kwargs {
        json.key(key);
        json.text(value);
    }
    json.close_object();
}

fn accepts_effort(request: &WireRequest<'_>) -> bool {
    request
        .compat
        .is_none_or(|compat| compat.accepts_reasoning_effort)
}

/// The allowance sent to a provider, including a budget-mapped model's
/// thinking tokens when that family counts them as output.
fn answer_allowance(dialect: &Dialect, request: &WireRequest<'_>) -> u64 {
    match (dialect.thinking, request.thinking) {
        (
            ThinkingControl::Budget { .. },
            ThinkingPlan::Budget {
                thinking_tokens,
                answer_tokens,
                ..
            },
        ) => answer_tokens.saturating_add(*thinking_tokens),
        _ => request.answer_tokens,
    }
}

fn thinking_scalar<'a>(dialect: &Dialect, plan: &'a ThinkingPlan) -> Option<Placed<'a>> {
    match dialect.thinking {
        ThinkingControl::Effort { path } => match plan {
            ThinkingPlan::Effort { value, .. } => value.as_ref().map(|effort| Placed {
                parents: path.parents,
                key: path.key,
                value: Scalar::Text(effort),
            }),
            ThinkingPlan::Disabled | ThinkingPlan::Budget { .. } => None,
        },
        ThinkingControl::Budget { path, .. } => match plan {
            ThinkingPlan::Budget {
                thinking_tokens, ..
            } => Some(Placed {
                parents: path.parents,
                key: path.key,
                value: Scalar::Number(*thinking_tokens),
            }),
            ThinkingPlan::Disabled | ThinkingPlan::Effort { .. } => None,
        },
    }
}

/// Writes each shared parent object once, even when several fields occupy it.
///
/// `prefix` is the path already open around the writer, so a group written
/// inside a family's envelope passes that envelope's own chain rather than the
/// empty one.
pub(super) fn write_group(json: &mut Json<'_>, prefix: &[&'static str], placed: &[Placed<'_>]) {
    for entry in placed.iter().filter(|entry| entry.parents == prefix) {
        json.key(entry.key);
        match entry.value {
            Scalar::Text(text) => json.text(text),
            Scalar::Number(number) => json.number(number),
            Scalar::Boolean(flag) => json.boolean(flag),
            Scalar::TextList(items) => {
                json.open_array();
                for item in items {
                    json.text(item);
                }
                json.close_array();
            }
        }
    }
    let mut written: Vec<&'static str> = Vec::new();
    for entry in placed {
        if !entry.parents.starts_with(prefix) {
            continue;
        }
        let Some(next) = entry.parents.get(prefix.len()) else {
            continue;
        };
        if written.contains(next) {
            continue;
        }
        written.push(next);
        let mut deeper: Vec<&'static str> = prefix.to_vec();
        deeper.push(next);
        json.key(next);
        json.open_object();
        write_group(json, &deeper, placed);
        json.close_object();
    }
}
