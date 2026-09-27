// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import type { Metadata } from "next";
import { withBasePath } from "@/lib/base-path";
import { moved } from "@/lib/content";
import { redirects, routeMeta } from "@/lib/site";

const target = redirects["/privacy-policy/"];

/**
 * The old privacy address, kept as a static page because GitHub Pages has
 * no server redirects. A refresh sends the browser on at once; the link is
 * there for anything that ignores the refresh. Search engines are told not
 * to index it and are pointed at the policy's own address.
 */
export const metadata: Metadata = {
  title: { absolute: routeMeta[target].title },
  robots: { index: false, follow: true },
  alternates: { canonical: target },
};

export default function PrivacyPolicyRedirect() {
  const href = withBasePath(target);
  return (
    <>
      <meta httpEquiv="refresh" content={`0; url=${href}`} />
      <div className="container-site pt-[var(--space-xl)] pb-[var(--space-3xl)]">
        <h1 className="type-h2">{moved.title}</h1>
        <a href={href} className="link-arrow mt-4">
          {moved.linkLabel}
        </a>
      </div>
    </>
  );
}
