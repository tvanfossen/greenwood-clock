---
proposal_id: "test-framework-setup"
title: "Greenwood Clock - Comprehensive Testing Framework Setup"
github_issue: null
created: "2025-12-01"
updated: "2025-12-01"

status: "BACKLOG"
priority: "medium"
complexity: "medium"

category: ["testing", "infrastructure"]
tags: ["esp32", "unity", "pytest", "ci-cd", "quality-assurance"]

estimated_hours: 8
actual_hours: 0
progress_percent: 0

depends_on: ["greenwood-clock-maintenance-handoff"]
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

# Greenwood Clock - Comprehensive Testing Framework Setup

## Problem Statement

The greenwood-clock project currently lacks automated testing infrastructure. While the codebase is functional, there is no systematic way to verify correctness, prevent regressions, or ensure code quality. Manual testing on hardware is time-consuming and doesn't scale as the project grows.

## Proposed Solution

Implement a comprehensive testing framework using Unity (for on-device unit tests) and pytest (for host-based component tests). This will enable:
- Automated unit tests for critical components
- Integration tests for API clients
- Host-based tests for CI/CD pipeline
- Code coverage analysis
- Regression prevention

## Current State

- No automated tests
- Manual testing only
- No code coverage metrics
- No CI/CD pipeline
- Testing infrastructure: None

## Implementation Plan

### Phase 1: Unity Framework Setup
- [ ] Create `test/` directory for test applications
- [ ] Set up Unity test runner for ESP32
- [ ] Configure test build separate from main firmware
- [ ] Create test runner main.c with Unity initialization
- [ ] Document test execution workflow

### Phase 2: Component Unit Tests
- [ ] Write tests for `time_sync` component
  - [ ] Timezone conversion tests
  - [ ] Time structure validation
  - [ ] Edge cases (leap years, DST transitions)
- [ ] Write tests for `weather` component
  - [ ] API response parsing
  - [ ] Data structure validation
  - [ ] Error handling
- [ ] Write tests for `network` component
  - [ ] Wi-Fi state machine
  - [ ] SNTP synchronization logic
  - [ ] Connection retry behavior

### Phase 3: Mocking and Isolation
- [ ] Integrate CMock for mocking dependencies
- [ ] Create mocks for HTTP client (for weather/astronomy tests)
- [ ] Create mocks for Wi-Fi driver (for network tests)
- [ ] Create mocks for SNTP client
- [ ] Enable true unit testing (isolated from hardware)

### Phase 4: Host-Based Testing
- [ ] Set up pytest framework
- [ ] Create host-compatible test builds (Linux/macOS)
- [ ] Port component tests to run on host
- [ ] Create test fixtures and utilities
- [ ] Enable fast feedback loop (no hardware required)

### Phase 5: Coverage and Quality
- [ ] Enable gcov for code coverage
- [ ] Set up coverage reporting
- [ ] Target >80% coverage for critical components
- [ ] Add static analysis (cppcheck, clang-tidy)
- [ ] Create coverage badge for README

### Phase 6: CI/CD Integration
- [ ] Create GitHub Actions workflow
- [ ] Run host tests on every commit
- [ ] Run hardware tests on PR (if runner available)
- [ ] Fail builds on test failures
- [ ] Publish coverage reports

## Acceptance Criteria

- [ ] Unity test framework integrated and working
- [ ] At least 10 unit tests covering core functionality
- [ ] Tests pass on device (ESP32) and host (Linux)
- [ ] Test documentation explains how to run and write tests
- [ ] Code coverage >60% for initial implementation
- [ ] CI/CD pipeline runs tests automatically

## Technical Details

### Test Structure
```
test/
├── CMakeLists.txt              # Test app build config
├── main/
│   ├── test_main.c             # Unity test runner
│   ├── test_time_sync.c        # Time sync tests
│   ├── test_weather.c          # Weather tests
│   └── test_network.c          # Network tests
├── host_test/                  # Host-compatible tests
│   ├── test_*.py               # Pytest test files
│   └── fixtures/               # Test data
└── README.md                   # Test documentation
```

### Unity Test Example
```c
#include "unity.h"
#include "time_sync.h"

void test_time_sync_converts_utc_to_local(void) {
    time_t utc = 1638316800;  // 2021-12-01 00:00:00 UTC
    struct tm local;

    time_sync_setup("EST5EDT,M3.2.0/2,M11.1.0/2");
    time_sync_get_local(&utc, &local);

    TEST_ASSERT_EQUAL(2021, local.tm_year + 1900);
    TEST_ASSERT_EQUAL(11, local.tm_mon);  // November (0-indexed)
    TEST_ASSERT_EQUAL(30, local.tm_mday);
    TEST_ASSERT_EQUAL(19, local.tm_hour); // UTC-5
}
```

### CI/CD Workflow
```yaml
name: Tests

on: [push, pull_request]

jobs:
  host-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - name: Install ESP-IDF
        run: |
          git clone --depth 1 -b v5.5 https://github.com/espressif/esp-idf.git
          cd esp-idf && ./install.sh
      - name: Run host tests
        run: |
          source esp-idf/export.sh
          cd test
          pytest
      - name: Upload coverage
        uses: codecov/codecov-action@v2
```

## Risks & Mitigations

| Risk | Impact | Likelihood | Mitigation |
|------|--------|------------|------------|
| Tests require hardware | High | High | Implement host-based tests with mocks |
| Network-dependent tests | Medium | High | Mock HTTP/Wi-Fi, add integration test flag |
| Test maintenance overhead | Medium | Medium | Keep tests simple, use fixtures |
| Long test execution time | Low | Medium | Parallelize tests, use test categories |

## Success Metrics

- 0 test failures on main branch
- >80% code coverage for critical components (time_sync, network, weather)
- >60% overall project coverage
- Test suite completes in <5 minutes on host
- All PRs require passing tests before merge

## Related Work

- Unity Test Framework: https://github.com/ThrowTheSwitch/Unity
- CMock: https://github.com/ThrowTheSwitch/CMock
- ESP-IDF Testing Guide: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/unit-tests.html

## Notes

### Test Categories

1. **Unit Tests**: Test individual functions in isolation (with mocks)
2. **Component Tests**: Test component interfaces (may use real dependencies)
3. **Integration Tests**: Test multiple components together (on hardware)
4. **System Tests**: End-to-end tests (full firmware on hardware)

### Testing Best Practices

- Keep tests fast and focused
- Use descriptive test names: `test_<function>_<scenario>_<expected_result>`
- One assertion per test when possible
- Use setup/teardown for common initialization
- Mock external dependencies (network, APIs, hardware)
- Test error paths, not just happy paths

### Future Enhancements

- Property-based testing with fuzzing
- Performance/benchmark tests
- Memory leak detection
- Thread safety testing (for multi-threaded components)
- Hardware-in-the-loop (HIL) testing
- Automated UI testing (LVGL simulator)

---

**Repository**: https://github.com/tvanfossen/greenwood-clock

**Created**: 2025-12-01

**Status**: Backlog (awaiting prioritization)
