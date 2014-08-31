
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
 * replay.c --
 *
 *      Replay only code: reading entries from replay log.
 */
#include <alloca.h>
#include "pub_core_basics.h"
#include "pub_core_options.h"
#include "pub_core_xarray.h"
#include "pub_core_mallocfree.h"
#include "pub_core_libcbase.h"
#include "pub_core_libcprint.h"
#include "pub_core_libcfile.h"
#include "pub_core_libcassert.h"
#include "pub_core_vki.h"
#include "pub_core_threadstate.h"

#include "pub_core_recordreplay.h"
#include "priv_recordreplay.h"

void ML_(readFromLog)(LogEntry* rt_ent)
{
   /* The following fields of rt_ent should already be filled:
        rt_ent->tid, rt_ent->type, rt_ent->aux_len, and rt_ent->aux_addr(if aux_len > 0)
    */   
   LogEntry* recorded;
   UInt ret;
   
   if(VG_(clo_record_replay) != REPLAYONLY) return;
   recorded = alloca(sizeof(LogEntry));
   ret = VG_(read)(ML_(log_fd_rr), recorded, sizeof(LogEntry)); 
   vg_assert2(ret == sizeof(LogEntry), "Error in reading replay log\n"); 
   
   /*************** sanity check *******************/ 
   vg_assert2(rt_ent->type == recorded->type, "Log entry not expected. "
                "Log type:runtime/recorded=%d/%d\n", rt_ent->type, recorded->type);
   vg_assert2(rt_ent->tid == recorded->tid, "Log entry not expected. "
                "Thread id:runtime/recorded=%d/%d\n", rt_ent->tid, recorded->tid);
   vg_assert2(rt_ent->tid < 1024, "thread id %d is bigger than 1024, not supported for now\n", rt_ent->tid);

   switch(rt_ent->type){
      case CLIENT_CMDLINE:
         vg_assert(rt_ent->u.client_cmdline.addr == NULL);
         vg_assert(recorded->u.client_cmdline.len > 0);
         vg_assert2(recorded->u.client_cmdline.len < MAX_CMDLINE_LENGTH, "Too long client command line."
                    "Only support a length less than %d\n", MAX_CMDLINE_LENGTH);
         rt_ent->u.client_cmdline.len = recorded->u.client_cmdline.len;
         break;

      case SYSCALL_RET:
         rt_ent->u.syscall_ret = recorded->u.syscall_ret;
         break; 

      case SYSCALL_ARGS:
         vg_assert(rt_ent->u.syscall_args.tid == recorded->u.syscall_args.tid);
         vg_assert2(rt_ent->u.syscall_args.sysno == recorded->u.syscall_args.sysno, 
                      "Log entry not expected. Syscall number: runtime/recorded=%d/%d\n", 
                      rt_ent->u.syscall_args.sysno, recorded->u.syscall_args.sysno);
         break;

      case SYSCALL_DISPATCH_CTR:
         vg_assert(rt_ent->u.syscall_dispatch_ctr.isBefore == recorded->u.syscall_dispatch_ctr.isBefore);
         {
          static int first = 0;
         if(rt_ent->u.syscall_dispatch_ctr.counter != recorded->u.syscall_dispatch_ctr.counter) {
             first = 1; 
             if(first == 1)
                  VG_(printf)( "Log entry not expected. Syscall dispatch counter: runtime/recorded=%d/%d\n",
                      rt_ent->u.syscall_dispatch_ctr.counter, recorded->u.syscall_dispatch_ctr.counter);
         }
        }
/*
         vg_assert2(rt_ent->u.syscall_dispatch_ctr.counter == recorded->u.syscall_dispatch_ctr.counter,
                      "Log entry not expected. Syscall dispatch counter: runtime/recorded=%d/%d\n",
                      rt_ent->u.syscall_dispatch_ctr.counter, recorded->u.syscall_dispatch_ctr.counter);
*/
         break;

      case THREAD_CREATE: 
         rt_ent->u.thread_create.vg_tid = recorded->u.thread_create.vg_tid;
         vg_assert2(recorded->u.thread_create.vg_tid < 1024, "thread id number too big: %d\n", 
                       recorded->u.thread_create.vg_tid);
         rt_ent->u.thread_create.lwpid = recorded->u.thread_create.lwpid;
         break;

/*
      case THREAD_EXIT:
         break;
*/

      case ACQUIRE_BIGLOCK:
         vg_assert(recorded->u.acquire_biglock.tid < VG_N_THREADS);
         rt_ent->u.acquire_biglock = recorded->u.acquire_biglock;
         break;

      case RELEASE_BIGLOCK:
         vg_assert2(VG_(memcmp)(rt_ent->u.release_biglock.who, recorded->u.release_biglock.who, 16) == 0,
                      "Log entry not expected. Thread exit: runtime/recorded=%s/%s\n",
                      rt_ent->u.release_biglock.who, recorded->u.release_biglock.who); 
         break;
 
#if defined(VGA_x86) || defined(VGA_amd64)
      case RDTSC:
         rt_ent->u.rdtsc = recorded->u.rdtsc;
         break;
#endif
      
      case INITIMG_MEMLAYOUT:
         rt_ent->u.initimg_memlayout = recorded->u.initimg_memlayout;
         break;

      case INITIMG_CLSTK:
         rt_ent->u.initimg_clstk = recorded->u.initimg_clstk;
         break;

      case DATA2:
         vg_assert2(rt_ent->u.data.len == recorded->u.data.len, "Log entry not expected."
                      "Data len: runtime/recorded=%d/%d\n", rt_ent->u.data.len, recorded->u.data.len);
         // vg_assert2(rt_ent->u.data.addr == recorded->u.data.addr, "Log entry not expected."
         //             "Data addr: runtime/recorded=%X/%X\n", rt_ent->u.data.addr, recorded->u.data.addr);
         break;

      case DATA1:
         vg_assert2(rt_ent->u.data.len == recorded->u.data.len, "Log entry not expected."
                      "Data len: runtime/recorded=%d/%d\n", rt_ent->u.data.len, recorded->u.data.len);
         break;

      default:
         vg_assert2(0, "bad log entry\n");
   }
   /*********** end of sanity check ****************/

   if((rt_ent->type == DATA1 || rt_ent->type == DATA2) && rt_ent->u.data.len > 0){
      ret = VG_(read)(ML_(log_fd_rr), rt_ent->u.data.addr, rt_ent->u.data.len);
      vg_assert2(ret == rt_ent->u.data.len, "Error in reading replay log\n"); 
   }
   else if(rt_ent->type == CLIENT_CMDLINE){
      UInt len = rt_ent->u.client_cmdline.len;
      rt_ent->u.client_cmdline.addr = (Char*)VG_(malloc)("rr.load_record_data", len+1);
      ret = VG_(read)(ML_(log_fd_rr), rt_ent->u.client_cmdline.addr, len);
      vg_assert2(ret == len, "Error in reading replay log\n"); 
      rt_ent->u.client_cmdline.addr[len] = '\0';
   }
}

