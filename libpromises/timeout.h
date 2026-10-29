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

#ifndef CFENGINE_TIMEOUT_H
#define CFENGINE_TIMEOUT_H

/* Arm a timeout, but leave its clock stopped: wherever cf_popen() lives in this
 * tree, the alarm is started by StartTimeOutClock() once there is a child to
 * terminate. Platforms whose pipes are implemented elsewhere start it here. */
void SetTimeOut(int timeout);

/* Start the clock armed by SetTimeOut(). Called by cf_popen() right after the
 * child's pid reaches ALARM_PID, so an alarm can never be pending with nothing
 * registered to kill. No-op when no timeout is armed, or when the clock is
 * already running. */
void StartTimeOutClock(void);

/* Cancel a pending alarm and restore the default handler. Does not clear what
 * TimeOutHasFired()/TimeOutSignalledProcess() report; SetTimeOut() does. */
void ClearTimeOut(void);

/* True between SetTimeOut() and the disarm, whether or not the clock has
 * started. Tells a forking caller whether the child needs a process group of
 * its own. */
bool TimeOutIsArmed(void);

/* True if the last armed alarm fired, even if the command's exit status reads
 * as success. Cleared by SetTimeOut(). */
bool TimeOutHasFired(void);

/* True if that alarm also had a process to signal. False means timed out but
 * not terminated. */
bool TimeOutSignalledProcess(void);
void TimeOut(void);
time_t SetReferenceTime(void);

#endif
