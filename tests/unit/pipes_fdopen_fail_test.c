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

#include <cf3.defs.h>
#include <cf3.extern.h>
#include <pipes.h>
#include <mutex.h>
#include <global_mutex.h>
#include <exec_tools.h>
#include <rlist.h>
#include <policy.h>
#include <eval_context.h>
#include <file_lib.h>
#include <signals.h>
#include <string_lib.h>
#include <timeout.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

/* Test-TU interposition for fdopen() inside libpromises/pipes_unix.c with zero
 * production seams. Every header included by pipes_unix.c is already included
 * above so the macro expands only at the eight fdopen() call sites inside
 * pipes_unix.c. */
static bool fail_next_fdopen = false;
static int fdopen_fail_count = 0;
static pid_t fdopen_saw_alarm_pid = -1;

static FILE *test_fdopen(int fd, const char *mode)
{
    if (fail_next_fdopen)
    {
        fail_next_fdopen = false;
        fdopen_fail_count++;
        fdopen_saw_alarm_pid = ALARM_PID;
        errno = ENOMEM;
        return NULL;
    }
    return fdopen(fd, mode);
}

#define fdopen test_fdopen
#include "../../libpromises/pipes_unix.c"
#undef fdopen

static const char *TrueBinaryPath(void)
{
    if (access("/bin/true", X_OK) == 0)
    {
        return "/bin/true";
    }
    return "/usr/bin/true";
}

static int CountOpenFds(void)
{
    int count = 0;
    for (int fd = 0; fd < 256; fd++)
    {
        if (fcntl(fd, F_GETFD) != -1 || errno != EBADF)
        {
            count++;
        }
    }
    return count;
}

static void ArmFdopenFailure(int *fds_before)
{
    ALARM_PID = -1;
    fail_next_fdopen = true;
    fdopen_fail_count = 0;
    fdopen_saw_alarm_pid = -1;
    *fds_before = CountOpenFds();
}

static void AssertFdopenFailureCleanup(FILE *pp, int fds_before)
{
    assert_true(pp == NULL);
    assert_int_equal(fdopen_fail_count, 1);
    assert_true(fdopen_saw_alarm_pid > 0);
    assert_int_equal((int) ALARM_PID, -1);

    int status = 0;
    errno = 0;
    assert_int_equal((int) waitpid(-1, &status, WNOHANG), -1);
    assert_int_equal(errno, ECHILD);

    assert_int_equal(CountOpenFds(), fds_before);
}

static void test_cf_popen_r_fdopen_fail(void)
{
    int fds_before;
    ArmFdopenFailure(&fds_before);

    FILE *pp = cf_popen(TrueBinaryPath(), "r", true);
    AssertFdopenFailureCleanup(pp, fds_before);
}

static void test_cf_popen_w_fdopen_fail(void)
{
    int fds_before;
    ArmFdopenFailure(&fds_before);

    FILE *pp = cf_popen(TrueBinaryPath(), "w", false);
    AssertFdopenFailureCleanup(pp, fds_before);
}

static void test_cf_popensetuid_r_fdopen_fail(void)
{
    int fds_before;
    ArmFdopenFailure(&fds_before);

    FILE *pp = cf_popensetuid(TrueBinaryPath(), NULL, "r",
                              (uid_t) -1, (gid_t) -1, NULL, NULL, 0);
    AssertFdopenFailureCleanup(pp, fds_before);
}

static void test_cf_popensetuid_w_fdopen_fail(void)
{
    int fds_before;
    ArmFdopenFailure(&fds_before);

    FILE *pp = cf_popensetuid(TrueBinaryPath(), NULL, "w",
                              (uid_t) -1, (gid_t) -1, NULL, NULL, 0);
    AssertFdopenFailureCleanup(pp, fds_before);
}

static void test_cf_popen_sh_r_fdopen_fail(void)
{
    int fds_before;
    ArmFdopenFailure(&fds_before);

    FILE *pp = cf_popen_sh("true", "r");
    AssertFdopenFailureCleanup(pp, fds_before);
}

static void test_cf_popen_sh_w_fdopen_fail(void)
{
    int fds_before;
    ArmFdopenFailure(&fds_before);

    FILE *pp = cf_popen_sh("true", "w");
    AssertFdopenFailureCleanup(pp, fds_before);
}

static void test_cf_popen_shsetuid_r_fdopen_fail(void)
{
    int fds_before;
    ArmFdopenFailure(&fds_before);

    FILE *pp = cf_popen_shsetuid("true", "r",
                                 (uid_t) -1, (gid_t) -1, NULL, NULL, 0);
    AssertFdopenFailureCleanup(pp, fds_before);
}

static void test_cf_popen_shsetuid_w_fdopen_fail(void)
{
    int fds_before;
    ArmFdopenFailure(&fds_before);

    FILE *pp = cf_popen_shsetuid("true", "w",
                                 (uid_t) -1, (gid_t) -1, NULL, NULL, 0);
    AssertFdopenFailureCleanup(pp, fds_before);
}

static void test_cf_popen_healthy_control(void)
{
    ALARM_PID = -1;
    fail_next_fdopen = false;
    fdopen_fail_count = 0;
    const int fds_before = CountOpenFds();

    FILE *pp = cf_popen(TrueBinaryPath(), "r", true);
    assert_true(pp != NULL);
    assert_int_equal(fdopen_fail_count, 0);
    assert_int_equal(cf_pclose(pp), 0);
    assert_int_equal((int) ALARM_PID, -1);
    assert_int_equal(CountOpenFds(), fds_before);
}

static void test_cf_popen_sh_healthy_control(void)
{
    ALARM_PID = -1;
    fail_next_fdopen = false;
    fdopen_fail_count = 0;
    const int fds_before = CountOpenFds();

    FILE *pp = cf_popen_sh("true", "r");
    assert_true(pp != NULL);
    assert_int_equal(fdopen_fail_count, 0);
    assert_int_equal(cf_pclose(pp), 0);
    assert_int_equal((int) ALARM_PID, -1);
    assert_int_equal(CountOpenFds(), fds_before);
}

int main(void)
{
    PRINT_TEST_BANNER();

    const UnitTest tests[] =
    {
        unit_test(test_cf_popen_r_fdopen_fail),
        unit_test(test_cf_popen_w_fdopen_fail),
        unit_test(test_cf_popensetuid_r_fdopen_fail),
        unit_test(test_cf_popensetuid_w_fdopen_fail),
        unit_test(test_cf_popen_sh_r_fdopen_fail),
        unit_test(test_cf_popen_sh_w_fdopen_fail),
        unit_test(test_cf_popen_shsetuid_r_fdopen_fail),
        unit_test(test_cf_popen_shsetuid_w_fdopen_fail),
        unit_test(test_cf_popen_healthy_control),
        unit_test(test_cf_popen_sh_healthy_control),
    };

    return run_tests(tests);
}
