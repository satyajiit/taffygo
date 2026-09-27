// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The storage crate's tests, one module per property family.
//!
//! Split on what a test is *about* rather than on file length: the migration
//! ladder, the shape of the schema, the round trip, and deletion. A test that
//! does not obviously belong to one of them is a sign the crate grew a
//! responsibility nobody named.

mod migrations;
mod round_trip;
mod schema_shape;
mod search_and_deletion;
mod support;
mod workspace_lifecycle;
