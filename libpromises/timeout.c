/*
  Copyright 2024 Northern.tech AS

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

#include <cf3.defs.h>
#include <timeout.h>
#include <process_lib.h>

/* Seconds requested by SetChildTimeOut(), waiting for cf_popen*() to publish
 * the pid of the child they are meant for. */
static int TIMEOUT_PENDING = 0; /* GLOBAL_X */

void SetTimeOut(int timeout)
{
    ALARM_PID = -1;
    TIMEOUT_PENDING = 0;
    signal(SIGALRM, (void *) TimeOut);
    alarm(timeout);
}

void SetChildTimeOut(int timeout)
{
#if defined(__MINGW32__) || defined(__CYGWIN__)
    /* pipes_unix.c is not built here, so nothing would start the clock. */
    SetTimeOut(timeout);
#else
    /* A timer leaked by an earlier caller must not fire as ours. */
    SetTimeOut(0);
    TIMEOUT_PENDING = timeout;
#endif
}

void DisarmTimeOut(void)
{
    TIMEOUT_PENDING = 0;
    alarm(0);
    signal(SIGALRM, SIG_DFL);
}

int TakeChildTimeOut(void)
{
    const int seconds = TIMEOUT_PENDING;
    TIMEOUT_PENDING = 0;
    return seconds;
}

void StartTimeOutClock(int seconds)
{
#if !defined(__MINGW32__) && !defined(__CYGWIN__)
    /* Callers that disarm their timeout restore SIG_DFL; arming now would
     * then have SIGALRM terminate us. */
    struct sigaction current;
    if ((seconds > 0)
        && (sigaction(SIGALRM, NULL, &current) == 0)
        && ((void *) current.sa_handler == (void *) TimeOut))
    {
        alarm(seconds);
    }
#else
    (void) seconds;
#endif
}

/*************************************************************************/

/* The child last published in ALARM_PID, if it was put in a process group of
 * its own (see cf_popen*()), else -1. Recorded at fork time because
 * getpgid() cannot be asked later: on macOS it fails for a group leader that
 * has exited and not been reaped, while its group lives on. */
static pid_t TIMED_GROUP = -1; /* GLOBAL_X */

void SetTimedChildGroup(pid_t pid)
{
    TIMED_GROUP = pid;
}

#ifndef __MINGW32__
/* Kill the group of pid, if it is the timed child's. It never is our own
 * group, nor that of a command that was not given a group of its own, which
 * may have made one for itself with setsid(). Only calls what a signal
 * handler may. */
static void KillGroupOfTimedChild(pid_t pid)
{
    if ((pid > 1) && (pid == TIMED_GROUP) && (pid != getpid()))
    {
        kill(-pid, SIGKILL);
    }
}
#endif

void KillTimedChildGroup(void)
{
#ifndef __MINGW32__
    KillGroupOfTimedChild(ALARM_PID);
#endif
}

void TimeOut()
{
    alarm(0);

    if (ALARM_PID != -1)
    {
        const pid_t pid = ALARM_PID;
        Log(LOG_LEVEL_VERBOSE, "Time out of process %jd", (intmax_t) pid);

        GracefulTerminate(pid, PROCESS_START_TIME_UNKNOWN);

#ifndef __MINGW32__
        /* Whatever the process spawned survives it and keeps its output pipe
         * open. */
        KillGroupOfTimedChild(pid);
#endif
    }
    else
    {
        Log(LOG_LEVEL_VERBOSE, "%s> Time out", VPREFIX);
    }
}

/*************************************************************************/

time_t SetReferenceTime(void)
{
    time_t tloc;

    if ((tloc = time((time_t *) NULL)) == -1)
    {
        Log(LOG_LEVEL_ERR, "Couldn't read system clock. (time: %s)", GetErrorStr());
    }

    CFSTARTTIME = tloc;
    Log(LOG_LEVEL_VERBOSE, "Reference time set to '%s'", ctime(&tloc));

    return tloc;
}
