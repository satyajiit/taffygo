// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { withBasePath } from "@/lib/base-path";
import { getTaffy } from "@/lib/content";

/**
 * The two ways to install TaffyGo, side by side: the official Google Play
 * badge, never redrawn, and the APK from GitHub Releases. `withSource` adds
 * the link to the source code underneath.
 */
export function StoreLinks({
  withSource = false,
  className = "",
}: {
  withSource?: boolean;
  className?: string;
}) {
  return (
    <div className={className}>
      <div className="flex flex-wrap items-center gap-x-4 gap-y-3">
        <a href={getTaffy.playHref} className="play-badge">
          <img
            src={withBasePath("/brand/google-play-badge.webp")}
            width={646}
            height={250}
            alt={getTaffy.playBadgeAlt}
            decoding="async"
          />
        </a>
        <a href={getTaffy.apkHref} className="btn btn-line">
          {getTaffy.apkLabel}
        </a>
      </div>
      <p className="type-small mt-3 text-secondary">{getTaffy.apkNote}</p>
      {withSource ? (
        <a href={getTaffy.sourceHref} className="link-arrow mt-2">
          {getTaffy.sourceLabel}
        </a>
      ) : null}
    </div>
  );
}
