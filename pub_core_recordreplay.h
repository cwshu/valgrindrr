
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
 * pub_recordreplay.h --
 *
 *      Definition of record/replay APIs exposed to other modules.
 */

#ifndef _PUB_RECORDREPLAY_H_
#define _PUB_RECORDREPLAY_H_

typedef enum RRState{
   UNINITIALIZED = -1,
   NORECORDREPLAY,
   RECORDONLY,
   REPLAYONLY
}RRState;
   
/* Command line options. */
extern RRState VG_(clo_record_replay);
extern Char* VG_(clo_log_name_rr);

/*
 * Global functions
 * All record/replay APIs exposed to other modules are listed as follow. Do not discriminate current 
 * record/replay state from the global variable VG_(clo_record_replay) when using these APIs. The 
 * implementation of them take this job. 
 */

/* Initialization and exit of m_recordreplay */
extern void VG_(RR_Init)(Int argc, HChar** argv, HChar** p_toolname);
extern void VG_(RR_Exit)(void);

/* Client command line */
extern void VG_(RR_ClientCmdLine) (void);

/* Multi-thread support */
extern void VG_(RR_Thread_Create)(UInt vg_ptid, UInt vg_ctid, unsigned long* lwpid);
extern void VG_(RR_Thread_Exit)(ThreadId tid);
/* get the BigLock and run */
extern void VG_(RR_Thread_Acquire)(ThreadId tid, Char* who);
/* release the BigLock and yield */
extern void VG_(RR_Thread_Release)(ThreadId tid, Char* who);

/* Syscall related */
// extern void VG_(RR_syscall_args) (ThreadId tid, UInt sysno, VexGuestArchState* cpuState); 
extern void VG_(RR_Syscall_VexGuestArchState) (ThreadId tid, UInt sysno, void* vex);
extern void VG_(RR_Syscall_Ret)(ULong* ret);
// VG_(RR_syscall_mem)(Int numAddrs, void* addr1, Int len1, void* addr2, Int len2, ...)
extern void VG_(RR_Syscall_Mem)(Int numAddrs, ...);
/* Remember dispatch counter around every syscall in record, and check it in replay */
extern void VG_(RR_Syscall_DispatchCtr)(UInt ctr, Bool isBefore);

/* Memory layout and initial client state */
extern void VG_(RR_InitState_MemLayout)(Addr* vstart, Addr* client_stacktop);
extern void VG_(RR_InitState_VexGuestArchState) (void* vex);
//extern void VG_(RR_InitState_ClientStack)(Addr clstk_top, IIFinaliseImageInfo* iifii);
extern void VG_(RR_InitState_ClientStack)(Addr clstk_top, void* iifii);

/* An exatra instrumentation pass to record/replay non-deterministic priviledge instructions */
extern IRSB* VG_(instrumentRecordReplay)( void* closureV,
                                          IRSB* sbIn,
                                          VexGuestLayout* layout,
                                          VexGuestExtents* vge,
                                          IRType gWordTy, IRType hWordTy );

/* Thread id conversion between the tid from kernel's view and from client's view. Replay-only */
extern unsigned long VG_(kernel_id_to_recorded_id)(unsigned long kernel_id);
extern unsigned long VG_(recorded_id_to_kernel_id)(unsigned long recorded_id);
#if defined(VGP_x86_linux) || defined(VGP_amd64_linux)
extern void VG_(monitor_clear_child_tid)(ThreadId vg_tid, void* addr);
#endif

#endif /* ifndef _PUB_RECORDREPLAY_H_ */

