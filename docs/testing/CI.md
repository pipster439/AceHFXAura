# Continuous integration

Canonical reusable software CI and local hardware-smoke boundaries are documented in
[development/CI.md](../development/CI.md).

Run `pwsh ./tools/ci/run-ci.ps1` from the repository root. The GitHub Windows workflow
uses this same entrypoint. Packaging and physical acceptance remain separate gates;
see [Packaging](../development/PACKAGING.md) and [Manual acceptance](MANUAL_TESTS.md).
