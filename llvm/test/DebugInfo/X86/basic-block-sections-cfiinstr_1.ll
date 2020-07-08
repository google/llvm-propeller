; This test checks if CFI instructions for callee saved registers are emitted
; correctly with basic block sections.
; RUN: llc -O3 %s -mtriple=x86_64-unknown-linux-gnu -filetype=asm --basicblock-sections=all  -stop-after=cfi-instr-inserter  -o - | FileCheck --check-prefix=CFI_INSTR %s

; CFI_INSTR: _Z7computebiiiiii
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
; extern int f1(int, int, int);
;
; int compute(bool k, int p1, int p2, int p3, int p4, int p5, int p6) {
;   int result = p1;
;   if (k)
;     result = f1(p1,p3,p5) + f1(p2, p4, p6);
;   return result;
; }

define dso_local i32 @_Z7computebiiiiii(i1 zeroext %k, i32 %p1, i32 %p2, i32 %p3, i32 %p4, i32 %p5, i32 %p6) local_unnamed_addr {
entry:
  br i1 %k, label %if.then, label %if.end

if.then:                                          ; preds = %entry
  %call = tail call i32 @_Z2f1iii(i32 %p1, i32 %p3, i32 %p5)
  %call1 = tail call i32 @_Z2f1iii(i32 %p2, i32 %p4, i32 %p6)
  %add = add nsw i32 %call1, %call
  br label %if.end

if.end:                                           ; preds = %if.then, %entry
  %result.0 = phi i32 [ %add, %if.then ], [ %p1, %entry ]
  ret i32 %result.0
}

declare dso_local i32 @_Z2f1iii(i32, i32, i32) local_unnamed_addr #1
