// Copyright (c) 2026 Matterward Labs Private Limited.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { readFile, writeFile, mkdir } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { JSDOM } from 'jsdom';

const root = fileURLToPath(new URL('..', import.meta.url));
const ignored = new Set(['SCRIPT', 'STYLE', 'SVG', 'NAV', 'BUTTON', 'INPUT', 'SELECT']);
const blocks = new Set(['ARTICLE', 'SECTION', 'DIV', 'HEADER', 'FIGURE', 'FIGCAPTION', 'DETAILS', 'P', 'OL', 'UL', 'BLOCKQUOTE']);

/** Convert the static page, retaining headings, links, lists, and model names. */
export function pageToMarkdown(html, canonical) {
  const { document } = new JSDOM(html).window;
  const main = document.querySelector('main');
  if (!main?.querySelector('h1')) throw new Error(`No main heading in ${canonical}`);
  const heading = main.querySelector('h1');
  for (const br of heading.querySelectorAll('br')) br.replaceWith(' ');
  const title = heading.textContent.trim();
  heading.remove();
  const visit = node => {
    if (node.nodeType === 3) return node.textContent.replace(/\s+/g, ' ');
    if (node.nodeType !== 1 || ignored.has(node.tagName) || node.getAttribute('aria-hidden') === 'true') return '';
    if (node.tagName === 'IMG') return node.alt ? `\n\n![${node.alt}](${new URL(node.getAttribute('src'), canonical).href})\n\n` : '';
    if (node.tagName === 'BR') return ' ';
    const text = [...node.childNodes].map(visit).join('').trim();
    if (!text) return '';
    if (/^H[1-6]$/.test(node.tagName)) return `\n\n${'#'.repeat(Number(node.tagName[1]))} ${text}\n\n`;
    if (node.tagName === 'A' && node.hasAttribute('href')) return `[${text}](${new URL(node.getAttribute('href'), canonical).href}) `;
    if (node.tagName === 'LI') return node.querySelector('details') ? `\n\n${text}\n\n` : `\n- ${text.replace(/\n+/g, ' ')}\n`;
    if (node.tagName === 'SUMMARY') return `\n\n### ${text}\n\n`;
    if (node.tagName === 'CODE') return `\`${text}\` `;
    if (blocks.has(node.tagName)) return `\n\n${text}\n\n`;
    return ` ${text} `;
  };
  const text = visit(main).replace(/[ \t]{2,}/g, ' ').replace(/[ \t]+\n/g, '\n').replace(/\n{3,}/g, '\n\n').trim();
  return `# ${title}\n\n${text}\n\n---\nSource: ${canonical}\nGenerated from the statically rendered page.\nAI crawling, retrieval, and training policy: https://taffygo.com/ai-policy.txt\n`;
}

async function publish(path, contents) {
  for (const directory of ['public', 'out']) {
    const target = join(root, directory, path);
    await mkdir(dirname(target), { recursive: true });
    await writeFile(target, contents);
  }
}

async function main() {
  const sitemap = new JSDOM(await readFile(join(root, 'out/sitemap.xml'), 'utf8'), { contentType: 'text/xml' });
  const pages = [];
  for (const loc of sitemap.window.document.querySelectorAll('loc')) {
    const canonical = loc.textContent;
    const url = new URL(canonical);
    const html = await readFile(join(root, 'out', url.pathname, 'index.html'), 'utf8');
    const document = new JSDOM(html).window.document;
    const path = url.pathname === '/' ? 'index.md' : `${url.pathname.slice(1, -1)}.md`;
    const markdown = pageToMarkdown(html, canonical);
    await publish(path, markdown);
    pages.push({ title: document.title, description: document.querySelector('meta[name="description"]')?.content ?? '', url: canonical, markdown: new URL(path, url.origin).href });
  }
  const catalog = await readFile(join(root, 'lib/provider-directory.json'), 'utf8');
  await publish('providers.json', catalog);
  const intro = '# TaffyGo\n\n> Free, open-source AI-native Chromium browser for Android, with one assistant, Taffy. No account needed. Matterward Labs runs no server for the app.\n\nPublished by Matterward Labs Private Limited.\n';
  const resources = '\n## Machine-readable resources\n\n- [Public content and AI training permission](https://taffygo.com/ai-policy.txt)\n- [Machine-readable policy](https://taffygo.com/.well-known/ai-policy.json)\n- [Full provider and model catalog](https://taffygo.com/providers.json)\n- [Site summary](https://taffygo.com/llms-full.txt)\n\nAll canonical pages have a Markdown version. The HTML is statically generated and readable without JavaScript. Public first-party site content may be crawled, indexed, used for retrieval, and used for AI training. Third-party material retains its own terms.\n';
  const get = '\n## Get TaffyGo\n\n- [Google Play](https://play.google.com/store/apps/details?id=com.taffygo.browser)\n- [APK releases](https://github.com/satyajiit/taffygo/releases/latest)\n- [Source](https://github.com/satyajiit/taffygo)\n- [Report a bug](https://github.com/satyajiit/taffygo/issues)\n- [Discussions](https://github.com/satyajiit/taffygo/discussions)\n';
  await publish('llms.txt', intro + '\n## Pages\n\n' + pages.map(page => `- [${page.title}](${page.url}) — [Markdown](${page.markdown})`).join('\n') + '\n' + resources + get);
  await publish('llms-full.txt', intro + '\n' + pages.map(page => `## ${page.title}\n${page.url}\n${page.description}\nMarkdown: ${page.markdown}\n`).join('\n') + '\n## Product boundaries\n\nThe Android build includes an embedded Python worker for registered document and spreadsheet tools; its phone-level execution checks remain pending. Government-document and banking demos use sample data and are not verified end-to-end phone runs. Model catalog inclusion does not guarantee provider access. AI requests go directly to the chosen provider and may incur provider charges.\n\nCrawling, retrieval, and training on first-party public site content are allowed: https://taffygo.com/ai-policy.txt\n' + get);
  await publish('markdown-index.json', JSON.stringify({ pages }, null, 2) + '\n');
  console.log(`Exported ${pages.length} Markdown pages, model catalog, and AI discovery files.`);
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) await main();
