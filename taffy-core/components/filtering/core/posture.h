// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_FILTERING_CORE_POSTURE_H_
#define TAFFY_COMPONENTS_FILTERING_CORE_POSTURE_H_

#include <string>
#include <string_view>

#include "base/containers/flat_set.h"

namespace taffy::filtering {

// Whether filtering acts on a given site: the profile's master toggle plus
// the per-site exceptions a person has recorded. Pure data, cached by the
// browser half and replaced only when those preferences change.
class FilteringPosture {
 public:
  FilteringPosture(bool enabled, base::flat_set<std::string> exception_hosts);
  FilteringPosture(const FilteringPosture&);
  FilteringPosture& operator=(const FilteringPosture&);
  FilteringPosture(FilteringPosture&&);
  FilteringPosture& operator=(FilteringPosture&&);
  ~FilteringPosture();

  bool enabled() const { return enabled_; }
  const base::flat_set<std::string>& exception_hosts() const {
    return exception_hosts_;
  }

  // Whether blocking acts on a page at `document_host`: the master toggle is
  // on and no exception covers the host. This is the request path's question.
  bool ActiveForHost(std::string_view document_host) const;

  // Whether a person has recorded an exception covering `document_host`,
  // whatever the master toggle says. An exception covers the host it names
  // and every subdomain of it — the sheet records the host a person was
  // looking at, and `sub.example.test` after an exception for `example.test`
  // is the same site as far as the person's intent goes.
  //
  // Separate from `ActiveForHost` because the sheet has different words for
  // "blocking is off for this site" and "blocking is off for TaffyGo", and one
  // boolean cannot serve both (decision 0128).
  bool ExceptedForHost(std::string_view document_host) const;

 private:
  bool enabled_;
  base::flat_set<std::string> exception_hosts_;
};

// Whether `host` is `exception` or a subdomain of it. Exposed for the tests
// that pin the subdomain rule down.
bool HostCoveredByException(std::string_view host, std::string_view exception);

}  // namespace taffy::filtering

#endif  // TAFFY_COMPONENTS_FILTERING_CORE_POSTURE_H_
