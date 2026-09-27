// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/taffy_browser_effect_source.h"

#include <stddef.h>

#include <string_view>
#include <utility>

#include "base/strings/string_util.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/download_item_utils.h"
#include "content/public/browser/web_contents.h"

namespace taffy {

namespace {

// A media type is short. The bound is here so that a pathological header
// cannot be walked character by character.
constexpr size_t kMaxMediaTypeChars = 128;

// The IANA top-level media types registry, and nothing else. Written out
// rather than derived, because the whole value of this field is that the set
// is closed and a reviewer can check it against one document.
struct TopLevelTypeName {
  std::string_view name;
  MediaTopLevelType type;
};

constexpr TopLevelTypeName kTopLevelTypes[] = {
    {"application", MediaTopLevelType::kApplication},
    {"audio", MediaTopLevelType::kAudio},
    {"example", MediaTopLevelType::kExample},
    {"font", MediaTopLevelType::kFont},
    {"haptics", MediaTopLevelType::kHaptics},
    {"image", MediaTopLevelType::kImage},
    {"message", MediaTopLevelType::kMessage},
    {"model", MediaTopLevelType::kModel},
    {"multipart", MediaTopLevelType::kMultipart},
    {"text", MediaTopLevelType::kText},
    {"video", MediaTopLevelType::kVideo},
};

// RFC 9110 section 5.6.2. Written out rather than taken from //net so that the
// rule this file enforces is the rule this file states — the alternative is a
// reader having to go and check whether some other parser happens to accept a
// space today.
bool IsTokenChar(char c) {
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9')) {
    return true;
  }
  switch (c) {
    case '!':
    case '#':
    case '$':
    case '%':
    case '&':
    case '\'':
    case '*':
    case '+':
    case '-':
    case '.':
    case '^':
    case '_':
    case '`':
    case '|':
    case '~':
      return true;
    default:
      return false;
  }
}

bool IsToken(std::string_view value) {
  if (value.empty()) {
    return false;
  }
  for (const char c : value) {
    if (!IsTokenChar(c)) {
      return false;
    }
  }
  return true;
}

DownloadState ProjectState(const download::DownloadItem& item) {
  switch (item.GetState()) {
    case download::DownloadItem::IN_PROGRESS:
      return item.IsPaused() ? DownloadState::kPaused
                             : DownloadState::kInProgress;
    case download::DownloadItem::COMPLETE:
      return DownloadState::kComplete;
    case download::DownloadItem::CANCELLED:
      return DownloadState::kCancelled;
    case download::DownloadItem::INTERRUPTED:
      return DownloadState::kInterrupted;
    case download::DownloadItem::MAX_DOWNLOAD_STATE:
      // Upstream's uninitialized value. Listed rather than defaulted so that a
      // new download state is a compile error here instead of silently
      // reporting a transfer as freshly created.
      return DownloadState::kCreated;
  }
}

// What makes an emission worth making. See the header: a byte count is not on
// the list, because a progress tick settles nothing.
bool IsWorthEmitting(const DownloadFlowFacts& previous,
                     const DownloadFlowFacts& current) {
  return previous.state != current.state ||
         previous.directory_class != current.directory_class ||
         previous.media_type != current.media_type;
}

}  // namespace

MediaTopLevelType MediaTopLevelTypeOf(std::string_view reported) {
  if (reported.empty() || reported.size() > kMaxMediaTypeChars) {
    return MediaTopLevelType::kUnknown;
  }
  const size_t slash = reported.find('/');
  if (slash == std::string_view::npos) {
    return MediaTopLevelType::kUnknown;
  }
  const std::string_view top_level = reported.substr(0, slash);
  // The subtype has to parse and is then thrown away. It is the half a server
  // writes a file name into, and the half no verifier asks about.
  if (!IsToken(top_level) || !IsToken(reported.substr(slash + 1))) {
    return MediaTopLevelType::kUnknown;
  }
  for (const TopLevelTypeName& candidate : kTopLevelTypes) {
    // A media type is case-insensitive (RFC 9110 section 8.3.1), so
    // `Application/PDF` is the same claim as `application/pdf` and must reach
    // the same enumerator. Matching case-sensitively would hand a server a
    // free kUnknown for the asking.
    if (base::EqualsCaseInsensitiveASCII(top_level, candidate.name)) {
      return candidate.type;
    }
  }
  return MediaTopLevelType::kUnknown;
}

TaffyBrowserEffectSource::TaffyBrowserEffectSource(
    content::WebContents* action_scope)
    : content::WebContentsObserver(action_scope) {
  CHECK(action_scope);
}

TaffyBrowserEffectSource::~TaffyBrowserEffectSource() = default;

void TaffyBrowserEffectSource::Observe(content::DownloadManager* manager) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  CHECK(manager);
  StopWatching();
  manager_observation_.Observe(manager);
}

void TaffyBrowserEffectSource::RegisterDirectoryClass(
    DownloadDestinationKind directory_class,
    const base::FilePath& directory) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (directory.empty()) {
    return;
  }
  directory_classes_.push_back({directory_class, directory, {}});
}

void TaffyBrowserEffectSource::RegisterDirectoryClassResolver(
    DownloadDestinationKind directory_class,
    DirectoryResolver resolver) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!resolver) {
    return;
  }
  directory_classes_.push_back(
      {directory_class, base::FilePath(), std::move(resolver)});
}

void TaffyBrowserEffectSource::AddObserver(BrowserEffectObserver* observer) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  // Deliberately no replay of the last evidence to a new observer. The
  // subscriber does not say which dispatch it is asking about, so a replay
  // could only hand over everything and let the reader sort it out — which is
  // the corroboration the attribution rule exists to refuse.
  //
  // The host installs this source before it can dispatch an action. There is
  // still deliberately no replay here, so a caller that relies on browser-flow
  // evidence must attach its verifier before initiating the flow.
  observers_.AddObserver(observer);
}

void TaffyBrowserEffectSource::RemoveObserver(BrowserEffectObserver* observer) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  observers_.RemoveObserver(observer);
}

DispatchWatermark TaffyBrowserEffectSource::NoteDispatch() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  // A fresh value every time, and never a reused one. A verifier holds its
  // watermark for the life of its attempt, so reusing a number would let a
  // finished attempt's evidence match a later one's.
  const DispatchWatermark watermark{++last_watermark_};
  // One window at a time. The previous dispatch's window closes here rather
  // than lingering, because two open windows would mean one download binding
  // to whichever the code happened to look at first.
  open_dispatch_ = watermark;
  return watermark;
}

void TaffyBrowserEffectSource::CloseDispatch(DispatchWatermark watermark) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (open_dispatch_ == watermark) {
    open_dispatch_.reset();
  }
}

bool TaffyBrowserEffectSource::HasOpenDispatch() const {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  return open_dispatch_.has_value();
}

void TaffyBrowserEffectSource::DidGetUserInteraction(
    const blink::WebInputEvent& event) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  // The event is not read, and it must not be: what matters is that Chromium's
  // input pipeline delivered one to this tab, which is the browser's own
  // statement that a person acted. Reading which key or where would be reading
  // the person's input, which this class has no business holding.
  //
  // From here nothing is attributed until the next dispatch, and the next
  // dispatch is in turn bounded by that action's terminal result.
  // A transfer this click causes does not exist yet — it appears when the
  // response head arrives — so if an action goes out in between, the person's
  // download is first seen inside that action's window and is bound to it. The
  // header says why nothing at this layer closes that, and decision 0062
  // section 6 records it against [Open (OD-056)].
  //
  // Within the limit, the cost of being wrong in this direction is only an
  // action that fails to corroborate.
  open_dispatch_.reset();
}

void TaffyBrowserEffectSource::OnDownloadCreated(
    content::DownloadManager* manager,
    download::DownloadItem* item) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!item || !InScope(item)) {
    return;
  }
  // Upstream states that this may be called an arbitrary number of times for
  // one item, history restore included, so the observation is added once.
  if (!item_observations_.IsObservingSource(item)) {
    item_observations_.AddObservation(item);
  }
  EmitIfChanged(*item);
}

void TaffyBrowserEffectSource::ManagerGoingDown(
    content::DownloadManager* manager) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  // The manager takes its items with it. Dropping every observation here is
  // what keeps this class from holding a pointer into a destroyed download
  // system; the per-item OnDownloadDestroyed calls that follow would arrive
  // too late to be the mechanism.
  StopWatching();
}

void TaffyBrowserEffectSource::OnDownloadUpdated(download::DownloadItem* item) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!item) {
    return;
  }
  // Asked again on every update, not only when the observation was added.
  // Scope is a property of the tab, and the tab can be destroyed while a
  // transfer this witness is already observing is still running — the
  // ordinary case for a download the person leaves going. Deciding scope once
  // at creation would leave a witness reporting on behalf of a tab that no
  // longer exists, which is the fallback-to-the-profile behaviour this class
  // is built to refuse.
  if (!InScope(item)) {
    return;
  }
  EmitIfChanged(*item);
}

void TaffyBrowserEffectSource::OnDownloadDestroyed(
    download::DownloadItem* item) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!item) {
    return;
  }
  if (item_observations_.IsObservingSource(item)) {
    item_observations_.RemoveObservation(item);
  }
  watched_.erase(item->GetId());
}

bool TaffyBrowserEffectSource::InScope(
    const download::DownloadItem* item) const {
  content::WebContents* const scope = web_contents();
  if (!scope) {
    // The tab this witness corroborates is gone. Refusing everything is the
    // only safe answer: an item with no attached WebContents also reports
    // null, and treating that as a match would turn a destroyed scope into a
    // witness for every download in the profile.
    return false;
  }
  // GetOriginalWebContents rather than GetWebContents: upstream nulls the
  // latter once the tab's primary page changes, and a download that outlives
  // the navigation that started it is the ordinary case, not an edge one.
  return content::DownloadItemUtils::GetOriginalWebContents(item) == scope;
}

DownloadFlowFacts TaffyBrowserEffectSource::Project(
    const download::DownloadItem& item) const {
  DownloadFlowFacts facts;
  facts.download_id = item.GetId();
  facts.media_type = MediaTopLevelTypeOf(item.GetMimeType());
  facts.received_bytes = item.GetReceivedBytes();
  facts.directory_class = ClassifyDirectory(item.GetTargetFilePath());
  facts.state = ProjectState(item);
  // Attribution is not projected from the item. It is decided once, when this
  // witness first sees the transfer, and remembered — see EmitIfChanged.
  return facts;
}

DownloadDestinationKind TaffyBrowserEffectSource::ClassifyDirectory(
    const base::FilePath& target_path) const {
  if (target_path.empty()) {
    return DownloadDestinationKind::kUndecided;
  }
  DownloadDestinationKind matched = DownloadDestinationKind::kUndecided;
  size_t matched_length = 0;
  for (const ClassifiedDirectory& candidate : directory_classes_) {
    const base::FilePath directory =
        candidate.resolver ? candidate.resolver.Run() : candidate.directory;
    if (directory.empty() || !directory.IsParent(target_path)) {
      continue;
    }
    // Longest match wins, so a registered directory nested inside another one
    // — an application-private directory beneath the downloads directory, say
    // — reports the specific class rather than whichever was registered first.
    const size_t length = directory.value().size();
    if (length >= matched_length) {
      matched_length = length;
      matched = candidate.directory_class;
    }
  }
  return matched;
}

void TaffyBrowserEffectSource::EmitIfChanged(
    const download::DownloadItem& item) {
  DownloadFlowFacts facts = Project(item);
  const auto [entry, inserted] = watched_.try_emplace(facts.download_id);
  if (inserted) {
    // Bound at first sight and never revisited. Deciding it later would mean a
    // transfer that started before the action — or after the person clicked
    // something — could acquire an attribution by still running when the next
    // dispatch opened a window.
    entry->second.attributed_to = open_dispatch_;
  } else if (!IsWorthEmitting(entry->second.last_emitted, facts)) {
    return;
  }
  facts.attributed_to = entry->second.attributed_to;
  entry->second.last_emitted = facts;

  BrowserFlowEvidence evidence;
  evidence.flow_started = true;
  evidence.kind = BrowserFlowKind::kDownload;
  evidence.download = std::move(facts);
  for (BrowserEffectObserver& observer : observers_) {
    observer.OnBrowserFlowStarted(evidence);
  }
}

void TaffyBrowserEffectSource::StopWatching() {
  manager_observation_.Reset();
  item_observations_.RemoveAllObservations();
  watched_.clear();
}

}  // namespace taffy
