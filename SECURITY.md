# Security policy

## Report a vulnerability privately

Use GitHub's private vulnerability reporting:
<https://github.com/satyajiit/taffygo/security/advisories/new>. Only you and
the maintainer can see the report.

Do not open a public issue, discussion or pull request about a vulnerability,
and do not describe it anywhere public before a fix is released.

A useful report says:

- what an attacker can do, and what they need first (a page the person visits,
  a malicious app on the phone, a network position);
- the steps to reproduce it, and a proof of concept if you have one;
- the TaffyGo version (Settings, then About and help, then About), whether it
  came from Google Play, a GitHub release or your own build, and the phone
  model and Android version.

Leave out real API keys, passwords and other people's data. If a key was
exposed while you tested, revoke it at the provider.

## What happens next

TaffyGo has one maintainer. You get a reply in the advisory thread, and the
fix is worked out there with you. There is no fixed response time yet and no
bug bounty. A fix ships as a new release on GitHub and Google Play, and the
advisory is published after that release, crediting you if you want the
credit.

## Supported versions

| Version | Security fixes |
|---|---|
| The latest 1.x release | Yes |
| Anything older | No: update to the latest release |

TaffyGo is built on a pinned Chromium release, 152.0.7977.42 for TaffyGo 1.0.
An upstream Chromium fix reaches TaffyGo when the pin moves to a Chromium
release that carries it, or when the fix is cherry-picked onto the pin under
`chromium/patches/security/`. A bug in Chromium itself, which TaffyGo did not
change, goes to the Chromium project:
<https://www.chromium.org/Home/chromium-security/reporting-security-bugs/>.

## Scope

In scope:

- TaffyGo's own code in this repository: `taffy-core/`, the patch queue under
  `chromium/patches/`, `tools/` and `website/`;
- the APK attached to a GitHub release, and the app on Google Play;
- the website at <https://taffygo.com>.

These are the promises the product makes, and a way around any of them is a
vulnerability:

- Taffy acts only within the approvals it asks for. It needs your OK before it
  opens more tabs, a form fill uses the exact values you approved once and
  submits nothing, and Take over, Pause and Stop work at any moment.
- Credentials, one-time codes, payment details and passkeys never go into
  anything Taffy sends to an AI provider.
- A provider key goes only to the provider it was saved for, and the phone's
  keystore seals it.
- A web page cannot give itself authority. Text on a page that tries to steer
  Taffy is in scope when it gets Taffy past an approval, onto a site the
  task did not allow, or to send data somewhere the person did not choose.
- An encrypted backup cannot be read without its recovery key, and a file
  that was changed is refused.
- TaffyGo sends no page content, prompts, history, passwords, cookies or keys
  to any server of its own. It runs none.

Out of scope:

- bugs in upstream Chromium that TaffyGo did not change (report them to
  Chromium, as above);
- the AI provider's own service, and answers that are wrong or unhelpful
  without crossing one of the promises above (open an ordinary issue for
  those);
- attacks that need a rooted phone, a compromised operating system, or an
  unlocked phone in someone else's hands;
- an ad or tracker that gets through. The rules come from EasyList and
  EasyPrivacy, so report a missed one as an ordinary issue.

## Please do not

- test against phones, accounts, keys or services that are not yours;
- open, change or keep anyone else's data. If you reach some, stop and say so
  in the report;
- run load or denial-of-service tests against taffygo.com, GitHub or any AI
  provider;
- use social engineering or phishing against anyone involved with the
  project.
