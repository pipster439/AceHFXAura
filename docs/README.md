# Documentation index

Current code is the implementation authority. Guides below have distinct roles; archived reports do not override them.

| Guide | Purpose |
|---|---|
| [Project README](../README.md) | Features, prerequisites, build and quick start |
| [Contributor rules](../AGENT.md) | Current engineering constraints |
| [Architecture](architecture/README.md) | Native client, daemon, WebView2 and runtime boundaries |
| [ASUS platform M1](architecture/ASUS_PLATFORM_PHASE3_M1.md) / [Broker security](architecture/ASUS_PLATFORM_SECURITY.md) | Read-only discovery, authenticated IPC and independent Windows service |
| [Hardware](hardware/README.md) | Native HID, calibrated keymap, Light Bar and HAL research |
| [Testing](testing/TESTING.md) / [CI](development/CI.md) | Automated tests, manual tools and evidence limits |
| [WinUI development launch](development/WINUI_DEVELOPMENT.md) | Incremental native/client build, isolated data and local launch |
| [Manual acceptance](testing/MANUAL_TESTS.md) | Physical keyboard and CS2 checks |
| [Studio workflow](studio/STUDIO_WORKFLOW.md) | Blockly editing, preview, publishing and orchestration |
| [Packaging](development/PACKAGING.md) / [Release checklist](development/RELEASE_CHECKLIST.md) | WinUI ZIP layout, runtime ownership and release gates |
| [alpha.4 client contract](development/ALPHA4_CLIENT_CONTRACT.md) | Native GSI routes, client lifecycle and validation |
| [Configuration](development/CONFIGURATION.md) | Canonical config, defaults, Studio ownership and write contract |
| [Cleanup audit](development/REPOSITORY_CLEANUP.md) | Complete pre-cleanup tracked-file inventory and decisions |
| [Repository hygiene](development/REPOSITORY_HYGIENE.md) | Tracking boundaries, audit artifact policy and Git rules |
| [History](archive/README.md) | Superseded decisions, reports and patch snapshots |
| [alpha.7 current closure](release/HARDWARE_SLOT_RELEASE_CLOSURE.md) / [package candidate](release/PACKAGE_CANDIDATE_AUDIT.md) | Current acceptance state; historical checkpoints are explicitly superseded |
| [Plugin reload closure](release/PLUGIN_RELOAD_ROOT_CAUSE.md) / [ASUS command9 closure](release/ASUS_COMMAND9_RELEASE_CLOSURE.md) | Fixture isolation and additive protocol v1 compatibility evidence |
| [Changelog](../CHANGELOG.md) | Version history |

Runtime configuration is local and ignored. The checked-in [example](../config.example.json) is the small canonical first-run template, not a second user configuration. [VERSION](../VERSION) is the release version source.
