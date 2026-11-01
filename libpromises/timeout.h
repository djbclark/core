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

/* Arm a timeout for the next child forked by cf_popen*(). The clock only
 * starts once the child's pid is in ALARM_PID, so an alarm can never expire
 * with no process to terminate. */
void SetTimeOut(int timeout);
/* Called by cf_popen*() in the parent, after publishing the child's pid. */
void StartTimeOutClock(void);
void TimeOut(void);
time_t SetReferenceTime(void);

#endif
