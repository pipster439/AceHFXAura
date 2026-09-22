# Historical milestone harnesses

These M1/M5 experiments are retained for their boundary cases and investigation history. No file here is registered by CMake or GitHub Actions, and this directory is excluded from ordinary unittest discovery (no package initializer).

- `test_adversarial_m1.cpp`, `test_adversarial_m1_ast.cpp`, `test_adversarial_m1_stress.cpp`: old manually compiled harnesses. They contain milestone-specific API and fixture assumptions, including `test_diag_hook_test.exe`, which has no current CMake target. No current pass/coverage claim is made.
- `test_adversarial_m1_compiler.py`: compiler endpoint adversarial experiment; retained with repository-root resolution adjusted after the move. It is not an isolated release gate.
- `verify_adversarial_m5.py`: live integration experiment that posts configuration to port 19898. Do not run against a user's active session as part of automated verification.

Current supported checks and manual diagnostics are documented in [Testing](../../docs/testing/TESTING.md). Port useful assertions into isolated production-backed tests before restoring any of these to CI.
