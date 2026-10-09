---
schema_version: 1
handoff_id: fe07
parent_handoff_ids: [a12c]
lineage: deterministic
chain: [standalone-3fd9]
repo: cfengine-core
workspace: main
branch: fix/simulate-pkg-mapremove
head_sha: 5ddb6d97b11ed82655887decd48cb39c221275c8
created_at: 2026-10-09T08:25:17-0400
writer: claude-code
---
# Handoff — simulate-json upstream go-package ready; advfix2 folded into #48

## The Goal
Get the `--simulate-json` work (and the pre-existing simulate-mode package bugs its adversary
reviews surfaced, plus the #6302 default-route resubmission) ready for djbclark to send to
upstream cfengine/core and NorthernTechHQ/libntech. The rework brief is
`~/.local/state/handoffs/chains/standalone-3fd9/rework-brief-2026-10-08.md` (sections A–F,
ground rules 1–9). Standing order: fix everything found, nits included, as long as the PRs do
not get awkwardly long. Nothing is sent upstream by an agent; djbclark gives the go.

## Where We Are
Everything is prepared and doubly verified (arm64 container Linux gate as root + fork x86_64
CI 10/10 on every tip). Nothing has been sent upstream. The go-package is three texts files in
the chain dir, each with PR title, body (ELI5 block, adversary verdict line, Claude Code
footer), head SHA and send order:
- `upstream-pr-texts-2026-10-09.md`: A #39 a5a6b6b6a, B #44 9f89e085f, C #45 6c053e100,
  D #40 f8342de12, E #41 e00b8990b (base simulate-json-files; Draft until D merges),
  F libntech djbclark/libntech#9 1a3b12fef.
- `upstream-6302-texts.md`: #47 684043972 (default route lowest metric, Linux before/after).
- `upstream-advfix-texts-2026-10-09.md`: #48 6eb70adaf (record op validation + all-invalid
  guard, 3 unit tests), #49 c5e6a37fb (map key collision), plus a rebase note for the
  simulate-json stack once #48/#49 merge.
All eight fork PR heads were re-read with `gh pr view` at 08:05 EDT and match these SHAs.

This session's last slice (advfix2) amended #48 from 00d53669a to 6eb70adaf: the manifest
`assert((MapSize(present) != 0) || (MapSize(absent) != 0))` became an INFO log
("No present or absent packages to manifest") plus destroy-and-`return true`, mirroring the
diff path, with `test_manifest_all_records_invalid`. Report, brief, adversary notes and PR body
are `advfix2-{report,brief,adversary,pr48-body}.md` in the chain dir.

The main checkout `~/src/cfengine-core` sits on an old branch (`fix/simulate-pkg-mapremove`
@ 5ddb6d97b, behind origin by one August review commit) and was never edited by agents; its
dirty `.gitignore` (graft cache line) and `simulate_mode_test.xml` predate the session. All
work happened in cow pastures, all now removed; `~/opt/cfengine-dev` kept, every other
`~/opt/cfengine-*` of this session removed (`~/opt/cfengine-dev-47{27,34,35}` are older,
not ours; djbclark chose to leave them).

## What We Tried
1. Reopening fork PR #42 for CI after a force-push: GitHub refuses; a fresh CI-only PR (#46,
   base master) was used instead and closed after 10/10.
2. gh overnight: "token invalid" was first read (by me and the phase3-gate agent) as a revoked
   token. Real cause: the keychain-stored token is unreadable while the login session is
   locked. Fixed permanently by djbclark's choice with `gh auth login --with-token
   --insecure-storage` (token now in `~/.config/gh/hosts.yml`, 0600). The sudo-secretspec
   `GH_TOKEN` wrapper and Composio GitHub tools remain fallbacks.
3. Mac acceptance `29_simulate_mode/`: three tests fail as uid 501 for environmental reasons
   (expected files assume root; `testall --bindir` does not link `diff`). Only the Linux gate
   as root passes all four. Recorded in the checkout's CLAUDE.md.
4. `handoffs` branch: the first commit (2aeb02a00) carried only README.md because
   `.git/info/exclude` lists `docs/handoffs/` and excludes apply to every worktree, so
   `git add docs/handoffs` was a silent no-op. Fixed with `git add -f` (ccf89ecc4).
   Memory note `handoffs-branch-needs-git-add-f.md`.
5. Running two Linux gate containers plus a Mac build at once oversubscribes the 8-core Mac;
   one or two gate lanes max. Also: I started a duplicate gate on #47 while the advfix agent
   had one running; stopped mine.
6. `cow-pasture remove a b --force` ignores `--force` with several names; one name per call.
   Queued in `~/src/djbclark-ade/docs/queue.md`.

## Key Decisions
- Pre-existing bugs found by adversary reviews became standalone fix PRs on upstream/master
  (#48, #49) rather than being folded into the simulate-json stack (keeps D/E reviewable;
  rebase note covers the overlap). Rejected: fixing them inside #40/#41.
- #6302 resubmitted as #47 with Linux before/after rather than reopening the old PR.
- advfix2 added only the manifest guard and one test; no symmetric diff-path test (that path
  never had the assert); adopted the adversary's hardening that the test asserts the
  "Invalid package operation record" line is present.
- Handoffs live on the orphan `handoffs` branch of the fork (djbclark's choice), never on a
  PR branch; the working tree keeps `docs/handoffs/` git-excluded.
- Keychain copy of the gh token left in place (djbclark's choice).
- Old 2026-10-08 texts moved to `old/` rather than deleted.

## Evidence & Data
- Fork CI run for #48 tip 6eb70adaf: 37923575793, 10/10, acceptance 44m30s, green 08:16 EDT.
- Linux gate on 6eb70adaf: unit 75/75, simulate_mode_test 5/5, acceptance 4/4
  (`$S/advfix2-linuxgate/out/6eb70adaf…/progress.log` in the session scratchpad; gate
  scripts in `chain/linuxgate/`).
- Gate recipe: image `cfengine-build:u24`, `container run --rm --name gate-X -c 3 -m 6G
  -v <pasture>:/src:ro -v <gatedir>:/gate cfengine-build:u24 /gate/gate-core.sh <sha>`.
- Reports in the chain dir: phase2-pkgs, phase3-gate, prep-6302, adversary-6302, advfix,
  advfix2, fork-backlog-triage(-2).

## Operator Feedback
- "Do as much as you can without me, but of course do not submit anything upstream."
- Overnight choices: fix/re-gate/re-push real defects; adversary bugs become fix PRs on the
  fork; #6302 fully gated and added to the batch; clean up, handoff, one Hermes ping.
- gh fix: insecure file storage. Handoffs: dedicated branch. Stray ~/opt trees: leave.
- Agent dispatch rules: acp-dispatch footer on every brief, STATUS line first, `.done` marker,
  BLOCKED: instead of questions, bounded foreground waits; builds/tests through
  `~/ops/site-private/bin/bg`; agents never edit `~/src/cfengine-core`.

## Where We're Going
1. **djbclark reviews the three texts files and gives the go.** Then send in order A, B, C,
   then D, then E (Draft until D merges); F to NorthernTechHQ/libntech; #47, #48, #49 are
   independent. Fill the `<A>/<B>/...` placeholders as upstream numbers appear. Agents are
   hook-denied from `gh` writes to cfengine/* and NorthernTechHQ/*: he sends, or lifts the
   hook for that step.
2. Before sending, re-verify tips: `for n in 39 44 45 40 41 47 48 49; do gh pr view $n -R
   djbclark/core --json headRefOid -q .headRefOid; done`.
3. After #48/#49 merge upstream: rebase the simulate-json stack per the rebase note in
   `upstream-advfix-texts-2026-10-09.md` (one extra hunk in `ManifestPkgOperations()`).
4. Deferred by djbclark: delete `fix/default-route-lowest-metric` (3d10206ee) only if upstream
   #6302 no longer needs it; `nix-collect-garbage` on vps; keep or cut the ELI5 blocks.
5. If upstream review asks for changes, work in a fresh pasture:
   `~/src/djbclark-ade/bin/cow-pasture create <name> --source ~/src/cfengine-core`, gate
   with the Linux recipe above, adversary review, then push with `--force-with-lease`.

## Detached jobs
none (no open bigteam job records for this session; every sub-agent delivered its `.done`).

## Quick Start
```bash
cat ~/.local/state/handoffs/chains/standalone-3fd9/SESSION_LOG.md
ls ~/.local/state/handoffs/chains/standalone-3fd9/
gh pr list -R djbclark/core --state open          # expect #39 #40 #41 #44 #45 #47 #48 #49
for n in 39 44 45 40 41 47 48 49; do gh pr view $n -R djbclark/core --json headRefOid -q .headRefOid; done
git -C ~/src/cfengine-core ls-tree -r --name-only handoffs   # handoff copies on the fork
```
