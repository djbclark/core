---
schema_version: 1
handoff_id: 0650
parent_handoff_ids: [f3ca]
lineage: deterministic
chain: [standalone-3fd9]
repo: cfengine-core
workspace: main
branch: fix/simulate-pkg-unversioned-install-assert
head_sha: d5052b78632150c83fa5ac20e9d09bbddbab8403
created_at: 2026-10-20T09:40:00-0400
writer: claude-code
---

# Handoff — C sent upstream as cfengine/core#6424

## The Goal

Land the `--simulate-json` batch upstream one PR at a time, in a temporary
fork that exists only so upstream gets something worth accepting. Slices in
flight: A (#6389, merged), B (#6412, merged), **C (this one)**, D (#40 on the
fork), E (#41, stacked on D), F (libntech#9).

## Where We Are

C is **sent**: <https://github.com/cfengine/core/pull/6424>, open, head
`d5052b786`, base `master`, 2 files, +40/−1.

The session opened with the gate shut and ended with it reopened by someone
else's action: #6412 merged at 2026-10-20T07:25:04Z while nobody was looking,
which under the one-at-a-time rule made C eligible. djbclark gave the go after
reviewing the title, the full body and the code diff.

Everything claimed in C's PR body is measured on the exact head that was sent,
not carried over from an older base — that was the substance of the session's
work, not the send itself.

## What We Tried

- **Ran the unit test against a stale build and believed it.** The first local
  run reported `All 5 tests passed` from a binary built before the rebase.
  The source registers 7. Rebuilt, and then hit the real version of the same
  trap (below).
- **`make -C tests/unit simulate_mode_test` alone, after the rebase.** It
  relinks the test file but not `cf-agent/libcf-agent.la`, which the tests
  `LDADD`. Result: #6412's own `test_diff_removal_with_version` failed, looking
  exactly like C regressing B. It was a stale library, not a regression —
  `make -C cf-agent libcf-agent.la` first, then 7/7. Written into AGENTS.md.
- **Negative control on the Mac.** Reverted the guard, rebuilt, and all 7 tests
  *passed* — because this tree is NDEBUG and `assert()` is compiled out. The
  control is meaningless here; it needs `--enable-debug`, i.e. the Linux gate.
  Also written into AGENTS.md.
- **Misread `git checkout <ref> -- <file>` as a no-op.** It *stages*, so plain
  `git diff` was empty and the revert looked like it had not applied. It had.
  `git diff --cached` was the right check.
- **Looked for a worker report to decide whether fleet-watch saw the merge.**
  There was none, and I called the watcher broken. Wrong test: a merge is
  ping-only by design ("there is nothing to prepare"). See Key Decisions.
- **`bg` refused the build** (rc=3) because I had just recorded a
  `load-watch ci decide ... ci` decision for this directory, so the first
  "build" never ran at all. A single test binary is the targeted case its own
  message allows; ran it directly after that.
- **First `gate-core-neg.sh` run exited 127**: top-level `make` does not build
  unit-test binaries. Fixed with `make -C tests/unit simulate_mode_test`.

## Key Decisions

- **Re-run the Linux root gate rather than soften the stale figures**
  (djbclark's call, offered against two cheaper options). The body claimed
  `All 75 tests passed` from base `6c053e100`; #6412 had since added tests.
  Measured truth on the sent head is **76**. Had we "fixed" it by arithmetic
  we would have published a number nobody ran.
- **Fork CI is the Linux `--enable-debug` unit gate, but NOT the acceptance
  gate.** `unit_tests.yml` and `acceptance_tests.yml` both configure with
  `--enable-debug`, but the acceptance job runs only
  `--tests=common,errorlog,errorexit,errorpass` — `29_simulate_mode/` is not
  in it. That is why the container gate still exists and cannot be replaced by
  pushing to the fork.
- **Wrote a negative-control script rather than trusting the carried-over
  transcript.** `gate-core-neg.sh` keeps the tip's tests and reverts only
  `cf-agent/simulate_mode.c` to the base, so the assert fires on exactly the
  code under review. Rejected alternative: quote the old macOS transcript and
  note it was not re-run — cheaper, but the quoted wording was macOS libc's,
  and upstream reviewers are on glibc. The body now quotes the glibc output
  that actually ran.
- **fleet-watch is correct; the earlier "watcher missed the merge" note in the
  Tier 1 log was my misdiagnosis.** `handled_at` for thread 26372141192 was
  advanced to exactly the merge time, and `handled_at` only advances after a
  successful Hermes ping (a failed ping leaves the thread for retry). A merge
  dispatches no worker by design. Also verified forward: an unknown thread
  falls back to `seeded_at` (fleet-watch:884), so #6424's first maintainer
  activity — newer than the 2026-10-16 seed — will fire as an event. No fix
  made, none needed.
- **Kept the duplicated guard instead of factoring a helper** shared with
  `ManifestPkgOperations()`. Minimal diff for a bug fix; flagged to djbclark
  as the one judgment call before he approved. A reviewer may still ask.
- **AGENTS.md is untracked** (`.git/info/exclude:26`), so documenting the
  gotchas could not pollute the PR branch. Worth knowing before worrying about
  which branch to edit on.

## Evidence & Data

Verification of `d5052b786`, all on the sent head:

| Check | Result |
|---|---|
| Rebase onto `upstream/master` `4eae47542` | clean, same diff |
| Fork CI, 10 jobs, run 39392874711 | 10/10 pass |
| Linux `--enable-debug` as root, `make -C tests/unit check` | `All 76 tests passed` |
| `simulate_mode_test` direct | `All 7 tests passed` |
| `29_simulate_mode/` acceptance as root | Passed 4, Failed 0, Total 4 |
| Negative control (guard reverted, tests kept) | abort `simulate_mode.c:676`, rc=134 |
| Asserts actually on in that build | `0` NDEBUG lines in `config.h` |
| macOS local, after full relink | 7/7, no regression on B |

Gate artifacts: `~/.local/state/handoffs/chains/standalone-3fd9/linuxgate/out/d5052b78632150c83fa5ac20e9d09bbddbab8403/`
and `...-neg/`. Gate container `cfengine-build:u24`, digest `5855de024597`,
~2.5 min end to end.

The change itself: `cf-agent/simulate_mode.c` +4/−1 — the install-record
comparison in `DiffPkgOperations()` now checks both versions are non-empty
before calling `PkgVersionIsGreater()` (which asserts that), keeping versioned
over unversioned, else the higher version, else the first recorded.
`tests/unit/simulate_mode_test.c` +36 — `test_diff_repeated_unversioned_install`
(the one that aborts without the fix) and
`test_diff_unversioned_then_versioned_install`.

## Operator Feedback

- **Show long text as wrapped reply prose, never raw tool output** (2026-10-20):
  "Can you show me the text here in a nicer way that includes word wrap? This
  is hard to read. If you can, you should always do it that way." Paseo's
  tool-output blocks do not wrap; `cat`-ing a PR body for review is unreadable.
  Saved as memory `show-long-text-as-wrapped-reply-prose`, and relayed to
  `djbclark-ade-64` to fold into the toolchain's guidance.
- **Show the diff before approving a body that claims to have read it.** He
  asked for the code diff "so I'm not lying re looking at it" — the body says
  "I did look at the logic and it seemed sound". Offer the diff with the body
  next time, unprompted.
- **Rigour over speed when a published number is at stake**: given soften /
  re-run / infer, he chose re-run.

## Where We're Going

1. **Wait on #6424.** `gh pr view 6424 -R cfengine/core --json state,mergedAt,reviewDecision`.
   Do not offer D until upstream acts AND djbclark gives a go — the merge of
   one PR is not a go for the next. fleet-watch will ping on activity; a review
   dispatches a worker under `~/.local/state/bigteam/upstream-pr/`, a merge or
   close only pings.
2. On upstream action + go, prepare **D** (#40, branch `simulate-json-files`):
   rebase onto `upstream/master`, re-run the Linux gate on the new head, and
   refresh section D's figures in
   `~/.local/state/handoffs/chains/standalone-3fd9/upstream-pr-texts-2026-10-09.md`
   the same way C's were — do not carry old numbers forward.
3. **E** (#41, `simulate-json-packages`): rebase onto D only once D is sent.
4. If a reviewer asks for the shared guard helper on #6424, that refactor
   touches both `DiffPkgOperations()` and `ManifestPkgOperations()` and needs
   the gate re-run, not just the unit tests.

## Detached jobs

None. This session owned no bigteam job records; all background work was
`run_in_background` tasks and container runs, which completed within the
session (gate containers used `--rm` and none remain — `container list` empty).

## Quick Start

```sh
cd ~/src/cfengine-core
gh pr view 6424 -R cfengine/core --json state,mergedAt,reviewDecision,comments

# Linux gate (the only source of real 29_simulate_mode figures and assert aborts)
S=~/.local/state/handoffs/chains/standalone-3fd9/linuxgate
container run --rm --name gate-x -c 3 -m 6G \
  -v /Users/djbclark/src/cfengine-core:/src:ro -v $S:/gate \
  cfengine-build:u24 /gate/gate-core.sh <sha>
# negative control: /gate/gate-core-neg.sh <sha> <base-sha>

# local unit test: rebuild the library FIRST or you will debug a phantom
make -C cf-agent libcf-agent.la && make -C tests/unit simulate_mode_test \
  && ./tests/unit/simulate_mode_test
```

Gotchas and the gate recipe are also in `~/src/cfengine-core/AGENTS.md`
(untracked by design). Send texts for every slice:
`~/.local/state/handoffs/chains/standalone-3fd9/upstream-pr-texts-2026-10-09.md`.
