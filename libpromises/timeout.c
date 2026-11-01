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

void SetTimeOut(int timeout)
{
    ALARM_PID = -1;
    signal(SIGALRM, (void *) TimeOut);
    alarm(timeout);
}

bool TimeOutIsArmed(void)
{
#ifdef __MINGW32__
    return false;
#else
    /* Callers disarm by restoring SIG_DFL, so the handler tells. */
    struct sigaction current;
    return (sigaction(SIGALRM, NULL, &current) == 0)
        && ((void *) current.sa_handler == (void *) TimeOut);
#endif
}

/*************************************************************************/

void TimeOut()
{
    alarm(0);

    if (ALARM_PID != -1)
    {
        const pid_t pid = ALARM_PID;
        Log(LOG_LEVEL_VERBOSE, "Time out of process %jd", (intmax_t) pid);

#ifndef __MINGW32__
        /* Read while the process is alive: getpgid() fails after it dies. */
        const pid_t pgid = getpgid(pid);
#endif

        GracefulTerminate(pid, PROCESS_START_TIME_UNKNOWN);

#ifndef __MINGW32__
        /* Whatever the process spawned survives it and keeps its output pipe
         * open. Only ever signal a group the process led: if setpgid() did
         * not take effect, -pid could name our own group. */
        if (pgid == pid)
        {
            kill(-pid, SIGKILL);
        }
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
