
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
 * syswrapRR-x86-linux.c --
 *
 *      RECORDREPLAY wrappers for the syscalls defined in syswrap-x86-linux.c.
 */

/* 
 * This file is included into syswrap-x86-linux.c. 
 * The order of syscall appeared in this file is the same as the order in syswrap-x86-linux.c,
 * except the pre-succeeded syscalls, which have no RECORDREPLAY wrapper and are handled
 * in their PRE wrappers.
 */

#define RECORDREPLAY(name)   DEFN_RECORDREPLAY_TEMPLATE(x86_linux, name)

RECORDREPLAY(sys_lstat64)
{
   SHARED_RECORDREPLAY_HEADER;

   if(ARG2 != 0) {
      VG_(RR_Syscall_Mem) (1, (void*)ARG2, sizeof(struct vki_stat64));
   }
}

RECORDREPLAY(sys_stat64)
{
   SHARED_RECORDREPLAY_HEADER;

   if(ARG2 != 0) {
      VG_(RR_Syscall_Mem)(1, (void*)ARG2, sizeof(struct vki_stat64));
   }
}

RECORDREPLAY(sys_fstat64)
{
   SHARED_RECORDREPLAY_HEADER;

   if(ARG2 != 0) {
      VG_(RR_Syscall_Mem)(1, (void*)ARG2, sizeof(struct vki_stat64));
   }
}

RECORDREPLAY(sys_socketcall)
{
#  define ARG2_0  (((UWord*)ARG2)[0])
#  define ARG2_1  (((UWord*)ARG2)[1])
#  define ARG2_2  (((UWord*)ARG2)[2])
#  define ARG2_3  (((UWord*)ARG2)[3])
#  define ARG2_4  (((UWord*)ARG2)[4])
#  define ARG2_5  (((UWord*)ARG2)[5])

   SHARED_RECORDREPLAY_HEADER;

   switch (ARG1 /* request */) {
   case VKI_SYS_SOCKETPAIR:
   {
      VG_(RR_Syscall_Mem)(1, (void*)ARG2_3, sizeof(Int)*2);
      break;
   }

   case VKI_SYS_SOCKET:
   case VKI_SYS_BIND:
   case VKI_SYS_LISTEN:
   {
      /* no memory side effect */
      break;
   }

   case VKI_SYS_ACCEPT:
   {
      VG_(RR_Syscall_Mem)(2, (void*)ARG2_1, sizeof(struct vki_sockaddr), (void*)ARG2_2, sizeof(UInt));
      break;
   }

   case VKI_SYS_SENDTO:
   case VKI_SYS_SEND:
   {
      /* no memory side effect */
      break;
   }

   case VKI_SYS_RECVFROM:
   {
      /* len = ARG2_2;
         Although actual received bytes may be less than len, we record
         len bytes here for simplicity */
      VG_(RR_Syscall_Mem)(1, (void*)ARG2_1, (ULong)ARG2_2);
      break;
   }

   case VKI_SYS_RECV:
   {
      /* len = ARG2_2;
         Although actual received bytes may be less than len, we record
         len bytes here for simplicity */
      VG_(RR_Syscall_Mem)(1, (void*)ARG2_1, (ULong)ARG2_2);
      break;
   }

   case VKI_SYS_CONNECT:
   case VKI_SYS_SETSOCKOPT:
   {
      /* no memory write to replay */
      break;
   }

   case VKI_SYS_GETSOCKOPT:
   {
      ULong len;
      /* I am not sure this is true. some test needed */
      VG_(RR_Syscall_Mem)(1, (void*)ARG2_4, sizeof(ULong));
      len = *((ULong*)ARG2_4);
      VG_(RR_Syscall_Mem)(1, (void*)ARG2_3, len);
      break;
   }

   case VKI_SYS_GETSOCKNAME:
   {
      ULong len;
      /* I am not sure this is true. some test needed */
      VG_(RR_Syscall_Mem)(1, (void*)ARG2_2, sizeof(ULong));
      len = *((ULong*)ARG2_2);
      VG_(RR_Syscall_Mem)(1, (void*)ARG2_1, len);
      break;
   }

   case VKI_SYS_GETPEERNAME:
   {
      ULong len;
      /* I am not sure this is true. some test needed */
      VG_(RR_Syscall_Mem)(1, (void*)ARG2_2, sizeof(ULong));
      len = *((ULong*)ARG2_2);
      if(len != 0) {
         VG_(RR_Syscall_Mem)(1, (void*)ARG2_1, len);
      }
      break;
   }

   case VKI_SYS_SHUTDOWN:
   case VKI_SYS_SENDMSG:
   {
      /* no memory write to replay */
      break;
   }

   case VKI_SYS_RECVMSG:
   {
      /* memory layout difference of record execution and replay execution should be known.
       * because the members of msghdr may point to other places that we cannot know from
       * the syscall arguments 
       */
      VG_(core_panic)("recvmsg not supported yet... bye!\n");
     /* ML_(generic_POST_sys_recvmsg)( tid, ARG2_0, ARG2_1 ); */
      break;
   }
   default:
      VG_(message)(Vg_DebugMsg,"FATAL: unhandled socketcall 0x%x",ARG1);
      VG_(core_panic)("... bye!\n");
      break; /*NOTREACHED*/
   } //end of switch

#  undef ARG2_0
#  undef ARG2_1
#  undef ARG2_2
#  undef ARG2_3
#  undef ARG2_4
#  undef ARG2_5
}

