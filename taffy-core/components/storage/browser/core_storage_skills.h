// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_SKILLS_H_
#define TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_SKILLS_H_

#include <string>

#include "sql/database.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

// The durable half of decision 0055: an authored skill and a learned procedure
// are one record, so there is one table set, one lifecycle, and one place a
// person can see and switch off everything the assistant holds.
//
// There is no query surface here and there will not be one. What a shipping
// durable store can be asked is OD-104, and the answer this build takes is the
// bounded bootstrap list below: every skill whole, and the most recent runs
// ranked by recency. A search seam added here would answer OD-104 by accident
// and would commit the product to a database it has not chosen -- fts5 is
// compiled into nothing that runs on a device.

// Restores every installed skill and the recall list, into an already
// partially built bootstrap. Both are bounded by the contract's own limits,
// and a row this build cannot read closes the whole bootstrap rather than
// being skipped: a skill silently dropped is a standing arrangement a person
// believes they still have.
bool LoadSkills(sql::Database* database,
                core_service::mojom::CoreBootstrap* bootstrap);

// Commits one INSTALL_SKILL, SET_SKILL_STATUS, RECORD_SKILL_RUN or
// FORGET_SKILL effect in its own transaction. Re-delivering an effect already
// committed succeeds without writing again.
bool CommitSkillOperation(sql::Database* database,
                          const core_service::mojom::EffectEnvelope& effect);

// Commits one DELETE_SOURCE effect: the workspace snapshot the deletion
// produced and the removal of every skill recorded against that site, in one
// transaction.
bool CommitSourceDeletion(sql::Database* database,
                          const core_service::mojom::EffectEnvelope& effect);

// Removes every skill scoped to one origin, with its versions and its runs.
//
// This is the site half of source deletion and it does not open a transaction:
// it runs inside the caller's, and CommitSourceDeletion is the only caller.
// The two halves cannot be separate transactions, because between them a skill
// exists that is about a site the person asked to be forgotten -- and a crash
// in that window leaves it there for good.
bool RemoveSkillsForSite(sql::Database* database, const std::string& origin);

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_SKILLS_H_
