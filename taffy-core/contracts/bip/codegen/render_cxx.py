#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The generated C++ constant shared by the BIP browser and renderer seams."""

from __future__ import annotations

from layout import GENERATED_MARK


def version(document: dict) -> str:
    """Render the C++ protocol version from the version owner document."""
    protocol_version = document["protocol_version"]
    return f'''// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// {GENERATED_MARK}
// Source of truth: taffy-core/contracts/bip/schema/bip.version.json

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_BIP_SCHEMA_VERSION_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_BIP_SCHEMA_VERSION_H_

namespace taffy {{

// The protocol version this build speaks, as it appears in every message's
// schema_version field.
inline constexpr char kBipSchemaVersion[] = "{protocol_version}";

}}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_BIP_SCHEMA_VERSION_H_
'''
