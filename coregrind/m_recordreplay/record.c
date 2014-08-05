
/*******************************************************************
   This file is part of Valgrind, a dynamic binary instrumentation
   framework.

   Copyright (C) 2008

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA.

   The GNU General Public License is contained in the file COPYING.
********************************************************************/

/*
 * record.c --
 *
 *      Record only code: writing entries to log and a sync bar for the writing.
 */

#ifndef _PRIV_RECORDREPLAY_H_
#define _PRIV_RECORDREPLAY_H_

#include "pub_core_basics.h"
#include "pub_core_libcfile.h"
#include "pub_core_libcassert.h"
#include "pub_core_syscall.h"
#include "pub_core_vkiscnums.h" /* for __NR_fsync */

#include "pub_core_recordreplay.h"
#include "priv_recordreplay.h"

void ML_(rrsync)(void)
{
   (void)VG_(do_syscall1)(__NR_fsync, ML_(log_fd_rr)); 
}

void ML_(writeToLog)(LogEntry* entry)
{
   UInt ret;

   if(VG_(clo_record_replay) != RECORDONLY) return;
   ret = VG_(write)(ML_(log_fd_rr), entry, sizeof(LogEntry));
   vg_assert2(ret == sizeof(LogEntry), "Error in writeToLog.\n");

   if((entry->type == DATA1 || entry->type == DATA2) && entry->u.data.len > 0)
      VG_(write)(ML_(log_fd_rr), entry->u.data.addr, entry->u.data.len);
   else if(entry->type == CLIENT_CMDLINE){
      UInt len = entry->u.client_cmdline.len;
      VG_(write)(ML_(log_fd_rr), entry->u.client_cmdline.addr, len);
   }
}

#endif /* ifndef _PRIV_RECORDREPLAY_H_ */

