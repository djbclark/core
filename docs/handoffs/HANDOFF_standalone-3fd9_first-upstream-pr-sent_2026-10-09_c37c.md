---
schema_version: 1
handoff_id: c37c
parent_handoff_ids: [fe07]
lineage: deterministic
chain: [standalone-3fd9]
repo: cfengine-core
workspace: main
branch: fix/simulate-pkg-mapremove
head_sha: 5ddb6d97b11ed82655887decd48cb39c221275c8
created_at: 2026-10-09T08:50:00-0400
writer: claude-code
---
# Handoff — first upstream PR sent (cfengine/core#6389); one PR at a time on djbclark's go

## The Goal
Send the prepared `--simulate-json` batch (five cfengine/core PRs A–E, one libntech PR F)
plus the three independent fixes (#47 default route, #48 record validation, #49 map key)
from the fork `djbclark/core` to upstream, one PR at a time, each on djbclark's explicit go.
Parent handoff fe07 (same directory) holds the preparation history; the rework brief is
`~/.local/state/handoffs/chains/standalone-3fd9/rework-brief-2026-10-08.md`.

## Where We Are
- **A is upstream: https://github.com/cfengine/core/pull/6389** (swap fix, fork #39, head
  a5a6b6b6a, base master, open, not draft). Sent 2026-10-09 ~08:55 EDT from this session with
  `gh pr create`; the review-gate hook did not block it. No upstream reaction yet.
- Nothing else has been sent. Remaining in send order: B (#44, 9f89e085f), C (#45, 6c053e100),
  then D (#40, f8342de12), then E (#41, e00b8990b; Draft until D merges), F to
  NorthernTechHQ/libntech (djbclark/libntech#9, 1a3b12fef); #47 (684043972), #48 (6eb70adaf),
  #49 (c5e6a37fb) independent. All eight fork tips re-verified this session.
- **ELI5 blocks are gone from every upstream text** (djbclark's ruling this session: the
  audience is experienced C programmers with deep cfengine/libntech knowledge). Eight blocks
  stripped from `upstream-pr-texts-2026-10-09.md` and `upstream-advfix-texts-2026-10-09.md`
  in the chain dir; the ELI5 versions are `old/*-with-eli5.md` there. Fork link text is now
  "Fork CI: <url>". The fork PRs themselves still carry their ELI5 blocks (left on purpose).
- Texts file bookkeeping: section A marked SENT with the URL; E's stacking line now reads
  "#6389, #<B>, #<C>"; `<D>` and D's `<libntech PR URL>` still to fill.
- Fork records updated: comment on djbclark/core#39 and on umbrella issue djbclark/core#1.
- Main checkout unchanged by agents: `fix/simulate-pkg-mapremove` @ 5ddb6d97b, dirty only with
  the pre-existing `.gitignore` graft line and `simulate_mode_test.xml`.
- Memory note written: `~/.claude/projects/-Users-djbclark-src-cfengine-core/memory/upstream-texts-audience-no-eli5.md`.

## What We Tried
1. Showing all seven first-round submissions at once: djbclark stopped that; he wants exactly
   one PR shown and sent at a time. Do not batch-present.
2. `session_log.py write` with `blockers` as a string fails ("must be a list of strings");
   pass a list.
3. The `ls --time-style` flag does not exist on macOS `ls`; use `ls -lt`.

## Key Decisions
- **No ELI5 blocks upstream** (djbclark, 2026-10-09). Rejected: keeping them with the
  "logic explained without C" link; rejected: trimming them. Fork PRs keep theirs.
- **One PR per go.** Approval for A was not approval for B. Rejected: sending the whole
  independent first round (A, B, C, F, #47, #48, #49) together.
- **A sent without a Jira ticket** (`Ticket: none`, no `CFE-` key), per the rework brief's
  A3/B3/C3/D16/E11 items. djbclark asked why GitHub rather than Jira; answered: PRs are
  GitHub, bug tickets are Jira (CFE at northerntech.atlassian.net), `CFE-####:` titles only
  when a ticket exists; upstream merges both forms. He has not yet chosen between leaving
  #6389 ticketless, filing a CFE ticket for it, or deciding per PR going forward.
- **Disclosure wording left as written** in each file (his voice). Flagged, not unified:
  simulate/advfix bodies say "AI-assisted. I can't vouch for the C syntax beyond a basic
  level, but I did look at the logic and it seemed sound."; the #47 text says "AI-assisted,
  reviewed and verified by me before submitting." and has no Fork CI line or Claude Code
  footer.
- Fork-side comments (#39, #1) posted as housekeeping; upstream writes only on his go.

## Evidence & Data
- #6389 body: 915 chars total, 342 chars of prose, rest is test output, gate line,
  disclosure, footer. The shared rules (`~/src/cfengine-all/AGENTS.md`, "prose proportional
  to code") name ~100 chars as the ceiling for a 2-line change; this is the one soft spot
  against upstream's August asks. If a maintainer comments on length, trim B onward to one
  sentence plus the CI link.
- Scratch copies of what was sent: `<scratchpad>/pr-A-body.md`, `pr-A-create.log`
  (session-specific; the texts file is the durable copy).
- Fork tips (gh, 2026-10-09 ~08:50 EDT): 39 a5a6b6b6a, 40 f8342de12, 41 e00b8990b,
  44 9f89e085f, 45 6c053e100, 47 684043972, 48 6eb70adaf, 49 c5e6a37fb; all open, none draft.
- Tests run this session: none (no code changed; all gating is in fe07).

## Operator Feedback
- "We don't want to include the ELI5 blocks in the upstream issues or PRs. The upstream
  issues or PRs should be geared to people who are experienced C programmers with a deep
  knowledge of the cfengine and libntech codebases."
- "No just show me the first one. We want to do only one at a time."
- "Go ahead and submit it" (A only).
- Asked whether upstream uses Jira or GitHub, then whether we have already done what
  upstream desires; answered yes except the prose-length soft spot above.

## Where We're Going
1. **Wait for djbclark's go on B**, then: verify
   `gh api repos/djbclark/core/git/ref/heads/fix/simulate-pkg-removal-with-version -q .object.sha`
   = 9f89e085ff0cd24b82b6b3b35af90e97abef028c; extract section B's BEGIN/END BODY from
   `~/.local/state/handoffs/chains/standalone-3fd9/upstream-pr-texts-2026-10-09.md` to a
   file; `gh pr create -R cfengine/core --base master --head
   djbclark:fix/simulate-pkg-removal-with-version --title 'Fixed simulate diff mode not
   reporting a removal with a version' --body-file <file>`; then mark B SENT in the texts
   file, fill `#<B>` in E's line, comment on fork #44 and #1, `hermes-ping` once. Show him
   the B text first if he asks; never show more than one.
2. Before each send, check #6389 for a maintainer reaction
   (`gh pr view 6389 -R cfengine/core --comments`); a length complaint changes the form of
   every later body (one sentence + CI link).
3. Open for djbclark: ticket policy (leave #6389 ticketless / file CFE ticket / per PR);
   disclosure wording and missing footer in `upstream-6302-texts.md`.
4. After #48/#49 merge upstream: rebase the simulate-json stack per the rebase note at the
   end of `upstream-advfix-texts-2026-10-09.md`.
5. Deferred by djbclark (unchanged): delete `fix/default-route-lowest-metric` only if
   upstream #6302 no longer needs it; `nix-collect-garbage` on vps; keychain gh token copy.
6. Handoffs go on the fork's orphan `handoffs` branch via a temporary worktree and
   `git add -f` (`.git/info/exclude` lists `docs/handoffs/`); this file is committed there
   as well as kept in the chain dir.

## Detached jobs
none (no open bigteam job records owned by this session).

## Quick Start
```bash
cat ~/.local/state/handoffs/chains/standalone-3fd9/SESSION_LOG.md
gh pr view 6389 -R cfengine/core --json state,comments -q '"\(.state) comments=\(.comments|length)"'
gh pr list -R cfengine/core --author djbclark --state open
for n in 44 45 40 41 47 48 49; do gh pr view $n -R djbclark/core --json headRefOid -q .headRefOid; done
sed -n '/^## B\. /,/^END BODY/p' ~/.local/state/handoffs/chains/standalone-3fd9/upstream-pr-texts-2026-10-09.md
```

## Addendum 2026-10-10 — rule correction

"One PR at a time, each on djbclark's explicit go" above understates the rule. djbclark
clarified on 2026-10-10: only one of our PRs is open upstream at a time. B is neither offered
nor sent until upstream has reviewed or merged #6389, and then still only on his go. As of
2026-10-10 20:30 EDT #6389 has only the cf-bottom bot comment (10:48 UTC, pinging larsewi);
we are waiting. Durable copies of the rule: `~/src/cfengine-all/AGENTS.md` item 11, memory
note `one-upstream-pr-open-at-a-time`, and the Tier 1 log. Also this day: `.gitignore`
`/graft/` drift traced to graft (trailhq/Graft#582); `GRAFT_NO_GITIGNORE=1` set on the graft
MCP definitions and hooks, tree clean.
