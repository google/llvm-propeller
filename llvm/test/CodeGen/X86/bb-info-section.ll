; Check the basic block sections labels option
; RUN: llc < %s -mtriple=x86_64-pc-linux -bb-info-section | FileCheck %s

define void @_Z3bazb(i1 zeroext) personality i32 (...)* @__gxx_personality_v0 {
  %2 = alloca i8, align 1
  %3 = zext i1 %0 to i8
  store i8 %3, i8* %2, align 1
  %4 = load i8, i8* %2, align 1
  %5 = trunc i8 %4 to i1
  br i1 %5, label %6, label %11

6:
  %7 = invoke i32 @_Z3barv()
          to label %11 unwind label %9
  br label %13

9:
  landingpad { i8*, i32 }
          catch i8* null
  br label %13

11:
  %12 = call i32 @_Z3foov()
  br label %13

13:
  ret void
}

declare i32 @_Z3barv() #1

declare i32 @_Z3foov() #1

declare i32 @__gxx_personality_v0(...)

; CHECK-LABEL: _Z3bazb:
; CHECK: .Lfunc_begin0:
; CHECK: .LBB_END0_[[L1:[0-9]+]]:
; CHECK: .LBB0_[[L2:[0-9]+]]:
; CHECK: .LBB_END0_[[L2]]:
; CHECK: .LBB0_[[L3:[0-9]+]]:
; CHECK: .LBB_END0_[[L3]]:
; CHECK: .LBB0_[[L4:[0-9]+]]:
; CHECK: .LBB_END0_[[L4]]:
; CHECK: .Lfunc_end0:

; CHECK: .section	.bb_info,"o",@progbits,.text
; CHECK-NEXT: .quad	.Lfunc_begin0
; CHECK-NEXT: .byte	4
; CHECK-NEXT: .uleb128 .LBB_END0_[[L1]]-.Lfunc_begin0
; CHECK-NEXT: .byte	0
; CHECK-NEXT: .uleb128 .LBB0_[[L2]]-.Lfunc_begin0
; CHECK-NEXT: .uleb128 .LBB_END0_[[L2]]-.LBB0_[[L2]]
; CHECK-NEXT: .byte	0
; CHECK-NEXT: .uleb128 .LBB0_[[L3]]-.Lfunc_begin0
; CHECK-NEXT: .uleb128 .LBB_END0_[[L3]]-.LBB0_[[L3]]
; CHECK-NEXT: .byte	1
; CHECK-NEXT: .uleb128 .LBB0_[[L4]]-.Lfunc_begin0
; CHECK-NEXT: .uleb128 .LBB_END0_[[L4]]-.LBB0_[[L4]]
; CHECK-NEXT: .byte	5
