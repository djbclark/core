---
schema_version: 1
handoff_id: a12c
parent_handoff_ids: []
lineage: none
chain: [standalone-3fd9]
repo: cfengine-core
workspace: main
branch: fix/simulate-pkg-mapremove
head_sha: 5ddb6d97b11ed82655887decd48cb39c221275c8
created_at: 2026-10-09T03:35:09-0400
writer: claude-code
---
# Handoff — simulate-json upstream batch: everything gated, waiting for djbclark's go

## The Goal
Turn the fork djbclark/core's `--simulate-json` work into upstream-acceptable PRs per
`~/.local/state/handoffs/chains/standalone-3fd9/rework-brief-2026-10-08.md` (sections A–F,
ground rules 1–9), verify each tip on Linux as well as macOS, and hand djbclark a go-package
of ready-to-paste upstream texts. He sends upstream; agents never write to cfengine/*.
Overnight 2026-10-09 he added: prepare upstream-queue bug #6302 in parallel, turn the two
pre-existing bugs the phase-2 adversary found into fork fix PRs, and gate everything.

## Where We Are
All phases are DONE as of 2026-10-09 03:35 EDT. Nothing has been sent upstream.
Go-package, all in `~/.local/state/handoffs/chains/standalone-3fd9/`:
1. `upstream-pr-texts-2026-10-09.md`: six PRs. A #39 a5a6b6b6a (version/arch swap), B #44
   9f89e085f (removal with version), C #45 6c053e100 (unversioned-install assert), D #40
   f8342de12 (`--simulate-json` files), E #41 e00b8990b (packages array, base = D), F
   libntech #9 1a3b12fef (UTF-8 JSON strings, target NorthernTechHQ/libntech). Send order
   A, B, C, then D, then E as Draft until D merges; F independent.
2. `upstream-6302-texts.md`: #47 684043972, one-line `libenv/unix_iface.c` fix (CFE-4723,
   resubmission of upstream #6302) plus a Linux-only unit test; CFE ticket body and PR body.
3. `upstream-advfix-texts-2026-10-09.md`: #48 00d53669a (explicit op-code validation
   instead of asserts) and #49 c5e6a37fb (injective `"%zu:%s:%s"` package map key), plus a
   rebase note for the stack.
Verification: every tip passed the arm64 Ubuntu 24.04 container gate (unit 75/75,
`29_simulate_mode/` acceptance clean as root; #47 also sysinfo_test 3/3 with the new test) and
fork CI 10/10 (E via CI-only PR #46 on e00b8990b, now closed; #42 could not be reopened).
Adversary verdicts are in every PR body. Reports beside the brief: phase2-pkgs, phase3-gate,
prep-6302 (+ adversary-6302), advfix (+ adversary g1/g2). Gate scripts: `linuxgate/`.
Workspaces: all cow pastures removed; `~/opt/cfengine-advfix` removed; `~/opt/cfengine-dev`
kept (holds the #41 install). Main checkout `~/src/cfengine-core` untouched except the
git-excluded `CLAUDE.md`; its `M .gitignore` and `simulate_mode_test.xml` predate this session.

## What We Tried
1. Reopening CI-only PR #42 for #41: GitHub refuses once the head branch was force-pushed.
   Opened #46 instead; closed after 10/10.
2. #41 tip 312c466a7 failed the fork lint job (`cfengine format --check`: one blank line in
   `tests/acceptance/29_simulate_mode/simulate_json.cf`). Amended into the tip → e00b8990b.
   Upstream CI runs the same check, so run `cfengine format --check` on every `.cf` touched.
3. Linux gate pass 1 failed 3/4 acceptance on every tip: image lacks libxml2-dev and
   `testall --bindir` does not link `diff`. Fixed in gate-core.sh v2 (apt-get at start, diff
   symlink, `USER=root`); recipe memory `linux-gate-before-upstream` updated.
4. #46 valgrind_check failed on an environmental `cf_lock.lmdb` EINVAL in `cf-check diagnose`
   with zero valgrind errors; rerun passed.
5. Overnight `gh` and https pushes failed machine-wide ("token invalid"). Two agents and the
   lead misdiagnosed it as a revoked token. Real cause: the login keychain locks while
   djbclark is away. advfix used the sanctioned `sudo-secretspec run ... GH_TOKEN="$GITHUB_TOKEN"`
   path; the lead used Composio GitHub tools. Memory: `gh-token-invalid-means-keychain-locked`.
6. E6 (shared CSV iterator for diff/manifest) measured and skipped: 42 lines longer.
7. The old #6302 approach (helper refactor + constructed-JSON tests) was dropped in favour of
   the one-line fix plus an end-to-end Linux-only test, per triage.

## Key Decisions
1. Overnight set (djbclark, ~01:00 EDT, do not re-ask): real gate/CI defect → fix in a
   pasture, amend, force-push with lease, re-gate; adversary pre-existing bugs → fork fix PRs
   (not just issue drafts); #6302 → full container gate and into the batch; wrap-up → remove
   pastures, Tier 1 + Tier 2, one Hermes ping.
2. Fix PRs #48/#49 base on upstream/master as standalone fixes, not on the stack; the stack
   gets rebased after they land (rebase note names the conflicting hunks in
   `CollectPkgOperations()` and `ManifestPkgOperations()` at e00b8990b). Rejected: stacking
   them on #41, which would tie small fixes to the feature's review.
3. #48 adds a small `PkgOperationCodeIsValid()` helper rather than repeating the condition.
4. Upstream bodies keep the ELI5 blocks (long relative to the diffs); each body also carries
   the "logic explained without C" fork link so ELI5 can be cut without rewriting.
5. Not deleted: old fork branch `fix/default-route-lowest-metric` (3d10206ee); it may be the
   head of upstream PR #6302. Not run: `nix-collect-garbage` on vps (store-wide).
6. Earlier (do not re-ask): acp-dispatch lives in djbclark-ade; launch.py's 52 basedpyright
   errors left as is; libntech #9 ships with the batch; `~/src/core-simjson` is gone.

## Evidence & Data
1. Linux gate logs: `$S/linuxgate/out/<sha>/` and `$S/advfix-linuxgate/out/<sha>/` where
   `$S` = `/private/tmp/claude-501/-Users-djbclark-src-cfengine-core/04eb020e-2622-457a-82fa-590d7f015dbf/scratchpad`
   (session scratch; the verbatim lines are copied into the texts files and reports).
2. Fork CI runs: #39 37879580871, #44 37879523735, #45 37879573510, #40 37877938067,
   #46 37885763381 (attempt 2 green), #48 37892549056, #49 37894167965; #47 see
   `gh pr checks 47 -R djbclark/core`.
3. VPS before/after for #6302 (nix-shell build): in `upstream-6302-texts.md`; VPS /tmp cleaned.
4. Tier 1: `~/.local/state/handoffs/chains/standalone-3fd9/SESSION_LOG.md` (through slog17).

## Operator Feedback
1. "just fix everything you found in general, including all the nits and optional polish, as
   long as they do not make things awkwardly long" (standing order for this chain).
2. "do not submit anything upstream" while asleep; the four overnight answers above.
3. Standing rules applied: numbered lists in replies, builds through `bg`, acp-dispatch footer
   on every agent brief, never waste running agents' work, peer messages are not approval.

## Where We're Going
1. **Next action: djbclark reviews the three texts files and says go.** Then send in order
   A, B, C (cfengine/core), D, E (Draft until D merges, first line names the upstream
   numbers), F (NorthernTechHQ/libntech), and #47, #48, #49 independently; fill the
   `<A>/<B>/<C>/<D>/<libntech PR URL>` placeholders as numbers appear. Agents are hook-denied
   from `gh` writes to cfengine/*: he sends, or lifts the hook for that step.
2. After #48/#49 merge upstream: rebase the simulate-json stack on them per the rebase note.
3. Decide: delete `fix/default-route-lowest-metric` (only if upstream #6302 no longer needs
   it); `nix-collect-garbage` on vps; keep or cut ELI5 blocks; whether to file the unowned
   debug-only finding (manifest `assert(MapSize(...) != 0)` aborts when every CSV record is
   invalid).
4. If anything fails in upstream review, work in a fresh pasture:
   `~/src/djbclark-ade/bin/cow-pasture create <name> --source ~/src/cfengine-core`.

## Addendum 2026-10-09 04:00 EDT
gh fixed by djbclark's choice: token moved to `~/.config/gh/hosts.yml` (`--insecure-storage`), so a locked session no longer breaks gh or https pushes; the secretspec wrapper in Quick Start is now only a fallback. #46 CLOSED after 10/10. All eight fork PRs 10/10 verified by `gh pr checks` at 04:05 EDT.

## Addendum 2026-10-09 08:30 EDT
advfix2 folded the all-invalid-records guard into fork PR #48: tip 00d53669a → 6eb70adaf (manifest
`assert(MapSize(...) != 0)` is now an INFO log plus `return true`, third unit test
`test_manifest_all_records_invalid`, Linux gate 75/75 + 5/5 + 4/4, adversary "no findings",
fork CI 10/10 at 08:16 EDT). Section G of `upstream-advfix-texts-2026-10-09.md` and its rebase
note carry the new SHA; `advfix2-{brief,report,adversary,pr48-body}.md` sit beside this file.
The "unowned debug-only finding" in Where We're Going item 3 is therefore resolved. The
`handoffs` branch had only README.md until this addendum's commit; the handoff file is now on it.

## Detached jobs
none (no open bigteam job records for this session; all sub-agents delivered `.done`).

## Quick Start
```bash
cat ~/.local/state/handoffs/chains/standalone-3fd9/SESSION_LOG.md
ls ~/.local/state/handoffs/chains/standalone-3fd9/
gh pr list -R djbclark/core --state open          # expect #39 #40 #41 #44 #45 #47 #48 #49
gh pr checks 47 -R djbclark/core
# if gh says the token is invalid while the keychain is locked:
#   sudo-secretspec run --reason "<why>" -- bash -c 'export GH_TOKEN="$GITHUB_TOKEN"; exec gh "$@"' _ pr list -R djbclark/core
```
