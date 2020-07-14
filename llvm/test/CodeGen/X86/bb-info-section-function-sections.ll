; RUN: llc < %s -mtriple=x86_64-linux -bb-info-section -function-sections | FileCheck %s

;; Check we add SHF_LINK_ORDER for .bb_info and link it with the corresponding .text sections.
; CHECK: .section        .text._Z3barv,"ax",@progbits
; CHECK-LABEL: _Z3barv:
; CHECK-NEXT: [[BAR_BEGIN:.+]]:
; CHECK: .section        .bb_info,"o",@progbits,.text._Z3barv{{$}}
; CHECK-NEXT: .quad [[BAR_BEGIN]]
; CHECK: .section        .text._Z3foov,"ax",@progbits
; CHECK-LABEL: _Z3foov:
; CHECK-NEXT: [[FOO_BEGIN:.+]]:
; CHECK: .section        .bb_info,"o",@progbits,.text._Z3foov{{$}}
; CHECK-NEXT: .quad [[FOO_BEGIN]]

;; Check we add .bb_info section to a COMDAT group with the corresponding .text section if such a COMDAT exists.
; CHECK: .section        .text._Z4fooTIiET_v,"axG",@progbits,_Z4fooTIiET_v,comdat
; CHECK-LABEL: _Z4fooTIiET_v:
; CHECK-NEXT: [[FOOCOMDAT_BEGIN:.+]]:
; CHECK: .section        .bb_info,"Go",@progbits,_Z4fooTIiET_v,comdat,.text._Z4fooTIiET_v{{$}}
; CHECK-NEXT: .quad [[FOOCOMDAT_BEGIN]]


$_Z4fooTIiET_v = comdat any

define dso_local i32 @_Z3barv() {
  ret i32 0
}

define dso_local i32 @_Z3foov() {
  %1 = call i32 @_Z4fooTIiET_v()
  ret i32 %1
}

define linkonce_odr dso_local i32 @_Z4fooTIiET_v() comdat {
  ret i32 0
}
