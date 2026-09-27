// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PAGE_MEDIA_OBSERVATION_H_
#define TAFFY_BROWSER_PAGE_MEDIA_OBSERVATION_H_

#include <stdint.h>

#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/ref_counted.h"
#include "base/time/time.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace taffy {

class ProfilePageMediaStore;

enum class PageMediaObservationKind : uint8_t {
  kImage = 0,
  kVideo = 1,
  kPdf = 2,
  kPageScreenshot = 3,
};

// The complete browser-owned binding for one media read. It deliberately
// carries no selector, address, media bytes, or renderer object. Exact tab,
// frame, document, revision and node identity must all still resolve when the
// asynchronous read begins and when it completes.
struct PageMediaObservationRequest {
  std::string task_id;
  std::string effect_id;
  uint64_t service_generation = 0u;
  std::string tab_id;
  std::string frame_id;
  std::string page_epoch;
  uint64_t minimum_graph_revision = 0u;
  PageMediaObservationKind kind = PageMediaObservationKind::kImage;
  std::optional<std::string> node_id;
  base::TimeDelta deadline;
};

using PageMediaObservationCompletion =
    base::OnceCallback<void(core_service::mojom::MediaObservationResultPtr,
                            uint32_t suppressed_secret_value_count)>;

// Starts one bounded, asynchronous media read and returns its cancellation
// closure. Image/video/page-fallback pixels remain in the browser and cross
// the Core Service seam only as a one-use opaque handle. The page fallback is
// additionally bounded, black-redacted and provenance-tagged. PDF bytes are
// never requested; native text is read a page at a time from the platform's
// already-loaded native PDF document, rescanned, bounded, and returned as
// typed facts with source offsets.
base::OnceClosure StartPageMediaObservation(
    content::BrowserContext* browser_context,
    PageMediaObservationRequest request,
    scoped_refptr<ProfilePageMediaStore> media_store,
    PageMediaObservationCompletion completion);

}  // namespace taffy

#endif  // TAFFY_BROWSER_PAGE_MEDIA_OBSERVATION_H_
