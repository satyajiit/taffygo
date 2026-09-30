// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import type { Metadata, Viewport } from "next";
import localFont from "next/font/local";
import type { ReactNode } from "react";
import { Footer } from "@/components/Footer";
import { Header } from "@/components/Header";
import { SkipLink } from "@/components/SkipLink";
import { ServiceWorkerRegistration } from "@/components/ServiceWorkerRegistration";
import { appTheme } from "@/lib/app-theme";
import { absoluteUrl, links, site } from "@/lib/site";
import "../tailwind.css";

/**
 * Space Grotesk, self-hosted from the committed Latin subset. next/font
 * copies it under _next/static, so it follows the base path, is preloaded,
 * and gets a metric-matched fallback while it loads. The licence stays at
 * public/fonts/OFL.txt beside the source file.
 */
const grotesk = localFont({
  src: "../public/fonts/space-grotesk-latin-variable.woff2",
  weight: "300 700",
  style: "normal",
  display: "swap",
  variable: "--font-grotesk",
  adjustFontFallback: "Arial",
});

const instrument = localFont({
  src: "../public/fonts/instrument-serif-latin.woff2",
  weight: "400",
  style: "normal",
  display: "swap",
  variable: "--font-instrument",
  preload: false,
});

export const metadata: Metadata = {
  metadataBase: new URL(site.url),
  title: {
    default: site.title,
    template: `%s | ${site.name}`,
  },
  description: site.description,
  applicationName: site.name,
  creator: site.publisher,
  publisher: site.publisher,
  authors: [{ name: site.publisher, url: site.url }],
  category: "technology",
  keywords: [
    "AI browser for Android",
    "AI-native Android browser",
    "open source AI browser",
    "Android browser with ad blocker",
    "browser with your own AI provider",
    "Rust browser core",
    "on-device Python browser",
    "Chromium browser",
    "ad blocker",
    "open source browser",
    "AI assistant",
    "TaffyGo",
  ],
  alternates: { canonical: "/" },
  openGraph: {
    type: "website",
    siteName: site.name,
    title: site.title,
    description: site.description,
    url: site.url,
    locale: "en_IN",
    images: [{ url: site.ogImage, width: 1200, height: 630, alt: site.ogImageAlt }],
  },
  twitter: {
    card: "summary_large_image",
    title: site.title,
    description: site.description,
    images: [{ url: site.ogImage, width: 1200, height: 630, alt: site.ogImageAlt }],
  },
  robots: { index: true, follow: true, googleBot: { index: true, follow: true, "max-image-preview": "large", "max-snippet": -1, "max-video-preview": -1 } },
  formatDetection: { email: false, address: false, telephone: false },
};

export const viewport: Viewport = {
  themeColor: [
    { media: "(prefers-color-scheme: light)", color: appTheme.lightSurface },
    { media: "(prefers-color-scheme: dark)", color: appTheme.darkSurface },
  ],
  colorScheme: "light dark",
};

/**
 * Resolves the theme before first paint: a stored choice wins, then the OS
 * preference, then light. It must be a plain inline <script> rendered as the
 * first child of <body>: next/script with strategy="beforeInteractive" is
 * never emitted into the prerendered HTML of a static export (it survives
 * only in the client flight payload), so it cannot set data-taffy-theme
 * before paint. With JavaScript off, data-taffy-theme stays unset and the
 * generated token projection plus the site's no-JS media rules follow the
 * operating-system preference.
 */
const themeInit = `(function(){var t="light";try{t=window.matchMedia("(prefers-color-scheme: dark)").matches?"dark":"light";}catch(e){}try{var s=window.localStorage.getItem("taffygo-theme");if(s==="light"||s==="dark")t=s;}catch(e){}document.documentElement.dataset.taffyTheme=t;})();`;

/**
 * What a search engine may say about TaffyGo: a free, open-source Android
 * app from Matterward Labs. Addresses name the canonical origin whatever
 * base path this build is served under.
 */
const structuredData = {
  "@context": "https://schema.org",
  "@graph": [
    { "@type": "WebSite", "@id": `${site.url}/#website`, url: site.url, name: site.name, inLanguage: "en", publisher: { "@id": `${site.url}/#organization` } },
    {
      "@type": "Organization",
      "@id": `${site.url}/#organization`,
      name: site.publisher,
      url: "https://matterwardlabs.com",
      sameAs: ["https://matterwardlabs.com"],
      logo: absoluteUrl("/brand/matterward-labs.svg"),
    },
    {
      "@type": "SoftwareApplication",
      "@id": `${site.url}/#app`,
      name: site.name,
      applicationCategory: "BrowserApplication",
      operatingSystem: "Android 10 or later",
      description: site.description,
      url: site.url,
      image: absoluteUrl(site.ogImage),
      installUrl: links.googlePlay,
      downloadUrl: links.releases,
      license: "https://www.mozilla.org/MPL/2.0/",
      isAccessibleForFree: true,
      sameAs: [links.repository, links.googlePlay],
      featureList: ["Built-in ad and tracker blocking", "Bring your own AI provider", "Page questions with sources", "User-controlled browsing tasks"],
      offers: { "@type": "Offer", price: "0", priceCurrency: "INR" },
      author: { "@id": `${site.url}/#organization` },
    },
  ],
};

export default function RootLayout({ children }: { children: ReactNode }) {
  return (
    // suppressHydrationWarning: the inline script sets data-taffy-theme on
    // <html> before hydration, which would otherwise trip a mismatch warning.
    <html lang="en" className={`${grotesk.variable} ${instrument.variable}`} suppressHydrationWarning>
      <body className="min-h-dvh">
        <script
          id="taffygo-theme-init"
          // Static, author-written string; no user input reaches it.
          dangerouslySetInnerHTML={{ __html: themeInit }}
        />
        <script
          type="application/ld+json"
          // Static, author-written object; no user input reaches this string.
          dangerouslySetInnerHTML={{ __html: JSON.stringify(structuredData) }}
        />
        <SkipLink />
        <Header />
        <main id="main">{children}</main>
        <Footer />
        <ServiceWorkerRegistration />
      </body>
    </html>
  );
}
