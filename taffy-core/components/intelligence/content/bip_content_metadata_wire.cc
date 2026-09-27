// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/bip_content_metadata_wire.h"

#include "taffy/components/intelligence/content/bip_graph_payload.h"

namespace taffy::bip_payload {
namespace {

std::optional<uint8_t> ContentSignalWireValue(mojom::ContentSignal signal) {
  switch (signal) {
    case mojom::ContentSignal::kHiddenByStyle:
    case mojom::ContentSignal::kZeroWidthCharacters:
    case mojom::ContentSignal::kBidiControlCharacters:
    case mojom::ContentSignal::kEncodedBlob:
    case mojom::ContentSignal::kImperativeInstructionShape:
    case mojom::ContentSignal::kLanguageMismatch:
    case mojom::ContentSignal::kCrossOriginFrameAuthored:
      return static_cast<uint8_t>(signal);
  }
  return std::nullopt;
}

}  // namespace

std::optional<uint8_t> RendererAuthorship(
    std::optional<mojom::ContentTrust> trust) {
  if (!trust.has_value()) {
    return static_cast<uint8_t>(mojom::ContentTrust::kUnknownUntrusted);
  }
  switch (*trust) {
    case mojom::ContentTrust::kFirstPartyDocument:
    case mojom::ContentTrust::kUserGeneratedContent:
    case mojom::ContentTrust::kThirdPartyEmbedded:
    case mojom::ContentTrust::kUnknownUntrusted:
      return static_cast<uint8_t>(*trust);
    case mojom::ContentTrust::kUserAuthored:
    case mojom::ContentTrust::kTaffyAuthored:
    case mojom::ContentTrust::kModelAuthored:
      return std::nullopt;
  }
  return std::nullopt;
}

bool ContentSignalsAreCanonical(
    const std::vector<mojom::ContentSignal>& signals) {
  if (signals.size() > kMaxBipContentSignals) {
    return false;
  }
  std::optional<uint8_t> previous;
  for (mojom::ContentSignal signal : signals) {
    const std::optional<uint8_t> value = ContentSignalWireValue(signal);
    if (!value.has_value() || (previous.has_value() && *value <= *previous)) {
      return false;
    }
    previous = *value;
  }
  return true;
}

void WriteContentSignals(Writer& out,
                         const std::vector<mojom::ContentSignal>& signals) {
  out.Count(signals.size());
  for (mojom::ContentSignal signal : signals) {
    // ContentSignalsAreCanonical checked this exact list before any caller
    // reaches this function.
    out.U8(*ContentSignalWireValue(signal));
  }
}

}  // namespace taffy::bip_payload
