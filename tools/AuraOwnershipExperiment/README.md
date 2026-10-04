# AuraOwnershipExperiment — separate, blocked Gate A candidate

No arguments = DRY RUN: current-user metadata, canonical COM activation/SDK2 QI and eleven
Enumerate/Count observations. Never ownership. Only a separately approved reviewed revision could
enable the exact `--execute-gate-a` flag. The delivered compiled gate is **closed**.

Reserve zero is established, but vendor release internally clears colors/Apply on a nonempty cache;
MTA dry run returned Count=3. See [analysis](../../docs/research/aura-ownership-callsite-analysis.md)
and [runbook](../../docs/testing/AURA_GATE_A_RUNBOOK.md). Do not run ownership or flip a boolean.

```powershell
cmake -S tools/AuraOwnershipExperiment -B build/aura-gate-a -A x64 -DAURA_GATE_A_SOFTWARE_TESTS=ON
cmake --build build/aura-gate-a --config Release
ctest --test-dir build/aura-gate-a -C Release --output-on-failure
```

Tests/owner fixture link only core/fakes, never NativeAuraApi/vendor COM. The experiment is outside
Aura.slnx/WinUI/product publish. CI builds/runs only fake tests/guards, never candidate. Five seconds
bounds the release-attempt policy, not vendor return/restoration. Hard failures require human review.
