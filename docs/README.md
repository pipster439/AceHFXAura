# Documentation index

Current code is the implementation authority. Guides below have distinct roles; archived reports do not override them.

| Guide | Purpose |
|---|---|
| [Project README](../README.md) | Features, prerequisites, build and quick start |
| [Contributor rules](../AGENT.md) | Current engineering constraints |
| [Architecture](architecture/README.md) | Native client, daemon, WebView2 and runtime boundaries |
| [Hardware](hardware/README.md) | Native HID, calibrated keymap, Light Bar and HAL research |
| [Testing](testing/TESTING.md) / [CI](testing/CI.md) | Automated tests, manual tools and evidence limits |
| [Manual acceptance](testing/MANUAL_TESTS.md) | Physical keyboard and CS2 checks |
| [Studio workflow](studio/STUDIO_WORKFLOW.md) | Blockly editing, preview, publishing and orchestration |
| [Packaging](development/PACKAGING.md) / [Release checklist](development/RELEASE_CHECKLIST.md) | WinUI ZIP layout, runtime ownership and release gates |
| [alpha.4 client contract](development/ALPHA4_CLIENT_CONTRACT.md) | Native GSI routes, client lifecycle and validation |
| [Configuration](development/CONFIGURATION.md) | Canonical config, defaults, Studio ownership and write contract |
| [Cleanup audit](development/REPOSITORY_CLEANUP.md) | Complete pre-cleanup tracked-file inventory and decisions |
| [Repository hygiene](development/REPOSITORY_HYGIENE.md) | Tracking boundaries, audit artifact policy and Git rules |
| [History](archive/README.md) | Superseded decisions, reports and patch snapshots |
| [Changelog](../CHANGELOG.md) | Version history |

Runtime configuration is local and ignored. The checked-in [example](../config.example.json) is the small canonical first-run template, not a second user configuration. [VERSION](../VERSION) is the release version source.
