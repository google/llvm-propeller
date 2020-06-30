; This test checks if CFI instructions for callee saved registers are emitted
; correctly with basic block sections.
; RUN: llc -O3 %s -mtriple=x86_64-unknown-linux-gnu -filetype=asm --basicblock-sections=all  -stop-after=cfi-instr-inserter  -o - | FileCheck --check-prefix=CFI_INSTR %s

; CFI_INSTR: _Z7computebiiiiiiiiiiii
; CFI_INSTR: bb.0
; CFI_INSTR: bb.1
; CFI_INSTR: CFI_INSTRUCTION def_cfa $rsp
; CFI_INSTR-NEXT: CFI_INSTRUCTION offset
; CFI_INSTR-NEXT: CFI_INSTRUCTION offset
; CFI_INSTR-NEXT: CFI_INSTRUCTION offset
; CFI_INSTR-NEXT: CFI_INSTRUCTION offset
; CFI_INSTR: bb.2
; CFI_INSTR: CFI_INSTRUCTION def_cfa $rsp
; CFI_INSTR-NEXT: CFI_INSTRUCTION offset
; CFI_INSTR-NEXT: CFI_INSTRUCTION offset
; CFI_INSTR-NEXT: CFI_INSTRUCTION offset
; CFI_INSTR-NEXT: CFI_INSTRUCTION offset

; Exhaust caller-saved parameter registers and  force callee saved registers to
; be used in the computation.  This tests that CFI directives for callee saved
; registers are generated with basic block sections.
; int compute(bool k, int p1, int p2, int p3, int p4, int p5, int p6, int p7,
;             int p8, int p9, int pa, int pb, int pc) {
;   int result = p1;
;   if (k)
;     result = p1 * p2 + p3 / p4 - p5 * p6 + p7 / p8  - p9 * pa + pb / pc;
;   return result;
; }

define dso_local i32 @_Z7computebiiiiiiiiiiii(i1 zeroext %k, i32 %p1, i32 %p2, i32 %p3, i32 %p4, i32 %p5, i32 %p6, i32 %p7, i32 %p8, i32 %p9, i32 %pa, i32 %pb, i32 %pc) local_unnamed_addr {
entry:
  br i1 %k, label %if.then, label %if.end

if.then:                                          ; preds = %entry
  %mul = mul nsw i32 %p2, %p1
  %div = sdiv i32 %p3, %p4
  %div2 = sdiv i32 %p7, %p8
  %mul1.neg = mul i32 %p6, %p5
  %mul4.neg = mul i32 %pa, %p9
  %div6 = sdiv i32 %pb, %pc
  %reass.add = add i32 %mul4.neg, %mul1.neg
  %add = sub i32 %mul, %reass.add
  %0 = add i32 %add, %div
  %sub5 = add i32 %0, %div2
  %add7 = add i32 %sub5, %div6
  br label %if.end

if.end:                                           ; preds = %if.then, %entry
  %result.0 = phi i32 [ %add7, %if.then ], [ %p1, %entry ]
  ret i32 %result.0
}
