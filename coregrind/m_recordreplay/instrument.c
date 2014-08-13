
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
 * instrument.c --
 *
 *      Instrumetation pass to deal with non-deterministic priviledged instructions.
 */
#include <alloca.h>
#include "pub_core_basics.h"
#include "pub_core_libcbase.h"
#include "pub_core_libcprint.h"
#include "pub_core_libcfile.h"
#include "pub_core_libcassert.h"
#include "pub_core_vki.h"
#include "pub_core_threadstate.h"

#include "pub_core_recordreplay.h"  /* for VG_(running_tid) */

#include "priv_recordreplay.h"

#include "libvex.h"

/* horrible hack.... each dirtyhelper is replaced by the one RR_ prefixed */

/* All non-deterministic priviledged instructions are processed here.
   A seperate instrumentation pass was added into "Phase 3. Instrumentation: flat IR --> flat IR".
   This pass must happen before vta->instrument1 and vta->instrument2, so that the processing
   of those sensitive instructions is transparent to tools.
*/

#if defined(VGA_x86)
extern ULong x86g_dirtyhelper_RDTSC ( void );
static ULong RR_x86g_dirtyhelper_RDTSC( void );

extern void  x86g_dirtyhelper_CPUID_sse0 ( VexGuestX86State* );
extern void  x86g_dirtyhelper_CPUID_sse1 ( VexGuestX86State* );
extern void  x86g_dirtyhelper_CPUID_sse2 ( VexGuestX86State* );
extern void  x86g_dirtyhelper_FXSAVE ( VexGuestX86State*, HWord );
extern UInt x86g_dirtyhelper_IN  ( UInt portno, UInt sz/*1,2 or 4*/ );
extern void x86g_dirtyhelper_OUT ( UInt portno, UInt data,  UInt sz/*1,2 or 4*/ );
#elif defined(VGA_amd64)
extern ULong amd64g_dirtyhelper_RDTSC ( void );
extern void  amd64g_dirtyhelper_CPUID ( VexGuestAMD64State* st );
extern void  amd64g_dirtyhelper_FXSAVE ( VexGuestAMD64State*, HWord );
extern ULong amd64g_dirtyhelper_IN  ( ULong portno, ULong sz/*1,2 or 4*/ );
extern void  amd64g_dirtyhelper_OUT ( ULong portno, ULong data, ULong sz/*1,2 or 4*/ );
#endif

IRSB* VG_(instrumentRecordReplay) ( void* closureV,
                      IRSB* sbIn,
                      VexGuestLayout* layout,
                      VexGuestExtents* vge,
                      IRType gWordTy, IRType hWordTy )
{
   IRDirty*   di;
   Int        i;
   IRSB*      sbOut;
   IRTypeEnv* tyenv = sbIn->tyenv;

   /* Valgrind doesn't support this currently */
   vg_assert2(gWordTy == hWordTy, "host/guest word size mismatch\n");

   /* Set up the result SB */
   sbOut = deepCopyIRSBExceptStmts(sbIn);

   // Copy verbatim any IR preamble preceding the first IMark 
   i = 0;
   while (i < sbIn->stmts_used && sbIn->stmts[i]->tag != Ist_IMark) {
      addStmtToIRSB( sbOut, sbIn->stmts[i] );
      i++;
   }

   for (/*use current i*/; i < sbIn->stmts_used; i++) {
      IRStmt* st = sbIn->stmts[i];
      if (!st || st->tag == Ist_NoOp) continue;

      switch (st->tag) {
         case Ist_NoOp:
         case Ist_AbiHint:
         case Ist_Put:
         case Ist_PutI:
         case Ist_MBE:
         case Ist_IMark:
         case Ist_WrTmp:
         case Ist_Store:
            addStmtToIRSB( sbOut, st );
            break;

         case Ist_Dirty: {
            IRDirty* d = st->Ist.Dirty.details;
#if defined(VGA_x86)
            if(d->cee->addr == (void*)x86g_dirtyhelper_RDTSC)
               d->cee->addr = (void*)RR_x86g_dirtyhelper_RDTSC;
            /* if found FXSAVE, CPUID, IN, OUT, just assert(0) to remind us of
               which sensitive instruction is found and should be dealed with */
            if(d->cee->addr == (void*)x86g_dirtyhelper_FXSAVE);
               /* TODO: add RR version of x86g_dirtyhelper_FXSAVE */ 
               // vg_assert(0);
            if(d->cee->addr == (void*)x86g_dirtyhelper_CPUID_sse0);
               // vg_assert(0);
            if(d->cee->addr == (void*)x86g_dirtyhelper_CPUID_sse1);
               // vg_assert(0);
            if(d->cee->addr == (void*)x86g_dirtyhelper_CPUID_sse2);
               // vg_assert(0);
            if(d->cee->addr == (void*)x86g_dirtyhelper_IN);
               // vg_assert(0);
            if(d->cee->addr == (void*)x86g_dirtyhelper_OUT);
               // vg_assert(0);
#elif defined(VGA_amd64)
            if(d->cee->addr == (void*)amd64g_dirtyhelper_RDTSC)
               d->cee->addr = (void*)RR_amd64g_dirtyhelper_RDTSC;
            /* if found FXSAVE, CPUID, IN, OUT, just assert(0) to remind us of
               which sensitive instruction is found and should be dealed with */
            if(d->cee->addr == (void*)amd64g_dirtyhelper_FXSAVE);
               // vg_assert(0);
            if(d->cee->addr == (void*)amd64g_dirtyhelper_CPUID);
               // vg_assert(0);
            if(d->cee->addr == (void*)amd64g_dirtyhelper_IN);
               // vg_assert(0);
            if(d->cee->addr == (void*)amd64g_dirtyhelper_OUT);
               // vg_assert(0);
#endif
            addStmtToIRSB( sbOut, st );
            break;
         }

         case Ist_Exit:
            addStmtToIRSB( sbOut, st );      // Original statement
            break;

         default:
            vg_assert(0);
      }
   }

   return sbOut;
}

/* dirtyhelper substitutes */
#if defined(VGA_x86) || defined(VGA_amd64)
#if defined(VGA_x86)
static ULong RR_x86g_dirtyhelper_RDTSC( void )
#elif defined(VGA_amd64)
/* XXX:__x86_64__ version not tested */
ULong RR_amd64g_dirtyhelper_RDTSC ( void )
#endif
{
//#  if defined(__i386__) || defined(__x86_64__)
   UInt eax, edx;
   LogEntry* le;

   le = alloca(sizeof(LogEntry));
   le->type = RDTSC;
   le->tid = VG_(running_tid); 
   
   if(VG_(clo_record_replay) == RECORDONLY){
   /* Do we have to use volatile here? Intel manual doesn't say so. 
      Just to be consistent with VEX/priv/guest-x86/ghelpers.c/x86g_dirtyhelper_RDTSC(...) */
      __asm__ __volatile__("rdtsc" : "=a" (eax), "=d" (edx));
      le->u.rdtsc.eax = eax;
      le->u.rdtsc.edx = edx; 
   }

   PROCESS_LOGENTRY;

   if(VG_(clo_record_replay) == REPLAYONLY){
      eax = le->u.rdtsc.eax;
      edx = le->u.rdtsc.edx;
   }

   if(VG_(clo_record_replay) == NORECORDREPLAY) 
   /* Do we have to use volatile here? Intel manual doesn't say so. 
      Just to be consistent with VEX/priv/guest-x86/ghelpers.c/x86g_dirtyhelper_RDTSC(...) */
      __asm__ __volatile__("rdtsc" : "=a" (eax), "=d" (edx));

   return (((ULong)edx) << 32) | ((ULong)eax);
//#  else
//   return 1ULL;
//#  endif
}
#endif

