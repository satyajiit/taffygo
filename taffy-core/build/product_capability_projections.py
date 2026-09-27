# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Deterministic projections of the validated product capability authority."""

from __future__ import annotations

import json
from typing import Any


def _cpp_milestone(value: str | None) -> str:
    if value is None:
        return "std::nullopt"
    return (
        "std::optional<core_service::mojom::TaskMilestone>{"
        f"core_service::mojom::TaskMilestone::k{value}" + "}"
    )


def _rust_milestone(type_name: str, value: str | None) -> str:
    if value is None:
        return "None"
    return f"Some({type_name}::{value})"


def render_release_json(document: dict[str, Any], fingerprint: str) -> str:
    projection = {
        "schema_version": document["schema_version"],
        "configuration_fingerprint": fingerprint,
        "accepted_milestone": document["accepted_milestone"],
        "profiles": document["profiles"],
        "chromium_profiles": document["chromium_profiles"],
    }
    return json.dumps(projection, indent=2, sort_keys=True) + "\n"


def render_gni(document: dict[str, Any], fingerprint: str) -> str:
    del document
    return f'''# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Generated from //taffy/build/product-capabilities.json. Do not edit.
# Configuration fingerprint: {fingerprint}

declare_args() {{
  # A new or unclassified profile is fail-closed by default.
  taffy_capability_profile = "candidate"
}}

assert(taffy_capability_profile == "candidate" ||
           taffy_capability_profile == "development",
       "taffy_capability_profile must be candidate or development")

if (taffy_capability_profile == "candidate") {{
  taffy_capability_cpp_define = "TAFFY_CAPABILITY_PROFILE_CANDIDATE"
  taffy_capability_rustflags = [ "--cfg=taffy_candidate_capabilities" ]
}} else {{
  taffy_capability_cpp_define = "TAFFY_CAPABILITY_PROFILE_DEVELOPMENT"
  taffy_capability_rustflags = []
}}
'''


def render_cpp(document: dict[str, Any], fingerprint: str) -> str:
    candidate = document["profiles"]["candidate"]
    development = document["profiles"]["development"]
    accepted = document["accepted_milestone"]
    return f'''// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Generated from //taffy/build/product-capabilities.json. Do not edit.

#ifndef TAFFY_BROWSER_GENERATED_PRODUCT_CAPABILITIES_H_
#define TAFFY_BROWSER_GENERATED_PRODUCT_CAPABILITIES_H_

#include <optional>
#include <string_view>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy::product_capabilities {{

struct Profile final {{
  std::string_view name;
  core_service::mojom::TaskMilestone accepted_milestone;
  bool delegated_task_start;
  std::optional<core_service::mojom::TaskMilestone> task_milestone;
}};

inline constexpr std::string_view kConfigurationFingerprint = "{fingerprint}";
inline constexpr Profile kCandidate{{
    "candidate", core_service::mojom::TaskMilestone::k{accepted},
    {str(candidate['delegated_task_start']).lower()},
    {_cpp_milestone(candidate['task_milestone'])}}};
inline constexpr Profile kDevelopment{{
    "development", core_service::mojom::TaskMilestone::k{accepted},
    {str(development['delegated_task_start']).lower()},
    {_cpp_milestone(development['task_milestone'])}}};

// Kept out of line so the caller's validation path is live in both compiled
// profiles; compile-time folding would make one branch an -Wunreachable-code
// error before the other profile could be built.
const Profile& Active();

// The generator owns when the candidate task surface opens. These checks
// assert only the language-local projection shape, so accepting a later
// milestone cannot leave behind an obsolete fail-closed assertion.
static_assert(kCandidate.delegated_task_start ==
              kCandidate.task_milestone.has_value());
static_assert(!kCandidate.task_milestone.has_value() ||
              *kCandidate.task_milestone == kCandidate.accepted_milestone);

}}  // namespace taffy::product_capabilities

#endif  // TAFFY_BROWSER_GENERATED_PRODUCT_CAPABILITIES_H_
'''


def render_cpp_impl(document: dict[str, Any], fingerprint: str) -> str:
    del document, fingerprint
    return '''// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Generated from //taffy/build/product-capabilities.json. Do not edit.

#include "taffy/browser/generated/product_capabilities.h"

namespace taffy::product_capabilities {

const Profile& Active() {
#if defined(TAFFY_CAPABILITY_PROFILE_CANDIDATE) && \\
    defined(TAFFY_CAPABILITY_PROFILE_DEVELOPMENT)
#error "exactly one TaffyGo capability profile must be selected"
#elif defined(TAFFY_CAPABILITY_PROFILE_CANDIDATE)
  return kCandidate;
#elif defined(TAFFY_CAPABILITY_PROFILE_DEVELOPMENT)
  return kDevelopment;
#else
#error "a TaffyGo capability profile must be selected by the GN projection"
#endif
}

}  // namespace taffy::product_capabilities
'''


def render_rust(document: dict[str, Any], fingerprint: str) -> str:
    candidate = document["profiles"]["candidate"]
    development = document["profiles"]["development"]
    accepted = document["accepted_milestone"]
    return f'''// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Generated from taffy-core/build/product-capabilities.json. Do not edit.

use policy_engine::PolicyMilestone;
use task_engine::Milestone;

/// One compiled product exposure selected by the build profile.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Profile {{
    /// Stable internal profile name recorded by release evidence.
    pub name: &'static str,
    /// Latest milestone whose exit has been formally accepted.
    pub accepted_milestone: Milestone,
    /// Whether the browser and core may admit a delegated task start.
    pub delegated_task_start: bool,
    /// Exact task vocabulary for starts, absent when starts are quarantined.
    pub task_milestone: Option<Milestone>,
    /// Exact policy surface paired with `task_milestone`.
    pub policy_milestone: Option<PolicyMilestone>,
}}

/// Digest of the canonical validated authority document.
pub const CONFIGURATION_FINGERPRINT: &str = "{fingerprint}";

/// Candidate/release exposure. It exactly follows the accepted milestone.
pub const CANDIDATE: Profile = Profile {{
    name: "candidate",
    accepted_milestone: Milestone::{accepted},
    delegated_task_start: {str(candidate['delegated_task_start']).lower()},
    task_milestone: {_rust_milestone('Milestone', candidate['task_milestone'])},
    policy_milestone: {_rust_milestone('PolicyMilestone', candidate['policy_milestone'])},
}};

/// Development-only bring-up exposure. It is never a promotable candidate.
pub const DEVELOPMENT: Profile = Profile {{
    name: "development",
    accepted_milestone: Milestone::{accepted},
    delegated_task_start: {str(development['delegated_task_start']).lower()},
    task_milestone: {_rust_milestone('Milestone', development['task_milestone'])},
    policy_milestone: {_rust_milestone('PolicyMilestone', development['policy_milestone'])},
}};

#[cfg(taffy_candidate_capabilities)]
/// Capabilities selected by the current GN build.
pub const ACTIVE: Profile = CANDIDATE;

#[cfg(not(taffy_candidate_capabilities))]
/// Capabilities selected by the Cargo loop or a development GN build.
pub const ACTIVE: Profile = DEVELOPMENT;
'''
