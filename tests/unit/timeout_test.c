#include <test.h>

#include <cf3.defs.h>
#include <cf3.extern.h>
#include <pipes.h>
#include <timeout.h>

/* A timeout must still bound a child forked after it would already have
 * expired: the child is terminated one timeout after it started. */
static void test_timeout_survives_a_stall_before_fork(void)
{
    SetTimeOut(1);

    /* Stands in for the agent stalling between arming and forking. */
    sleep(2);

    time_t start = time(NULL);
    FILE *pfp = cf_popen("/bin/sleep 20", "r", true);
    assert_true(pfp != NULL);

    /* Block on the output as the agent does: it ends when the child dies. No
     * shell here, as a shell terminated on its own would leave its child
     * holding the pipe open. */
    char line[16];
    assert_true(fgets(line, sizeof(line), pfp) == NULL);
    cf_pclose(pfp);
    time_t elapsed = time(NULL) - start;

    alarm(0);
    signal(SIGALRM, SIG_DFL);

    assert_true(elapsed < 15);
}

/* A timeout disarmed before any child was forked must stay disarmed: a later
 * child must not be started under it, or SIGALRM would terminate us. */
static void test_disarmed_timeout_does_not_start_later(void)
{
    SetTimeOut(1);
    alarm(0);
    signal(SIGALRM, SIG_DFL);

    FILE *pfp = cf_popen("/bin/sleep 3", "r", true);
    assert_true(pfp != NULL);
    cf_pclose(pfp);
}

int main()
{
    PRINT_TEST_BANNER();
    const UnitTest tests[] = {
        unit_test(test_timeout_survives_a_stall_before_fork),
        unit_test(test_disarmed_timeout_does_not_start_later),
    };

    return run_tests(tests);
}
