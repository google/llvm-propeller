; RUN: llc -basicblock-sections=all -mtriple x86_64-pc-linux-gnu < %s | FileCheck %s
@_ZTIi = external constant i8*

define i32 @main() uwtable optsize ssp personality i8* bitcast (i32 (...)* @__gxx_personality_v0 to i8*) {
; Verify that each basic block section gets its own LSDA exception symbol.
;
; CHECK:    .text
; CHECK:  main:
; CHECK:    .Lfunc_begin0:
; CHECK:    .cfi_startproc
; CHECK:    .cfi_personality 3, __gxx_personality_v0
; CHECK:    .cfi_lsda 3, .Lexception0
; CHECK:    pushq %rax
; CHECK:  .Ltmp0:
; CHECK:    callq _Z1fv
; CHECK:  .Ltmp1:
; CHECK:    jmp	r.BB.main
; CHECK:    .cfi_endproc

; CHECK:    .section        .text,"ax",@progbits,unique,1
; CHECK:  r.BB.main:                              # %try.cont
; CHECK:    .cfi_startproc
; CHECK:    .cfi_personality 3, __gxx_personality_v0
; CHECK:    .cfi_lsda 3, .Lexception1
; CHECK:    xorl %eax, %eax
; CHECK:    popq %rcx
; CHECK:    retq
; CHECK:  .Ltmp3:
; CHECK:    .size   r.BB.main, .Ltmp3-r.BB.main
; CHECK:    .cfi_endproc

; CHECK:    .section        .text,"ax",@progbits,unique,2
; CHECK:  lr.BB.main:                             # %lpad
; CHECK:    .cfi_startproc
; CHECK:    .cfi_personality 3, __gxx_personality_v0
; CHECK:    .cfi_lsda 3, .Lexception2
; CHECK:    nop                             # avoids zero-offset landing pad
; CHECK:  .Ltmp2:
; CHECK:    movq %rax, %rdi
; CHECK:    callq _Unwind_Resume
; CHECK:  .Ltmp4:
; CHECK:    .size lr.BB.main, .Ltmp4-lr.BB.main
; CHECK:    .cfi_endproc



entry:
  invoke void @_Z1fv() optsize
          to label %try.cont unwind label %lpad

lpad:
  %0 = landingpad { i8*, i32 }
          cleanup
          catch i8* bitcast (i8** @_ZTIi to i8*)
  br label %eh.resume

try.cont:
  ret i32 0

eh.resume:
  resume { i8*, i32 } %0
}

declare void @_Z1fv() optsize

declare i32 @__gxx_personality_v0(...)

; Verify that the exception table gets split across the three basic block sections.
;
; CHECK:  GCC_except_table0:
; CHECK-NEXT:    .p2align	2
; CHECK-NEXT:  .Lexception0:
; CHECK-NEXT:    .byte	0                       # @LPStart Encoding = absptr
; CHECK-NEXT:    .quad	lr.BB.main
; CHECK-NEXT:    .byte	3                       # @TType Encoding = udata4
; CHECK-NEXT:    .uleb128 .Lttbase0-.Lttbaseref0
; CHECK-NEXT:    .Lttbaseref0:
; CHECK-NEXT:    .byte	1                       # Call site Encoding = uleb128
; CHECK-NEXT:    .uleb128 .Lcst_end0-.Lcst_begin0
; CHECK-NEXT:    .Lcst_begin0:
; CHECK-NEXT:    .uleb128 .Ltmp0-.Lfunc_begin0   # >> Call Site 1 <<
; CHECK-NEXT:    .uleb128 .Ltmp1-.Ltmp0          #   Call between .Ltmp0 and .Ltmp1
; CHECK-NEXT:    .uleb128 .Ltmp2-lr.BB.main      #     jumps to .Ltmp2
; CHECK-NEXT:    .byte	3                       #   On action: 2
; CHECK-NEXT:    .p2align	2
; CHECK-NEXT:  .Lexception1:
; CHECK-NEXT:    .byte	0                       # @LPStart Encoding = absptr
; CHECK-NEXT:    .quad	lr.BB.main
; CHECK-NEXT:    .byte	3                       # @TType Encoding = udata4
; CHECK-NEXT:    .uleb128 .Lttbase0-.Lttbaseref1
; CHECK-NEXT:  .Lttbaseref1:
; CHECK-NEXT:    .byte	1                       # Call site Encoding = uleb128
; CHECK-NEXT:    .uleb128 .Lcst_end0-.Lcst_begin1
; CHECK-NEXT:    .Lcst_begin1:
; CHECK-NEXT:    .p2align 2
; CHECK-NEXT:  .Lexception2:
; CHECK-NEXT:    .byte	0                       # @LPStart Encoding = absptr
; CHECK-NEXT:    .quad	lr.BB.main
; CHECK-NEXT:    .byte	3                       # @TType Encoding = udata4
; CHECK-NEXT:    .uleb128 .Lttbase0-.Lttbaseref2
; CHECK-NEXT:  .Lttbaseref2:
; CHECK-NEXT:    .byte	1                       # Call site Encoding = uleb128
; CHECK-NEXT:    .uleb128 .Lcst_end0-.Lcst_begin2
; CHECK-NEXT:  .Lcst_begin2:
; CHECK-NEXT:    .uleb128 lr.BB.main-lr.BB.main  # >> Call Site 2 <<
; CHECK-NEXT:    .uleb128 .Ltmp4-lr.BB.main      #   Call between lr.BB.main and .Ltmp4
; CHECK-NEXT:    .byte	0                       #     has no landing pad
; CHECK-NEXT:    .byte	0                       #   On action: cleanup
; CHECK-NEXT:    .Lcst_end0:
; CHECK-NEXT:    .byte	0                       # >> Action Record 1 <<
; CHECK-NEXT:                                        #   Cleanup
; CHECK-NEXT:    .byte	0                       #   No further actions
; CHECK-NEXT:    .byte	1                       # >> Action Record 2 <<
; CHECK-NEXT:                                        #   Catch TypeInfo 1
; CHECK-NEXT:    .byte	125                     #   Continue to action 1
; CHECK-NEXT:    .p2align 2
; CHECK-NEXT:                                   # >> Catch TypeInfos <<
; CHECK-NEXT:    .long _ZTIi                    # TypeInfo 1
; CHECK-NEXT:  .Lttbase0:
; CHECK-NEXT:    .p2align 2
; CHECK-NEXT:                                   # -- End function
