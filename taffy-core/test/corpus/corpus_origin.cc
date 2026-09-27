// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/corpus/corpus_origin.h"

namespace taffy::test {

CorpusOrigin::CorpusOrigin() = default;
CorpusOrigin::CorpusOrigin(const CorpusOrigin&) = default;
CorpusOrigin::CorpusOrigin(CorpusOrigin&&) noexcept = default;
CorpusOrigin& CorpusOrigin::operator=(const CorpusOrigin&) = default;
CorpusOrigin& CorpusOrigin::operator=(CorpusOrigin&&) noexcept = default;
CorpusOrigin::~CorpusOrigin() = default;

}  // namespace taffy::test
