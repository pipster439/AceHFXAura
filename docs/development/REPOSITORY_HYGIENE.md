# Repository Hygiene and Audit Artifact Policy

This document defines repository tracking boundaries, audit artifact lifecycle rules, and Git hygiene standards for AceHFXAura contributors and AI coding agents.

---

## 1. Core Principles

The Git repository tracks **only**:
- Production source code (`src/`, `include/`, `plugins/src/`, `winui/`, `frontend/`, `web/index.html`)
- Official configuration files (`config.example.json`, `CMakeLists.txt`, etc.)
- Official project and architectural documentation (`docs/`, `README.md`, `CHANGELOG.md`, `AGENT.md`)
- Official test suites, models, and compatibility fixtures (`tests/`)
- Production packaging and tooling scripts (`tools/`, `package_release.*`)
- Necessary static runtime assets (`calibrated_keymap.json`, `lightbar_mapping.json`, icons)

The Git repository **never** tracks:
- Intermediate build outputs, binaries, libraries, or object files (`*.obj`, `*.exe`, `*.dll`, `*.lib`, `bin/`, `obj/`, `build/`, `out/`)
- Agent / audit scratch files, reproduction scripts, temporary patches, or raw logs
- Complete source snapshots or duplicated repositories inside audit folders
- Local machine state, private runtime configs (`config.json`), or agent session cache

---

## 2. Agent and Audit Artifact Lifecycle

When performing debugging, code auditing, adversarial analysis, or benchmark reviews:

### 2.1 Workspace Isolation
- All temporary audit outputs, scratch data, repro harnesses, build logs, and temporary patches **must be placed exclusively inside `/audit_artifacts/`** (or `/audit_repros/`).
- **Strictly forbidden**:
  - Creating temporary reports (`*_audit.md`, `*_report.md`, `*_summary.md`), diffs, or `*.patch` files in the repository root.
  - Copying the repository source tree, headers, or build directories into an audit directory and committing them to version control.
  - Committing compiler logs, test dumps, or temporary binaries into Git.

### 2.2 Version Control Exclusions
- `/audit_artifacts/` and `/audit_repros/` are ignored in `.gitignore` as full directories.
- Root patterns `/*.patch`, `/*_audit.md`, `/*_report.md`, `/*_report*.md`, and `/*_summary.md` are ignored in `.gitignore`.
- Contributors and agents must not bypass `.gitignore` with `git add -f` for temporary artifacts.

### 2.3 Curating Long-Term Conclusions
If an audit or investigation produces findings, security remediation baselines, or hardware verification conclusions that require permanent preservation:
1. **Do not commit raw audit workspaces**.
2. **Synthesize and curate**: Extract the core findings, reproduction steps, and conclusions into a formal, reviewed engineering document.
3. **Target location**: Place the curated document into the appropriate `docs/` subdirectory:
   - Historical audits and reviews: `docs/archive/reports/`
   - Hardware reverse engineering: `docs/hardware/`
   - Architectural decisions and baselines: `docs/architecture/`
   - Development & release processes: `docs/development/`
4. Formal milestone patches (if intentionally preserved) belong in `docs/archive/patches/`, not in the repository root.

---

## 3. Pre-Commit Hygiene Checklist

Before submitting changes or committing:

1. **Check for untracked leftovers**:
   ```bash
   git status
   ```
   Ensure no temporary files, logs, or build artifacts appear as untracked (`??`).

2. **Verify ignored entries**:
   ```bash
   git status --ignored
   ```
   Confirm that all temporary and local files are properly categorized under ignored (`!!`).

3. **Verify Git index changes**:
   ```bash
   git diff --staged --stat
   ```
   Inspect every file scheduled for commit. Confirm that only intended code, tests, or curated documentation are staged.

4. **Accidentally tracked artifacts**:
   If a temporary artifact was accidentally tracked in Git, use:
   ```bash
   git rm --cached <path>
   ```
   This untracks the file from version control while preserving the local copy on disk.
