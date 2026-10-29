#include <test.h>

#include <cf3.defs.h>
#include <cf3.extern.h>
#include <pipes.h>
#include <timeout.h>
#include <sys/resource.h>

/* The alarm handler interrupts the sleep, so this normally returns as soon as
 * it fires. Loop anyway: the alarm may already have fired before we get here,
 * and a sleep interrupted by anything else must not end the wait early. */
static void WaitForAlarm(void)
{
    for (int i = 0; (i < 30) && !TimeOutHasFired(); i++)
    {
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 100 * 1000 * 1000 };
        nanosleep(&ts, NULL);
    }
}

static void test_set_arms_and_resets_the_record(void)
{
    SetTimeOut(3600);
    assert_true(TimeOutIsArmed());
    assert_false(TimeOutHasFired());
    assert_false(TimeOutSignalledProcess());
    ClearTimeOut();
}

/* A command that finished in time must not leave the flag set for the next,
 * unrelated, child: it is what decides whether cf_popen()'s child puts itself
 * in a process group of its own. */
static void test_clear_disarms(void)
{
    SetTimeOut(3600);
    ClearTimeOut();
    assert_false(TimeOutIsArmed());
}

static void test_fired_alarm_without_a_process(void)
{
    SetTimeOut(1);
    /* Production starts the clock only once a pid is registered; start it by
     * hand to reach the handler's no-process branch. */
    StartTimeOutClock();
    WaitForAlarm();

    assert_true(TimeOutHasFired());
    /* Nothing else disarms on this path -- the handler does it itself. */
    assert_false(TimeOutIsArmed());
    /* SetTimeOut() cleared ALARM_PID, so there was no process to signal and
     * the caller must not describe the command as terminated. */
    assert_false(TimeOutSignalledProcess());
}

/* ClearTimeOut() runs after the command is reaped, before the caller reports
 * on it. Clearing the record there would make a timed-out command whose exit
 * status reads as success indistinguishable from one that finished. */
static void test_clear_preserves_the_record(void)
{
    SetTimeOut(1);
    StartTimeOutClock();
    WaitForAlarm();
    assert_true(TimeOutHasFired());

    ClearTimeOut();
    assert_false(TimeOutIsArmed());
    assert_true(TimeOutHasFired());
    assert_false(TimeOutSignalledProcess());
}

/* The other tests never give TimeOut() a process to signal, so they cannot
 * tell a ClearTimeOut() that wipes a TRUE TimeOutSignalledProcess() from one
 * that leaves it alone -- both look identical when the flag started false.
 * Fork a real child and let the alarm reach it. */
static void test_clear_preserves_a_true_signalled_flag(void)
{
    pid_t child = fork();
    if (child == 0)
    {
        /* Long enough to still be alive when TimeOut() runs GracefulTerminate()
         * on it; short enough that a leaked child does not linger. */
        sleep(5);
        _exit(0);
    }
    assert_true(child > 0);

    SetTimeOut(1);
    ALARM_PID = child;
    /* Same order as cf_popen(): publish the pid, then start the clock. */
    StartTimeOutClock();
    WaitForAlarm();

    assert_true(TimeOutHasFired());
    assert_true(TimeOutSignalledProcess());

    ClearTimeOut();
    assert_false(TimeOutIsArmed());
    assert_true(TimeOutSignalledProcess());

    int status;
    waitpid(child, &status, 0);
}

/* CFE-4727: cf_pclose() used to clear ALARM_PID before waiting for the child,
 * so a command that closed its output but kept running was unreachable by the
 * time the alarm fired -- TimeOut() found no process to signal, and the full
 * sleep ran out inside an unbounded waitpid(). The pid must stay registered
 * until the reap, so an alarm firing during it still terminates the child.
 *
 * The sleep is 30 seconds and the elapsed bound 25 because the termination
 * can take a while: each of the ladder's two waits is nominally 1 second but
 * iteration-counted, which overshoots several-fold on Darwin (CFE-4728), and
 * with no process_darwin.c the ladder cannot see the child die and burns its
 * full waits regardless (CFE-4718). Terminated is ~2+2 seconds where those
 * are fixed and up to ~20 here; only running to completion reaches 30.
 *
 * cf_popen_sh() runs before SetTimeOut(), not after: arming first (as
 * verify_exec.c does) leaves a window between the alarm being armed and
 * cf_popen()'s fork actually publishing the child's pid into ALARM_PID, and
 * under load the 2 second alarm can fire inside that window -- observed
 * directly under a parallel `make check`, not theoretical. That race is
 * real but is not what this test pins (CFE-4734 closes it by starting the
 * clock only once cf_popen() has the pid); forking first and registering
 * the already-known pid, the same way test_clear_preserves_a_true_signalled_flag
 * does above, removes it from this test so a flake there cannot masquerade
 * as a regression here. Because the fork already happened, the clock has to
 * be started by hand, in cf_popen()'s order: publish the pid, then start.
 * The payload chains through exec so the timed process is the pid
 * cf_popen_sh() returned, with no intermediate shell left to hold the pipe
 * open once it is killed. */
static void test_pclose_leaves_the_alarm_its_process(void)
{
    FILE *pfp = cf_popen_sh("exec 1>&- 2>&-; exec sleep 30", "r");
    assert_true(pfp != NULL);
    pid_t child;
    assert_true(PipeToPid(&child, pfp));

    SetTimeOut(2);
    ALARM_PID = child;
    StartTimeOutClock();
    time_t start = time(NULL);

    char buf[64];
    while (fgets(buf, sizeof(buf), pfp) != NULL)
    {
    }

    int ret = cf_pclose(pfp);
    time_t elapsed = time(NULL) - start;
    ClearTimeOut();

    assert_true(TimeOutHasFired());
    /* The blocking wait in cf_pclose() is where the alarm found the child. */
    assert_true(TimeOutSignalledProcess());
    /* A terminated child cannot have run its 30 second sleep out. */
    assert_true(elapsed < 25);
    /* And it did not run to completion's exit 0. */
    assert_int_not_equal(0, ret);
    /* The reap still deregisters the pid afterwards. */
    assert_true(ALARM_PID == -1);
}

/* CFE-4735: RepairExec() armed a timeout, then a cf_popen*() call could fail
 * (pipe()/fork() failure, or fdopen() failure on the successfully forked
 * pipe) and return early without ever calling ClearTimeOut(). The alarm
 * stayed armed and ticking. If the *next* command -- even one with no
 * exec_timeout of its own -- happened to be running when that leftover
 * alarm fired, TimeOut() would find ALARM_PID naming that unrelated child
 * (GenericCreatePipeAndFork() publishes it unconditionally) and kill it.
 * The fix is ClearTimeOut() on the early-return path; this pins that it
 * actually prevents the kill, not just that the flags look right.
 *
 * RepairExec() itself lives in cf-agent and is not reachable from this
 * libpromises-only unit test, so this drives the same primitives it calls:
 * SetTimeOut() to arm, a real cf_popen_sh() failure to reach the early
 * return, then ClearTimeOut() standing in for RepairExec()'s new call.
 * Portable POSIX, no interposition: RLIMIT_NOFILE is dropped low enough
 * that the pipe() inside cf_popen_sh() fails deterministically (a plain
 * fdopen() failure cannot be forced this way -- fdopen() does not consume
 * a new fd on this libc -- so this exercises the pipe()/fork()-failure
 * shape of the same early-return family, not the eight fdopen()-failure
 * sites; those are covered by inspection and object-code review instead).
 *
 * Asserting on TimeOutHasFired()/TimeOutSignalledProcess() rather than on
 * whether the decoy is actually dead: a SIGKILLed child that nothing has
 * waitpid()ed yet is a zombie, and kill(pid, 0) succeeds against a zombie
 * exactly as it does against a live process -- that ambiguity, not a real
 * pass, is what an earlier attempt at this test using a liveness poll was
 * actually measuring. Whether TimeOut() ran and decided to act on
 * ALARM_PID is unambiguous and doesn't need the termination ladder (whose
 * own timing, CFE-4728/CFE-4718, is orthogonal to this defect) to finish.
 *
 * Discriminate by commenting out this test's own ClearTimeOut() call (not
 * touching verify_exec.c, which this test cannot reach): the leftover
 * alarm then fires against the decoy's freshly published ALARM_PID, and
 * the assertions below fail. */
static void test_leftover_alarm_does_not_kill_next_child(void)
{
    struct rlimit original;
    assert_true(getrlimit(RLIMIT_NOFILE, &original) == 0);

    SetTimeOut(2);

    struct rlimit tiny = { .rlim_cur = 3, .rlim_max = original.rlim_max };
    assert_true(setrlimit(RLIMIT_NOFILE, &tiny) == 0);

    FILE *failed = cf_popen_sh("true", "r");
    assert_true(failed == NULL);

    assert_true(setrlimit(RLIMIT_NOFILE, &original) == 0);

    /* Stand-in for RepairExec()'s new early-return call. */
    ClearTimeOut();

    FILE *decoy = cf_popen_sh("exec sleep 10", "r");
    assert_true(decoy != NULL);
    pid_t decoy_pid;
    assert_true(PipeToPid(&decoy_pid, decoy));

    /* Past when the leftover 2 second alarm would have fired, well short of
     * the decoy's own 10 second sleep. */
    struct timespec ts = { .tv_sec = 3, .tv_nsec = 0 };
    nanosleep(&ts, NULL);

    /* Fixed: ClearTimeOut() cancelled the alarm outright (alarm(0) plus
     * SIG_DFL), so it never fires again and never touches the decoy's now-
     * current ALARM_PID. */
    assert_false(TimeOutHasFired());
    assert_false(TimeOutSignalledProcess());

    kill(decoy_pid, SIGKILL);
    cf_pclose(decoy);
}

static void test_next_set_resets_the_record(void)
{
    SetTimeOut(1);
    StartTimeOutClock();
    WaitForAlarm();
    assert_true(TimeOutHasFired());

    SetTimeOut(3600);
    assert_true(TimeOutIsArmed());
    assert_false(TimeOutHasFired());
    assert_false(TimeOutSignalledProcess());
    ClearTimeOut();
}

/* The arming-order half of the timeout guarantee: the clock must not run
 * before cf_popen() has a pid to kill. Arming immediately, as SetTimeOut()
 * used to, lets the alarm fire during the caller's own setup -- umask(),
 * logging, cf_popen dispatch -- and burn the whole timeout on nothing, after
 * which the command runs unbounded.
 *
 * Deliberately not a fixed-wait liveness check: it waits twice the timeout
 * before forking at all, so the old behaviour fires with certainty rather than
 * by timing luck, and the command it then runs outlives its timeout by 4s. */
static void test_clock_does_not_run_before_the_fork(void)
{
    SetTimeOut(1);

    for (int i = 0; (i < 20) && !TimeOutHasFired(); i++)
    {
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 100 * 1000 * 1000 };
        nanosleep(&ts, NULL);
    }
    /* Two seconds into a one-second timeout, with no child yet. */
    assert_false(TimeOutHasFired());

    FILE *pp = cf_popen("/bin/sleep 5", "r", true);
    assert_true(pp != NULL);

    /* Ends at EOF, which arrives when the alarm terminates the child. */
    char buf[64];
    while (fread(buf, 1, sizeof(buf), pp) > 0)
    {
    }
    cf_pclose(pp);

    assert_true(TimeOutHasFired());
    assert_true(TimeOutSignalledProcess());
    ClearTimeOut();
}

static void test_set_leaves_the_clock_stopped(void)
{
    SetTimeOut(3600);
    assert_true(TimeOutIsArmed());
    /* alarm(0) returns the seconds left on a running clock, and 0 if none is
     * running. Armed, but not yet ticking. */
    assert_int_equal(alarm(0), 0);
    ClearTimeOut();
}

/* One-shot: a second fork under the same timeout runs on the time already
 * ticking, so the second start must not restart the command's budget. The
 * second StartTimeOutClock() is deliberately issued against a *running* clock
 * -- reading it with alarm(0) first would cancel it, and a starter that resets
 * a live timer would then look identical to one that leaves it alone. */
static void test_start_runs_the_clock_once(void)
{
    SetTimeOut(5);
    StartTimeOutClock();

    struct timespec ts = { .tv_sec = 1, .tv_nsec = 0 };
    nanosleep(&ts, NULL);

    StartTimeOutClock();

    const int left = alarm(0);
    assert_true(left > 0);
    /* The remainder of the original 5s, not a fresh 5s. */
    assert_true(left <= 4);
    ClearTimeOut();
}

/* A timeout armed but never followed by a fork -- cf_popen() failing, or a
 * caller returning early -- must be fully retired, not left able to start. */
static void test_clear_retires_an_unstarted_clock(void)
{
    SetTimeOut(3600);
    ClearTimeOut();

    StartTimeOutClock();
    assert_int_equal(alarm(0), 0);
    assert_false(TimeOutIsArmed());
}

int main()
{
    const UnitTest tests[] =
    {
        unit_test(test_set_arms_and_resets_the_record),
        unit_test(test_clear_disarms),
        unit_test(test_fired_alarm_without_a_process),
        unit_test(test_clear_preserves_the_record),
        unit_test(test_clear_preserves_a_true_signalled_flag),
        unit_test(test_pclose_leaves_the_alarm_its_process),
        unit_test(test_leftover_alarm_does_not_kill_next_child),
        unit_test(test_next_set_resets_the_record),
        unit_test(test_clock_does_not_run_before_the_fork),
        unit_test(test_set_leaves_the_clock_stopped),
        unit_test(test_start_runs_the_clock_once),
        unit_test(test_clear_retires_an_unstarted_clock)
    };

    PRINT_TEST_BANNER();
    return run_tests(tests);
}
