
/*--------------------------------------------------------------------*/
/*--- rrcheck: check replay correctness             rrcheck_main.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of rrcheck, a Valgrind tool that detects replay 
   divergence

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
*/

#include "pub_tool_basics.h"
#include "pub_tool_tooliface.h"

#include "pub_tool_vki.h"
#include "pub_tool_debuginfo.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_libcfile.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_libcproc.h"
#include "pub_tool_machine.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_options.h"
#include "pub_tool_oset.h"
#include "pub_tool_xarray.h"
#include "pub_tool_clientstate.h"
#include "pub_tool_machine.h"      // VG_(fnptr_to_fnentry)
#include "libvex_guest_offsets.h"

/* 
 * TODO: Record/replay memory side-effects of load, and register GET. Then we
 *       can detect divergence a little earlier.
 */

/* XXX: Copied directly from coregrind/pub_core_recordreplay.h */
typedef enum RRState{
   UNINITIALIZED = -1,
   NORECORDREPLAY,
   RECORDONLY,
   REPLAYONLY
}RRState;

/* ------------- command line options --------------- */
static Char* rrcheck_out_file = "execution_trace.log";
static Bool clo_trace_mem       = False;
static Bool clo_trace_reg       = False;
static Bool clo_trace_sbs       = True;

extern RRState VG_(clo_record_replay);  /* 1, record; 2, replay */

static Bool rrcheck_process_cmd_line_option(Char* arg)
{
   if VG_STR_CLO(arg, "--log-name", rrcheck_out_file) {}
   else if VG_BOOL_CLO(arg, "--trace-mem",         clo_trace_mem) {}
   else if VG_BOOL_CLO(arg, "--trace-reg",         clo_trace_reg) {}
   else if VG_BOOL_CLO(arg, "--trace-superblocks", clo_trace_sbs) {}
   else
      return False;

   tl_assert(rrcheck_out_file);
   tl_assert(rrcheck_out_file[0]);
   return True;
}

static void rrcheck_print_usage(void)
{
   VG_(printf)(
"    --trace-mem=no|yes        trace all loads and stores [no]\n"
"    --trace-reg=no|yes        trace all register puts/gets [no]\n"
"    --trace-superblocks=yes|no  trace all superblock entries [yes]\n"
"    --log-name=<log file name>  the name of execution trace log [execution-trace.log]\n"
   );
}

static void rrcheck_print_debug_usage(void)
{
   VG_(printf)(
"    (none)\n"
   );
}
/* ---------- end of command line options ----------- */


SysRes  sres;
Int fd = -1;

static void rrcheck_post_clo_init(void)
{
   tl_assert2(VG_(clo_record_replay) == RECORDONLY || VG_(clo_record_replay) == REPLAYONLY,
               "rrcheck can only be enabled for Valgrind during record-only mode or during replay-only mode\n"); 
   tl_assert2(clo_trace_sbs == True || clo_trace_mem == True || clo_trace_reg == True,
         "Either --trace-superblocks=yes or --trace-mem=yes or --trace-reg=yes should be set.");

   if(VG_(clo_record_replay) == RECORDONLY)
      sres = VG_(open)(rrcheck_out_file, VKI_O_CREAT|VKI_O_TRUNC|VKI_O_WRONLY,
                                            VKI_S_IRUSR|VKI_S_IWUSR);
   else /* replay */
      sres = VG_(open)(rrcheck_out_file, VKI_O_RDONLY, VKI_S_IRUSR|VKI_S_IWUSR);

   if (sr_isError(sres)) {
      // If the file can't be opened for whatever reason, give up now.
      VG_(message)(Vg_UserMsg,
         "error: can't open output file '%s'",
         rrcheck_out_file );
      VG_(exit)(1);
   } else {
      fd = sr_Res(sres);
   }   
}

#define FILE_LEN     VKI_PATH_MAX
#define FN_LEN       256
static void get_debug_info(Addr instr_addr, Char file[FILE_LEN],
                           Char fn[FN_LEN], Int* line)
{
   Char dir[FILE_LEN];
   Bool found_dirname;
   Bool found_file_line = VG_(get_filename_linenum)(
                             instr_addr, 
                             file, FILE_LEN,
                             dir,  FILE_LEN, &found_dirname,
                             line
                          );
   Bool found_fn        = VG_(get_fnname)(instr_addr, fn, FN_LEN);

   if (!found_file_line) {
      VG_(strcpy)(file, "???");
      *line = 0;
   }
   if (!found_fn) {
      VG_(strcpy)(fn,  "???");
   }


   if (found_dirname) {
      // +1 for the '/'.
      tl_assert(VG_(strlen)(dir) + VG_(strlen)(file) + 1 < FILE_LEN);
      VG_(strcat)(dir, "/");     // Append '/'
      VG_(strcat)(dir, file);    // Append file to dir
      VG_(strcpy)(file, dir);    // Move dir+file to file
   }
}

/* for tracing memory: instruction read, data read, data write */
#define MAX_DSIZE    512

/* used for memory events counting */
static ULong count_ir = 0;
static ULong count_dr = 0;
static ULong count_dw = 0;
static ULong count_rp = 0;
static ULong count_rg = 0;

static ULong n_instr_mismatch = 0;;
static ULong n_load_mismatch = 0;;
static ULong n_store_mismatch = 0;;
static ULong n_put_mismatch = 0;;
static ULong n_get_mismatch = 0;;

/* Last instruction address */
static Addr last_instr;
static SizeT last_sz;

/* check rt_line with next line in the execution trace log. If equal return True, else return False */
/* TODO: refine me */
static Bool checkWithNextLogLine(Char* rt_line, Bool print)
{
   Char buf[512]; // a line in log
   UInt len;
   UInt rt_len;
   Bool ret;
   
   rt_len = VG_(strlen)(rt_line);
   tl_assert(rt_len < 511 ); 
   len = VG_(read)(fd, buf, rt_len);
   buf[len] = '\0';
   if(len != VG_(strlen)(rt_line))
      ret = False;
   else if(VG_(strcmp)(buf, rt_line) != 0)
      ret = False;
   else ret = True;

   if(ret == False && print == True){
      Int line;
      Char    file[FILE_LEN], fn[FN_LEN];
      VG_(printf)("\n******** Log entry mismatch ********\nRuntime:%s    Log:%s\n", rt_line, buf);
      VG_(printf)("Instruction address: 0x%08lX size=%lu\n", last_instr, last_sz);
      //get debug info for runtime instruction address
      get_debug_info(last_instr, file, fn, &line);
      VG_(printf)("file=%s\tfn=%s\tline=%d\n", file, fn, line);
      //get debug info for logged instruction address
      {
         Addr tmp_addr = 0;
         if(buf[0] == 'I')
            tmp_addr = VG_(strtoll16)(&buf[3], NULL);
         else if(buf[0] == 'S' && buf[1] == 'B')
            tmp_addr = VG_(strtoll16)(&buf[4], NULL);
         if(tmp_addr > 0){
            VG_(printf)("Logged instruction address: 0x%08lX\n", tmp_addr);
            get_debug_info(tmp_addr, file, fn, &line);
            VG_(printf)("file=%s\tfn=%s\tline=%d\n", file, fn, line);
         }
      }
      VG_(printf)("\n");
   }
   return ret;
}

static VG_REGPARM(2) void trace_instr(Addr addr, SizeT size)
{
   Char    buf[512];

   tl_assert( (VG_MIN_INSTR_SZB <= size && size <= VG_MAX_INSTR_SZB)
            || VG_CLREQ_SZB == size );

   /* remember the address and size */
   last_instr = addr;
   last_sz = size;
   count_ir++;
   VG_(sprintf)(buf, "I  %08lX,%lu\n", addr, size);

   if(VG_(clo_record_replay) == RECORDONLY)
      VG_(write)(fd, (void*)buf, VG_(strlen)(buf));
   else /* replay */
      if(!checkWithNextLogLine(buf, n_instr_mismatch==0))
         n_instr_mismatch++;
}

static VG_REGPARM(3) void trace_mem_load(Addr addr, HWord val, SizeT size)
{
   Char    buf[512];

   tl_assert(size >= 1 && size <= MAX_DSIZE);

   count_dr++;
   VG_(sprintf)(buf, "L  %08lX,%08lX,%lu\n", addr, val, size);

   if(VG_(clo_record_replay) == RECORDONLY) /* record*/
      VG_(write)(fd, (void*)buf, VG_(strlen)(buf));
   else /* replay */
      if(!checkWithNextLogLine(buf, n_load_mismatch==0))
         n_load_mismatch++;
}

static VG_REGPARM(3) void trace_mem_store(Addr addr, HWord val, SizeT size)
{
   Char    buf[512];

   tl_assert(size >= 1 && size <= MAX_DSIZE);

   count_dw++;
   VG_(sprintf)(buf, "S  %08lX,%08lX,%lu\n", addr, val, size);

   if(VG_(clo_record_replay) == RECORDONLY) /* record*/
      VG_(write)(fd, (void*)buf, VG_(strlen)(buf));
   else /* replay */
      if(!checkWithNextLogLine(buf, n_store_mismatch==0))
         n_store_mismatch++;
}

static VG_REGPARM(2) void trace_reg_put(Addr addr, HWord val)
{
   Char    buf[512];

   count_rp++;
   VG_(sprintf)(buf, "P  %08lX,%08lX\n", addr, val);

   if(VG_(clo_record_replay) == RECORDONLY) /* record*/
      VG_(write)(fd, (void*)buf, VG_(strlen)(buf));
   else /* replay */
      if(!checkWithNextLogLine(buf, n_put_mismatch==0))
         n_put_mismatch++;
}

static VG_REGPARM(2) void trace_reg_get(Addr addr, HWord val)
{
   Char    buf[512];

   count_rg++;
   VG_(sprintf)(buf, "G  %08lX,%08lX\n", addr, val);

   if(VG_(clo_record_replay) == RECORDONLY) /* record*/
      VG_(write)(fd, (void*)buf, VG_(strlen)(buf));
   else /* replay */
      if(!checkWithNextLogLine(buf, n_get_mismatch==0))
         n_get_mismatch++;
}

/* for tracing superblock entries */
/* for tracing superblock entries */
static ULong n_sb = 0;
static void trace_superblock(Addr addr)
{
   Char buf[512];
   static ULong n_sb_mismatch = 0;;

   /* Superblock entry is the first instruction of this block */
   last_instr = addr;
   last_sz = 0; /* have no idea what the size is at this time */

   n_sb++;
   VG_(sprintf)(buf, "SB  %08lX\n", addr);

   if(VG_(clo_record_replay) == RECORDONLY) /* record*/
      VG_(write)(fd, (void*)buf, VG_(strlen)(buf));
   else /* replay */
      if(!checkWithNextLogLine(buf, n_sb_mismatch==0))
         n_sb_mismatch++;
}

static
IRSB* rrcheck_instrument ( VgCallbackClosure* closure,
                      IRSB* sbIn,
                      VexGuestLayout* layout, 
                      VexGuestExtents* vge,
                      VexArchInfo* archinfo_host,
                      IRType gWordTy, IRType hWordTy )
{
   IRDirty*   di;
   Int        i;
   IRSB*      sbOut;
   IRTypeEnv* tyenv = sbIn->tyenv;
   IRType     type;
   Addr       mask;

#if (VG_WORDSIZE == 4)
   type = Ity_I32;
   mask = ~0x03;
#elif  (VG_WORDSIZE == 8)
   type = Ity_I64;
   mask = ~0x07;
#endif

   if (gWordTy != hWordTy) {
      /* We don't currently support this case. */
      VG_(tool_panic)("host/guest word size mismatch");
   }

   /* Set up the result SB */
   sbOut = deepCopyIRSBExceptStmts(sbIn);

   // Copy verbatim any IR preamble preceding the first IMark 
   i = 0;
   while (i < sbIn->stmts_used && sbIn->stmts[i]->tag != Ist_IMark) {
      addStmtToIRSB( sbOut, sbIn->stmts[i] );
      i++;
   }

   /* count this superblock */
   if (clo_trace_sbs) {
      /* Print this superblock's address. */
      di = unsafeIRDirty_0_N( 
              0, "trace_superblock", 
              VG_(fnptr_to_fnentry)( &trace_superblock ),
              mkIRExprVec_1( mkIRExpr_HWord( vge->base[0] ) ) 
           );
      addStmtToIRSB( sbOut, IRStmt_Dirty(di) );
   }   
   
   for (/*use current i*/; i < sbIn->stmts_used; i++) {
      IRStmt* st = sbIn->stmts[i];
      if (!st || st->tag == Ist_NoOp) continue;
      
      switch (st->tag) {
         case Ist_NoOp:
         case Ist_AbiHint:
         case Ist_PutI:
         case Ist_MBE:
            addStmtToIRSB( sbOut, st );
            break;

         case Ist_IMark:

            if(clo_trace_mem){
               di = unsafeIRDirty_0_N( 
                       2, "trace_instr", 
                       VG_(fnptr_to_fnentry)( &trace_instr ),
                       mkIRExprVec_2( mkIRExpr_HWord( (HWord)st->Ist.IMark.addr ),
                                      mkIRExpr_HWord( st->Ist.IMark.len) ) 
                    );
               addStmtToIRSB( sbOut, IRStmt_Dirty(di) );
            }
 
            addStmtToIRSB( sbOut, st );
            break;

         case Ist_WrTmp:
         {
            IRExpr* data = st->Ist.WrTmp.data;
            if(data->tag == Iex_Load && clo_trace_mem){
               // Add a call to trace_load()
                //  IRTemp t = newIRTemp(tyenv, data->Iex.Load.ty);
                //  addStmtToIRSB(sbOut, IRStmt_WrTmp(t, IRExpr_RdTmp(st->Ist.WrTmp.tmp)));
               di = unsafeIRDirty_0_N( 
                       3, "trace_mem_load", 
                       VG_(fnptr_to_fnentry)( &trace_mem_load ),
                       mkIRExprVec_3( mkIRExpr_HWord( (HWord)data->Iex.Load.addr ),
                                      mkIRExpr_HWord( -1 ),
                      //                IRExpr_RdTmp(st->Ist.WrTmp.tmp),
                      //                IRExpr_RdTmp(t),
                                      mkIRExpr_HWord( sizeofIRType(data->Iex.Load.ty) )
                                    ) 
                    );
               addStmtToIRSB( sbOut, IRStmt_Dirty(di) );
            }

            if(data->tag == Iex_Get && clo_trace_reg){
               IRExpr* data = st->Ist.WrTmp.data;
               di = unsafeIRDirty_0_N( 
                       2, "trace_reg_get", 
                       VG_(fnptr_to_fnentry)( &trace_reg_get ),
                       mkIRExprVec_2( mkIRExpr_HWord( (HWord)data->Iex.Get.offset ),
                                      mkIRExpr_HWord( -1 )
                                    ) 
                    );
                  
               addStmtToIRSB( sbOut, IRStmt_Dirty(di) );
            }
         }

            addStmtToIRSB( sbOut, st );
            break;

         case Ist_Store:
            if(clo_trace_mem){
               IRExpr* data  = st->Ist.Store.data;
               if(typeOfIRExpr(tyenv, data) == type){
                  di = unsafeIRDirty_0_N( 
                          3, "trace_mem_store", 
                          VG_(fnptr_to_fnentry)( &trace_mem_store ),
                          mkIRExprVec_3( mkIRExpr_HWord( (HWord)st->Ist.Store.addr ),
                                         data,
                                         mkIRExpr_HWord( sizeofIRType(typeOfIRExpr(tyenv, data)) )
                                       ) 
                       );
                  /* add in the original instruction second */
                  addStmtToIRSB( sbOut, IRStmt_Dirty(di) );
                  addStmtToIRSB( sbOut, st );
               }
               else{
                  di = unsafeIRDirty_0_N( 
                          3, "trace_mem_store", 
                          VG_(fnptr_to_fnentry)( &trace_mem_store ),
                          mkIRExprVec_3( mkIRExpr_HWord( (HWord)st->Ist.Store.addr ),
                                         mkIRExpr_HWord( -1 ),
                                         mkIRExpr_HWord( sizeofIRType(typeOfIRExpr(tyenv, data)) )
                                       ) 
                       );
                  /* add in the original instruction first */
                  addStmtToIRSB( sbOut, IRStmt_Dirty(di) );
                  addStmtToIRSB( sbOut, st );
               }
            }
            else
               addStmtToIRSB( sbOut, st );
            break;

         case Ist_Put:
            /* general register Put */
            if(clo_trace_reg){
               if(typeOfIRExpr(tyenv, st->Ist.Put.data) == type)
                  di = unsafeIRDirty_0_N( 2, "trace_reg_put", &trace_reg_put, 
                              mkIRExprVec_2(mkIRExpr_HWord(st->Ist.Put.offset),
                                    st->Ist.Put.data));
               else
                  di = unsafeIRDirty_0_N( 2, "trace_reg_put", &trace_reg_put,
                              mkIRExprVec_2(mkIRExpr_HWord(st->Ist.Put.offset),
                                    mkIRExpr_HWord(-1)));
               /* Add in the original instruction first. */
               addStmtToIRSB( sbOut, st );
               addStmtToIRSB( sbOut, IRStmt_Dirty(di) );
            }
            else
               addStmtToIRSB( sbOut, st );

            break;

         case Ist_Dirty: {
            if(clo_trace_mem){
               Int      dsize;
               IRDirty* d = st->Ist.Dirty.details;

               if (d->mFx != Ifx_None) {
                  // This dirty helper accesses memory.  Collect the details.
                  // This case is rare, so we don't bother to read runtime memory values.
                  tl_assert(d->mAddr != NULL);
                  tl_assert(d->mSize != 0);
                  dsize = d->mSize;
                  if (d->mFx == Ifx_Read || d->mFx == Ifx_Modify){
                     di = unsafeIRDirty_0_N( 
                             3, "trace_mem_load", 
                             VG_(fnptr_to_fnentry)( &trace_mem_load ),
                             mkIRExprVec_3( mkIRExpr_HWord( (HWord)d->mAddr ),
                                            mkIRExpr_HWord( (HWord)-1 ),
                                            mkIRExpr_HWord( dsize )
                                          ) 
                          );
                     addStmtToIRSB( sbOut, IRStmt_Dirty(di) );
                  }
                  if (d->mFx == Ifx_Write || d->mFx == Ifx_Modify){
                     di = unsafeIRDirty_0_N( 
                             3, "trace_mem_store", 
                             VG_(fnptr_to_fnentry)( &trace_mem_store ),
                             mkIRExprVec_3( mkIRExpr_HWord( (HWord)d->mAddr ),
                                            mkIRExpr_HWord( (HWord)-1 ),
                                            mkIRExpr_HWord( dsize )
                                          ) 
                          );
                     addStmtToIRSB( sbOut, IRStmt_Dirty(di) );
                  }
               } else {
                  tl_assert(d->mAddr == NULL);
                  tl_assert(d->mSize == 0);
               }
            }
            
            addStmtToIRSB( sbOut, st );
            break;
         }

         case Ist_Exit:         
            addStmtToIRSB( sbOut, st );      // Original statement
            break;

         default:
            tl_assert(0);
      }
   }
   
   return sbOut;
}

static void rrcheck_fini(Int exitcode)
{
   VG_(message)(Vg_UserMsg, "**** RECORD/REPLAY correctness check: instructions summary ****");
   if(clo_trace_mem){
      VG_(message)(Vg_UserMsg, "instruction reads: %ld", count_ir);
      VG_(message)(Vg_UserMsg, "data reads: %ld", count_dr);
      VG_(message)(Vg_UserMsg, "data writes: %ld", count_dw);
      VG_(message)(Vg_UserMsg, "data total: %ld", count_dr+count_dw);
   }
   if(clo_trace_reg){
      VG_(message)(Vg_UserMsg, "register gets: %ld", count_rg);
      VG_(message)(Vg_UserMsg, "register puts: %ld", count_rp);
   }
   if(clo_trace_sbs)
      VG_(message)(Vg_UserMsg, "superblocks entered: %llu\n", n_sb);

   if(clo_trace_mem && VG_(clo_record_replay) == REPLAYONLY){
      VG_(message)(Vg_UserMsg, "number of mismatched instruction: %ld", n_instr_mismatch);
      VG_(message)(Vg_UserMsg, "number of mismatched memory loads: %ld", n_load_mismatch);
      VG_(message)(Vg_UserMsg, "number of mismatched memory stores: %ld", n_store_mismatch);
   }
   if(clo_trace_reg && VG_(clo_record_replay) == REPLAYONLY){
      VG_(message)(Vg_UserMsg, "number of mismatched register gets: %ld", n_get_mismatch);
      VG_(message)(Vg_UserMsg, "number of mismatched register puts: %ld", n_put_mismatch);
   }
   if(VG_(clo_record_replay) == RECORDONLY) {
      VG_(message)(Vg_UserMsg, "Execution trace log is saved into %s.\n", rrcheck_out_file);
   }

   if(fd != -1) {
      VG_(close) (fd);
   }
}

static void rrcheck_pre_clo_init(void)
{
   VG_(details_name)            ("rrcheck");
   VG_(details_version)         (NULL);
   VG_(details_description)     ("a binary JIT-compiler");
   VG_(details_copyright_author)(
      "Copyright (C) 2002-2007, and GNU GPL'd, by Nicholas Nethercote.");
   VG_(details_bug_reports_to)  (VG_BUGS_TO);

   VG_(basic_tool_funcs)        (rrcheck_post_clo_init,
                                 rrcheck_instrument,
                                 rrcheck_fini);

   VG_(needs_command_line_options)(rrcheck_process_cmd_line_option,
                                   rrcheck_print_usage,
                                   rrcheck_print_debug_usage);


   /* No needs, no core events to track */
}

VG_DETERMINE_INTERFACE_VERSION(rrcheck_pre_clo_init)



/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/
