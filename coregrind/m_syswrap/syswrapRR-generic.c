
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
 * syswrapRR-generic.c --
 *
 *      RECORDREPLAY wrappers for the syscalls defined in syswrap-generic.c.
 */

/* 
 * This file is included into syswrap-generic.c. 
 * The order of syscall appeared in this file is the same as the order in syswrap-generic.c,
 * except the pre-succeeded syscalls, which have no RECORDREPLAY wrapper and are handled
 * in their PRE wrappers.
 */

#define RECORDREPLAY(name)   DEFN_RECORDREPLAY_TEMPLATE(generic, name)

RECORDREPLAY(sys_access)
{
   SHARED_RECORDREPLAY_HEADER;
}

RECORDREPLAY(sys_gettimeofday)
{
   SHARED_RECORDREPLAY_HEADER;

   if(ARG2 == 0) {
      VG_(RR_Syscall_Mem)(1, (void*)ARG1, sizeof(struct vki_timeval));
   } else {
      VG_(RR_Syscall_Mem)(2, (void*)ARG1, sizeof(struct vki_timeval), (void*)ARG2, sizeof(struct vki_timezone));
   }
}

RECORDREPLAY(sys_settimeofday)
{
   SHARED_RECORDREPLAY_HEADER;
}

RECORDREPLAY(sys_read)
{
   SHARED_RECORDREPLAY_HEADER;

   if(ARG2 != 0) {
      VG_(RR_Syscall_Mem)(1, (void*)ARG2, *sys_ret);
   }
}

RECORDREPLAY(sys_write)
{
   SHARED_RECORDREPLAY_HEADER;
}

RECORDREPLAY(sys_time)
{
   SHARED_RECORDREPLAY_HEADER;

   if(ARG1 != 0) {
      VG_(RR_Syscall_Mem)(1, (void*)ARG1, sizeof(vki_time_t));
   }
}

RECORDREPLAY(sys_times)
{
   SHARED_RECORDREPLAY_HEADER;

   if(ARG1 != 0) {
      VG_(RR_Syscall_Mem)(1, (void*)ARG1, sizeof(struct vki_tms));
   }
}

RECORDREPLAY(sys_newuname)
{
   SHARED_RECORDREPLAY_HEADER;

   if(ARG1 != 0) {
      VG_(RR_Syscall_Mem)(1, (void*)ARG1, sizeof(struct vki_new_utsname));
   }
}
