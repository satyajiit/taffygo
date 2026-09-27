# Evidence index

`./tools/evidence` is the single assembler and verifier for the evidence set
selected for one source revision. It is an index, not a test runner and not an
approval mechanism.

Each producing job writes a fragment with exactly two top-level fields:

```json
{
  "source_revision": "40-or-64-character-git-revision",
  "records": []
}
```

Every record names the requirement it observed, its result, owner, command,
environment and UTC interval, then links at least one staged file by relative
path, byte length and SHA-256. A milestone-exit record additionally lists the
record ids the owner reviewed in `supports`.

The normalizer writes that shape and hashes the observed file, so producing
jobs do not each grow their own JSON dialect:

```bash
./tools/evidence fragment --out test-results/evidence/device.fragment.json \
  --artifacts-dir test-results/evidence --id m8-device-suite \
  --class EV-04 --milestone M8 --kind test --result pass \
  --required-for-candidate --owner "release owner" \
  --observed-command "./tools/check device" \
  --started-at 2026-09-04T01:00:00Z --completed-at 2026-09-04T01:20:00Z \
  --environment-kind device --environment-id matrix-device-01 \
  --requirement PAR-REL-001 --evidence test-results/evidence/device.json
```

```bash
./tools/evidence assemble --profile development --out test-results/evidence/index.json \
  --fragment test-results/evidence/device.fragment.json
./tools/evidence verify test-results/evidence/index.json --root .
./tools/evidence verify artifacts/release/evidence-index.json \
  --artifacts-dir artifacts/release --root . --candidate
```

Development indexes may be incomplete and report why. Candidate verification
is closed until all nine M0–M8 exit records pass, each cites passing evidence,
each has direct non-exit support from its own milestone, all selected candidate
records pass, the source was clean, and the generated capability authority says
M8 is accepted. The release manifest then hashes the
same index as an `evidence-index` artifact. Candidate readiness additionally
requires delegated task start at task milestone M8 on policy milestone M7, so
neither the current task-disabled M0 candidate nor a crippled M8 profile can be
hand-marked ready. Neither document can substitute for the other.
