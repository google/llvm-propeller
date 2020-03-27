; BB cluster sections.
; Basic blocks #0 (entry) and #4 will be placed in the same section.
; Basic block 1 will be placed in a unique section.
; The rest (BBs #2 and #3) end up in the cold section.
; RUN: echo '!f' > %t
; RUN: echo '!!0 4' >> %t
; RUN: echo '!!1' >> %t
; RUN: llc < %s -O0 -mtriple=x86_64-pc-linux -function-sections -basicblock-sections=%t | FileCheck %s -check-prefix=LINUX-SECTIONS

declare void @stub(i32*)

define i32 @f(i32* %ptr, i1 %cond) {
  entry:
    br i1 %cond, label %left, label %right

  left:                                             ; preds = %entry
    %is_null = icmp eq i32* %ptr, null
    br i1 %is_null, label %null, label %not_null

  not_null:                                         ; preds = %left
    %val = load i32, i32* %ptr
    ret i32 %val

  null:                                             ; preds = %left
    call void @stub(i32* %ptr)
    unreachable

  right:                                            ; preds = %entry
    call void @stub(i32* null)
    unreachable
}

; LINUX-SECTIONS:	.section        .text.f,"ax",@progbits
; LINUX-SECTIONS:	f:
; LINUX-SECTIONS:	jne     a.BB.f
; LINUX-SECTIONS-NOT:   {{jne|je|jmp}}
; LINUX-SECTIONS:	aara.BB.f:
; LINUX-SECTIONS:	.section        .text.f,"ax",@progbits,unique,1
; LINUX-SECTIONS:	a.BB.f:
; LINUX-SECTIONS:       je      ara.BB.f
; LINUX-SECTIONS-NEXT:  jmp     ra.BB.f
; LINUX-SECTIONS-NOT:   {{jne|je|jmp}}
; LINUX-SECTIONS:	.size   a.BB.f, .Ltmp0-a.BB.f
; LINUX-SECTIONS:	.section        .text.f.unlikely,"ax",@progbits
; LINUX-SECTIONS:	ra.BB.f:
; LINUX-SECTIONS:	ara.BB.f:
; LINUX-SECTIONS:	.size   ra.BB.f, .Ltmp1-ra.BB.f
; LINUX-SECTIONS:	.section        .text.f,"ax",@progbits
; LINUX-SECTIONS-NEXT:	.Lfunc_end0:
; LINUX_SECTIONS-NEXT:	.size   f, .Lfunc_end0-f

