// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/corpus/corpus_canary.h"

namespace taffy::test {

CorpusCanary::CorpusCanary() = default;
CorpusCanary::CorpusCanary(const CorpusCanary&) = default;
CorpusCanary::CorpusCanary(CorpusCanary&&) noexcept = default;
CorpusCanary& CorpusCanary::operator=(const CorpusCanary&) = default;
CorpusCanary& CorpusCanary::operator=(CorpusCanary&&) noexcept = default;
CorpusCanary::~CorpusCanary() = default;

}  // namespace taffy::test
