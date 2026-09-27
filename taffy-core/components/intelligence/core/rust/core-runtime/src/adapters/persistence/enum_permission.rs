// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Closed native-permission projections for the durable task transaction.

use core_service_types as wire;
use task_engine::{PermissionDecision, PlatformPermission};

pub(super) const fn permission(value: PlatformPermission) -> wire::PersistedPlatformPermission {
    match value {
        PlatformPermission::Notifications => wire::PersistedPlatformPermission::Notifications,
        PlatformPermission::Microphone => wire::PersistedPlatformPermission::Microphone,
        PlatformPermission::Camera => wire::PersistedPlatformPermission::Camera,
        PlatformPermission::Location => wire::PersistedPlatformPermission::Location,
    }
}

pub(super) const fn unpermission(value: wire::PersistedPlatformPermission) -> PlatformPermission {
    match value {
        wire::PersistedPlatformPermission::Notifications => PlatformPermission::Notifications,
        wire::PersistedPlatformPermission::Microphone => PlatformPermission::Microphone,
        wire::PersistedPlatformPermission::Camera => PlatformPermission::Camera,
        wire::PersistedPlatformPermission::Location => PlatformPermission::Location,
    }
}

pub(super) const fn permission_decision(
    value: PermissionDecision,
) -> wire::PersistedPermissionDecision {
    match value {
        PermissionDecision::Granted => wire::PersistedPermissionDecision::Granted,
        PermissionDecision::Denied => wire::PersistedPermissionDecision::Denied,
        PermissionDecision::Dismissed => wire::PersistedPermissionDecision::Dismissed,
        PermissionDecision::Unavailable => wire::PersistedPermissionDecision::Unavailable,
    }
}

pub(super) const fn unpermission_decision(
    value: wire::PersistedPermissionDecision,
) -> PermissionDecision {
    match value {
        wire::PersistedPermissionDecision::Granted => PermissionDecision::Granted,
        wire::PersistedPermissionDecision::Denied => PermissionDecision::Denied,
        wire::PersistedPermissionDecision::Dismissed => PermissionDecision::Dismissed,
        wire::PersistedPermissionDecision::Unavailable => PermissionDecision::Unavailable,
    }
}
