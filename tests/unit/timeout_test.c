#include <test.h>

#include <cf3.defs.h>
#include <cf3.extern.h>
#include <pipes.h>
#include <timeout.h>
#include <signals.h>
#include <poll.h>
#include <sys/wait.h>

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

/* Runs a shell command under a one second timeout, the way the agent reads
 * its output (which ends when everything holding the pipe is gone), and
 * returns how long it took. */
static time_t RunShellUnderTimeOut(const char *command)
{
    SetChildTimeOut(1);

    time_t start = time(NULL);
    FILE *pfp = cf_popen_sh(command, "r");
    assert_true(pfp != NULL);

    char line[16];
    assert_true(fgets(line, sizeof(line), pfp) == NULL);
    cf_pclose(pfp);
    time_t elapsed = time(NULL) - start;

    DisarmTimeOut();
    return elapsed;
}

/* A timeout must also end what the command spawned: a shell terminated on its
 * own leaves its child holding the output pipe open, and the read blocks for
 * as long as that lives. */
static void test_timeout_terminates_what_the_command_spawned(void)
{
    assert_true(RunShellUnderTimeOut("sleep 30; exit 0") < 15);
}

/* The same where the shell has already exited when the timeout fires. It is
 * a zombie until reaped, and on macOS getpgid() fails for a zombie although
 * its group lives on. */
static void test_timeout_terminates_group_of_exited_leader(void)
{
    assert_true(RunShellUnderTimeOut("sleep 30 &") < 15);
}

static bool ReadableWithin(int fd, int seconds)
{
    struct pollfd p = { .fd = fd, .events = POLLIN };
    return poll(&p, 1, seconds * 1000) == 1;
}

/* The agent is terminated through its process group, which no longer holds
 * the children that run under a timeout: it must end them itself, however
 * they take the signals it is sent. The "agent" is a process of its own,
 * running the agent's signal handler. Its command ignores SIGINT and SIGTERM
 * (as a background job of `sh -c` does for SIGINT), starts a job and exits,
 * leaving that in a group whose leader is a zombie. Every process of the
 * group inherits the write end of a pipe, so the read end reports end of
 * file once all of them are gone. */
static void test_agent_termination_ends_timed_child_group(void)
{
    int alive[2];
    assert_int_equal(pipe(alive), 0);

    fflush(NULL);
    pid_t agent = fork();
    assert_true(agent >= 0);
    if (agent == 0)
    {
        close(alive[0]);
        signal(SIGTERM, HandleSignalsForAgent);

        SetChildTimeOut(60);
        FILE *pfp = cf_popen_sh("trap '' INT TERM; sleep 30 & echo $$", "r");
        char pgid[32] = "";
        if ((pfp == NULL) || (fgets(pgid, sizeof(pgid), pfp) == NULL))
        {
            _exit(EXIT_FAILURE);
        }
        (void) !write(alive[1], pgid, strlen(pgid));

        raise(SIGTERM);
        _exit(EXIT_FAILURE); /* the handler exits */
    }
    close(alive[1]);

    char pgid[32] = "";
    assert_true(ReadableWithin(alive[0], 10));
    assert_true(read(alive[0], pgid, sizeof(pgid) - 1) > 0);

    /* The end of file means that the child's group is gone too. */
    char byte;
    const bool gone = ReadableWithin(alive[0], 15)
                      && (read(alive[0], &byte, 1) == 0);
    if (!gone)
    {
        kill(-atoi(pgid), SIGKILL); /* do not leave the sleep behind */
    }
    close(alive[0]);

    int status;
    assert_int_equal(waitpid(agent, &status, 0), agent);
    assert_true(WIFEXITED(status) && (WEXITSTATUS(status) == 0));
    assert_true(gone);
}

/* A stale ALARM_PID that names the process itself must never have its own
 * group killed, even where it leads one. */
static void test_timed_child_group_is_never_our_own(void)
{
    fflush(NULL);
    pid_t pid = fork();
    assert_true(pid >= 0);
    if (pid == 0)
    {
        setpgid(0, 0);
        ALARM_PID = getpid();
        SetTimedChildGroup(getpid());
        KillTimedChildGroup();
        _exit(EXIT_SUCCESS); /* only reached if we were not killed */
    }

    int status;
    assert_int_equal(waitpid(pid, &status, 0), pid);
    assert_true(WIFEXITED(status) && (WEXITSTATUS(status) == EXIT_SUCCESS));
}

/* A command that was not given a group of its own may have made one for
 * itself (setsid(1)): it is outside the agent's group on purpose, and not
 * for the agent to kill. */
static void test_group_the_command_made_for_itself_is_left_alone(void)
{
    int ready[2], go[2];
    assert_int_equal(pipe(ready), 0);
    assert_int_equal(pipe(go), 0);

    fflush(NULL);
    pid_t pid = fork();
    assert_true(pid >= 0);
    if (pid == 0)
    {
        char byte = 0;
        setpgid(0, 0);
        (void) !write(ready[1], &byte, 1);
        (void) !read(go[0], &byte, 1);
        _exit(EXIT_SUCCESS); /* only reached if we were not killed */
    }

    char byte = 0;
    assert_int_equal(read(ready[0], &byte, 1), 1);
    ALARM_PID = pid;
    SetTimedChildGroup(-1); /* what cf_popen*() records for such a child */
    KillTimedChildGroup();
    ALARM_PID = -1;
    assert_int_equal(write(go[1], &byte, 1), 1);

    int status;
    assert_int_equal(waitpid(pid, &status, 0), pid);
    assert_true(WIFEXITED(status) && (WEXITSTATUS(status) == EXIT_SUCCESS));

    close(ready[0]);
    close(ready[1]);
    close(go[0]);
    close(go[1]);
}

int main()
{
    PRINT_TEST_BANNER();
    const UnitTest tests[] = {
        unit_test(test_timeout_survives_a_stall_before_fork),
        unit_test(test_SetTimeOut_arms_at_once_SetChildTimeOut_waits),
        unit_test(test_disarmed_timeout_does_not_start_later),
        unit_test(test_failed_popen_does_not_leave_timeout_for_next_child),
        unit_test(test_timeout_terminates_what_the_command_spawned),
        unit_test(test_timeout_terminates_group_of_exited_leader),
        unit_test(test_agent_termination_ends_timed_child_group),
        unit_test(test_timed_child_group_is_never_our_own),
        unit_test(test_group_the_command_made_for_itself_is_left_alone),
    };

    return run_tests(tests);
}
