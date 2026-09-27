// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The thinking ladder and its per-model mapping.
//!
//! One internal ladder covers every provider, and every catalog entry maps the
//! ladder onto whatever its provider actually accepts. The mapping is a
//! tristate, and the third state is the interesting one:
//!
//! | State | Meaning |
//! |---|---|
//! | absent | levels through `HIGH` use the adapter's default mapping; the two above are opt-in and unsupported |
//! | value | the adapter sends exactly this value |
//! | null | the level is unsupported, hidden from selection, and clamped away |
//!
//! A request for an unsupported level clamps to the nearest supported one,
//! searching upward first and then downward. That rule exists so a catalog
//! update that narrows a model's levels degrades a request instead of failing
//! it — the catalog can re-describe a model, but it must never be able to break
//! a request that was valid a minute ago.
//!
//! These are internal identifiers. Whatever a surface calls them is governed by
//! the voice and naming guide, not by this module.

use std::collections::BTreeMap;

use crate::catalog::WireApi;
use crate::defaults;

/// One rung of the internal ladder, ordered from least to most.
#[derive(
    Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, serde::Serialize, serde::Deserialize,
)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum ThinkingLevel {
    /// No thinking phase.
    Off,
    /// The smallest thinking phase the provider offers.
    Minimal,
    /// A short thinking phase.
    Low,
    /// The middle of the ladder.
    Medium,
    /// A long thinking phase.
    High,
    /// Opt-in, above `HIGH`; unsupported unless the catalog maps it.
    #[serde(rename = "XHIGH")]
    XHigh,
    /// Opt-in, the top of the ladder; unsupported unless the catalog maps it.
    Max,
}

/// The ladder in order, for iteration and clamping.
pub const LADDER: [ThinkingLevel; 7] = [
    ThinkingLevel::Off,
    ThinkingLevel::Minimal,
    ThinkingLevel::Low,
    ThinkingLevel::Medium,
    ThinkingLevel::High,
    ThinkingLevel::XHigh,
    ThinkingLevel::Max,
];

impl ThinkingLevel {
    /// Position on the ladder, `0` for `OFF`.
    pub fn rung(self) -> usize {
        match self {
            Self::Off => 0,
            Self::Minimal => 1,
            Self::Low => 2,
            Self::Medium => 3,
            Self::High => 4,
            Self::XHigh => 5,
            Self::Max => 6,
        }
    }

    /// Whether the level is one of the two opt-in rungs above `HIGH`.
    pub fn is_opt_in(self) -> bool {
        matches!(self, Self::XHigh | Self::Max)
    }
}

/// What a catalog entry says about one rung.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LevelSupport<'a> {
    /// Supported through the adapter's own default mapping.
    AdapterDefault,
    /// Supported; the adapter sends exactly this value.
    Value(&'a str),
    /// Not supported by this model.
    Unsupported,
}

impl LevelSupport<'_> {
    /// Whether the level may be selected.
    pub fn is_supported(self) -> bool {
        !matches!(self, Self::Unsupported)
    }
}

/// A model's mapping of the ladder onto provider values.
#[derive(Clone, Debug, Default, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(transparent)]
pub struct ThinkingLevels(BTreeMap<ThinkingLevel, Option<String>>);

impl ThinkingLevels {
    /// An empty map: every rung through `HIGH` takes the adapter default, and
    /// the two opt-in rungs are unsupported.
    pub fn adapter_defaults() -> Self {
        Self(BTreeMap::new())
    }

    /// Builds a map from explicit entries.
    pub fn from_entries<I>(entries: I) -> Self
    where
        I: IntoIterator<Item = (ThinkingLevel, Option<String>)>,
    {
        Self(entries.into_iter().collect())
    }

    /// What the entry says about one rung.
    pub fn support(&self, level: ThinkingLevel) -> LevelSupport<'_> {
        match self.0.get(&level) {
            Some(Some(value)) => LevelSupport::Value(value),
            Some(None) => LevelSupport::Unsupported,
            None if level.is_opt_in() => LevelSupport::Unsupported,
            None => LevelSupport::AdapterDefault,
        }
    }

    /// Every rung a surface may offer, in ladder order.
    pub fn selectable(&self) -> Vec<ThinkingLevel> {
        LADDER
            .into_iter()
            .filter(|level| self.support(*level).is_supported())
            .collect()
    }

    /// The nearest supported rung to `requested`, searching upward first.
    ///
    /// Returns `None` only when the entry supports no rung at all, which
    /// validation refuses, so a validated catalog always answers.
    pub fn clamp(&self, requested: ThinkingLevel) -> Option<ThinkingLevel> {
        if self.support(requested).is_supported() {
            return Some(requested);
        }
        let start = requested.rung();
        for level in LADDER.into_iter().skip(start + 1) {
            if self.support(level).is_supported() {
                return Some(level);
            }
        }
        LADDER
            .into_iter()
            .take(start)
            .rev()
            .find(|level| self.support(*level).is_supported())
    }
}

/// How a request should carry its thinking control.
#[derive(Clone, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum ThinkingPlan {
    /// No thinking phase is requested.
    Disabled,
    /// A named effort value.
    Effort {
        /// The rung actually used after clamping.
        level: ThinkingLevel,
        /// The catalog value, or `None` to use the adapter's default mapping.
        value: Option<String>,
    },
    /// A token budget alongside a preserved answer allowance.
    Budget {
        /// The rung actually used after clamping.
        level: ThinkingLevel,
        /// Tokens the thinking phase may spend.
        thinking_tokens: u64,
        /// Tokens held back for the answer.
        answer_tokens: u64,
    },
}

impl ThinkingPlan {
    /// The rung the plan settled on, if any.
    pub fn level(&self) -> Option<ThinkingLevel> {
        match self {
            Self::Disabled => None,
            Self::Effort { level, .. } | Self::Budget { level, .. } => Some(*level),
        }
    }
}

/// Why a thinking plan could not be built.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ThinkingError {
    /// The catalog entry supports no rung at all.
    NoSupportedLevel,
    /// The model's answer ceiling is zero, so nothing can be requested.
    NoOutputAllowance,
}

impl core::fmt::Display for ThinkingError {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        let text = match self {
            Self::NoSupportedLevel => "the model supports no thinking level",
            Self::NoOutputAllowance => "the model has no output allowance",
        };
        f.write_str(text)
    }
}

/// Turns a requested rung into the control one protocol family accepts.
///
/// `requested_answer_tokens` is the caller's intended answer allowance and
/// `ceiling` is the model's own output limit. For a budget-mapped family the
/// thinking budget is added on top of the answer allowance and the whole thing
/// is clamped to the ceiling, with the reserved answer allowance preserved
/// first — so the answer always has room, whatever the ladder asked for.
pub fn plan(
    wire_api: WireApi,
    levels: &ThinkingLevels,
    requested: ThinkingLevel,
    requested_answer_tokens: u64,
    ceiling: u64,
) -> Result<ThinkingPlan, ThinkingError> {
    if ceiling == 0 {
        return Err(ThinkingError::NoOutputAllowance);
    }
    let level = levels
        .clamp(requested)
        .ok_or(ThinkingError::NoSupportedLevel)?;
    if level == ThinkingLevel::Off {
        return Ok(ThinkingPlan::Disabled);
    }
    if wire_api.is_effort_mapped() {
        let value = match levels.support(level) {
            LevelSupport::Value(value) => Some(value.to_owned()),
            LevelSupport::AdapterDefault | LevelSupport::Unsupported => None,
        };
        return Ok(ThinkingPlan::Effort { level, value });
    }
    // A budget-mapped family has no vocabulary above the top published budget,
    // so the two opt-in rungs settle onto the highest one that does.
    let budget_level = if level.is_opt_in() {
        ThinkingLevel::High
    } else {
        level
    };
    let answer = requested_answer_tokens
        .min(ceiling)
        .max(defaults::RESERVED_ANSWER_TOKENS.min(ceiling));
    let room = ceiling.saturating_sub(answer);
    let thinking = defaults::thinking_budget_tokens(budget_level).min(room);
    Ok(ThinkingPlan::Budget {
        level,
        thinking_tokens: thinking,
        answer_tokens: answer,
    })
}
