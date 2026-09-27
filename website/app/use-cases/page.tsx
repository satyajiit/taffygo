// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { ArrowLeft, ArrowUpRight } from "lucide-react";
import { WorkflowDemo } from "@/components/studio/WorkflowDemo";
import { PageSchema } from "@/components/studio/PageSchema";
import { withBasePath } from "@/lib/base-path";
import { pageMetadata } from "@/lib/site";

export const metadata = pageMetadata("/use-cases/");

export default function UseCasesPage() {
  return <>
    <PageSchema route="/use-cases/" />
    <div className="container-site route-hero"><a className="route-back" href={withBasePath("/")}><ArrowLeft size={16} /> TaffyGo</a><h1>Try a task with Taffy.</h1><p>These interactive demos show the planned document-download and form-filling flows. Watch the pages change, handle verification, and approve a submission. They use sample data, not live accounts.</p></div>
    <div className="container-site route-content"><h2 className="sr-only">Explore the workflows</h2><WorkflowDemo />
      <section className="route-notes" aria-label="More ways to browse with Taffy">
        <article><h2>Download a government document</h2><p>Find the service, open the document page, and pause for sign-in or CAPTCHA. After you hand back, the demo locates the record and offers a sample PDF to download.</p></article>
        <article><h2>Fill a banking form</h2><p>Use selected details from your Library, fill the form, and prepare a photo below 200 KB. This demo actually resizes its sample photo in your browser, then waits for your approval before showing submission.</p></article>
        <article><h2>Ask about a page</h2><p>Ask Taffy a question about the page you are reading. Check the linked sources alongside the answer.</p><a className="text-link" href={withBasePath("/built-for-phones/")}>See real app screens <ArrowUpRight size={16} /></a></article>
        <article><h2>Research across pages</h2><p>Give Taffy an errand across a few sites. Watch its tabs and progress, then inspect the result and its sources. You can pause, stop, or take over at any point.</p><a className="text-link" href={withBasePath("/product/")}>Follow an errand in the app <ArrowUpRight size={16} /></a></article>
        <article><h2>Does Taffy bypass CAPTCHA?</h2><p>No. Taffy pauses at a CAPTCHA, sign-in, or sensitive field. You complete that step on the website and choose when to hand back.</p></article>
        <article><h2>Does Taffy submit forms without asking?</h2><p>No. The form example includes a review and explicit approval before submission. Approval for a form does not authorize a payment or a different transaction.</p></article>
      </section>
      <p className="simulation-note">The complete government-document and banking flows have not yet been verified end to end on a phone.</p>
    </div>
  </>;
}
