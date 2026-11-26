#include <test.h>

#include <compiler.h>
#include <process_lib.h>
#include <process_unix_priv.h>

/* This mock implements single fake process with a several tunable parameters:
   - process' start time,
   - reaction time for STOP/CONT/INT/TERM/KILL signal,
   - whether SIGINT/SIGTERM are blocked,
   - does the caller have permission to send signals to fake process

   This mock also records what signals were delivered to the process to check later.
*/

/* Settings */

time_t proc_1_start_time;
time_t proc_1_reaction_time;
bool proc_1_int_blocked;
bool proc_1_term_blocked;
bool proc_1_have_access;

/* State */

time_t current_time;

bool exists;
bool stopped;

time_t signal_time;
bool has_stop;
bool has_cont;
bool has_int;
bool has_term;
bool has_kill;

/* Matching constants from libpromises/process_unix.c */
#define TEST_SLEEP_POLL_TIMEOUT_NS 10000000LL
#define TEST_STOP_WAIT_TIMEOUT_NS 999999999LL

typedef struct
{
    int sig;
    time_t time_ns;
} SignalEvent;

#define MAX_SIGNAL_EVENTS 32

/* History */

bool was_stopped;
int exit_signal;

static SignalEvent signal_events[MAX_SIGNAL_EVENTS];
static size_t signal_event_count;
static bool got_eintr;
static int eintr_count;
static time_t nanosleep_slack_ns;

void InitTime(void)
{
    current_time = 1;
    got_eintr = false;
    eintr_count = 0;
    nanosleep_slack_ns = 0;
}

void InitFakeProcess(time_t start_time, time_t reaction_time,
                     bool int_blocked, bool term_blocked, bool have_access)
{
    proc_1_start_time = start_time;
    proc_1_reaction_time = reaction_time;
    proc_1_int_blocked = int_blocked;
    proc_1_term_blocked = term_blocked;
    proc_1_have_access = have_access;

    exists = true;
    stopped = false;

    signal_time = -1;
    has_stop = false;
    has_cont = false;
    has_int = false;
    has_term = false;
    has_kill = false;

    was_stopped = false;
    exit_signal = 0;
    signal_event_count = 0;
}

time_t GetProcessStartTime(pid_t pid)
{
    assert_int_equal(pid, 1);

    if (proc_1_have_access && exists)
    {
        return proc_1_start_time;
    }
    else
    {
        return PROCESS_START_TIME_UNKNOWN;
    }
}

ProcessState GetProcessState(pid_t pid)
{
    assert_int_equal(pid, 1);

    if (!proc_1_have_access || !exists)
    {
        return PROCESS_STATE_DOES_NOT_EXIST;
    }

    if (stopped)
    {
        return PROCESS_STATE_STOPPED;
    }
    else
    {
        return PROCESS_STATE_RUNNING;
    }
}

void FakeProcessDoSignals(void)
{
    /* React immediately to STOP and CONT in order to properly do the
     * STOP-kill-CONT sequence inside SafeKill(). */
    if (has_stop)
    {
        stopped = true;
        was_stopped = true;
        has_stop = false;
    }

    if (has_cont)
    {
        stopped = false;
        has_cont = false;
    }

    /* Do NOT react to other signals if it's not the time yet. */
    if (current_time < signal_time)
    {
        return;
    }

    if (has_int)
    {
        if (!proc_1_int_blocked)
        {
            exists = false;
            exit_signal = SIGINT;
            stopped = false;
            signal_time = -1;
        }
        has_int = false;
    }

    if (has_term)
    {
        if (!proc_1_term_blocked)
        {
            exists = false;
            exit_signal = SIGTERM;
            stopped = false;
            signal_time = -1;
        }
        has_term = false;
    }

    if (has_kill)
    {
        exists = false;
        exit_signal = SIGKILL;
        stopped = false;
        signal_time = -1;

        has_kill = false;
    }

    signal_time = -1;
}

int kill(pid_t pid, int signal)
{
    assert_int_equal(pid, 1);

    if (!proc_1_have_access)
    {
        errno = EPERM;
        return -1;
    }

    if (!exists)
    {
        errno = ESRCH;
        return -1;
    }

    if (signal == 0)
    {
        return 0;
    }

    if (signal_time == -1)
    {
        signal_time = current_time + proc_1_reaction_time;
    }

    if (signal == SIGSTOP)
    {
        has_stop = true;
    }
    else if (signal == SIGCONT)
    {
        has_cont = true;
    }
    else if (signal == SIGINT)
    {
        has_int = true;
    }
    else if (signal == SIGTERM)
    {
        has_term = true;
    }
    else if (signal == SIGKILL)
    {
        has_kill = true;
    }
    else
    {
        errno = EINVAL;
        return -1;
    }

    if (signal_event_count < MAX_SIGNAL_EVENTS)
    {
        signal_events[signal_event_count].sig = signal;
        signal_events[signal_event_count].time_ns = current_time;
        signal_event_count++;
    }

    FakeProcessDoSignals();

    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem)
{
    time_t sleep_time;

    /* Simulate EINTR every second time */

    if (!got_eintr)
    {
        got_eintr = true;
        eintr_count++;
        sleep_time = 2 * req->tv_nsec / 3;
    }
    else
    {
        got_eintr = false;
        sleep_time = req->tv_nsec + nanosleep_slack_ns;
    }

    time_t next_time = current_time + sleep_time;
    if (signal_time != -1 && next_time >= signal_time)
    {
        FakeProcessDoSignals();
    }

    current_time = next_time;

    if (got_eintr)
    {
        rem->tv_sec = 0;
        rem->tv_nsec = req->tv_nsec - sleep_time;
        errno = EINTR;
        return -1;
    }
    else
    {
        return 0;
    }
}

/* The poll loops in process_unix.c measure how long they have actually waited
 * instead of assuming every nanosleep() costs exactly what was requested, so
 * the fake clock has to drive clock_gettime() as well. Without this the mocked
 * nanosleep() above advances fake time while the loops read real time, and the
 * fake process reacts on a schedule the loops cannot observe.
 *
 * current_time is a nanosecond counter, matching the units the tests already
 * use for reaction times (e.g. 2000000000 for two seconds). */
int clock_gettime(clockid_t clk_id, struct timespec *tp)
{
    (void) clk_id;

    tp->tv_sec = current_time / 1000000000;
    tp->tv_nsec = current_time % 1000000000;

    return 0;
}

/* Tests */

void test_kill_simple_process(void)
{
    InitTime();
    InitFakeProcess(12345, 100, false, false, true);

    int res = GracefulTerminate(1, 12345);
    assert_true(res);

    FakeProcessDoSignals();

    assert_false(exists);
    assert_int_equal(exit_signal, SIGINT);
}

void test_kill_wrong_process(void)
{
    InitTime();
    InitFakeProcess(66666, 100, false, false, true);

    int res = GracefulTerminate(1, 12345);
    assert_false(res);

    FakeProcessDoSignals();

    assert_true(exists);
    assert_false(stopped);
    assert_false(was_stopped); /* We should not touch this process at all */
    assert_int_equal(signal_time, (time_t)-1); /* No pending signals either */
}

void test_kill_long_reacting_signal(void)
{
    /* This process is very slow in reaction. It should not be left stopped though */
    InitTime();
    InitFakeProcess(12345, 2000000000, false, false, true);

    int res = GracefulTerminate(1, 12345);
    assert_true(res);

    FakeProcessDoSignals();

    /* This process is not even reacting to SIGKILL. */
    assert_true(exists);
    assert_false(stopped);
}

static void assert_elapsed_budget_ladder(time_t slack_ns)
{
    InitTime();
    nanosleep_slack_ns = slack_ns;
    const time_t start_time = current_time;
    InitFakeProcess(12345, 2100000000, true, true, true);

    int res = GracefulTerminate(1, 12345);
    assert_true(res);
    assert_false(stopped);
    assert_true(eintr_count > 0);

    /* SafeKill sends SIGSTOP, <signal>, SIGCONT for each ladder stage:
     * SIGINT, then SIGTERM, then SIGKILL. */
    const int expected_sigs[9] = {
        SIGSTOP, SIGINT, SIGCONT,
        SIGSTOP, SIGTERM, SIGCONT,
        SIGSTOP, SIGKILL, SIGCONT,
    };
    assert_int_equal((int) signal_event_count, 9);
    for (size_t i = 0; i < 9; i++)
    {
        assert_int_equal(signal_events[i].sig, expected_sigs[i]);
    }

    const int64_t stage1_ns =
        (int64_t) (signal_events[4].time_ns - signal_events[1].time_ns);
    const int64_t stage2_ns =
        (int64_t) (signal_events[7].time_ns - signal_events[4].time_ns);
    const int64_t total_ns = (int64_t) (current_time - start_time);

    assert_true(stage1_ns >= TEST_STOP_WAIT_TIMEOUT_NS);
    assert_true(stage1_ns <=
                TEST_STOP_WAIT_TIMEOUT_NS + TEST_SLEEP_POLL_TIMEOUT_NS);

    assert_true(stage2_ns >= TEST_STOP_WAIT_TIMEOUT_NS);
    assert_true(stage2_ns <=
                TEST_STOP_WAIT_TIMEOUT_NS + TEST_SLEEP_POLL_TIMEOUT_NS);

    assert_true(total_ns >= 2 * TEST_STOP_WAIT_TIMEOUT_NS);
    assert_true(total_ns <=
                2 * TEST_STOP_WAIT_TIMEOUT_NS + TEST_SLEEP_POLL_TIMEOUT_NS);
}

static void test_kill_elapsed_budget_under_eintr(void)
{
    assert_elapsed_budget_ladder(0);
}

static void test_kill_elapsed_budget_under_sleep_overshoot(void)
{
    /* Each completed poll overshoots by 4ms (simulating coarse kernel timer
     * granularity). Deadline-based polling stays within one poll quantum of
     * STOP_WAIT_TIMEOUT per stage; iteration counting would overshoot by ~400ms
     * per stage. */
    assert_elapsed_budget_ladder(4000000);
}

void test_kill_no_sigint(void)
{
    /* This process blocks SIGINT */
    InitTime();
    InitFakeProcess(12345, 100, true, false, true);

    int res = GracefulTerminate(1, 12345);
    assert_true(res);

    FakeProcessDoSignals();

    assert_false(exists);
    assert_int_equal(exit_signal, SIGTERM);
}

void test_kill_no_sigint_sigterm(void)
{
    /* This process only can be killed by SIGKILL */
    InitTime();
    InitFakeProcess(12345, 100, true, true, true);

    int res = GracefulTerminate(1, 12345);
    assert_true(res);

    /* Sleep a bit so that KILL is processed. */
    current_time += 100;

    FakeProcessDoSignals();

    assert_false(exists);
    assert_int_equal(exit_signal, SIGKILL);
}

void test_kill_anothers_process(void)
{
    /* This process is not owned by killer */
    InitTime();
    InitFakeProcess(12345, 100, true, true, false);

    int res = GracefulTerminate(1, 12345);
    assert_false(res);

    FakeProcessDoSignals();

    assert_true(exists);
    assert_false(was_stopped);
}

int main()
{
    PRINT_TEST_BANNER();

    const UnitTest tests[] =
    {
        unit_test(test_kill_simple_process),
        unit_test(test_kill_wrong_process),
        unit_test(test_kill_long_reacting_signal),
        unit_test(test_kill_elapsed_budget_under_eintr),
        unit_test(test_kill_elapsed_budget_under_sleep_overshoot),
        unit_test(test_kill_no_sigint),
        unit_test(test_kill_no_sigint_sigterm),
        unit_test(test_kill_anothers_process),
    };

    return run_tests(tests);
}
