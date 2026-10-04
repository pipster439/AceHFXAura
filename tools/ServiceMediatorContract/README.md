# ServiceMediator M2.9 evidence model

Pure software reconstruction/decision code; no vendor COM, execution flags, write worker, hardware
transport or producer exists here. `model.py` parses evidence and fails closed on incomplete contracts.
Native current TypeLib extraction remains in LightingBackendProbe `--abi` (REGKIND_NONE, no activation).
Its runtime immutable allowlist remains DISPIDs 3/4/63 only. Other getter-shaped methods are not enabled.

Run fixture tests with:

```powershell
python -B -m unittest discover -s tests -p test_servicemediator_contract.py -v
```

The report/evidence are under `audit_artifacts/phase3-m2.9`. Native decompilation databases and raw
vendor disassembly remain local; deliver curated offsets, claims and binary hashes rather than vendor code.
