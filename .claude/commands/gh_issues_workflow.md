# /gh_issues_workflow - GitHub Issues to Proposals

Processes open GitHub issues and creates proposals in TODO/ folder.

## Usage

```
/gh_issues_workflow
```

No arguments - processes ALL open GitHub issues without existing proposals.

## Workflow

```
GitHub Issues → Analysis → Proposal Creation → TODO/
```

## Core Principles

1. **One issue, one proposal** - Each GitHub issue gets exactly one proposal file
2. **Idempotent** - Safe to run multiple times (won't duplicate)
3. **Structured output** - All proposals use YAML frontmatter schema
4. **No markdown bloat** - Only creates proposals in `.claude/AGENTS/PROPOSALS/TODO/`
5. **Link tracking** - Proposals reference GitHub issue numbers

## Process

### 1. Fetch Open Issues

```bash
# Get all open issues
gh issue list --state open --json number,title,body,labels,assignees,createdAt

# Output example:
[
  {
    "number": 27,
    "title": "Add dark mode support",
    "body": "Users want dark mode...",
    "labels": ["enhancement", "ui"],
    "assignees": [],
    "createdAt": "2025-11-10T10:00:00Z"
  }
]
```

### 2. Check Existing Proposals

```bash
# For each issue, check if proposal exists
find .claude/AGENTS/PROPOSALS -name "*issue-${number}-*.md"

# Check YAML frontmatter for github_issue field
grep "github_issue: ${number}" .claude/AGENTS/PROPOSALS/**/*.md
```

**Skip if**:
- Proposal already exists in ANY folder (TODO, ACTIVE_WORK, STAGED, COMPLETED, STALLED)
- Issue is referenced in existing proposal frontmatter

### 3. Analyze Issue

For each NEW issue:

**Extract**:
- Problem statement from issue body
- Labels → map to proposal categories/priority
- Complexity estimate based on description
- Dependencies (if mentioned in issue)

**Categorization**:
```python
# Map GitHub labels to proposal fields
label_mapping = {
    'bug': {'category': ['bug'], 'priority': 'high'},
    'enhancement': {'category': ['feature'], 'priority': 'medium'},
    'documentation': {'category': ['documentation'], 'priority': 'low'},
    'critical': {'priority': 'critical'},
    'technical debt': {'category': ['technical-debt']},
    'testing': {'category': ['testing']}
}

# Estimate complexity from issue body length and labels
if 'epic' in labels:
    complexity = 'epic'
elif len(body) > 1000:
    complexity = 'complex'
elif len(body) > 500:
    complexity = 'medium'
else:
    complexity = 'simple'
```

### 4. Create Proposal

Generate proposal file using TEMPLATE.md:

**Filename**: `issue-{number}-{slug}.md`
- Slug: kebab-case from title (max 4 words)
- Example: `issue-27-dark-mode-support.md`

**YAML Frontmatter**:
```yaml
---
proposal_id: "issue-27-dark-mode-support"
title: "Add dark mode support"
github_issue: 27
created: "2025-11-14"
updated: "2025-11-14"

status: "TODO"
priority: "medium"  # from labels
complexity: "medium"  # estimated

category: ["feature", "ui"]  # from labels
tags: ["enhancement", "dark-mode"]

estimated_hours: 8  # rough estimate
actual_hours: 0
progress_percent: 0

depends_on: []  # parse from issue body if mentioned
blocks: []

commits: []
branches: []
pull_requests: []

agent_notes:
  - timestamp: "2025-11-14T10:00:00Z"
    agent: "gh_issues_workflow"
    note: "Created proposal from GitHub issue #27"
    action: "started"

stall_reason: null
unblock_requirements: []

completion_date: null
verification_status: "pending"
---

# Proposal: Add dark mode support

## Problem Statement

{extracted from GitHub issue body}

## Proposed Solution

{architect should fill this in, or agent can propose based on issue discussion}

## Implementation Plan

### Phase 1: Analysis
- [ ] Research dark mode CSS patterns
- [ ] Inventory all UI components
- [ ] Define color palette

### Phase 2: Implementation
- [ ] Implement dark mode CSS
- [ ] Add theme toggle component
- [ ] Update all components for dark mode

### Phase 3: Testing
- [ ] Test in all browsers
- [ ] Test theme persistence
- [ ] Verify accessibility

## Acceptance Criteria

{extracted from issue or generated}

- [ ] User can toggle dark/light mode
- [ ] Theme persists across sessions
- [ ] All UI components support both themes
- [ ] Tests passing (>90% coverage)
- [ ] Pre-commit hooks pass

## Technical Details

{To be filled in during ACTIVE_WORK}

## Risks & Mitigations

| Risk | Impact | Likelihood | Mitigation |
|------|--------|------------|------------|
| TBD | TBD | TBD | TBD |

## Success Metrics

{To be defined}

## Related Work

- GitHub issue: #27
- Related issues: {if mentioned in GitHub issue}

## Notes

Created from GitHub issue #27: {issue_url}
Original author: {issue_author}
Created: {issue_created_at}
```

**Save to**:
```bash
.claude/AGENTS/PROPOSALS/TODO/issue-27-dark-mode-support.md
```

### 5. Validate Schema

```python
import yaml
import jsonschema

# Validate frontmatter
with open(proposal_file) as f:
    content = f.read()
    parts = content.split('---', 2)
    frontmatter = yaml.safe_load(parts[1])

with open('.claude/AGENTS/PROPOSALS/proposal-schema.json') as f:
    schema = json.load(f)

try:
    jsonschema.validate(frontmatter, schema)
except jsonschema.ValidationError as e:
    print(f"INVALID: {proposal_file}")
    print(f"Error: {e.message}")
    # Fix and retry
```

### 6. Report

After processing all issues:

```
GitHub Issues Workflow Complete

Processed: {total_issues} open issues
Created: {new_proposals} new proposals
Skipped: {skipped} (already have proposals)

New Proposals:
1. issue-27-dark-mode-support.md → TODO/
2. issue-28-export-calendar.md → TODO/
3. issue-29-keyboard-shortcuts.md → TODO/

Next Steps:
1. Review proposals in .claude/AGENTS/PROPOSALS/TODO/
2. Prioritize and move to ACTIVE_WORK/
3. Run /gh_active_work to begin implementation
```

## Proposal Priority Assignment

Based on GitHub labels:

| Label | Priority | Rationale |
|-------|----------|-----------|
| critical, blocker, urgent | critical | Blocks work or critical bug |
| bug, regression | high | Affects functionality |
| enhancement, feature | medium | New functionality |
| documentation, chore | low | Nice to have |

Can be overridden by architect later.

## Complexity Estimation

Heuristics for initial estimate:

| Indicator | Complexity |
|-----------|------------|
| Label: epic, large | epic (>40h) |
| Body >1000 chars, multiple features | complex (16-40h) |
| Body 500-1000 chars, single feature | medium (4-16h) |
| Body <500 chars, clear scope | simple (1-4h) |
| Label: trivial, quick-fix | trivial (<1h) |

Estimates can be refined during ACTIVE_WORK.

## Dependencies

If issue mentions:
- "Depends on #XX" → add to `depends_on`
- "Blocks #XX" → add to `blocks`
- "Related to #XX" → mention in Related Work section

Parse from issue body using regex:
```python
import re

depends_pattern = r'[Dd]epends on #(\d+)'
blocks_pattern = r'[Bb]locks #(\d+)'
related_pattern = r'[Rr]elated to #(\d+)'

depends_on = re.findall(depends_pattern, issue_body)
blocks = re.findall(blocks_pattern, issue_body)
```

## Rules

### MUST DO

✅ Check if proposal already exists before creating
✅ Use YAML frontmatter with valid schema
✅ Link to GitHub issue number in frontmatter
✅ Create files ONLY in TODO/ folder
✅ Validate frontmatter against schema
✅ Use filename format: `issue-{number}-{slug}.md`
✅ Map GitHub labels to proposal fields
✅ Add agent_note for creation

### MUST NOT DO

❌ Create duplicate proposals for same issue
❌ Create proposals in project root
❌ Create proposals for closed issues
❌ Skip schema validation
❌ Hardcode priority/complexity (use heuristics)
❌ Create proposals for issues that already have them

## Integration with Other Commands

**After /gh_issues_workflow**:
1. Architect reviews TODO/ folder
2. Architect moves high-priority proposals to ACTIVE_WORK/
3. `/gh_active_work` picks them up and implements
4. `/gh_verify` validates completion
5. `/gh_issue_closure` updates GitHub issues

## Example Session

```
$ /gh_issues_workflow

Fetching open GitHub issues...
Found 15 open issues

Checking existing proposals...
- Issue #21: proposal exists (COMPLETED)
- Issue #22: proposal exists (STAGED)
- Issue #25: no proposal
- Issue #26: no proposal
- Issue #27: no proposal
...

Creating proposals for 8 new issues:

Issue #25: "Add export to PDF feature"
  → Priority: medium (label: enhancement)
  → Complexity: complex (detailed requirements)
  → Created: issue-25-export-pdf.md

Issue #26: "Fix timezone bug in calendar"
  → Priority: high (label: bug)
  → Complexity: simple (clear fix)
  → Created: issue-26-timezone-bug.md

Issue #27: "Dark mode support"
  → Priority: medium (label: enhancement)
  → Complexity: medium (UI work)
  → Created: issue-27-dark-mode-support.md

...

✅ Workflow complete

Summary:
- 8 new proposals created → TODO/
- 7 issues skipped (already have proposals)
- All proposals validated against schema

Next: Review .claude/AGENTS/PROPOSALS/TODO/ and prioritize
```

## Success Criteria

- All open issues have proposals (or are skipped intentionally)
- No duplicate proposals
- All proposals validate against schema
- Proposals properly categorized and prioritized
- GitHub issue numbers tracked in frontmatter

---

**Status**: Specification (command implementation required)
**Next Step**: Implement `/gh_issues_workflow` slash command handler
