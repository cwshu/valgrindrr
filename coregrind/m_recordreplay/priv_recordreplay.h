
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
 * priv_recordreplay.h --
 *
 *      Definition of log format and log entry processing functions.
 */

#ifndef _RPIV_RECORDREPLAY_H_
#define _PRIV_RECORDREPLAY_H_

typedef enum EntryType{
   INVALID = 0,
   SYSCALL_ARGS,
   SYSCALL_RET,
   SYSCALL_DISPATCH_CTR,
   THREAD_CREATE,   
   //THREAD_EXIT,
   ACQUIRE_BIGLOCK,
   RELEASE_BIGLOCK,
#if defined(VGA_x86) || defined(VGA_amd64)
   RDTSC,
#endif
   CLIENT_CMDLINE,
   INITIMG_CLSTK,
   INITIMG_MEMLAYOUT,
   DATA2, /* addr and len are both known before reading log entry */
   DATA1  /* only len is known before */
}EntryType;

typedef struct LogEntry{
   EntryType type;
   UInt tid;
   union{
      struct{            /* CLIENT_CMDLINE */
         UInt len;
         /* the address to store client command line */
         Char* addr;
      }client_cmdline;
      struct{            /* SYSCALL_ARGS */
         UInt tid;
         UInt sysno;
      }syscall_args;
#if defined(VGA_x86) || defined(VGA_amd64)
      ULong syscall_ret; /* SYSCALL_RET */
#else
#error not supported architecture
#endif
      struct{            /* SYSCALL_DISPATCH_CTR */   
         UWord isBefore;
         UWord counter;
      }syscall_dispatch_ctr;
      struct{           /* THREAD_CREATE */
         UInt vg_tid;
         UInt lwpid;
      }thread_create;
      //Char meaningless;  /* THREAD_EXIT */
      struct{           /* ACQUIRE_BIGLOCK */
         UInt tid;
         UInt type;
      }acquire_biglock;
      struct{           /* RELEASE_BIGLOCK */
         Char who[16];
      }release_biglock;
#if defined(VGA_x86) || defined(VGA_amd64)
      struct{
         UInt eax;
         UInt edx;
      }rdtsc;
#endif
      /* TODO: refine the following two */
      struct{
         UInt stksz;
         Addr auxv_addr;
      }initimg_clstk;         
      struct{
         Addr vstart;
         Addr clstk_top;
      }initimg_memlayout;    
  
      struct{           /* DATA1, DATA2 */
         UWord len;
         void* addr;
      }data;
      struct{           /* ALL */
         Char c[16];
      }all;
   }u;
}LogEntry;

/* maximum length of client command line we support */
#define MAX_CMDLINE_LENGTH 4096

extern Int ML_(log_fd_rr); /* the fd for replay log */

/*
 * Module-private global functions
 */

extern void ML_(writeToLog)(LogEntry* entry);
extern void ML_(readFromLog)(LogEntry* entry);
extern void ML_(rrsync) (void);

#define PROCESS_LOGENTRY                                \
   do{                                                  \
      if(VG_(clo_record_replay) == RECORDONLY) {        \
         ML_(writeToLog)(le);                           \
      } else if(VG_(clo_record_replay) == REPLAYONLY) { \
         ML_(readFromLog)(le);                          \
      }                                                 \
   }while(0)

#endif // _PRIV_RECORDREPLAY_H_

