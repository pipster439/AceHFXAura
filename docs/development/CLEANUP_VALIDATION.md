# Cleanup validation — Windows, 2026-09-20

Base: `7ed61c7` on `feat/winui-lighting`. Local toolchain: Visual Studio 18 2026, MSVC 19.51, Windows SDK 10.0.26100.0, Python 3.13, .NET 10. Results below were executed locally, not inferred from CI.

| Check | Result |
|---|---|
| `cmake -S . -B build/cleanup -G "Visual Studio 18 2026" -A x64` | PASS; separate build directory avoids altering the pre-existing generator cache |
| `cmake --build build/cleanup --config Release --parallel 4` | PASS, including manual diagnostic targets; existing C++ warnings remain |
| `ctest --test-dir build/cleanup -C Release --output-on-failure` | PASS 6/6, including production Lighting schema/config checks |
| Python full `unittest discover -s tests -p "test_*.py" -v` | NOT PASS: 74 tests reported, 1 failure + 3 errors due to absent packaged Aura.exe; launcher class setup error is separate from test count |
| Python non-packaging subset against fresh binaries | PASS 71/71, zero skips |
| `dotnet test tests/Aura.Tests/Aura.Tests.csproj -c Release` | PASS 28/28; existing MSTest analyzer style warnings |
| `dotnet build winui/Aura.WinUI.csproj -c Release -p:Platform=x64` | PASS, zero warnings/errors |
| `npm ci`, `npm test`, `npm run build` in frontend | PASS: 24 passed, 3 skipped; build output identical to tracked web/index.html |
| Tracked JSON parsing / Python AST parsing | PASS; JSON syntax is distinct from schema behavior, which is exercised by CTest Lighting and runtime tests |
| Markdown file targets / active moved-path scan / `git diff --check` | PASS |
| CMake / workflow / package source inspection | PASS static review; CI explicit build now includes registered lighting_service target |

Python full discovery used `AURA_BIN_DIR=G:\Aura\build\cleanup\Release`. Its missing artifacts are `Aura.exe` in that selected directory and `dist/Aura.exe` for Studio release cases A/B/F. To verify the available suite against fresh binaries, the second run excluded `TestLauncherEntrypoint` and Studio cases A/B/F and set `test_studio_release_regression.WEB_UI_EXE` to the fresh build path before running unittest. All other discovered cases ran. The first full run includes legacy tests that default to `build/Release`; only the second run is the fresh-binary subset claim.

The frontend's three skipped cases are the portable generated-C++ frame harness, native overlay-manager harness and complete CS2 example C++ harness. Passing JS tests do not substitute for these native executions.

No packaging run, physical HID/HAL probe, GUI interaction, actual keyboard observation or CS2 match acceptance was performed. Packaging is legacy and mutates the runtime cache; retained code was inspected rather than invoked. Windows-only C++/.NET/WinUI builds **were** executed successfully. Owner still needs physical acceptance and future WinUI distribution validation, not a repeat simply because this was a cloud environment.

Local logs are in ignored `build/cleanup-*.log`; they are not shipped in the repository. GitHub Actions status is reported on the pull request independently from this local record.
