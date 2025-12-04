# /gh_active_work - Autonomous Proposal Implementation

Autonomously implements a single proposal from ACTIVE_WORK folder with minimal architect intervention.

## Usage

```
/gh_active_work
```

No arguments - picks ONE proposal from `.claude/AGENTS/PROPOSALS/ACTIVE_WORK/` and works it to completion.

## Workflow

```
ACTIVE_WORK/*.md → Questions → Implementation → Git Tracking → STAGED or STALLED
```

## Core Principles

1. **ONE proposal at a time** - Never work multiple proposals simultaneously
2. **Minimal architect input** - Only ask critical clarifying questions
3. **Autonomous execution** - Follow proposal implementation plan independently
4. **Git tracking** - Track all commits back to the proposal
5. **Single source of truth** - NEVER create secondary proposals or duplicate markdowns
6. **Notes in frontmatter** - All work tracking goes in YAML `agent_notes`

## Process

### 1. Selection

```bash
# List all proposals in ACTIVE_WORK
ls .claude/AGENTS/PROPOSALS/ACTIVE_WORK/*.md

# Pick ONE based on:
# - Priority (critical > high > medium > low)
# - Dependencies (unblocked first)
# - Progress (continue in-progress before starting new)
```

**Selection criteria**:
1. No `depends_on` blockers
2. Highest priority
3. Already in progress (progress_percent > 0)
4. Least complex if multiple match

### 2. Load & Validate

```python
import yaml

# Load proposal
with open(proposal_file) as f:
    content = f.read()
    parts = content.split('---', 2)
    frontmatter = yaml.safe_load(parts[1])
    markdown = parts[2].strip()

# Validate against schema
import jsonschema
with open('.claude/AGENTS/PROPOSALS/proposal-schema.json') as f:
    schema = json.load(f)
    jsonschema.validate(frontmatter, schema)

# Verify status
assert frontmatter['status'] == 'ACTIVE_WORK'
```

### 3. Ask Clarifying Questions

**Ask architect ONLY for**:
- Ambiguities in acceptance criteria
- Missing technical specifications
- Preferred approach when multiple valid options
- Scope clarification

**Format**:
```
Working on: {proposal_id}
Title: {title}
Progress: {progress_percent}%

Questions for architect:
1. [Specific question about requirement X]
2. [Clarification needed on approach Y]
3. [Which option: A or B for feature Z]

Please answer briefly to proceed autonomously.
```

**Do NOT ask**:
- How to implement (follow proposal plan)
- What to test (>90% branch and line coverage required, all tests (unit and BDD) passing, cognitive and cyclomatic complexity <15)
- Whether to commit (yes, always track work)

### 4. Implementation

Follow the proposal's Implementation Plan section:

```markdown
## Implementation Plan

### Phase 1: Setup
- [ ] Task 1
- [ ] Task 2

### Phase 2: Core
- [ ] Task 3
```

**For each task**:
1. Implement the task
2. Run tests: `inv test`
3. Verify >90% coverage
4. Run pre-commit: `pre-commit run --all-files`
5. Commit with message: `{proposal_id}: {task description}`
6. Update progress_percent in frontmatter
7. Add agent_note with timestamp

**Example commit**:
```bash
git add .
git commit -m "issue-21-bdd-testing: Create BDD directory structure

- Created tests/bdd/features/
- Created tests/bdd/step_defs/
- Added base_steps.py with ABC helpers

Related: issue-21-bdd-testing"
```

### 5. Track Progress

**After EVERY significant action**, update frontmatter:

```yaml
agent_notes:
  - timestamp: "2025-11-14T10:00:00Z"
    agent: "gh_active_work"
    note: "Started Phase 1: Created directory structure"
    action: "started"
  - timestamp: "2025-11-14T11:30:00Z"
    agent: "gh_active_work"
    note: "Completed feature files, 3/12 scenarios implemented"
    action: "progressed"
```

**Update progress**:
```yaml
progress_percent: 40
actual_hours: 3
updated: "2025-11-14"
commits:
  - "abc123f"
  - "def456a"
```

### 6. Git Tracking

**Track ALL commits**:
```bash
# After each commit
COMMIT_SHA=$(git rev-parse --short HEAD)

# Add to frontmatter commits array
commits:
  - "abc123f"
  - "def456a"  # <-- new
```

**Use git log to verify work**:
```bash
git log --oneline --grep="issue-21" | head -10
# Shows all commits for this proposal
```

### 7. Completion Decision

When implementation plan is complete:

**If ALL criteria met**:
- ✅ All tasks checked off
- ✅ Tests passing (>90% coverage)
- ✅ Pre-commit hooks pass
- ✅ Acceptance criteria satisfied
- ✅ No known blockers

→ **Move to STAGED**:
```yaml
status: "STAGED"
progress_percent: 100
completion_date: "2025-11-14"
verification_status: "pending"
agent_notes:
  - timestamp: "2025-11-14T16:00:00Z"
    agent: "gh_active_work"
    note: "All implementation complete, tests passing, moved to STAGED for architect review"
    action: "completed"
```

```bash
mv .claude/AGENTS/PROPOSALS/ACTIVE_WORK/{proposal}.md \
   .claude/AGENTS/PROPOSALS/STAGED/
```

**If BLOCKED**:
- ❌ Dependency on external system
- ❌ Requires architect decision
- ❌ Technical blocker discovered

→ **Move to STALLED**:
```yaml
status: "STALLED"
stall_reason: "Blocked on external API access - need credentials"
unblock_requirements:
  - "API key for service X"
  - "Architect decision on approach Y"
agent_notes:
  - timestamp: "2025-11-14T14:00:00Z"
    agent: "gh_active_work"
    note: "Blocked: Cannot proceed without API credentials for service X"
    action: "blocked"
```

```bash
mv .claude/AGENTS/PROPOSALS/ACTIVE_WORK/{proposal}.md \
   .claude/AGENTS/PROPOSALS/STALLED/
```

### 8. Final Report

After moving to STAGED or STALLED, report to architect:

**For STAGED**:
```
✅ Completed: {proposal_id}

Summary:
- {X} commits made
- {Y} files changed
- Tests: {Z} passing, {coverage}% coverage
- Duration: {actual_hours} hours

Implementation notes:
{summary of what was done}

Moved to STAGED for verification.
Review at: .claude/AGENTS/PROPOSALS/STAGED/{filename}
```

**For STALLED**:
```
⚠️ Stalled: {proposal_id}

Progress: {progress_percent}%
Blocker: {stall_reason}

To unblock:
{unblock_requirements}

Moved to STALLED.
Review at: .claude/AGENTS/PROPOSALS/STALLED/{filename}
```

## Rules

### MUST DO

✅ Pick ONE proposal only
✅ Validate frontmatter against schema
✅ Ask clarifying questions upfront
✅ Track EVERY commit in frontmatter
✅ Update agent_notes after each milestone
✅ Run tests after each change
✅ Ensure >90% coverage
✅ Pass pre-commit hooks
✅ Move to STAGED when complete
✅ Move to STALLED if blocked

### MUST NOT DO

❌ Work multiple proposals simultaneously
❌ Create secondary proposal files
❌ Create markdown files in project root
❌ Skip testing
❌ Leave proposal in ACTIVE_WORK indefinitely
❌ Ask obvious questions architect can't answer
❌ Implement without following proposal plan
❌ Commit without updating frontmatter

## Settings Integration

Uses `settings.local.json` for:
- Auto-approval of safe bash commands
- Test command shortcuts
- Git workflow preferences

Example `settings.local.json`:
```json
{
  "autonomous_mode": true,
  "min_architect_questions": true,
  "auto_test_on_change": true,
  "coverage_threshold": 90
}
```

## Examples

### Example 1: Successful Completion

```
$ /gh_active_work

Found 2 proposals in ACTIVE_WORK:
1. issue-21-bdd-testing (priority: high, progress: 60%)
2. issue-25-new-feature (priority: medium, progress: 0%)

Selected: issue-21-bdd-testing (highest priority, in-progress)

Questions for architect:
1. For UI test scenarios, should I create placeholders or skip entirely?
   (proposal mentions "future playwright integration")

[Architect: "Create placeholders with pytest.skip"]

Proceeding with implementation...

Phase 1: Directory structure ✓ (already done)
Phase 2: Feature files
  - Creating model_behavior.feature... ✓
  - Creating chat_interface.feature... ✓
  - Creating lesson_management.feature... ✓
  Committed: abc123f

Phase 3: Step definitions
  - base_steps.py with ABC... ✓
  - model_steps.py... ✓
  - ui_steps.py (placeholders)... ✓
  Committed: def456a

Phase 4: Testing
  - Running inv test... ✓ (all passing, 91% coverage)
  - Pre-commit... ✓

All acceptance criteria met.

✅ Completed: issue-21-bdd-testing

Summary:
- 4 commits made
- 15 files created/modified
- Tests: 643 passing, 91% coverage
- Duration: 12 hours

Moved to STAGED for verification.
```

### Example 2: Blocked/Stalled

```
$ /gh_active_work

Found 1 proposal in ACTIVE_WORK:
1. issue-30-api-integration (priority: critical, progress: 0%)

Selected: issue-30-api-integration

Questions for architect:
1. Proposal mentions "external API" - do we have credentials?

[Architect: "Not yet, waiting on vendor"]

⚠️ Stalled: issue-30-api-integration

Progress: 0%
Blocker: Missing API credentials from vendor

To unblock:
- Obtain API key for ServiceX
- Update .env with credentials

Moved to STALLED.
```

## Verification

After completion, `/gh_verify` command will:
1. Review proposals in STAGED/
2. Run adversarial tests
3. Validate against acceptance criteria
4. Move to COMPLETED/ or back to ACTIVE_WORK/

## Success Criteria

- One proposal moved per run
- All commits tracked in frontmatter
- Agent notes document decisions
- Tests passing (>90% coverage)
- Proposal in STAGED or STALLED (never left in ACTIVE_WORK)
- No duplicate proposal files created
- No markdown bloat in project root

---

**Status**: Specification (command implementation required)
**Next Step**: Implement `/gh_active_work` slash command handler
