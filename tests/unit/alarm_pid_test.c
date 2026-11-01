#include <test.h>

#include <verify_exec.c> // Include .c file to reach static RepairExec()
#include <exec_tools.h>
#include <pipes.h>
#include <signals.h>
#include <sys/resource.h>

/* Every fdopen() in this binary fails; the tests only need it to. */
FILE *fdopen(int fd, const char *mode)
{
    (void) fd;
    (void) mode;
    errno = ENOMEM;
    return NULL;
}

/* Once the child is reaped its pid can be recycled, so ALARM_PID must not
 * name it any more. */

static void test_ShellCommandReturnsZero_clears_alarm_pid(void)
{
    ALARM_PID = -1;
    assert_true(ShellCommandReturnsZero("true", SHELL_TYPE_USE));
    assert_int_equal(ALARM_PID, -1);

    assert_false(ShellCommandReturnsZero("false", SHELL_TYPE_USE));
    assert_int_equal(ALARM_PID, -1);
}

static void test_popen_fdopen_failure_clears_alarm_pid(void)
{
    ALARM_PID = -1;
    assert_true(cf_popen("/bin/sh -c true", "r", false) == NULL);
    assert_int_equal(ALARM_PID, -1);

    assert_true(cf_popen_sh("true", "r") == NULL);
    assert_int_equal(ALARM_PID, -1);

    assert_true(cf_popensetuid("/bin/sh -c true", NULL, "r", -1, -1, NULL,
                               NULL, false) == NULL);
    assert_int_equal(ALARM_PID, -1);

    assert_true(cf_popen_shsetuid("true", "r", -1, -1, NULL, NULL, false)
                == NULL);
    assert_int_equal(ALARM_PID, -1);
}

/* A promise that arms exec_timeout but cannot open its pipe must not leave
 * the alarm pending for whatever runs next. pipe() is made to fail by
 * leaving no free file descriptor. */
static void test_RepairExec_disarms_timeout_when_popen_fails(void)
{
    char promiser[] = "/bin/sh";
    Promise pp = { .promiser = promiser };
    Attributes a = { 0 };
    a.contain.timeout = 60;
    a.contain.shelltype = SHELL_TYPE_USE;
    a.contain.owner = (uid_t) -1;
    a.contain.group = (gid_t) -1;
    a.transaction.action = cfa_fix;

    int first_free = dup(STDIN_FILENO);
    assert_true(first_free >= 0);
    close(first_free);

    struct rlimit original;
    assert_int_equal(getrlimit(RLIMIT_NOFILE, &original), 0);
    struct rlimit tight = original;
    tight.rlim_cur = first_free;
    assert_int_equal(setrlimit(RLIMIT_NOFILE, &tight), 0);

    mode_t umask_before = umask(0);
    PromiseResult result = PROMISE_RESULT_NOOP;
    ActionResult ret = RepairExec(NULL, &a, &pp, &result);
    umask(umask_before);

    assert_int_equal(setrlimit(RLIMIT_NOFILE, &original), 0);

    assert_int_equal(ret, ACTION_RESULT_FAILED);
    assert_int_equal(alarm(0), 0);
    signal(SIGALRM, SIG_DFL);
}

/* The termination request is not reset, so this must stay last. */
static void test_ShellCommandReturnsZero_clears_alarm_pid_on_term(void)
{
    HandleSignalsForDaemon(SIGTERM);

    ALARM_PID = -1;
    assert_false(ShellCommandReturnsZero("sleep 30", SHELL_TYPE_USE));
    assert_int_equal(ALARM_PID, -1);
}

int main()
{
    PRINT_TEST_BANNER();
    const UnitTest tests[] = {
        unit_test(test_ShellCommandReturnsZero_clears_alarm_pid),
        unit_test(test_popen_fdopen_failure_clears_alarm_pid),
        unit_test(test_RepairExec_disarms_timeout_when_popen_fails),
        unit_test(test_ShellCommandReturnsZero_clears_alarm_pid_on_term),
    };

    return run_tests(tests);
}
