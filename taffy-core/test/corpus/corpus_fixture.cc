// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/corpus/corpus_fixture.h"

namespace taffy::test {

CorpusFixture::CorpusFixture() = default;
CorpusFixture::CorpusFixture(const CorpusFixture&) = default;
CorpusFixture::CorpusFixture(CorpusFixture&&) noexcept = default;
CorpusFixture& CorpusFixture::operator=(const CorpusFixture&) = default;
CorpusFixture& CorpusFixture::operator=(CorpusFixture&&) noexcept = default;
CorpusFixture::~CorpusFixture() = default;

}  // namespace taffy::test
