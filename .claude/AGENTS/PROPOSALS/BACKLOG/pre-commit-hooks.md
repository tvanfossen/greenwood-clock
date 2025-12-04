---
proposal_id: "pre-commit-hooks"
title: "Greenwood Clock - Pre-commit Hooks for Code Quality"
github_issue: null
created: "2025-12-02"
updated: "2025-12-02"

status: "BACKLOG"
priority: "low"
complexity: "low"

category: ["infrastructure", "quality"]
tags: ["pre-commit", "code-quality", "ci", "linting"]

estimated_hours: 4
actual_hours: 0
progress_percent: 0

depends_on: []
blocks: []

commits: []
branches: []
pull_requests: []

agent_notes: []

stall_reason: null
unblock_requirements: []

completion_date: null
verification_status: "pending"
---

# Greenwood Clock - Pre-commit Hooks for Code Quality

## Problem Statement

The project currently lacks automated code quality checks before commits. This can lead to:

- Inconsistent code formatting
- Accidentally committed debug code or TODOs
- Large files committed to git (binaries, build artifacts)
- Trailing whitespace and other minor issues
- Secrets or sensitive data accidentally committed
- Code that doesn't follow project conventions

Manual code review catches some issues, but automated pre-commit hooks provide:
- **Immediate feedback** before code reaches the repository
- **Consistent enforcement** of quality standards
- **Reduced review burden** by catching trivial issues automatically
- **Prevention of common mistakes** (committing secrets, large files, etc.)

## Proposed Solution

Implement pre-commit hooks using the [pre-commit framework](https://pre-commit.com/) with a focus on:

1. **Primary Hook**: [knots](https://github.com/brandon-arrendondo/knots/tree/1.0.0) - Code quality checker for C/C++
2. **Standard Hooks**: Trailing whitespace, file size limits, merge conflicts, etc.
3. **ESP-IDF Specific**: Check for common ESP32 mistakes
4. **Custom Hooks**: Project-specific checks (hardcoded credentials, etc.)

Benefits:
- Catches issues before they're committed
- Runs automatically on `git commit`
- Easy to install and configure
- Integrates with CI/CD
- Can be bypassed when necessary (`--no-verify`)

## Current State

- **No pre-commit hooks** configured
- Code quality relies on manual review
- No automated formatting enforcement
- No checks for common mistakes

## Implementation Plan

### Phase 1: Pre-commit Framework Setup
- [ ] Install pre-commit framework
  ```bash
  pip install pre-commit
  ```
- [ ] Create `.pre-commit-config.yaml` in repository root
- [ ] Initialize pre-commit hooks
  ```bash
  pre-commit install
  ```
- [ ] Test with existing codebase
- [ ] Document installation in README

### Phase 2: Configure Knots Hook
- [ ] Add knots hook to `.pre-commit-config.yaml`
- [ ] Configure knots rules for C/C++ code
- [ ] Test knots on existing code
- [ ] Fix or whitelist any violations
- [ ] Document knots configuration

### Phase 3: Add Standard Hooks
- [ ] Trailing whitespace removal
- [ ] End-of-file fixer
- [ ] Large file checker (prevent >1MB commits)
- [ ] Merge conflict marker detection
- [ ] YAML/JSON syntax validation
- [ ] Mixed line endings check

### Phase 4: ESP-IDF Specific Hooks
- [ ] Check for `ESP_LOGI` without TAG
- [ ] Detect hardcoded WiFi credentials
- [ ] Warn about `ESP_ERROR_CHECK` in production code
- [ ] Check for memory leaks patterns (malloc without free)
- [ ] Validate partition table syntax

### Phase 5: Custom Project Hooks
- [ ] Detect hardcoded API keys
- [ ] Check for TODO/FIXME in committed code (warning only)
- [ ] Verify component CMakeLists.txt format
- [ ] Check for proper includes in .h files
- [ ] Validate settings structure version compatibility

### Phase 6: CI Integration
- [ ] Add pre-commit to CI/CD pipeline
- [ ] Run `pre-commit run --all-files` in CI
- [ ] Document CI integration
- [ ] Configure failure behavior

## Acceptance Criteria

- [ ] Pre-commit framework installed and configured
- [ ] Knots hook running on all C/C++ files
- [ ] At least 10 useful hooks enabled
- [ ] Hooks run automatically on `git commit`
- [ ] CI runs pre-commit checks on all PRs
- [ ] Documentation for developers on installation/usage
- [ ] Existing codebase passes all hooks (or violations documented)
- [ ] Bypass mechanism documented (`--no-verify`)

## Technical Details

### Pre-commit Configuration File

**`.pre-commit-config.yaml`:**
```yaml
# Greenwood Clock Pre-commit Configuration
# See https://pre-commit.com for more information

repos:
  # Knots - C/C++ code quality checker
  - repo: https://github.com/brandon-arrendondo/knots
    rev: 1.0.0
    hooks:
      - id: knots
        args: ['--config=.knots.yaml']
        files: \.(c|cpp|h|hpp)$

  # Standard pre-commit hooks
  - repo: https://github.com/pre-commit/pre-commit-hooks
    rev: v4.5.0
    hooks:
      # Prevent giant files from being committed
      - id: check-added-large-files
        args: ['--maxkb=1024']  # 1MB limit

      # Check for files that would conflict in case-insensitive filesystems
      - id: check-case-conflict

      # Ensure files end with a newline
      - id: end-of-file-fixer
        exclude: \.(bin|png|jpg|gif)$

      # Check for merge conflict strings
      - id: check-merge-conflict

      # Check for mixed line endings
      - id: mixed-line-ending
        args: ['--fix=lf']

      # Trim trailing whitespace
      - id: trailing-whitespace
        exclude: \.(md|patch)$

      # Check YAML syntax
      - id: check-yaml
        files: \.(yaml|yml)$

      # Check JSON syntax
      - id: check-json
        files: \.json$

      # Detect private keys
      - id: detect-private-key

  # ESP-IDF specific checks (custom local hooks)
  - repo: local
    hooks:
      # Warn about hardcoded WiFi credentials
      - id: check-wifi-credentials
        name: Check for hardcoded WiFi credentials
        entry: grep -rn --include="*.c" --include="*.cpp" --include="*.h" "WIFI_SSID\|WIFI_PASSWORD" components/ main/
        language: system
        pass_filenames: false
        always_run: true
        # This will fail if found - that's what we want

      # Check for hardcoded API keys (except in secrets.h)
        - id: check-api-keys
        name: Check for hardcoded API keys
        entry: bash -c 'grep -rn --include="*.c" --include="*.cpp" --include="*.h" --exclude="secrets.h" "API_KEY.*=" components/ main/ || true'
        language: system
        pass_filenames: false
        verbose: true

      # Check for TODOs and FIXMEs (warning only)
      - id: check-todos
        name: Check for TODO/FIXME comments
        entry: bash -c 'grep -rn --include="*.c" --include="*.cpp" --include="*.h" "TODO\|FIXME" components/ main/ || true'
        language: system
        pass_filenames: false
        verbose: true
        # Don't fail, just warn

      # Validate partition table
      - id: check-partition-table
        name: Validate partition table syntax
        entry: python -c "import csv; csv.reader(open('partitions.csv'))"
        language: system
        files: partitions.csv
        pass_filenames: false

# Configure hook behavior
default_stages: [commit]
fail_fast: false  # Run all hooks even if one fails
```

### Knots Configuration

**`.knots.yaml`:**
```yaml
# Knots configuration for Greenwood Clock
# https://github.com/brandon-arrendondo/knots

# File patterns to check
include:
  - "**/*.c"
  - "**/*.cpp"
  - "**/*.h"
  - "**/*.hpp"

exclude:
  - "build/**"
  - "managed_components/**"
  - ".espressif/**"

# Rules configuration
rules:
  # Maximum function length (lines)
  max_function_length: 100

  # Maximum line length
  max_line_length: 120

  # Maximum cyclomatic complexity
  max_complexity: 15

  # Maximum nesting depth
  max_nesting_depth: 4

  # Require braces for single-statement blocks
  require_braces: true

  # Check for magic numbers
  check_magic_numbers: true
  allowed_magic_numbers: [0, 1, -1, 2]

  # Function naming convention
  function_naming: snake_case

  # Variable naming convention
  variable_naming: snake_case

  # Check for memory leaks (basic)
  check_memory_leaks: true

  # Require header guards
  require_header_guards: true

  # Check for unused variables
  check_unused_variables: true

# Severity levels
severity:
  max_function_length: warning
  max_line_length: warning
  max_complexity: error
  max_nesting_depth: error
```

### Installation Instructions

**For Developers:**

1. **Install pre-commit framework:**
   ```bash
   pip install pre-commit
   ```

2. **Install git hooks:**
   ```bash
   cd /path/to/greenwood-clock
   pre-commit install
   ```

3. **Test hooks on existing files (optional):**
   ```bash
   pre-commit run --all-files
   ```

4. **Normal workflow:**
   ```bash
   git add .
   git commit -m "Your message"
   # Pre-commit hooks run automatically
   # Fix any issues and commit again
   ```

5. **Bypass hooks when necessary:**
   ```bash
   git commit --no-verify -m "Emergency fix"
   ```

### Hook Execution Flow

```
Developer runs: git commit
         ↓
Pre-commit framework activates
         ↓
┌────────────────────────────────┐
│ 1. Knots (C/C++ quality check) │
│    - Function length           │
│    - Complexity                │
│    - Naming conventions        │
└────────────────────────────────┘
         ↓
┌────────────────────────────────┐
│ 2. Standard checks             │
│    - Trailing whitespace       │
│    - Large files               │
│    - Merge conflicts           │
└────────────────────────────────┘
         ↓
┌────────────────────────────────┐
│ 3. ESP-IDF checks              │
│    - Hardcoded credentials     │
│    - API keys                  │
│    - TODOs                     │
└────────────────────────────────┘
         ↓
    All pass? ──No──> Fix issues
         │                 │
        Yes               │
         │                │
         └────────────────┘
         ↓
    Commit succeeds
```

### CI/CD Integration

**GitHub Actions Example:**
```yaml
# .github/workflows/pre-commit.yml
name: Pre-commit Checks

on: [push, pull_request]

jobs:
  pre-commit:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - uses: actions/setup-python@v4
        with:
          python-version: '3.10'
      - name: Install pre-commit
        run: pip install pre-commit
      - name: Run pre-commit
        run: pre-commit run --all-files
```

### Custom Hook Examples

**Check for ESP32 common mistakes:**
```bash
# .pre-commit-hooks/check-esp32-mistakes.sh
#!/bin/bash

# Check for ESP_ERROR_CHECK in interrupt handlers
if grep -rn "ESP_ERROR_CHECK.*IRAM_ATTR" components/ main/; then
    echo "Error: ESP_ERROR_CHECK used in ISR (IRAM_ATTR function)"
    exit 1
fi

# Check for printf in interrupt handlers
if grep -rn "printf.*IRAM_ATTR" components/ main/; then
    echo "Error: printf used in ISR (IRAM_ATTR function)"
    exit 1
fi

# Check for floating point in ISR
if grep -rn "float.*IRAM_ATTR\|double.*IRAM_ATTR" components/ main/; then
    echo "Warning: Floating point used in ISR"
fi

exit 0
```

**Check for settings version compatibility:**
```python
# .pre-commit-hooks/check-settings-version.py
#!/usr/bin/env python3
import re
import sys

# Check that SETTINGS_VERSION matches across files
def check_version_consistency():
    version_pattern = r'#define\s+SETTINGS_VERSION\s+(\d+)'

    files = [
        'components/settings/settings.c',
        'components/settings/settings.h',
    ]

    versions = {}
    for f in files:
        try:
            with open(f) as fp:
                content = fp.read()
                match = re.search(version_pattern, content)
                if match:
                    versions[f] = int(match.group(1))
        except FileNotFoundError:
            continue

    if len(set(versions.values())) > 1:
        print("Error: SETTINGS_VERSION mismatch:")
        for f, v in versions.items():
            print(f"  {f}: {v}")
        return 1

    return 0

if __name__ == '__main__':
    sys.exit(check_version_consistency())
```

## Benefits

### For Developers
- **Immediate feedback** on code quality issues
- **Prevents embarrassing mistakes** (hardcoded passwords, etc.)
- **Reduces code review time** by catching trivial issues
- **Enforces consistency** across the team
- **Easy to use** - runs automatically

### For Project
- **Higher code quality** baseline
- **Fewer bugs** caught by automated checks
- **Better maintainability** through consistent style
- **Reduced technical debt** by preventing bad patterns
- **Documentation** of coding standards (via config)

### For CI/CD
- **Faster pipelines** - issues caught before push
- **Reduced CI failures** from formatting issues
- **Consistent environment** - same checks locally and in CI
- **Early error detection** before expensive builds

## Risks & Mitigations

| Risk | Impact | Likelihood | Mitigation |
|------|--------|------------|------------|
| Hooks slow down commits | Medium | Low | Optimize hook speed, allow bypass |
| False positives from knots | Medium | Medium | Tune configuration, whitelist exceptions |
| Developers bypass hooks | Medium | High | Education, CI enforcement |
| Breaking existing workflow | Medium | Low | Gradual rollout, documentation |
| Hook maintenance burden | Low | Medium | Use standard hooks when possible |

## Success Metrics

- Pre-commit adoption: 100% of developers
- Hook execution time: <5 seconds for typical commit
- False positive rate: <5%
- CI pre-commit failures: <10% of builds
- Code quality issues caught: >50 per month
- Developer satisfaction: Positive feedback in retro

## Rollout Strategy

### Phase 1: Opt-in (Week 1)
- Add configuration files to repository
- Document installation in README
- Ask volunteers to test
- Gather feedback

### Phase 2: Encouraged (Week 2)
- Present benefits in team meeting
- Offer installation help
- Monitor adoption rate
- Fix any issues

### Phase 3: Required (Week 3+)
- Enable in CI/CD (fail on violations)
- Update contribution guidelines
- Enforce on all new PRs

## Configuration Tuning

Start with **lenient rules**, then tighten:

**Initial Settings:**
- Max function length: 150 lines (warning only)
- Max complexity: 20 (warning only)
- Max line length: 120 characters
- Most checks: warnings, not errors

**After 1 Month:**
- Review warning patterns
- Tighten frequently violated rules
- Promote common warnings to errors
- Add new project-specific checks

**After 3 Months:**
- Stricter limits based on codebase analysis
- Custom hooks for project patterns
- Integration with code coverage tools
- Automated formatting (if desired)

## Alternative Approaches

### 1. Clang-Format
**Pros:**
- Standard C++ formatting tool
- IDE integration
- Automatic fixing

**Cons:**
- Only formatting, not logic checks
- Requires `.clang-format` configuration
- ESP-IDF specific issues not caught

**Decision:** Use knots + pre-commit for broader checks

### 2. Manual Code Review Only
**Pros:**
- No tooling setup
- Flexible human judgment

**Cons:**
- Inconsistent enforcement
- Reviewer fatigue on trivial issues
- Slower feedback loop

**Decision:** Automate trivial checks, reserve review for logic

### 3. CI-Only Checks
**Pros:**
- No local setup required
- Central enforcement

**Cons:**
- Slow feedback (minutes vs seconds)
- Wastes CI resources
- Developer frustration

**Decision:** Run locally first, CI as backup

## Related Work

- Pre-commit Framework: https://pre-commit.com/
- Knots: https://github.com/brandon-arrendondo/knots
- Pre-commit Hooks: https://github.com/pre-commit/pre-commit-hooks
- ESP-IDF Style Guide: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/contribute/style-guide.html

## Notes

### Why Knots?

**Knots is specifically designed for C/C++ code quality:**
- Complexity analysis (cyclomatic, cognitive)
- Function size limits
- Nesting depth checks
- Memory leak detection (basic)
- Naming convention enforcement
- Header guard validation

**Alternatives considered:**
- **cppcheck**: More comprehensive but slower
- **clang-tidy**: Requires compilation, complex setup
- **lizard**: Python-based, good for complexity only
- **flawfinder**: Security-focused, different use case

**Decision:** Start with knots for its balance of features and speed

### Exempting Files

Some files may need exemption:
```yaml
# In .pre-commit-config.yaml
- id: knots
  exclude: |
    (?x)^(
      managed_components/.*|
      build/.*|
      components/lottie/.*|  # Third-party code
      main/legacy_code.c     # To be refactored
    )$
```

### Hook Performance

Typical hook execution times:
- Knots: 1-3 seconds (depends on file count)
- Standard hooks: <1 second total
- Custom hooks: <1 second each

**Total: ~5 seconds for typical commit**

If too slow, can optimize:
- Cache knots analysis results
- Run only on changed files
- Parallelize hook execution
- Skip expensive checks in `--no-verify` commits

### Integration with IDE

VS Code integration:
1. Install "pre-commit" extension
2. Enable "Run on Save"
3. See violations inline

This provides even faster feedback than commit-time checks.

---

**Repository**: https://github.com/tvanfossen/greenwood-clock

**Created**: 2025-12-02

**Status**: Backlog (low priority, quality-of-life improvement)
