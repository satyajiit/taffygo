// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/profile_store/profile_store_reader.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/bookmarks/browser/bookmark_utils.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "components/keyed_service/core/keyed_service_base_factory.h"
#include "components/keyed_service/core/service_access_type.h"

namespace taffy {
namespace {

// The whole tree, flattened, is bounded here so a person with thousands of
// bookmarks does not have them all copied before the cap applies.
constexpr size_t kMaxBookmarkWalk = 4096u;

void OnHistoryRead(ProfileStoreReader::EntriesCallback callback,
                   history::QueryResults results) {
  std::vector<TaskStoreEntry> entries;
  entries.reserve(results.size());
  for (size_t index = 0; index < results.size(); ++index) {
    const history::URLResult& result = results[index];
    entries.push_back(TaskStoreEntry{
        .title = result.title(),
        .url = result.url(),
        .when = result.visit_time(),
    });
  }
  std::move(callback).Run(std::move(entries));
}

std::vector<const bookmarks::BookmarkNode*> FlattenBookmarks(
    const bookmarks::BookmarkModel& model) {
  std::vector<const bookmarks::BookmarkNode*> urls;
  std::vector<const bookmarks::BookmarkNode*> pending = {model.root_node()};
  size_t visited = 0u;
  while (!pending.empty() && visited < kMaxBookmarkWalk) {
    const bookmarks::BookmarkNode* node = pending.back();
    pending.pop_back();
    ++visited;
    if (node->is_url()) {
      urls.push_back(node);
      continue;
    }
    for (size_t index = node->children().size(); index > 0; --index) {
      pending.push_back(node->children()[index - 1].get());
    }
  }
  return urls;
}

class ProfileStoreReaderChrome final : public ProfileStoreReader {
 public:
  explicit ProfileStoreReaderChrome(Profile* profile) : profile_(profile) {}
  ~ProfileStoreReaderChrome() override = default;

  void ReadHistory(const std::u16string& words,
                   size_t max_entries,
                   EntriesCallback callback) override {
    history::HistoryService* service = HistoryServiceFactory::GetForProfile(
        profile_, ServiceAccessType::EXPLICIT_ACCESS);
    if (!service || max_entries == 0u) {
      std::move(callback).Run({});
      return;
    }
    history::QueryOptions options;
    options.max_count = static_cast<int>(std::min<size_t>(
        max_entries, static_cast<size_t>(std::numeric_limits<int>::max())));
    options.duplicate_policy = history::QueryOptions::REMOVE_ALL_DUPLICATES;
    // Taffy's own trail (patch 0038 marks it as the actor's) is not the
    // person's history and stays out whatever the words.
    options.include_actor_visits = false;
    options.include_user_visits = true;
    service->QueryHistory(words, options,
                          base::BindOnce(&OnHistoryRead, std::move(callback)),
                          &tracker_);
  }

  void ReadBookmarks(const std::u16string& words,
                     size_t max_entries,
                     EntriesCallback callback) override {
    bookmarks::BookmarkModel* model =
        BookmarkModelFactory::GetForBrowserContext(profile_);
    if (!model || !model->loaded() || max_entries == 0u) {
      std::move(callback).Run({});
      return;
    }
    std::vector<const bookmarks::BookmarkNode*> nodes;
    if (words.empty()) {
      nodes = FlattenBookmarks(*model);
    } else {
      bookmarks::QueryFields query;
      query.word_phrase_query = std::make_unique<std::u16string>(words);
      nodes = bookmarks::GetBookmarksMatchingProperties(model, query,
                                                        kMaxBookmarkWalk);
    }
    std::stable_sort(nodes.begin(), nodes.end(),
                     [](const bookmarks::BookmarkNode* left,
                        const bookmarks::BookmarkNode* right) {
                       return left->date_added() > right->date_added();
                     });
    std::vector<TaskStoreEntry> entries;
    for (const bookmarks::BookmarkNode* node : nodes) {
      if (entries.size() >= max_entries) {
        break;
      }
      if (!node || !node->is_url()) {
        continue;
      }
      entries.push_back(TaskStoreEntry{
          .title = node->GetTitle(),
          .url = node->url(),
          .when = node->date_added(),
      });
    }
    std::move(callback).Run(std::move(entries));
  }

 private:
  const raw_ptr<Profile> profile_;
  base::CancelableTaskTracker tracker_;
};

}  // namespace

// static
std::vector<KeyedServiceBaseFactory*>
ProfileStoreReader::GetFactoryDependencies() {
  return {HistoryServiceFactory::GetInstance(),
          BookmarkModelFactory::GetInstance()};
}

// static
std::unique_ptr<ProfileStoreReader> ProfileStoreReader::Create(
    Profile* profile) {
  if (!profile || profile->IsOffTheRecord()) {
    return nullptr;
  }
  return std::make_unique<ProfileStoreReaderChrome>(profile);
}

}  // namespace taffy
