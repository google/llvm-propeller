; Check the basic block sections labels option
; RUN: llc < %s -mtriple=x86_64-unknown-linux-gnu -function-sections -basic-block-sections=labels | FileCheck %s -check-prefix=LINUX-LABELS

define void @_Z3bazb(i1 zeroext) personality i32 (...)* @__gxx_personality_v0 {
  br i1 %0, label %2, label %7

2:
  %3 = invoke i32 @_Z3barv()
          to label %7 unwind label %5
  br label %9

5:
  landingpad { i8*, i32 }
          catch i8* null
  br label %9

7:
  %8 = call i32 @_Z3foov()
  br label %9

9:
  ret void
}

declare i32 @_Z3barv() #1

declare i32 @_Z3foov() #1

declare i32 @__gxx_personality_v0(...)

; LINUX-LABELS-LABEL:	_Z3bazb:
; LINUX-LABELS:		.Lfunc_begin0:
; LINUX-LABELS:		.LBB_END0_[[L1:[0-9]+]]:
; LINUX-LABELS:		.LBB0_[[L2:[0-9]+]]:
; LINUX-LABELS:		.LBB_END0_[[L2]]:
; LINUX-LABELS:		.LBB0_[[L3:[0-9]+]]:
; LINUX-LABELS:		.LBB_END0_[[L3]]:
; LINUX-LABELS:		.LBB0_[[L4:[0-9]+]]:
; LINUX-LABELS:		.LBB_END0_[[L4]]:
; LINUX-LABELS:		.Lfunc_end0:

; LINUX-LABELS:		.section	.bb_addr_map,"o",@progbits,.text
; LINUX-LABELS-NEXT:	.quad	.Lfunc_begin0
; LINUX-LABELS-NEXT:	.byte	4
; LINUX-LABELS-NEXT:	.uleb128 .Lfunc_begin0-.Lfunc_begin0
; LINUX-LABELS-NEXT:	.uleb128 .LBB_END0_[[L1]]-.Lfunc_begin0
; LINUX-LABELS-NEXT:	.byte	0
; LINUX-LABELS-NEXT:	.uleb128 .LBB0_[[L2]]-.Lfunc_begin0
; LINUX-LABELS-NEXT:	.uleb128 .LBB_END0_[[L2]]-.LBB0_[[L2]]
; LINUX-LABELS-NEXT:	.byte	0
; LINUX-LABELS-NEXT:	.uleb128 .LBB0_[[L3]]-.Lfunc_begin0
; LINUX-LABELS-NEXT:	.uleb128 .LBB_END0_[[L3]]-.LBB0_[[L3]]
; LINUX-LABELS-NEXT:	.byte	1
; LINUX-LABELS-NEXT:	.uleb128 .LBB0_[[L4]]-.Lfunc_begin0
; LINUX-LABELS-NEXT:	.uleb128 .LBB_END0_[[L4]]-.LBB0_[[L4]]
; LINUX-LABELS-NEXT:	.byte	5
