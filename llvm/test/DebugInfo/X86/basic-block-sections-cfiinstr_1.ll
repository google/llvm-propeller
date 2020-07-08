; This test checks if CFI instructions for callee saved registers are emitted
; correctly with basic block sections.
; RUN: llc -O3 %s -mtriple=x86_64-unknown-linux-gnu -filetype=asm --basicblock-sections=all  -stop-after=cfi-instr-inserter  -o - | FileCheck --check-prefix=CFI_INSTR %s

; CFI_INSTR: _Z7computeb10structparmS_iii
; CFI_INSTR: bb.0
; CFI_INSTR: bb.1
; CFI_INSTR: CFI_INSTRUCTION def_cfa $rsp
; CFI_INSTR-NEXT: CFI_INSTRUCTION offset
; CFI_INSTR-NEXT: CFI_INSTRUCTION offset
; CFI_INSTR-NEXT: CFI_INSTRUCTION offset
; CFI_INSTR: bb.2
; CFI_INSTR: CFI_INSTRUCTION def_cfa $rsp
; CFI_INSTR-NEXT: CFI_INSTRUCTION offset
; CFI_INSTR-NEXT: CFI_INSTRUCTION offset
; CFI_INSTR-NEXT: CFI_INSTRUCTION offset

; Exhaust caller-saved parameter registers and  force callee saved registers to
; be used in the computation.  This tests that CFI directives for callee saved
; registers are generated with basic block sections.
; typedef struct {
;   int v[4];
; } structparm;
;
; int compute(bool k, structparm p, structparm q, int pa, int pb, int pc) {
;   int result = p.v[1];
;   if (k)
;     result = p.v[0] * p.v[1]  + p.v[2] / p.v[3] - q.v[0] * q.v[1] + q.v[2] / q.v[3] * pa + pb / pc;
;   return result;
; }


define dso_local i32 @_Z7computeb10structparmS_iii(i1 zeroext %k, i64 %p.coerce0, i64 %p.coerce1, i64 %q.coerce0, i64 %q.coerce1, i32 %pa, i32 %pb, i32 %pc) local_unnamed_addr {
entry:
  %p.sroa.2.0.extract.shift = lshr i64 %p.coerce0, 32
  %p.sroa.2.0.extract.trunc = trunc i64 %p.sroa.2.0.extract.shift to i32
  br i1 %k, label %if.then, label %if.end

if.then:                                          ; preds = %entry
  %q.sroa.5.8.extract.shift = lshr i64 %q.coerce1, 32
  %q.sroa.5.8.extract.trunc = trunc i64 %q.sroa.5.8.extract.shift to i32
  %q.sroa.3.8.extract.trunc = trunc i64 %q.coerce1 to i32
  %q.sroa.2.0.extract.shift = lshr i64 %q.coerce0, 32
  %q.sroa.2.0.extract.trunc = trunc i64 %q.sroa.2.0.extract.shift to i32
  %q.sroa.0.0.extract.trunc = trunc i64 %q.coerce0 to i32
  %p.sroa.6.8.extract.shift = lshr i64 %p.coerce1, 32
  %p.sroa.6.8.extract.trunc = trunc i64 %p.sroa.6.8.extract.shift to i32
  %p.sroa.4.8.extract.trunc = trunc i64 %p.coerce1 to i32
  %p.sroa.0.0.extract.trunc = trunc i64 %p.coerce0 to i32
  %mul = mul nsw i32 %p.sroa.2.0.extract.trunc, %p.sroa.0.0.extract.trunc
  %div = sdiv i32 %p.sroa.4.8.extract.trunc, %p.sroa.6.8.extract.trunc
  %mul13 = mul nsw i32 %q.sroa.2.0.extract.trunc, %q.sroa.0.0.extract.trunc
  %div18 = sdiv i32 %q.sroa.3.8.extract.trunc, %q.sroa.5.8.extract.trunc
  %mul19 = mul nsw i32 %div18, %pa
  %div21 = sdiv i32 %pb, %pc
  %add = sub i32 %mul, %mul13
  %sub = add i32 %add, %div
  %add20 = add i32 %sub, %mul19
  %add22 = add i32 %add20, %div21
  br label %if.end

if.end:                                           ; preds = %if.then, %entry
  %result.0 = phi i32 [ %add22, %if.then ], [ %p.sroa.2.0.extract.trunc, %entry ]
  ret i32 %result.0
}
