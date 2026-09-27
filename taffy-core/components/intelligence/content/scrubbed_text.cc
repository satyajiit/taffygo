// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/scrubbed_text.h"

#include <utility>

namespace taffy {

// The private constructor is defined here rather than in
// scrubbing_serializer.cc so that the type owns its own translation unit, but
// it stays private and ScrubbingSerializer stays its only friend: defining a
// private member needs no friendship, and calling it still does. That is the
// whole guarantee — there is no expression outside the serializer that
// produces one of these.
ScrubbedText::ScrubbedText(std::string value, ScrubReport report)
    : value_(std::move(value)), report_(report) {}

ScrubbedText::ScrubbedText(const ScrubbedText&) = default;
ScrubbedText& ScrubbedText::operator=(const ScrubbedText&) = default;
ScrubbedText::ScrubbedText(ScrubbedText&&) = default;
ScrubbedText& ScrubbedText::operator=(ScrubbedText&&) = default;
ScrubbedText::~ScrubbedText() = default;

}  // namespace taffy
