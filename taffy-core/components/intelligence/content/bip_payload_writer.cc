// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/bip_payload_writer.h"

namespace taffy::bip_payload {

Writer::Writer(size_t ceiling) : ceiling_(ceiling) {}

Writer::~Writer() = default;

}  // namespace taffy::bip_payload
