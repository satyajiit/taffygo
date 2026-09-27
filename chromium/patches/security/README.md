# Urgent security cherry-picks

**Authority:** Chromium fork and build strategy §1.4

Patches here exist **only** between an urgent upstream security fix and the
next milestone rebase. They are applied after the numbered patches in
`../`, in filename order.

The fast path, for an actively exploited critical fix — do **not** rebase the
milestone:

1. Cherry-pick the upstream commit(s) onto the current pinned revision and
   export them here as `NNNN-short-name.patch`.
2. Increment `level` in [`../../SECURITY_PATCH_LEVEL`](../../SECURITY_PATCH_LEVEL)
   and record the upstream advisory references in `advisories`.
3. Run the Chromium lane and the release-candidate lane against the existing
   baseline; sign and promote.
4. Delete these patches at the next milestone rebase — the fix arrives in the
   new pinned revision — and reset `level` to 0.

Each patch header must cite the upstream advisory or bug reference. Timed
drills of this path start at M1 and calibrate the candidate objective
(OD-052) before any service target is ratified.
