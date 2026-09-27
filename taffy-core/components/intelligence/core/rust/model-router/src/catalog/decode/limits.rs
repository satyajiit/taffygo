// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Every bound a catalog document is decoded against.
//!
//! One file, so "what is the largest thing a served catalog can make this
//! process allocate" has a single answer that fits on a screen. A bound with
//! no constant here is a bound nobody set.

/// Largest number of provider entries a document may carry.
pub const MAX_PROVIDERS: usize = 256;
/// Largest number of model entries a document may carry.
pub const MAX_MODELS: usize = 4_096;
/// Longest rendered name a document may carry, in bytes.
pub const MAX_DISPLAY_NAME_LEN: usize = 128;
/// Largest number of non-secret static headers a provider may carry.
pub const MAX_STATIC_HEADERS: usize = 16;

/// The setup facts one provider row may carry (decision 0094).
///
/// Exactly the three the decoder knows. The bound is here rather than implied
/// by the match so a document carrying a hundred unknown keys is refused on
/// its size before any of them is read.
pub const MAX_PRESENTATION_FIELDS: usize = 3;
/// Largest number of dialect or sampling overrides a model may carry.
pub const MAX_OVERRIDES: usize = 32;
/// Longest override key or value, in bytes.
pub const MAX_OVERRIDE_LEN: usize = 256;
/// Largest number of long-context tiers a price snapshot may carry.
pub const MAX_PRICE_TIERS: usize = 8;
