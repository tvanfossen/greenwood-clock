---
description: Interactive review with architect to close completed issues
---

# GitHub Issue Closure

Interactive workflow with architect to review completed work and close GitHub issues. This command facilitates the final review and closure process.

When this command is invoked, execute the following workflow:

1. **Scan for completed work**:
   ```bash
   # Find all completed proposals
   find PROPOSALS/COMPLETED -name "issue-*.md" -type f | sort

   # Get all open GitHub issues
   gh issue list --state open --json number,title,labels,state
   ```

2. **Identify issues ready for closure**:
   - Cross-reference completed proposals with open GitHub issues
   - Find issues that have corresponding completed proposals
   - Group by completion date (most recent first)

3. **For each completed issue, present to architect**:

   **FIRST: Check for @tvanfossen comments in proposal**:
   - Scan PROPOSALS/COMPLETED/issue-XX.md for any `@tvanfossen` comments
   - If @tvanfossen comments exist, include them prominently in presentation
   - Architect may have already provided closure instructions in markdown

   **A. Issue Summary**:
   ```markdown
   ## Issue #XX: [Title]

   **Status**: Completed
   **Proposal**: PROPOSALS/COMPLETED/issue-XX-name.md
   **Category**: Bug / Feature / etc.
   **Severity**: CRITICAL / HIGH / MEDIUM / LOW

   **Architect Comments** (@tvanfossen):
   [Show any @tvanfossen comments from the proposal file]

   **Implementation Summary**:
   - Implemented: [Date]
   - Commits: abc123f, def456g
   - Files changed: X files
   - Tests added: Y tests
   - Coverage: Z% → Z+N%
   ```

   **B. Implementation Details**:
   - Show key commits related to this issue
   - Show test coverage (unit tests + BDD tests)
   - Show any deviations from original proposal
   - Show success criteria checklist
   - Highlight any @tvanfossen comments about the implementation

   **C. Verification Questions**:
   Ask architect to respond with @tvanfossen marker:
   1. "Does the implementation meet the original requirements?"
   2. "Are the tests adequate?"
   3. "Any concerns with the approach taken?"
   4. "Ready to close this issue? (respond with @tvanfossen)"

4. **Based on architect response**:

   **If "Yes, close it"**:
   - Ask for closing comment/summary (or use default)
   - Close issue with architect-approved comment:
     ```markdown
     Completed in [commit hash(es)].

     **Implementation**:
     - [Summary of what was done]

     **Tests**:
     - Unit tests: [list]
     - BDD tests: [list]

     **Proposal**: See PROPOSALS/COMPLETED/issue-XX-name.md for full details.
     ```
   - Mark proposal as "Closed" in Implementation Notes
   - Continue to next issue

   **If "No, needs changes"**:
   - Ask: "What changes are needed?"
   - Document changes in proposal
   - Move proposal: `PROPOSALS/COMPLETED/` → `PROPOSALS/ACTIVE_WORK/`
   - Update proposal status to "ACTIVE - Revisions needed"
   - Add architect's feedback to "Implementation Notes"
   - Do NOT close GitHub issue
   - Skip to next issue

   **If "No, revert it"**:
   - Ask: "Revert completely or create follow-up issue?"
   - If revert:
     - Create revert commits
     - Move proposal to `PROPOSALS/STALLED/` with "REVERTED" status
     - Comment on issue explaining revert
     - Do NOT close issue
   - If follow-up:
     - Keep completed proposal where it is
     - Ask architect to create new issue for additional work
     - Close original issue as completed
   - Skip to next issue

   **If "Skip for now"**:
   - Move to next issue
   - Revisit at end of session

5. **Handle issues without completed proposals**:
   - Scan open issues that DON'T have proposals
   - Ask architect:
     ```markdown
     Issue #XX: [Title] is open but has no proposal.

     Options:
     1. Should I create a proposal? (run /gh_issues_workflow)
     2. Close as duplicate/wontfix?
     3. Keep open for now?
     ```

6. **Generate closure session report**:
   Update `docs/workflow_reports/closure_session.md`:
   ```markdown
   # Issue Closure Session

   **Date**: YYYY-MM-DD
   **Architect**: @username

   ## Closed (X issues)
   - Issue #XX: [Title] - Completed in abc123f
   - Issue #YY: [Title] - Completed in def456g

   ## Reopened for Revisions (Y issues)
   - Issue #ZZ: [Title] - Needs: [changes requested]

   ## Reverted (Z issues)
   - Issue #AA: [Title] - Reason: [why reverted]

   ## Skipped (pending further review)
   - Issue #BB: [Title]

   ## Statistics
   - Issues closed: X
   - Issues reopened: Y
   - Issues reverted: Z
   - Open proposals remaining: W
   ```

7. **Final summary**:
   ```markdown
   # Closure Session Complete

   **Issues Closed**: X
   **Issues Reopened**: Y
   **Issues Remaining Open**: Z

   **Next Steps**:
   - Implementor: Work on reopened issues in ACTIVE_WORK/
   - Architect: Review new issues without proposals
   - Architect: Approve proposals in TODO/

   **Open Issue Breakdown**:
   - With proposals in TODO/: X issues
   - With proposals in ACTIVE_WORK/: Y issues
   - With proposals in STALLED/: Z issues
   - With proposals in COMPLETED/ (pending closure): W issues
   - Without proposals: V issues
   ```

## Interactive Prompts

The command should be conversational and guide the architect through each issue. Example interaction:

```
Assistant: I found 3 completed issues ready for closure. Let's review them.

---
Issue #19: Tool call parsing failure
- Completed: 2025-11-07
- Commits: 8e22ca5
- Tests: 5 new regression tests added
- Coverage: 90% → 91%

Implementation:
- Replaced brace-counting parser with json.JSONDecoder.raw_decode()
- Handles nested braces in text fields correctly
- All existing tests pass

Ready to close this issue? (yes/no/skip)

Architect: yes
