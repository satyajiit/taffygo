// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/filtering/core/posture.h"

#include <utility>

namespace taffy::filtering {

FilteringPosture::FilteringPosture(bool enabled,
                                   base::flat_set<std::string> exception_hosts)
    : enabled_(enabled), exception_hosts_(std::move(exception_hosts)) {}

FilteringPosture::FilteringPosture(const FilteringPosture&) = default;
FilteringPosture& FilteringPosture::operator=(const FilteringPosture&) =
    default;
FilteringPosture::FilteringPosture(FilteringPosture&&) = default;
FilteringPosture& FilteringPosture::operator=(FilteringPosture&&) = default;
FilteringPosture::~FilteringPosture() = default;

bool FilteringPosture::ActiveForHost(std::string_view document_host) const {
  return enabled_ && !ExceptedForHost(document_host);
}

bool FilteringPosture::ExceptedForHost(std::string_view document_host) const {
  if (document_host.empty()) {
    return false;
  }
  // An exception can cover only the exact host or one of its label suffixes.
  // Probe those few candidates in the sorted set instead of scanning every
  // exception a profile has accumulated for every newly committed document.
  std::string_view candidate = document_host;
  while (true) {
    if (exception_hosts_.contains(candidate)) {
      return true;
    }
    const size_t separator = candidate.find('.');
    if (separator == std::string_view::npos) {
      break;
    }
    candidate.remove_prefix(separator + 1);
  }
  return false;
}

bool HostCoveredByException(std::string_view host, std::string_view exception) {
  if (exception.empty() || host.size() < exception.size()) {
    return false;
  }
  if (host == exception) {
    return true;
  }
  // A subdomain: the host ends with the exception, and the byte before the
  // suffix is a label separator, so `notexample.test` is never covered by an
  // exception for `example.test`.
  return host.size() > exception.size() &&
         host[host.size() - exception.size() - 1] == '.' &&
         host.substr(host.size() - exception.size()) == exception;
}

}  // namespace taffy::filtering
