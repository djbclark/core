/*
  Copyright 2026 Northern.tech AS

  This file is part of CFEngine 3 - written and maintained by Northern.tech AS.

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; version 3.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA

  To the extent this program is licensed as part of the Enterprise
  versions of CFEngine, the applicable Commercial Open Source License
  (COSL) may apply to this file if you as a licensee so wish it. See
  included file COSL.txt.
*/

#include <test.h>

#include <verify_exec.h>
#include <actuator.h>
#include <promises.h>
#include <files_lib.h>
#include <files_interfaces.h>
#include <vars.h>
#include <conversion.h>
#include <instrumentation.h>
#include <attributes.h>
#include <pipes.h>
#include <locks.h>
#include <evalfunction.h>
#include <exec_tools.h>
#include <misc_lib.h>
#include <writer.h>
#include <policy.h>
#include <string_lib.h>
#include <scope.h>
#include <ornaments.h>
#include <eval_context.h>
#include <retcode.h>
#include <timeout.h>
#include <cf3.extern.h>

#include <errno.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* Test-TU interposition for cf_popensetuid() and cf_popen_shsetuid() inside
 * cf-agent/verify_exec.c with zero production seams. */
static bool fail_popen = false;
static bool start_clock_on_fail = false;
static int popensetuid_calls = 0;
static int popen_shsetuid_calls = 0;

static FILE *test_cf_popensetuid(const char *command, const Seq *arglist,
                                 const char *type, uid_t uid, gid_t gid,
                                 char *chdirv, char *chrootv, int background)
{
    popensetuid_calls++;
    if (fail_popen)
    {
        if (start_clock_on_fail)
        {
            /* Mirror a post-fork fdopen() failure where
             * GenericCreatePipeAndFork() started the alarm clock before
             * fdopen() failed and cleaned up the child. */
            StartTimeOutClock();
        }
        errno = ENOMEM;
        return NULL;
    }
    return cf_popensetuid(command, arglist, type, uid, gid,
                          chdirv, chrootv, background);
}

static FILE *test_cf_popen_shsetuid(const char *command, const char *type,
                                    uid_t uid, gid_t gid, char *chdirv,
                                    char *chrootv, int background)
{
    popen_shsetuid_calls++;
    if (fail_popen)
    {
        if (start_clock_on_fail)
        {
            StartTimeOutClock();
        }
        errno = ENOMEM;
        return NULL;
    }
    return cf_popen_shsetuid(command, type, uid, gid,
                             chdirv, chrootv, background);
}

#define cf_popensetuid test_cf_popensetuid
#define cf_popen_shsetuid test_cf_popen_shsetuid
#include "../../cf-agent/verify_exec.c"
#undef cf_popensetuid
#undef cf_popen_shsetuid

static const char *TrueBinaryPath(void)
{
    if (access("/bin/true", X_OK) == 0)
    {
        return "/bin/true";
    }
    return "/usr/bin/true";
}

static void InitExecAttributes(Attributes *a, ShellType shelltype, int timeout)
{
    memset(a, 0, sizeof(*a));
    a->transaction.action = cfa_fix;
    a->contain.timeout = timeout;
    a->contain.owner = (uid_t) -1;
    a->contain.group = (gid_t) -1;
    a->contain.umask = 077;
    a->contain.shelltype = shelltype;
}

static void RunRepairExecFailureCase(ShellType shelltype,
                                     bool simulate_postfork_clock)
{
    const mode_t saved_umask = umask(0);
    umask(saved_umask);

    ClearTimeOut();
    ALARM_PID = -1;
    fail_popen = true;
    start_clock_on_fail = simulate_postfork_clock;
    popensetuid_calls = 0;
    popen_shsetuid_calls = 0;

    EvalContext *ctx = EvalContextNew();
    Policy *policy = PolicyNew();
    Bundle *bundle = PolicyAppendBundle(policy, "default", "test_exec",
                                        "agent", NULL, "test.cf",
                                        EVAL_ORDER_UNDEFINED);
    BundleSection *section = BundleAppendSection(bundle, "commands");
    Promise *pp = BundleSectionAppendPromise(
        section, TrueBinaryPath(),
        (Rval) { NULL, RVAL_TYPE_NOPROMISEE }, NULL, NULL);

    Attributes a;
    InitExecAttributes(&a, shelltype, 1);

    PromiseResult result = PROMISE_RESULT_NOOP;
    const ActionResult act_res = RepairExec(ctx, &a, pp, &result);

    umask(saved_umask);

    assert_int_equal(act_res, ACTION_RESULT_FAILED);
    if (shelltype == SHELL_TYPE_USE)
    {
        assert_int_equal(popen_shsetuid_calls, 1);
        assert_int_equal(popensetuid_calls, 0);
    }
    else
    {
        assert_int_equal(popensetuid_calls, 1);
        assert_int_equal(popen_shsetuid_calls, 0);
    }

    assert_false(TimeOutIsArmed());
    assert_int_equal((int) alarm(0), 0);
    assert_false(TimeOutHasFired());
    assert_false(TimeOutSignalledProcess());

    /* Even if a later fork invokes StartTimeOutClock(), the retired timeout
     * must stay dead and no alarm may fire past the 1-second exec_timeout. */
    StartTimeOutClock();
    assert_int_equal((int) alarm(0), 0);

    struct timespec ts = { .tv_sec = 2, .tv_nsec = 0 };
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
    {
    }

    assert_false(TimeOutHasFired());
    assert_false(TimeOutSignalledProcess());
    assert_false(TimeOutIsArmed());

    PolicyDestroy(policy);
    EvalContextDestroy(ctx);
}

static void test_repair_exec_popensetuid_fail_postfork_clears_timeout(void)
{
    RunRepairExecFailureCase(SHELL_TYPE_NONE, true);
}

static void test_repair_exec_popensetuid_fail_prefork_clears_timeout(void)
{
    RunRepairExecFailureCase(SHELL_TYPE_NONE, false);
}

static void test_repair_exec_popen_shsetuid_fail_postfork_clears_timeout(void)
{
    RunRepairExecFailureCase(SHELL_TYPE_USE, true);
}

static void test_repair_exec_healthy_control(void)
{
    const mode_t saved_umask = umask(0);
    umask(saved_umask);

    ClearTimeOut();
    ALARM_PID = -1;
    fail_popen = false;
    start_clock_on_fail = false;
    popensetuid_calls = 0;
    popen_shsetuid_calls = 0;

    EvalContext *ctx = EvalContextNew();
    Policy *policy = PolicyNew();
    Bundle *bundle = PolicyAppendBundle(policy, "default", "test_exec",
                                        "agent", NULL, "test.cf",
                                        EVAL_ORDER_UNDEFINED);
    BundleSection *section = BundleAppendSection(bundle, "commands");
    Promise *pp = BundleSectionAppendPromise(
        section, TrueBinaryPath(),
        (Rval) { NULL, RVAL_TYPE_NOPROMISEE }, NULL, NULL);

    Attributes a;
    InitExecAttributes(&a, SHELL_TYPE_NONE, 5);

    PromiseResult result = PROMISE_RESULT_NOOP;
    const ActionResult act_res = RepairExec(ctx, &a, pp, &result);

    umask(saved_umask);

    assert_int_equal(act_res, ACTION_RESULT_OK);
    assert_int_equal(popensetuid_calls, 1);
    assert_int_equal(result, PROMISE_RESULT_CHANGE);
    assert_false(TimeOutIsArmed());
    assert_int_equal((int) alarm(0), 0);
    assert_false(TimeOutHasFired());
    assert_false(TimeOutSignalledProcess());

    PolicyDestroy(policy);
    EvalContextDestroy(ctx);
}

int main(void)
{
    PRINT_TEST_BANNER();

    const UnitTest tests[] =
    {
        unit_test(test_repair_exec_popensetuid_fail_postfork_clears_timeout),
        unit_test(test_repair_exec_popensetuid_fail_prefork_clears_timeout),
        unit_test(test_repair_exec_popen_shsetuid_fail_postfork_clears_timeout),
        unit_test(test_repair_exec_healthy_control),
    };

    return run_tests(tests);
}
