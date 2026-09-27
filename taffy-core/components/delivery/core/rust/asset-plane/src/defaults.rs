// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The tuning this layer owns, in one place.
//!
//! Every number here is a policy decision rather than a measurement, so it
//! lives beside the others and not scattered through the code that reads it.
//! A number that turns out to be a measurement belongs in
//! `docs/quality/metrics.md` instead, which is the one place quality targets
//! live.

/// The most assets one device may hold at once.
///
/// A bound rather than a budget: the catalog is compiled in, so this can only
/// be exceeded by a catalog that grew past what a phone should carry, which is
/// a review finding and not a run-time condition.
pub const MAX_TRACKED_ASSETS: usize = 64;

/// The most transfers that may run at once.
///
/// One. Assets are large and few, the device is a phone, and two large
/// transfers on one radio finish later than the same two in sequence. It is a
/// constant rather than a setting because nothing has measured a case for a
/// second.
pub const MAX_CONCURRENT_TRANSFERS: usize = 1;

/// How many failed transfer attempts one automatic series permits.
///
/// After this the asset stays failed until a person asks again, or until the
/// profile starts again. An install that retries forever is an install that
/// spends a person's data forever.
pub const MAX_TRANSFER_ATTEMPTS: u32 = 5;

/// The first backoff delay, doubled per attempt up to the cap.
pub const BACKOFF_BASE_MILLIS: u64 = 2_000;

/// The longest backoff delay.
pub const BACKOFF_CAP_MILLIS: u64 = 300_000;

/// The share of a backoff delay that jitter may subtract, as a percentage.
pub const BACKOFF_JITTER_PERCENT: u32 = 20;

/// How much of a partial transfer is kept when it is interrupted.
///
/// All of it. Resuming is the whole reason the staging file is not deleted on
/// an interruption, and a rule that discards below some threshold is a rule
/// that discards exactly when the connection is worst.
pub const KEEP_PARTIAL_TRANSFERS: bool = true;

/// The most bytes a single asset variant may declare.
///
/// Four gibibytes. Past this the plane refuses the row rather than the device,
/// because a phone that cannot hold it should learn so from a catalog review
/// and not from a full disk.
pub const MAX_VARIANT_BYTES: u64 = 4 * 1024 * 1024 * 1024;
