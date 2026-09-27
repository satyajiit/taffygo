// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_FILTERING_LIST_READER_H_
#define TAFFY_BROWSER_FILTERING_LIST_READER_H_

#include "taffy/components/filtering/browser/filtering_ruleset_service.h"

namespace taffy {

class ProfileAssetPlane;

// The reader `FilteringRulesetService` is constructed over: the two rule-set
// members of the installed `easylist-base` pack, concatenated in list order.
// While that pack is unpublished, Android reads the same pinned snapshots
// from an uncompressed APK asset. Answers nullopt only when neither source
// has bytes, which the service treats as "stand ready with no rules". The
// plane must outlive the service, which the manager's member order guarantees.
filtering::FilteringRulesetService::ListReader MakeFilterListReader(
    ProfileAssetPlane* asset_plane);

}  // namespace taffy

#endif  // TAFFY_BROWSER_FILTERING_LIST_READER_H_
