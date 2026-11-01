#include <test.h>

#include <cf3.defs.h>
#include <cf3.extern.h>
#include <pipes.h>
#include <timeout.h>

/* Runs "/bin/sleep <seconds>" under the way the agent reads a command's
 * output, which ends when the child does, and returns how long it took. No
 * shell here, as a shell terminated on its own would leave its child holding
 * the pipe open. */
static time_t RunSleep(const char *command)
{
    time_t start = time(NULL);
    FILE *pfp = cf_popen(command, "r", true);
    assert_true(pfp != NULL);

    char line[16];
    assert_true(fgets(line, sizeof(line), pfp) == NULL);
    cf_pclose(pfp);

    return time(NULL) - start;
}

/* A timeout must still bound a child forked after it would already have
 * expired: the child is terminated one timeout after it started. */
static void test_timeout_survives_a_stall_before_fork(void)
{
    SetChildTimeOut(1);

    /* Stands in for the agent stalling between arming and forking. */
    sleep(2);

    time_t elapsed = RunSleep("/bin/sleep 20");

    DisarmTimeOut();

    assert_true(elapsed < 15);
}

/* SetTimeOut() is for callers that never fork, so it arms at once; only
 * SetChildTimeOut() waits for a child. DisarmTimeOut() undoes both. */
static void test_SetTimeOut_arms_at_once_SetChildTimeOut_waits(void)
{
    SetTimeOut(60);
    assert_true(alarm(0) > 0);
    DisarmTimeOut();
    assert_int_equal(alarm(0), 0);

    SetChildTimeOut(60);
    assert_int_equal(alarm(0), 0);
    DisarmTimeOut();
}

/* A request dropped by DisarmTimeOut() must not start later, whoever installs
 * the handler again in the meantime. */
static void test_disarmed_timeout_does_not_start_later(void)
{
    SetChildTimeOut(1);
    DisarmTimeOut();
    signal(SIGALRM, (void *) TimeOut);

    time_t elapsed = RunSleep("/bin/sleep 3");

    DisarmTimeOut();

    assert_true(elapsed >= 3);
}

/* A request is for one cf_popen*() call only: one that fails before forking
 * must not leave the request for the next, unrelated, child. */
static void test_failed_popen_does_not_leave_timeout_for_next_child(void)
{
    SetChildTimeOut(1);
    assert_true(cf_popen("/bin/true", "x", true) == NULL);

    time_t elapsed = RunSleep("/bin/sleep 3");

    DisarmTimeOut();

    assert_true(elapsed >= 3);
}

int main()
{
    PRINT_TEST_BANNER();
    const UnitTest tests[] = {
        unit_test(test_timeout_survives_a_stall_before_fork),
        unit_test(test_SetTimeOut_arms_at_once_SetChildTimeOut_waits),
        unit_test(test_disarmed_timeout_does_not_start_later),
        unit_test(test_failed_popen_does_not_leave_timeout_for_next_child),
    };

    return run_tests(tests);
}
