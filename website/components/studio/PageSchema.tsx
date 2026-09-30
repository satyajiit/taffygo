// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { absoluteUrl, routeMeta, site, type RoutePath } from "@/lib/site";

export function PageSchema({ route }: { route: RoutePath }) {
  const page = routeMeta[route];
  const data = {
    "@context": "https://schema.org",
    "@graph": [
      { "@type": "WebPage", "@id": absoluteUrl(`${route}#page`), url: absoluteUrl(route), name: page.title, description: page.description, isPartOf: { "@id": `${site.url}/#website` }, about: { "@id": `${site.url}/#app` }, inLanguage: "en" },
      { "@type": "BreadcrumbList", itemListElement: [
        { "@type": "ListItem", position: 1, name: site.name, item: site.url },
        { "@type": "ListItem", position: 2, name: page.title.split(" | ")[0], item: absoluteUrl(route) },
      ] },
    ],
  };
  return <script type="application/ld+json" dangerouslySetInnerHTML={{ __html: JSON.stringify(data).replace(/</g, "\\u003c") }} />;
}
