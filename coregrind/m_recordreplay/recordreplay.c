
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
 * recordreplay.c --
 *
 *      Record/replay APIs that m_recordreplay exposes to other modules.
 */

#include <alloca.h>

#include "pub_core_basics.h"
#include "pub_core_xarray.h"
#include "pub_core_mallocfree.h"
#include "pub_core_libcbase.h"
#include "pub_core_libcprint.h"
#include "pub_core_libcfile.h"
#include "pub_core_libcassert.h"
#include "pub_core_xarray.h"
#include "pub_core_clientstate.h"
#include "pub_core_vki.h"
#include "pub_core_options.h"
#include "pub_core_threadstate.h" 
#include "pub_core_libcproc.h" /* for VG_(gettid) */
#include "pub_core_machine.h"  /* for VG_(get_SP) */
#include "pub_core_stacks.h"   /* for VG_(unknown_SP_update) */
#include "pub_core_syscall.h"  /* for VG_(strerror) */
#include "pub_core_initimg.h"
#include "pub_core_recordreplay.h"
#include "pub_core_vkiscnums.h" /* for __NR_wait4 */
//#include "pub_core_stacktrace.h"    // For VG_(get_and_pp_StackTrace)()

#include "priv_recordreplay.h"

/* Global variables */
RRState VG_(clo_record_replay) = UNINITIALIZED; 
Char* VG_(clo_log_name_rr) = "_temp_rr_.log";
Int ML_(log_fd_rr) = -1; //file descriptor of VG_(clo_log_name_rr)

/*
 * Local data
 */

/* 
 * Next thread to schedule. When in replay, we need to figure out which
 * thread is supposed to run before acquired the BigLock.
 */
typedef struct NextToSchedule{
   UInt tid;
   UInt type;
}NextToSchedule;
/* for NextToSchedule.type */
#define CALLER_NORMAL 0
#define CALLER_SIGVKILL 1 
#define CALLER_ASYNCHANDLER 2
#define CALLER_UNKNOWN -1

/* maximum length of client command line we support */
#define MAX_CMDLINE_LENGTH 4096

typedef struct RRThreadState{
   /* The following two are for thread id mapping. Meaningful only in replay */ 
   unsigned long tid_recorded;
   unsigned long tid_kernel;
#if defined(VGP_x86_linux) || defined(VGP_amd64_linux)
   /*
    * When not NULL, the OS kernel has set up a mechanism to be triggered when the child
    * process will exit or when it will start execution a new program. In these two cases,
    * the kernel will clear this User Mode variable and will awaken any process waiting 
    * for this event. 
    * Currently, we remember this variable to monitor whether a thread has exited in
    * kernel part. (In syswrap-x86-linux.c, do_clone is handled, but do_fork_clone is not)
    */
   Addr clear_child_tid;
#endif
}RRThreadState;

/* Local variables */
static Char* client_cmdline = NULL;
static volatile NextToSchedule nextSchedule;
static volatile RRThreadState threads_rr[VG_N_THREADS]; 
static volatile unsigned long exiting_thread;
static UInt num_guest_state_mismatch = 0; // to remember how many guest registers mismatched

/*
 * XXX: This prototype is not included in any header files.
 * Only used by VG_(RR_Thread_Acquire). If OS schedules a wrong thread to run, 
 * VG_(RR_Thread_Acquire) calls it to yield.
 */
extern void VG_(replay_yield) (ThreadId tid);

/* Local fucntion prototypes */
/* Setup record/replay arguments: VG_(clo_record_replay), VG_(clo_log_name_rr), and ML_(log_fd_rr). */
static void setupRRArgs(Int argc, HChar** argv);
/* Compare runtime guest registers with recorded guest registers to detect any inconsistency */
static void check_VexGuestArchState(VexGuestArchState* runtimeVex, VexGuestArchState* recordedVex);
/* Record guest registers in record; check them in replay */
static void RR_VexGuestArchState(VexGuestArchState* runtimeVex);
/* wait for the completion of thread "exiting_thread" */
static void wait_thread_exits();

/*********** Implementation of record/replay APIs ****************************/

/*
 *----------------------------------------------------------------------------
 *
 * VG_(RR_Init) --
 *
 *       Initialize the module. This function is called when the
 *       module is loaded at the beginning of valgrind_main in m_main.c.
 *
 * Results:
 *       None 
 *
 * Side effects:
 *       None
 *
 *----------------------------------------------------------------------------
 */
void 
VG_(RR_Init)(Int argc, HChar** argv, HChar** p_toolname)
{
   int i;

   *p_toolname = "none"; //none tool defaulted
   /* (*p_toolname) may be overrided when processing command line in m_main.c */

   /* setup VG_(clo_record_replay) and VG_(clo_log_name_rr) */
   setupRRArgs(argc, argv);

   /* thread id mapping initialization*/
   for(i = 0; i < VG_N_THREADS; i++){
      threads_rr[i].tid_recorded = VG_INVALID_THREADID;
      threads_rr[i].tid_kernel = VG_INVALID_THREADID;
#if defined(VGP_x86_linux) || defined(VGP_amd64_linux) 
      threads_rr[i].clear_child_tid = NULL;
#endif
   }

   exiting_thread = VG_INVALID_THREADID;

   nextSchedule.tid = VG_INVALID_THREADID;
   nextSchedule.type = CALLER_UNKNOWN;
}

/*
 *----------------------------------------------------------------------------
 *
 * VG_(RR_Exit) --
 *
 *       Print summary of guest state divergence detected and free allocated 
 *       resources. This function is called when Valgrind is about to exit in 
 *       m_main.c/shutdown_actions_NORETURN(...).
 *
 * Results:
 *       None 
 *
 * Side effects:
 *       None
 *
 *----------------------------------------------------------------------------
 */
void 
VG_(RR_Exit)(void)
{
   /* Close the file descriptor used for record & replay */
   VG_(close) (ML_(log_fd_rr));
   if(VG_(clo_record_replay) == REPLAYONLY){ /* replay */
      VG_(printf)("REPLAY -- number of guest state mismatch: %d\n", num_guest_state_mismatch);
      vg_assert(client_cmdline != NULL);
      VG_(free) (client_cmdline);
   }

   VG_(clo_record_replay) = UNINITIALIZED;
   ML_(log_fd_rr) = -1;
}

/*
 *----------------------------------------------------------------------------
 *
 * VG_(RR_ClientCmdLine) --
 *
 *       Record/replay client command line. 
 *       
 *       The client command line comes from two global variables: 
 *       VG_(args_the_exename) and VG_(args_for_client). Combine these two to
 *       get a complete client command line in record; split the string read
 *       from replay log and feeded corresponding substrings to them.
 *
 * Results:
 *       None 
 *
 * Side effects:
 *       VG_(args_the_exename) and VG_(args_for_client) are filled up in replay.
 *
 *----------------------------------------------------------------------------
 */
void 
VG_(RR_ClientCmdLine) (void)
{
   LogEntry* le;

   le = alloca(sizeof(LogEntry));
   le->type = CLIENT_CMDLINE;
   le->tid = VG_(running_tid);
   if(VG_(clo_record_replay) == RECORDONLY){ /* in record */
      int i;
      Char* buf[MAX_CMDLINE_LENGTH];

      VG_(sprintf)(&buf[0], "%s", VG_(args_the_exename));
      for (i = 0; i < VG_(sizeXA)(VG_(args_for_client)); i++) {
         VG_(sprintf)(buf+VG_(strlen)(buf), " %s", *(Char**) VG_(indexXA)( VG_(args_for_client), i));
         vg_assert2(VG_(strlen)(buf) < MAX_CMDLINE_LENGTH, "Too long client command line. Only support"
                    " a length less than %d\n", MAX_CMDLINE_LENGTH);
      }
      buf[VG_(strlen)(buf)] = '\0';

      le->u.client_cmdline.len = VG_(strlen)(buf);
      le->u.client_cmdline.addr = buf;
   }

   PROCESS_LOGENTRY;

   if(VG_(clo_record_replay) == REPLAYONLY){ /* in replay */
      /* get VG_(args_the_exename), and VG_(args_for_client) */
      UInt i = 0;
      Char* p = le->u.client_cmdline.addr;
      UInt len = le->u.client_cmdline.len;  

      /* le->u.client_cmdline.addr is allocated at replay.c/ML_(readFromLog)(..)
         and freed at RR_Exit() */
      client_cmdline = le->u.client_cmdline.addr;

      while(i < len){
         if(VG_(isspace)(p[i])){
            p[i]='\0';
            break;
         }
         i++;
      }
      vg_assert(i <= len);

      VG_(args_the_exename) = &p[0];
      if(VG_(strlen)(p) == len) // no args for client
         return;
      /* the rest are args for the client */
      i = VG_(strlen) (&p[0]) + 1; /* '\0' delimiter at the end*/

      /* Add arguments to VG_(args_for_client) */
      {
         Char* tmp;
         Char* cp = &p[i];
         vg_assert(cp);
         while (True) {
            while (VG_(isspace)(*cp)) cp++;
            if (*cp == 0) break;
            tmp = cp;
            while ( !VG_(isspace)(*cp) && *cp != 0 ) cp++;
            if ( *cp != 0 ) *cp++ = '\0';
            VG_(addToXA)( VG_(args_for_client), &tmp);
         } //end while
      } 
   } //end if
}

/*
 *----------------------------------------------------------------------------
 *
 * VG_(RR_Thread_Create) --
 *
 *       For thread creation. 
 *
 * Results:
 *       None 
 *
 * Side effects:
 *       New runtime thread id and the one from replay log is saved into the
 *       array for thread id mapping in replay. 
 *       A recorded thread id is feeded back to client program in replay.
 *
 *----------------------------------------------------------------------------
 */
void 
VG_(RR_Thread_Create)(UInt vg_ptid, UInt vg_ctid, unsigned long* lwpid)
{
   LogEntry* le;

   vg_assert(VG_(running_tid) == vg_ptid);
   le = alloca(sizeof(LogEntry));
   le->tid = VG_(running_tid);
   le->type = THREAD_CREATE;

   if(VG_(clo_record_replay) == RECORDONLY){
      le->u.thread_create.vg_tid = vg_ctid;
      le->u.thread_create.lwpid = (*lwpid);
   }
   
   PROCESS_LOGENTRY;

   if (VG_(clo_record_replay) == REPLAYONLY){
      vg_assert(vg_ctid != VG_INVALID_THREADID && vg_ctid < VG_N_THREADS);
      vg_assert(threads_rr[vg_ctid].tid_recorded == VG_INVALID_THREADID && 
                   threads_rr[vg_ctid].tid_kernel == VG_INVALID_THREADID);
      threads_rr[vg_ctid].tid_kernel = (*lwpid);
      threads_rr[vg_ctid].tid_recorded = le->u.thread_create.lwpid;
#if defined(VGP_x86_linux) || defined(VGP_amd64_linux)
      threads_rr[vg_ctid].clear_child_tid = NULL;
#endif
      /* feed back the tid to client */
      *lwpid = le->u.thread_create.lwpid;
   }
}

/*
 *----------------------------------------------------------------------------
 *
 * VG_(RR_Thread_Exit) --
 *
 *       For thread exit. 
 *       Must be called before the BigLock is released.
 *
 * Results:
 *       None 
 *
 * Side effects:
 *       Which thread exiting is saved, and subsequent RR_Thread_Acquire should wait for
 *       completion of this exiting thread in the OS kernel part. 
 *
 *----------------------------------------------------------------------------
 */
void 
VG_(RR_Thread_Exit) (ThreadId tid)
{
   ThreadState* ts = VG_(get_ThreadState)(tid);

   vg_assert(tid != VG_INVALID_THREADID);
   VG_(RR_Thread_Release) (tid, "exit_thread");
   exiting_thread = tid; 
   threads_rr[tid].tid_kernel = VG_INVALID_THREADID;
   threads_rr[tid].tid_recorded = VG_INVALID_THREADID;
}

/*
 *----------------------------------------------------------------------------
 *
 * VG_(RR_Thread_Acquire) --
 *
 *       Record/replay VG_(acquire_BigLock), which happens when a new thread is 
 *       scheduled to run by OS kernel. 
 *       
 *       Save this fact into log in record; Make sure a desired thread is selected
 *       to run after this function in replay.
 *
 *       If there is a thread that is about to exit, wait for its completion.
 *
 * Results:
 *       None 
 *
 * Side effects:
 *       Thread "tid" is scheduled to run.
 *
 *----------------------------------------------------------------------------
 */
void
VG_(RR_Thread_Acquire) (ThreadId tid, Char* who)
{
   /* we have already got the BigLock now, so don't worry about thread-safe issue */
   
   if(nextSchedule.tid == VG_INVALID_THREADID){ /* main thread gets lock for the first time */
      vg_assert(tid == 1);
      nextSchedule.tid = 1;
      nextSchedule.type = CALLER_NORMAL;
      return;
   }

   if(VG_(clo_record_replay) == RECORDONLY){ /* in record */
      LogEntry* le;

      /* VG_(running_tid) is VG_INVALID_THREADID at this time */
      vg_assert(VG_(running_tid) == VG_INVALID_THREADID);
      le = alloca(sizeof(LogEntry));
      le->type = ACQUIRE_BIGLOCK;
      le->tid = VG_(running_tid);
      le->u.acquire_biglock.tid = tid;
#define VG_STREQN(nn,s1,s2) (0==VG_(strncmp_ws)((s1),(s2),(nn)))
      if(VG_STREQN(17,"sigvgkill_handler", who)) {
         le->u.acquire_biglock.type = CALLER_SIGVKILL;
      } else {
         le->u.acquire_biglock.type = CALLER_NORMAL;
      }

      ML_(writeToLog)(le); 
   }
   
//   PROCESS_LOGENTRY;

   else if(VG_(clo_record_replay) == REPLAYONLY){ /* in replay */
      ThreadState* tst;

      /* nextSchedule.tid and .type is are ready */
      if(VG_STREQN(17,"sigvgkill_handler", who)) {
#undef VG_STREQN
         vg_assert(nextSchedule.type == CALLER_SIGVKILL);
      } else {
         vg_assert(nextSchedule.type == CALLER_NORMAL);
      }

      while(nextSchedule.tid != tid){ /* I am not supposed to run now */
         //VG_(printf)("replay_yield: tid=%d nextSchedule.tid=%d\n", tid, nextSchedule.tid);
         VG_(replay_yield)(tid);
      }

      /* now it is time for current thread to run */

      tst = VG_(get_ThreadState)(tid);
      if(nextSchedule.type == CALLER_SIGVKILL){/* this acquire_BigLock is for sigvgkill_handler */
         /* This is supposed to be a replay of sigvkill_handler. But we have no way 
          * to replay post_call(..). Because of this, the instrumented memory writes 
          * and reads in replay are somewhat fewer than in record. */
         vg_assert(tst->status == VgTs_WaitSys);
         vg_assert(tst->os_state.lwpid == VG_(gettid)());
         vg_assert(VG_(running_tid) == VG_INVALID_THREADID);

         tst->status = VgTs_Runnable;
         VG_(running_tid) = tid;
         VG_(unknown_SP_update)(VG_(get_SP(tid)), VG_(get_SP(tid)));

         if (tst->sched_jmpbuf_valid) {
          /* Can't continue; must longjmp back to the scheduler and thus
              enter the sighandler immediately. */
            __builtin_longjmp(tst->sched_jmpbuf, True);
         }
         VG_(core_panic)("sigvgkill_handler couldn't return to the scheduler\n");
      }
   }

   /* 
    * Warning: don't move up this line.
    *
    * If there is some thread exiting, wait for its completion.
    */
   wait_thread_exits();
}

static void
wait_thread_exits(void)
{
#if defined(VGP_x86_linux) || defined(VGP_amd64_linux)
   int num_sleeps;
   volatile int* ptr;
   struct vki_timespec* t;

   if(exiting_thread == VG_INVALID_THREADID || threads_rr[exiting_thread].clear_child_tid == NULL
                                            /* Main thread may exit when there is still some thread existed. */
                                            || exiting_thread == 1) {
      return;
   }

   vg_assert(exiting_thread < VG_N_THREADS);

   //VG_(printf)("ptr=%X tid/exiting_tid=%d/%d next_tid=%d who=%s\n", ptr, tid, exiting_thread, nextSchedule.tid, who);
   //VG_(do_syscall1)(__NR_fsync, 1);

   num_sleeps = 0;
   ptr = (volatile int*)threads_rr[exiting_thread].clear_child_tid;
   t = alloca(sizeof(struct vki_timespec));
   t->tv_sec = 0;
   t->tv_nsec = 1000000; // 1 milli-second

   /* 
    * if(*ptr == 0) then the variable has been zero-ed by OS kernel. (This is a part of Linux ABI)
    *     -- Spin on the monitored clear_child_tid, until it becomes zero.  Normal case.
    * if(*ptr == -1) then pthread lib has marked the exiting_thread terminated or joined. (A horribal hack of pthread lib)
    */
   /* 
    * XXX: SIGSEGV is caught for the address ptr=0x7219BD8.... tid=1, exiting_thread=6 
    * I guess the scenario is that: exiting_thread was cleaned up by kernel so quickly that main thread's pthread_join didn't
    * even wait and then no syscall requested in pthread_join. When main thread reached this point, it is requesting a much 
    * later syscall.... but Valgrind record/replay is not aware of this and is using old address, so SIGSEGV
    */
   while((*ptr) != 0 && (*ptr) != -1) {
      //VG_(printf)("%d sleep 1 ms to wait for exiting of thread %d. addr/val=%X/%X\n", tid, exiting_thread, ptr, (*ptr));
      VG_(do_syscall2)(__NR_nanosleep, t, NULL);
      num_sleeps++;
      if(num_sleeps > 10) {
         VG_(printf)("\nRECORD/REPLAY warning: thread %d have been waited long enough time to exit."
                     "But it seems still not completed.\n", exiting_thread);  
         /* Let's take a chance */
         break;
      }
   } 

   //threads_rr element may be reused. let's clear the record
   threads_rr[exiting_thread].clear_child_tid = NULL;
#endif
   exiting_thread = VG_INVALID_THREADID;
}

/*
 *----------------------------------------------------------------------------
 *
 * VG_(RR_Thread_Release) --
 *
 *       Record/replay VG_(release_BigLock), which happens when current thread
 *       is about to switch out.
 *        
 *       When in record, it syncs all the log writing activity; when in replay,
 *       it reads the log and figures out which thread is to run next.
 * Results:
 *       None 
 *
 * Side effects:
 *       When in replay nextSchedule is filled, which means which thread to run 
 *       next is determined.
 *
 *----------------------------------------------------------------------------
 */
void 
VG_(RR_Thread_Release) (ThreadId tid, Char* who)
{
   LogEntry* le;

   /* VG_(running_tid) is VG_INVALID_THREADID now */
   //vg_assert(VG_(running_tid) == tid);
   le = alloca(sizeof(LogEntry));
   le->tid = tid;
   le->type = RELEASE_BIGLOCK;
   VG_(strncpy)(le->u.release_biglock.who, who, 16);

   PROCESS_LOGENTRY;

   if(VG_(clo_record_replay) == RECORDONLY){ /* in record */
      /* Make sure the record log is written out */
      ML_(rrsync)();
   }
   else if(VG_(clo_record_replay) == REPLAYONLY){ /* in replay */
      le->type = ACQUIRE_BIGLOCK;
      le->tid = VG_(running_tid);
      ML_(readFromLog)(le);
      nextSchedule.type = le->u.acquire_biglock.type;
      nextSchedule.tid = le->u.acquire_biglock.tid;
   }
}

/*
 *----------------------------------------------------------------------------
 *
 * VG_(RR_Syscall_VexGuestArchState) --
 *
 *       Save guest register states before doing a syscall in record; 
 *       Compare runtime states with logged ones to detect divergence in replay.
 *
 * Results:
 *       None 
 *
 * Side effects:
 *       If a mismatch found, num_guest_state_mismatch is incremented.
 *
 *----------------------------------------------------------------------------
 */
void 
VG_(RR_Syscall_VexGuestArchState) (ThreadId tid, UInt sysno, void* a)
{
   LogEntry* le;

   vg_assert(tid == VG_(running_tid));
   le = alloca(sizeof(LogEntry));
   le->type = SYSCALL_ARGS;
   le->tid = VG_(running_tid);
   le->u.syscall_args.sysno = sysno;
   le->u.syscall_args.tid = tid;
   
   PROCESS_LOGENTRY;

   RR_VexGuestArchState((VexGuestArchState*)a);
}

/*
 *----------------------------------------------------------------------------
 *
 * VG_(RR_Syscall_Ret) --
 *
 *       Record/replay system return value for a syscall.
 *       This is only for HandInToKernel syscalls, and the pre-succeeded ones
 *       are excluded. 
 *
 * Results:
 *       None 
 *
 * Side effects:
 *       None
 *
 *----------------------------------------------------------------------------
 */
void
VG_(RR_Syscall_Ret) (ULong* ret)
{
   LogEntry* le;

   vg_assert(ret != 0);
   le = alloca(sizeof(LogEntry));
   le->type = SYSCALL_RET;
   le->tid = VG_(running_tid);

   if(VG_(clo_record_replay) == RECORDONLY) {
#if defined(VGA_x86) || defined(VGA_amd64)
      le->u.syscall_ret = (*ret);
#else
#error Unsupported architecture
#endif
   }

   PROCESS_LOGENTRY;

   if(VG_(clo_record_replay) == REPLAYONLY) {
      *ret = le->u.syscall_ret;
   }
}

/*
 *----------------------------------------------------------------------------
 *
 * VG_(RR_Syscall_Mem) --
 *
 *       Record/replay memory side-effects of a syscall. The side-effects are 
 *       logged in record, and are feeded back in replay.
 *
 *       This is only for HandInToKernel syscalls, and the pre-succeeded ones
 *       are excluded. 
 *
 * Results:
 *       None 
 *
 * Side effects:
 *       None
 *
 *----------------------------------------------------------------------------
 */
/* void VG_(RR_Syscall_Mem) (Int numAddrs, void* addr1, Int len1, void* addr2, Int len2, ...) */
void
VG_(RR_Syscall_Mem) (Int numAddrs, ...)
{
   Int i;
   va_list vargs;
   void* addr;
   UInt len;
   LogEntry* le;

   vg_assert(numAddrs >= 0);
   le = alloca(sizeof(LogEntry));
   le->type = DATA2;
   le->tid = VG_(running_tid);

   va_start(vargs, numAddrs);
   for(i = 0; i < numAddrs; i++){ 
      addr = va_arg(vargs, void*);
      len = va_arg(vargs, Int); 
      vg_assert(addr != NULL);
      vg_assert(len >= 0);
      le->u.data.len = len;
      le->u.data.addr = addr;
      PROCESS_LOGENTRY;
   }
   va_end(vargs);
}

/*
 *----------------------------------------------------------------------------
 *
 * VG_(RR_Syscall_DispatchCtr) --
 *
 *       Record/replay-check dispatch counter around a syscall.
 *
 * Results:
 *       None 
 *
 * Side effects:
 *       Once a dispatch counter mismatch found, assert(0).
 *
 *----------------------------------------------------------------------------
 */
void
VG_(RR_Syscall_DispatchCtr)(UInt ctr, Bool isBefore)
{
   LogEntry* le;

   le = alloca(sizeof(LogEntry));
   le->tid = VG_(running_tid);
   le->type = SYSCALL_DISPATCH_CTR;
   le->u.syscall_dispatch_ctr.isBefore = isBefore;
   le->u.syscall_dispatch_ctr.counter = ctr;

   if(VG_(clo_trace_sched) && VG_(clo_trace_syscalls) && VG_(clo_verbosity) > 2) {
      VG_(printf)("dispatch_ctr_%s_syscall:ctr=%d\n", isBefore?"before":"after", ctr);
   }

   PROCESS_LOGENTRY; 
}

/*
 *----------------------------------------------------------------------------
 *
 * VG_(RR_InitState_MemLayout) --
 *
 *       Record/replay inital memory layout for a client.
 *
 * Results:
 *       None 
 *
 * Side effects:
 *       None
 *
 *----------------------------------------------------------------------------
 */
void
VG_(RR_InitState_MemLayout)(Addr* aspacem_vstart, Addr* clstk_top)
{
/* 
 * The initial memory layout depends on sp_at_startup. Unfortunately, its value
 * is set before Valgrind starts or before m_main/_start(..) has chance to run, 
 * and it varies in every execution. As a makeshift, we could control aspacem_vstart
 * (refert to m_aspacem/aspacem-linux.c) and client stack top to get nearly a 
 * deterministic memory layout of the whole Valgrind and get a totally same layout 
 * from the perspective of client program. 
 */
   LogEntry* le;

   vg_assert(aspacem_vstart != NULL);
   vg_assert(clstk_top != NULL);
   le = alloca(sizeof(LogEntry));
   le->type = INITIMG_MEMLAYOUT;
   le->tid = VG_(running_tid);
   le->u.initimg_memlayout.vstart = (*aspacem_vstart);
   le->u.initimg_memlayout.clstk_top = (*clstk_top);

   PROCESS_LOGENTRY;

   vg_assert(VG_IS_PAGE_ALIGNED(le->u.initimg_memlayout.clstk_top + 1));
   vg_assert(VG_IS_PAGE_ALIGNED(le->u.initimg_memlayout.vstart));

   if(VG_(clo_record_replay) == REPLAYONLY){
      *clstk_top = le->u.initimg_memlayout.clstk_top; 
      *aspacem_vstart = le->u.initimg_memlayout.vstart; 
   }
}

/*
 *----------------------------------------------------------------------------
 *
 * VG_(RR_InitState_ClientStack) --
 *
 *       Record/replay inital client stack image.
 *       
 *       We discsard the runtime stack image computed and use the recorded one
 *       in replay execution, so as to avoid indeterminsm due to initial states
 *       like environment variables.
 *
 * Results:
 *       None 
 *
 * Side effects:
 *       None
 *
 *----------------------------------------------------------------------------
 */
void
VG_(RR_InitState_ClientStack)(Addr clstk_top, void* arg_iifii)
{
   LogEntry* le;
   UInt stksz;
   IIFinaliseImageInfo* iifii = (IIFinaliseImageInfo*) arg_iifii;

   vg_assert(iifii != NULL);
   vg_assert(clstk_top != 0);
   le = alloca(sizeof(LogEntry));
   le->type = INITIMG_CLSTK;
   le->tid = VG_(running_tid);

   if(VG_(clo_record_replay) == RECORDONLY) {
      le->u.initimg_clstk.stksz = clstk_top - iifii->initial_client_SP;
   }

   PROCESS_LOGENTRY;

   if(VG_(clo_record_replay) == REPLAYONLY){
      /* clear old stack and fill it */
      iifii->initial_client_SP = clstk_top - le->u.initimg_clstk.stksz;
      /* redo side effects by m_initimg/initimg-linux.c/setup_client_stack(...) */
      iifii->client_auxv = (void*)le->u.initimg_clstk.auxv_addr;
      VG_(clstk_end) = clstk_top;
      VG_(clstk_base) = VG_PGROUNDDN(iifii->initial_client_SP);
   }

   /* another log entry */
   stksz = le->u.initimg_clstk.stksz;
   le->type = DATA2;
   le->u.data.len = stksz;
   le->u.data.addr = (void*)iifii->initial_client_SP;

   PROCESS_LOGENTRY;
}

/*
 *----------------------------------------------------------------------------
 *
 * VG_(RR_InitState_VexGuestArchState) --
 *
 *       Record/replay-check initial guest register states.
 *
 * Results:
 *       None 
 *
 * Side effects:
 *       None
 *
 *----------------------------------------------------------------------------
 */
void
VG_(RR_InitState_VexGuestArchState)(void* arg)
{
   RR_VexGuestArchState((VexGuestArchState*)arg);
}

unsigned long 
VG_(recorded_id_to_kernel_id)(unsigned long recorded_id)
{
   int i;

   for(i = 0; i < VG_N_THREADS; i++){
      if(threads_rr[i].tid_recorded == recorded_id)
         return threads_rr[i].tid_kernel;
   }
   
   return VG_INVALID_THREADID;
}

unsigned long 
VG_(kernel_id_to_recorded_id)(unsigned long kernel_id)
{
   int i;

   for(i = 0; i < VG_N_THREADS; i++){
      if(threads_rr[i].tid_kernel == kernel_id)
         return threads_rr[i].tid_recorded;
   }

   return VG_INVALID_THREADID;
}

#if defined(VGP_x86_linux) || defined(VGP_amd64_linux)
void
VG_(monitor_clear_child_tid)(ThreadId vg_tid, void* addr)
{
   vg_assert(vg_tid != VG_INVALID_THREADID && vg_tid < VG_N_THREADS);

   threads_rr[vg_tid].clear_child_tid = addr;
}
#endif

static void 
setupRRArgs(Int argc, HChar** argv)
{
   UInt i;
   HChar* str;

   vg_assert( argv );
   /* parse the options we have (only the record and replay options we care about now) */
   for (i = 1; i < argc; i++) {

      str = argv[i];
      vg_assert(str);

      if (argv[i][0] != '-')
         break;

      VG_NUM_CLO(str, "--record-replay", VG_(clo_record_replay))
      else VG_STR_CLO(str, "--log-file-rr", VG_(clo_log_name_rr))
      else continue;
   }

   if(VG_(clo_record_replay) == UNINITIALIZED){
      VG_(message)(Vg_UserMsg, "Please specify --record-replay=1|2. If you don't need record/replay, recompile Valgrind with MACRO RECORD_REPLAY undefined");
      VG_(exit)(1);
   }

   /* sanity check */
   if(VG_(clo_record_replay) > REPLAYONLY || VG_(clo_record_replay) < RECORDONLY) {
      VG_(message)(Vg_UserMsg, "--record-replay argument can only be 1|2.");
      VG_(err_bad_option)("--record-replay=");
   }

   /* setup ML_(log_fd_rr) */
   {
      SysRes sres;
      Int tmp_fd;

      vg_assert(VG_(clo_log_name_rr) != NULL);

      if(VG_(clo_record_replay) == RECORDONLY) {
         /* overwrite the existed file without asking questions */
         sres = VG_(open)(VG_(clo_log_name_rr),
                       VKI_O_CREAT|VKI_O_WRONLY|VKI_O_TRUNC,
                       VKI_S_IRUSR|VKI_S_IWUSR);
      } else if(VG_(clo_record_replay) == REPLAYONLY) { 
         sres = VG_(open)(VG_(clo_log_name_rr),
                       VKI_O_RDONLY,
                       VKI_S_IRUSR|VKI_S_IWUSR);
      }

      if (!sres.isError) {
         tmp_fd = sres.res;
      } else {
         VG_(message)(Vg_UserMsg,
                      "Can't open or create log file for record and replay '%s' (%s)",
                      VG_(clo_log_name_rr), VG_(strerror)(sres.err));
         VG_(err_bad_option)(
            "--log-file-rr=<file> (didn't work out for some reason.)");
         /*NOTREACHED*/
      }
      if (tmp_fd >= 0) {
         /* Move tmp_fd into the safe range, so it doesn't conflict with any app fds. */
         if(tmp_fd < 0) {
            vg_assert2(tmp_fd >= 0, "valgrind: failed to move logfile fd into safe range, exiting");
            /*NOTREACHED*/
         }
         else {
            ML_(log_fd_rr) = tmp_fd;
            VG_(fcntl)(ML_(log_fd_rr), VKI_F_SETFD, VKI_FD_CLOEXEC);
         }
      }
   }
}

static void 
RR_VexGuestArchState(VexGuestArchState* arg)
{
   LogEntry* le;
   VexGuestArchState* recorded_vex = NULL;
   VexGuestArchState* runtime_vex = arg;

   vg_assert(arg != NULL);
   le = alloca(sizeof(LogEntry));
   le->type = DATA1;
   le->tid = VG_(running_tid);
   le->u.data.len = sizeof(VexGuestArchState);

   if(VG_(clo_record_replay) == RECORDONLY) {
      le->u.data.addr = runtime_vex;
   }
   else if(VG_(clo_record_replay) == REPLAYONLY){
      recorded_vex = alloca(sizeof(VexGuestArchState));
      VG_(memset)(recorded_vex, 0, sizeof(VexGuestArchState));
      le->u.data.addr = recorded_vex;
   }

   PROCESS_LOGENTRY;

   if(VG_(clo_record_replay) == REPLAYONLY) {
      check_VexGuestArchState(runtime_vex, recorded_vex);
   }
}

/* replay-only code */
static void 
check_VexGuestArchState(VexGuestArchState* runtime_vex, VexGuestArchState* recorded_vex)
{
   Bool arg_mismatch = False;
   vg_assert(VG_(clo_record_replay) == REPLAYONLY);
#  if defined(VGP_x86_linux)
#define VEXGUESTARCHSTATE_CHECK(REG) \
      if(runtime_vex->guest_##REG != recorded_vex->guest_##REG){ \
       if(num_guest_state_mismatch < 10) { \
         VG_(printf)("Guest "#REG" not expected. runtime/recorded=0x%X/0x%X\n", \
                 runtime_vex->guest_##REG, recorded_vex->guest_##REG); \
         VG_(show_sched_status)(); \
       } \
         arg_mismatch = True; \
      }
      VEXGUESTARCHSTATE_CHECK(EAX);
      VEXGUESTARCHSTATE_CHECK(ECX);
      VEXGUESTARCHSTATE_CHECK(EDX);
      VEXGUESTARCHSTATE_CHECK(EBX);
      VEXGUESTARCHSTATE_CHECK(ESP);
      VEXGUESTARCHSTATE_CHECK(EBP);
      VEXGUESTARCHSTATE_CHECK(ESI);
      VEXGUESTARCHSTATE_CHECK(EDI);
      VEXGUESTARCHSTATE_CHECK(EIP);
      VEXGUESTARCHSTATE_CHECK(CS);
      VEXGUESTARCHSTATE_CHECK(DS);
      VEXGUESTARCHSTATE_CHECK(ES);
      VEXGUESTARCHSTATE_CHECK(FS);
      VEXGUESTARCHSTATE_CHECK(GS);
      VEXGUESTARCHSTATE_CHECK(SS);
      /*
       * guest_GDT and guest_LDT point to memory blocks that are used for LDT/GDT simulation.
       * The memory blocks are allocated in Valgrind core, so it is reasonable that the values 
       * of guest_LDT and guest_GDT vary in different executions. No need to check them.
       */
      //VEXGUESTARCHSTATE_CHECK(LDT);
      //VEXGUESTARCHSTATE_CHECK(GDT);
#undef VEXGUESTARCHSTATE_CHECK
#  endif

   if(arg_mismatch) {
      num_guest_state_mismatch++;
   }
}

