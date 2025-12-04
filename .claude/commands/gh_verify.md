# /gh_verify - STAGED Proposal Verification

Reviews proposals in STAGED/ folder, validates implementation, and moves to COMPLETED/ or back to ACTIVE_WORK/.

## Usage

```
/gh_verify
```

No arguments - processes ALL proposals in `.claude/AGENTS/PROPOSALS/STAGED/`

## Workflow

```
STAGED/*.md → Validation → Testing → COMPLETED or ACTIVE_WORK
```

## Core Principles

1. **Architect-driven** - Architect makes final accept/reject decisions
2. **Adversarial testing** - Challenge implementation with edge cases
3. **Schema validation** - Verify frontmatter against JSON schema
4. **Production readiness** - Ensure code is deployment-ready
5. **Update in place** - Modify proposal frontmatter, don't create new files

## Process

### 1. Discovery & Load

```bash
# Find all STAGED proposals
find .claude/AGENTS/PROPOSALS/STAGED -name "*.md" -type f | sort
```

For each proposal:
1. Parse YAML frontmatter
2. Validate against `proposal-schema.json`
3. Extract commits, progress, verification status
4. Check status is "STAGED"

### 2. Automated Validation

For EACH proposal, run:

**A. Frontmatter Validation**
```python
import jsonschema

with open('.claude/AGENTS/PROPOSALS/proposal-schema.json') as f:
    schema = json.load(f)

jsonschema.validate(frontmatter, schema)

# Verify required fields
assert frontmatter['status'] == 'STAGED'
assert frontmatter['progress_percent'] == 100
assert len(frontmatter['commits']) > 0
```

**B. Git Commit Verification**
```bash
# For each commit in frontmatter
for commit in commits:
    git show $commit --stat
    # Verify commit exists
    # Verify files changed match proposal scope
    # Check commit message references proposal_id
done

# Verify commits are in current branch
git log --oneline | grep -f <(echo "$commits")
```

**C. Test Validation**
```bash
inv test
# All tests must pass
# Coverage must be >90%
# No regressions from baseline
```

**D. Code Quality**
```bash
pre-commit run --all-files
# All hooks must pass
# No new linting warnings
# No security issues
```

### 3. Adversarial Analysis

Generate adversarial test report:

**Questions to answer**:
1. What could break with this implementation?
2. What edge cases weren't tested?
3. What assumptions could be wrong?
4. What happens under load/stress?
5. What security implications exist?
6. What maintainability concerns?

**Rate implementation**: 0-100%
- 90-100%: Production ready
- 70-89%: Minor concerns
- 40-69%: Needs rework
- <40%: Reject

**Example rating**:
```yaml
adversarial_rating: 85
adversarial_concerns:
  - "No integration test for error path X"
  - "Edge case: what if database is locked?"
  - "Performance: O(n²) algorithm in function Y"
```

### 4. Architect Review

Present summary to architect:

```
Proposal: {proposal_id}
Title: {title}
Priority: {priority}
Commits: {len(commits)}
Rating: {adversarial_rating}%

Summary:
{what was implemented}

✅ Automated Checks:
- Frontmatter valid
- {X} commits verified
- Tests passing ({Y} tests, {Z}% coverage)
- Pre-commit passing

⚠️ Concerns ({len(adversarial_concerns)}):
1. {concern_1}
2. {concern_2}

Recommendation: [ACCEPT | REWORK | REJECT]

What action?
1. ACCEPT → Move to COMPLETED
2. REWORK → Move back to ACTIVE_WORK with notes
3. REJECT → Delete (rare)
4. SKIP → Review later
```

### 5. Architect Decision Actions

**Option 1: ACCEPT → COMPLETED**

```yaml
# Update frontmatter
status: "COMPLETED"
completion_date: "2025-11-14"
verification_status: "passed"
agent_notes:
  - timestamp: "2025-11-14T17:00:00Z"
    agent: "gh_verify"
    note: "Verified by architect, all checks passed, moved to COMPLETED"
    action: "completed"
```

```bash
# Move file
mv .claude/AGENTS/PROPOSALS/STAGED/{proposal}.md \
   .claude/AGENTS/PROPOSALS/COMPLETED/

# Optional: Close GitHub issue
if [ -n "$github_issue" ]; then
    gh issue close $github_issue --comment "Completed via {proposal_id}"
fi
```

**Option 2: REWORK → ACTIVE_WORK**

```yaml
# Update frontmatter
status: "ACTIVE_WORK"
progress_percent: 80  # Reduce based on rework needed
verification_status: "failed"
agent_notes:
  - timestamp: "2025-11-14T17:00:00Z"
    agent: "gh_verify"
    note: "Architect requested rework: {specific issues}. Moved back to ACTIVE_WORK."
    action: "blocked"

# Add rework requirements
unblock_requirements:
  - "Add integration test for error path X"
  - "Optimize algorithm Y to O(n log n)"
  - "Handle database lock edge case"
```

```bash
# Move back
mv .claude/AGENTS/PROPOSALS/STAGED/{proposal}.md \
   .claude/AGENTS/PROPOSALS/ACTIVE_WORK/
```

**Option 3: REJECT → Delete**

```yaml
# Final notes
agent_notes:
  - timestamp: "2025-11-14T17:00:00Z"
    agent: "gh_verify"
    note: "Rejected by architect: {reason}. Archiving proposal."
    action: "stalled"
```

```bash
# Archive, don't delete (for historical tracking)
mv .claude/AGENTS/PROPOSALS/STAGED/{proposal}.md \
   .claude/AGENTS/PROPOSALS/.old/rejected-{proposal}.md
```

**Option 4: SKIP → Keep in STAGED**

No changes - proposal stays in STAGED for next review cycle.

### 6. Verification Report

After processing all proposals, create report:

```bash
# Write report
cat > .claude/AGENTS/REPORTS/verify-$(date +%Y-%m-%d).md << EOF
# Verification Session: $(date +%Y-%m-%d)

**Proposals Processed**: {count}

## Summary

| Status | Count | %age |
|--------|-------|------|
| COMPLETED | {n} | {x}% |
| REWORK | {n} | {x}% |
| REJECTED | {n} | {x}% |
| SKIPPED | {n} | {x}% |

## Details

### {proposal_id_1}
**Title**: {title}
**Action**: COMPLETED
**Rating**: 92%
**Commits**: 5
**Notes**: All checks passed, production ready

### {proposal_id_2}
**Title**: {title}
**Action**: REWORK
**Rating**: 65%
**Commits**: 3
**Issues**:
- Missing edge case tests
- Performance concern in algorithm X

EOF
```

## Verification Checklist

For each proposal, verify:

### Code Quality
- [ ] All tests passing
- [ ] Coverage >90%
- [ ] Pre-commit hooks pass
- [ ] No new linting warnings
- [ ] No security vulnerabilities

### Implementation Completeness
- [ ] All acceptance criteria met
- [ ] All tasks in implementation plan checked
- [ ] Edge cases handled
- [ ] Error handling comprehensive
- [ ] Documentation updated

### Git Hygiene
- [ ] Commits reference proposal_id
- [ ] Commit messages clear and descriptive
- [ ] All commits in frontmatter verified
- [ ] No uncommitted changes
- [ ] Branch clean (if using feature branches)

### Production Readiness
- [ ] No hardcoded values
- [ ] Secrets not committed
- [ ] Performance acceptable
- [ ] Logging adequate
- [ ] Monitoring/alerting considerations

### Maintainability
- [ ] Code follows project style
- [ ] Complexity within limits (CC <15)
- [ ] Proper abstractions used
- [ ] Comments for complex logic
- [ ] README/docs updated if needed

## Decision Matrix

| Rating | Tests | Concerns | Action |
|--------|-------|----------|--------|
| 90-100% | ✅ Pass | None/Minor | ACCEPT |
| 70-89% | ✅ Pass | Addressable | ACCEPT with notes |
| 70-89% | ✅ Pass | Significant | REWORK |
| 40-69% | ✅ Pass | Critical | REWORK |
| <40% | Any | Critical | REJECT |
| Any | ❌ Fail | Any | REWORK |

Architect can override any recommendation.

## Example Session

```
$ /gh_verify

Found 3 proposals in STAGED:
- issue-21-bdd-testing
- issue-25-new-feature
- issue-26-bug-fix

=== Proposal 1: issue-21-bdd-testing ===
Title: BDD Testing Framework Implementation
Priority: high
Commits: 4 (all verified ✓)
Tests: ✓ 643 passing, 91% coverage
Pre-commit: ✓ All hooks passed

Adversarial Analysis:
Rating: 88%
Concerns:
1. UI tests are placeholders (require playwright)
2. No performance benchmarks for BDD vs unit tests
3. Examples folder could use more conversation flows

Recommendation: ACCEPT (concerns are minor, can address in future)

What action?
1. ACCEPT → Move to COMPLETED
2. REWORK → Move to ACTIVE_WORK
3. REJECT → Archive
4. SKIP → Review later

[Architect: 1]

✅ Moved issue-21-bdd-testing to COMPLETED

=== Proposal 2: issue-25-new-feature ===
[... similar process ...]

---

Verification complete.
Report saved: .claude/AGENTS/REPORTS/verify-2025-11-14.md

Summary:
- 2 COMPLETED
- 1 REWORK
- 0 REJECTED
- 0 SKIPPED
```

## Integration with /gh_active_work

Proposals moved back to ACTIVE_WORK from verification:
- Have `unblock_requirements` populated
- `progress_percent` reduced appropriately
- `agent_notes` explain what needs rework
- Ready to be picked up by next `/gh_active_work` run

## Rules

### MUST DO

✅ Validate ALL proposals in STAGED
✅ Run automated checks (tests, linting, etc.)
✅ Perform adversarial analysis
✅ Get architect decision for each
✅ Update frontmatter in place
✅ Move to appropriate folder
✅ Create verification report
✅ Track all actions in agent_notes

### MUST NOT DO

❌ Auto-accept without architect review
❌ Create new proposal files
❌ Skip automated validation
❌ Leave proposals in STAGED indefinitely
❌ Delete proposals (archive instead)
❌ Modify implementation code during review

## Success Criteria

- All STAGED proposals reviewed
- Each moved to COMPLETED or ACTIVE_WORK
- Verification report generated
- Architect fully informed of decisions
- Git history intact and traceable

---

**Status**: Specification (command implementation required)
**Next Step**: Implement `/gh_verify` slash command handler
