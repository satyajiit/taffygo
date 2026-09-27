// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { PageHead } from "@/components/PageHead";
import { PhoneFrame } from "@/components/PhoneFrame";
import { StoreLinks } from "@/components/StoreLinks";
import { siteHref } from "@/lib/base-path";
import { product } from "@/lib/content/product";
import { PageSchema } from "@/components/studio/PageSchema";
import { pageMetadata } from "@/lib/site";

export const metadata = pageMetadata("/product/");

/** One errand from the request to the result, on three real screens. */
export default function ProductPage() {
  return (
    <>
      <PageSchema route="/product/" />
      <PageHead
        id="product-title"
        title={product.title}
        lede={product.lede}
        art={{ name: "section-asks-first", alt: product.imageAlt }}
      />

      <section
        aria-labelledby="steps-title"
        className="tour py-[var(--space-3xl)]"
      >
        <div className="container-site">
          <h2 id="steps-title" className="type-h2 max-w-[20ch]">
            {product.stepsTitle}
          </h2>
          <ol className="steps mt-[var(--space-2xl)]">
            {product.steps.map((step) => (
              <li key={step.id}>
                <div>
                  <span className="step-number type-h2 block text-secondary" aria-hidden="true" />
                  <h3 className="type-h3 mt-2">{step.title}</h3>
                  <p className="mt-3 text-secondary">{step.body}</p>
                </div>
                <PhoneFrame
                  screen={step.screen}
                  sizes="(min-width: 60rem) 18rem, 80vw"
                />
              </li>
            ))}
          </ol>
          <a href={siteHref(product.providersLink.href)} className="link-arrow mt-[var(--space-2xl)]">
            {product.providersLink.label}
          </a>
        </div>
      </section>

      <section aria-labelledby="closing-title" className="container-site py-[var(--space-3xl)]">
        <h2 id="closing-title" className="type-h2">
          {product.closing.title}
        </h2>
        <p className="type-lead mt-4 max-w-[36rem] text-secondary">{product.closing.body}</p>
        <StoreLinks className="mt-8" />
      </section>
    </>
  );
}
