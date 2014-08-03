
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
 * syswrapRR-linux.c --
 *
 *      RECORDREPLAY wrappers for the syscalls defined in syswrap-linux.c.
 */

/* 
 * This file is included into syswrap-linux.c. 
 * The order of syscall appeared in this file is the same as the order in syswrap-linux.c,
 * except the pre-succeeded syscalls, which have no RECORDREPLAY wrapper and are handled
 * in their PRE wrappers.
 */

#define RECORDREPLAY(name)   DEFN_RECORDREPLAY_TEMPLATE(linux, name)

RECORDREPLAY(sys_futex)
{
   SHARED_RECORDREPLAY_HEADER;
   /* kernel activity for another thread may write ARG1 and call sys_futex wake. 
      see linux2.6/kernel/fork.c/mm_release() */
   VG_(RR_Syscall_Mem)(1, (void*)ARG1, sizeof(int));
}

RECORDREPLAY(sys_set_robust_list)
{
   SysRes res;

   SHARED_RECORDREPLAY_HEADER;

   if(VG_(clo_record_replay) == REPLAYONLY) {
      /*
       * Kernel will read the robust list header when the threads exits. So we need to
       * make sure this syscall is really called.
       */
      res = VG_(do_syscall2)(__NR_set_robust_list, ARG1, ARG2);
      /* res must shows SUCCESS, or the resust is not consistent with recorded execution */
      vg_assert2(!sr_isError(res) && sr_Res(res) == (*sys_ret), 
                   "Replay failed. recorded/runtime=%llX/%X\n", (*sys_ret), sr_Res(res));
   }
}

RECORDREPLAY(sys_get_robust_list)
{
   SHARED_RECORDREPLAY_HEADER;

   VG_(RR_Syscall_Mem)(2, (void*)ARG2, sizeof(struct vki_robust_list_head *), ARG3, sizeof(struct vki_size_t *));
}

RECORDREPLAY(sys_gettid)
{
   SHARED_RECORDREPLAY_HEADER;
}

RECORDREPLAY(sys_set_tid_address)
{
   SysRes res;

   SHARED_RECORDREPLAY_HEADER;

   /* ARG1 is a sensitive user mode variable, which kernel may modify when a thread exits or starts
     executing a new program. */
   VG_(monitor_clear_child_tid)(tid, (Addr)ARG1);

   if(VG_(clo_record_replay) == REPLAYONLY) {
      /* This syscall produces long-term side-effect to kernel. Don't miss it. */
      res = VG_(do_syscall1)(__NR_set_tid_address, ARG1);
      /* res must shows SUCCESS, or the resust is not consistent with recorded execution */
      vg_assert2(!sr_isError(res) && sr_Res(res) == VG_(recorded_id_to_kernel_id)(*sys_ret), 
                   "Replay failed. recorded/runtime=%llX/%X\n", (*sys_ret), sr_Res(res));
   }
}

RECORDREPLAY(sys_clock_settime)
{
   SHARED_RECORDREPLAY_HEADER;
}

RECORDREPLAY(sys_clock_gettime)
{
   SHARED_RECORDREPLAY_HEADER;

   VG_(RR_Syscall_Mem)(1, (void*)ARG2, sizeof(struct vki_timespec));
}


