// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The one host call the task trace makes: hand a composed line to the
//! browser's log. Its own bridge rather than a line in the root, because the
//! root names the seam and nothing else does, and only
//! `service_bridge_trace.rs` calls it.

#[allow(non_snake_case)]
#[cxx::bridge(namespace = "taffy::core_bridge")]
pub(crate) mod ffi {
    unsafe extern "C++" {
        include!("taffy/services/core/service_bridge_cxx.h");

        fn TaskTrace(line: &str);
    }
}
