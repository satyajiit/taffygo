#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""What a published asset variant claims, and whether the claim is true.

A catalog variant marked `published` tells every device that the bytes are at
a URL, that they are exactly this many, and that they hash to exactly this.
Nothing static can check that: the generator refuses a digest on an
*unpublished* variant, which stops a placeholder becoming a pin, but a
`published` row naming an object nobody uploaded generates cleanly and fails
on a phone.

This module is the other half. It derives the URL from the catalog rather than
from an argument, fetches it, and compares what came back against what the row
promised. `./tools/assets` is the command around it.

It reads a credential for nothing, and there is no longer an upload half to
read one for: `verify` is an anonymous GET and that is the whole of what this
module does over the network.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import secrets
import sys
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CATALOG = os.path.join(
    ROOT, "taffy-core/components/delivery/core/rust/asset-plane/catalog/source/assets.json")
ORIGIN_GNI = os.path.join(ROOT, "taffy-core/browser/asset_delivery_configuration.gni")

#: The build argument that names the delivery host. One place, so a tool cannot
#: verify against a different origin from the one the product fetches from.
_ORIGIN = re.compile(r'^\s*taffy_asset_origin\s*=\s*"([^"]*)"', re.MULTILINE)

#: How long an anonymous GET may take before the answer is "it did not answer",
#: which is a different finding from "it answered wrongly" and is reported as
#: one.
TIMEOUT_SECONDS = 60


class Refusal(Exception):
    """Something was asked for that this module will not answer."""


def read_origin(path: str = ORIGIN_GNI) -> str:
    with open(path, encoding="utf-8") as handle:
        found = _ORIGIN.search(handle.read())
    if not found:
        raise Refusal(f"{path}: no taffy_asset_origin")
    return found.group(1)


def read_catalog(path: str = CATALOG) -> dict:
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def variants(catalog: dict) -> list[tuple[dict, dict]]:
    """Every (asset, variant) pair in the catalog, in the order it is written."""
    return [(asset, variant) for asset in catalog["assets"] for variant in asset["variants"]]


def select(catalog: dict, asset_id: str, platform: str) -> tuple[dict, dict]:
    for asset, variant in variants(catalog):
        if asset["id"] == asset_id and variant["platform"] == platform:
            return asset, variant
    known = sorted({asset["id"] for asset in catalog["assets"]})
    raise Refusal(f"{asset_id} on {platform}: no such variant (assets: {', '.join(known)})")


def key_for(variant: dict, override: str | None) -> str:
    """The object key, from the row when it has one and from the caller when not.

    A published row's path is authority and an override may only agree with it:
    a key typed by hand that differs from the row is a key the device will
    never ask for. An unpublished row names no path -- that is the state a
    first publication starts from -- so there the override is the only source
    and is required.
    """
    path = variant.get("path")
    if path and override and override != path:
        raise Refusal(f"the row names {path} and --key says {override}; they must agree")
    if path:
        return path
    if override:
        return override
    raise Refusal(
        "the row names no path, which is what an unpublished variant looks like before "
        "its first publication. Pass --key with the path the builder printed.")


def url_for(origin: str, key: str) -> str:
    if not origin:
        raise Refusal(
            "taffy_asset_origin is empty, so there is no host to verify against. That "
            "is the shipped state (decision 0202): the parts a person needs are in the "
            "package and are installed from there, so no origin serves them. Run "
            "`./tools/assets bundle` for what the package carries, and "
            "`./tools/assets bundle --check` to hold those bytes to the catalog. This "
            "command works again the day a build configures an origin.")
    return f"{origin.rstrip('/')}/{key}"


def digest_of(path: str) -> tuple[str, int]:
    sha = hashlib.sha256()
    size = 0
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            sha.update(block)
            size += len(block)
    return sha.hexdigest(), size


def artifact_findings(variant: dict, digest: str, size: int) -> list[str]:
    """Whether a local file is the one the row promises."""
    findings = []
    if variant.get("digest") and variant["digest"] != digest:
        findings.append(
            f"the row names digest {variant['digest']} and the file hashes to {digest}")
    if variant.get("transfer_bytes") and variant["transfer_bytes"] != size:
        findings.append(
            f"the row names {variant['transfer_bytes']} transfer bytes and the file is {size}")
    return findings


def _request(url: str, method: str, timeout: int) -> tuple[int, bytes]:
    request = urllib.request.Request(url, method=method,
                                     headers={"User-Agent": "taffy-assets/1",
                                              "Cache-Control": "no-cache"})
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return response.status, response.read()
    except urllib.error.HTTPError as error:
        return error.code, b""
    except (urllib.error.URLError, TimeoutError) as error:
        raise Refusal(f"{url}: the origin did not answer ({error})") from error


def uncached(url: str, nonce: str) -> str:
    """The same object, at a URL no shared cache has an answer for.

    A CDN caches a negative response, and Cloudflare's default for a missing R2
    object is four hours. Checking the plain URL therefore measures the edge's
    memory of the last check rather than the object -- and the first real run of
    this command hit exactly that, reporting 404 for bytes it had just uploaded
    successfully, because an earlier probe had populated the miss. A nonce in
    the query string changes the cache key, so what comes back is the object.
    """
    separator = "&" if "?" in url else "?"
    return f"{url}{separator}taffy-verify={nonce}"


def fetch(url: str, timeout: int = TIMEOUT_SECONDS) -> tuple[int, bytes]:
    """The status and body of an anonymous GET. A refusal is a status, not a raise."""
    return _request(url, "GET", timeout)


def head(url: str, timeout: int = TIMEOUT_SECONDS) -> int:
    """The status of the plain URL, which is what a device would be served."""
    return _request(url, "HEAD", timeout)[0]


def edge_findings(url: str, plain_status: int, object_status: int) -> list[str]:
    """Whether the object and what the edge serves for its URL disagree."""
    if object_status == 200 and plain_status != 200:
        return [
            f"the object is present, and the edge is serving {plain_status} for its URL "
            f"from cache. A device would be refused until that expires. Purge {url} in the "
            "Cloudflare dashboard, or wait out its max-age"
        ]
    return []


def response_findings(variant: dict, status: int, payload: bytes) -> list[str]:
    """Whether what the origin served is what the row promised."""
    if status != 200:
        return [f"the origin answered {status}, so the row promises bytes nobody uploaded"]
    findings = []
    digest = hashlib.sha256(payload).hexdigest()
    if variant.get("digest") != digest:
        findings.append(
            f"the origin served {len(payload)} bytes hashing to {digest}, and the row "
            f"names {variant.get('digest')}")
    if variant.get("transfer_bytes") != len(payload):
        findings.append(
            f"the origin served {len(payload)} bytes and the row names "
            f"{variant.get('transfer_bytes')}")
    return findings


def verify(origin: str, asset: dict, variant: dict, key: str | None = None,
           expect: dict | None = None) -> dict:
    """One variant's verdict. `unpublished` is a state, never a failure.

    `expect` is what a just-finished upload measured, and is what a first
    publication is checked against -- the row cannot be the authority for bytes
    it does not yet describe.
    """
    row = {"asset": asset["id"], "platform": variant["platform"],
           "publication": variant.get("publication")}
    if variant.get("publication") != "published" and expect is None:
        row["verdict"] = "unpublished"
        row["findings"] = []
        return row
    promised = expect if expect is not None else variant
    url = url_for(origin, key_for(variant, key))
    row["url"] = url
    status, payload = fetch(uncached(url, secrets.token_hex(8)))
    row["status"] = status
    findings = response_findings(promised, status, payload)
    plain = head(url) if status == 200 else status
    row["edge_status"] = plain
    findings += edge_findings(url, plain, status)
    row["findings"] = findings
    row["verdict"] = "ok" if not findings else "wrong"
    return row


def _self_test() -> int:
    checks: list[tuple[str, bool]] = []

    def check(name: str, condition: bool) -> None:
        checks.append((name, condition))

    payload = b"taffy"
    digest = hashlib.sha256(payload).hexdigest()
    good = {"platform": "android-arm64", "publication": "published",
            "path": "a/b/c.zip", "transfer_bytes": len(payload), "digest": digest}

    check("a matching response passes", not response_findings(good, 200, payload))
    check("a 404 is a finding", bool(response_findings(good, 404, b"")))
    check("a 404's finding says nobody uploaded",
          "nobody uploaded" in response_findings(good, 404, b"")[0])
    check("a wrong digest is a finding", bool(response_findings(good, 200, b"other")))
    check("a short body is two findings", len(response_findings(good, 200, b"taff")) == 2)
    check("a row with no digest is a finding",
          bool(response_findings({**good, "digest": None}, 200, payload)))

    check("a url is built from the origin and the key",
          url_for("https://assets.example", "a/b/c.zip") == "https://assets.example/a/b/c.zip")
    check("a trailing slash on the origin does not double",
          url_for("https://assets.example/", "a/b/c.zip") == "https://assets.example/a/b/c.zip")
    try:
        url_for("", "a/b/c.zip")
        check("an empty origin is refused", False)
    except Refusal:
        check("an empty origin is refused", True)

    check("a published row's path is the key", key_for(good, None) == "a/b/c.zip")
    check("an override that agrees is accepted", key_for(good, "a/b/c.zip") == "a/b/c.zip")
    check("a first publication takes the key from the caller",
          key_for({"platform": "p"}, "x/y.zip") == "x/y.zip")
    for label, variant, override in (
        ("an override that disagrees with the row is refused", good, "other/key.zip"),
        ("no path and no key is refused", {"platform": "p"}, None),
    ):
        try:
            key_for(variant, override)
            check(label, False)
        except Refusal:
            check(label, True)

    check("the verifying url carries a nonce",
          uncached("https://a.example/x.zip", "abcd") == "https://a.example/x.zip?taffy-verify=abcd")
    check("a nonce joins an existing query with &",
          uncached("https://a.example/x.zip?v=1", "ab").endswith("&taffy-verify=ab"))
    check("an object served on both urls passes",
          not edge_findings("https://a.example/x.zip", 200, 200))
    check("an object the edge still 404s for is a finding",
          bool(edge_findings("https://a.example/x.zip", 404, 200)))
    check("that finding names the cache, not the upload",
          "from cache" in edge_findings("https://a.example/x.zip", 404, 200)[0])
    check("an absent object raises no cache finding",
          not edge_findings("https://a.example/x.zip", 404, 404))

    check("a local file that matches passes", not artifact_findings(good, digest, len(payload)))
    check("a local file that does not is a finding",
          bool(artifact_findings(good, "0" * 64, len(payload))))

    catalog = read_catalog()
    check("the committed catalog parses", bool(catalog.get("assets")))
    check("every variant names a platform",
          all(variant.get("platform") for _asset, variant in variants(catalog)))
    try:
        select(catalog, "no-such-asset", "android-arm64")
        check("an unknown asset is refused", False)
    except Refusal:
        check("an unknown asset is refused", True)
    check("an unpublished variant is a state, not a failure",
          verify("https://assets.example", {"id": "x"},
                 {"platform": "p", "publication": "unpublished"})["verdict"] == "unpublished")
    check("the origin is read from the build argument", read_origin().startswith("https://")
          or read_origin() == "")

    failed = [name for name, passed in checks if not passed]
    for name, passed in checks:
        print(f"{'ok  ' if passed else 'FAIL'} {name}")
    print(f"{len(checks) - len(failed)}/{len(checks)} rules hold")
    return 1 if failed else 0


def bundle(catalog: dict) -> int:
    """What the package carries, from the catalog's own `bundled` block.

    A report and not a check. The checking is one mechanism and it lives with
    the rules — `bundled_rows.py` beside the asset-plane generator, run by the
    `catalog` lane — and a second copy here would be a second answer to one
    question. `./tools/assets bundle --check` runs that one.
    """
    rows = catalog.get("bundled") or []
    print(json.dumps({
        "rows": rows,
        "distinct_bytes": sum(int(row.get("bytes", 0)) for row in rows),
        "directory": "taffy-core/components/delivery/bundled",
        "provenance": "taffy-core/components/delivery/bundled/PROVENANCE.txt",
    }, indent=2, sort_keys=True))
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "command", choices=("plan", "confirm", "verify", "bundle", "self-test"))
    parser.add_argument("--asset")
    parser.add_argument("--platform")
    parser.add_argument("--file")
    parser.add_argument("--key", help="the object key, for a row that names none yet")
    parser.add_argument("--installed-bytes", type=int,
                        help="bytes on disk after install, when it differs from the transfer")
    args = parser.parse_args(argv)

    if args.command == "self-test":
        return _self_test()

    if args.command == "bundle":
        # Before `read_origin`, because what the package carries is a fact
        # about this tree and does not become unanswerable when no origin is
        # configured — which is now always.
        return bundle(read_catalog())

    origin = read_origin()
    catalog = read_catalog()

    if args.command == "plan":
        if not (args.asset and args.platform and args.file):
            parser.error("plan needs --asset, --platform and --file")
        asset, variant = select(catalog, args.asset, args.platform)
        digest, size = digest_of(args.file)
        findings = artifact_findings(variant, digest, size)
        key = key_for(variant, args.key)
        # The fragment is the row this artifact would justify, in the exact
        # shape the catalog holds, so filling a variant in is a paste rather
        # than four numbers copied by hand.
        fragment = {
            "platform": variant["platform"], "publication": "published", "path": key,
            "transfer_bytes": size,
            "installed_bytes": args.installed_bytes if args.installed_bytes else size,
            "digest": digest,
        }
        print(json.dumps({
            "asset": asset["id"], "revision": asset["revision"],
            "platform": variant["platform"], "publication": variant.get("publication"),
            "key": key, "url": url_for(origin, key),
            "findings": findings, "row": fragment,
        }, indent=2, sort_keys=True))
        return 1 if findings else 0

    if args.command == "confirm":
        # The plan arrives on stdin, so what is verified is exactly what was
        # uploaded rather than a row that has not been written yet.
        plan = json.load(sys.stdin)
        asset, variant = select(catalog, plan["asset"], plan["platform"])
        row = verify(origin, asset, variant, key=plan["key"], expect=plan["row"])
        print(json.dumps(row, indent=2, sort_keys=True))
        return 0 if row["verdict"] == "ok" else 1

    rows = []
    for asset, variant in variants(catalog):
        if args.asset and asset["id"] != args.asset:
            continue
        if args.platform and variant["platform"] != args.platform:
            continue
        rows.append(verify(origin, asset, variant, key=args.key))
    print(json.dumps({"origin": origin, "rows": rows}, indent=2, sort_keys=True))
    return 1 if any(row["verdict"] == "wrong" for row in rows) else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Refusal as refusal:
        print(f"taffy: {refusal}", file=sys.stderr)
        sys.exit(2)
