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

/* Arm a timeout now, for callers that do not fork. */
void SetTimeOut(int timeout);
/* Arm a timeout for the next child forked by cf_popen*(). The clock only
 * starts once the child's pid is in ALARM_PID, so an alarm can never expire
 * with no process to terminate. A request is good for one cf_popen*() call,
 * whether or not it forks, and is dropped by DisarmTimeOut(). */
void SetChildTimeOut(int timeout);
/* Cancel whatever SetTimeOut() or SetChildTimeOut() requested. */
void DisarmTimeOut(void);
/* Used by cf_popen*(): take the request for this child, then start its clock
 * in the parent once the child's pid is published. */
int TakeChildTimeOut(void);
void StartTimeOutClock(int seconds);
void TimeOut(void);
/* Recorded by cf_popen*() for every child it publishes in ALARM_PID: its pid
 * if it was put in a process group of its own, else -1. */
void SetTimedChildGroup(pid_t pid);
/* Kill the process group of the child running under a timeout, if it has one
 * of its own. For a process that is about to leave it behind. */
void KillTimedChildGroup(void);
time_t SetReferenceTime(void);

#endif
