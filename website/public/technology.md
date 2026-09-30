# Inside TaffyGo.

[TaffyGo](https://taffygo.com/)

A native Android interface, Chromium for web pages, Rust for tasks, and embedded Python for selected local tools. Explore how they fit together.

## Chromium. Rust. Python on your phone.

Web pages, task logic, and local tools each have their own process. Select a layer to see its job.

### Task logic in a separate process

Rust runs task planning, permission checks, and model routing inside a sandboxed service. The browser handles access to the web and Android.

[Read about the architecture](https://taffygo.com/technology/)

- 1 Your request

- 2 Permission check

- 3 Browser action

- 4 Result returned

## Chromium

Tabs, pages, navigation, and downloads rest on Chromium. You can use TaffyGo as a full manual browser without connecting an AI provider.

## A sandboxed Rust core

Portable task, policy, and model-routing logic lives in Rust. It runs as a sandboxed core service, separate from the browser process. The source makes those boundaries inspectable.

![Python](https://taffygo.com/providers/python.svg)

## Python, built into the app

The Android build includes CPython in a separate tool process. Registered document and spreadsheet builders accept structured data and return file bytes. No separate Python installation is needed.

The worker has limits for execution time, memory, and output. It runs registered tools; it does not accept arbitrary scripts from a page or a model. The worker compiles into the Android build, with phone-level execution and isolation checks still pending.

## Built-in request blocking

Built-in ad and tracker blocking stops matching requests. The filter engine checks requests before the browser loads them. Per-site controls let you turn blocking off when a page needs it.

## Native Android controls

Compose owns TaffyGo’s Android surfaces and trusted platform interactions. Back, forward, Ask Taffy, and tabs sit within reach at the bottom of your phone.

## Direct provider connections

Matterward Labs runs no server for TaffyGo. When you choose to use AI, the request and the page text it needs go to the provider you connected. Ordinary websites still receive your browsing requests.

## Open-source code

The first-party source is licensed under the Mozilla Public License 2.0. The TaffyGo name, logos, and character are reserved separately. Third-party components keep their own licences.

[Explore the source on GitHub](https://github.com/satyajiit/taffygo)

---
Source: https://taffygo.com/technology/
Generated from the statically rendered page.
AI crawling, retrieval, and training policy: https://taffygo.com/ai-policy.txt
