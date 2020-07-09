; This test checks if CFI instructions for callee saved registers are emitted
; correctly with basic block sections.
; RUN: llc -O3 %s -mtriple=x86_64-unknown-linux-gnu -filetype=asm --basicblock-sections=all  -stop-after=cfi-instr-inserter  -o - | FileCheck --check-prefix=CFI_INSTR %s

; CFI_INSTR: _Z3foobiiiiii
; CFI_INSTR: bb.0.entry:
; CFI_INSTR:      CFI_INSTRUCTION offset
; CFI_INSTR-NEXT: CFI_INSTRUCTION offset
; CFI_INSTR-NEXT: CFI_INSTRUCTION offset
; CFI_INSTR: bb.1.if.then (bbsections 1):
; CFI_INSTR: CFI_INSTRUCTION def_cfa $rsp
; CFI_INSTR-NEXT: CFI_INSTRUCTION offset
; CFI_INSTR-NEXT: CFI_INSTRUCTION offset
; CFI_INSTR-NEXT: CFI_INSTRUCTION offset
; CFI_INSTR: bb.2.if.end (bbsections 2):
; CFI_INSTR: CFI_INSTRUCTION def_cfa $rsp
; CFI_INSTR-NEXT: CFI_INSTRUCTION offset
; CFI_INSTR-NEXT: CFI_INSTRUCTION offset
; CFI_INSTR-NEXT: CFI_INSTRUCTION offset

; Exhaust caller-saved parameter registers and  force callee saved registers to
; be used.  This tests that CFI directives for callee saved registers are
; generated with basic block sections.
; extern void f1(int, int, int);
;
; void foo(bool k, int p1, int p2, int p3, int p4, int p5, int p6) {
;   // Using a conditional forces a basic block section.
;   if (k) {
;     // p1, p3 and p5 will use the same parameter registers as p2, p4 and p6
;     // respectively in making the calls below.  This forces the need to stash
;     // some of these values (already in the parameter registers) in callee
;     // saved registers.
;     f1(p1, p3, p5);
;     f1(p2, p4, p6);
;   }
; }

define dso_local void @_Z3foobiiiiii(i1 zeroext %k, i32 %p1, i32 %p2, i32 %p3, i32 %p4, i32 %p5, i32 %p6) local_unnamed_addr {
entry:
  br i1 %k, label %if.then, label %if.end

if.then:                                          ; preds = %entry
  tail call void @_Z2f1iii(i32 %p1, i32 %p3, i32 %p5)
  tail call void @_Z2f1iii(i32 %p2, i32 %p4, i32 %p6)
  br label %if.end

if.end:                                           ; preds = %if.then, %entry
  ret void
}

declare dso_local void @_Z2f1iii(i32, i32, i32) local_unnamed_addr
