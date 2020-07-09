; RUN: llc -O0 %s --basicblock-sections=all -mtriple=x86_64-unknown-linux-gnu -filetype=asm -o - | FileCheck --check-prefix=SECTIONS_CFI %s
; RUN: llc -O0 %s --basicblock-sections=all -mtriple=x86_64-unknown-linux-gnu -filetype=obj -o - | llvm-dwarfdump --debug-frame  - | FileCheck --check-prefix=DEBUG_FRAME %s

; void f1();
; void f3(bool b) {
;   if (b)
;     f1();
; }


; SECTIONS_CFI: _Z2f3b
; SECTIONS_CFI: .cfi_startproc
; SECTIONS_CFI: .cfi_def_cfa_offset
; SECTIONS_CFI: .cfi_def_cfa_register
; SECTIONS_CFI: .cfi_endproc

; SECTIONS_CFI: _Z2f3b.1
; SECTIONS_CFI-NEXT: .cfi_startproc
; SECTIONS_CFI-NEXT: .cfi_def_cfa
; SECTIONS_CFI-NEXT: .cfi_offset
; SECTIONS_CFI: .cfi_endproc

; SECTIONS_CFI: _Z2f3b.2
; SECTIONS_CFI-NEXT: .cfi_startproc
; SECTIONS_CFI-NEXT: .cfi_def_cfa
; SECTIONS_CFI-NEXT: .cfi_offset
; SECTIONS_CFI: .cfi_def_cfa
; SECTIONS_CFI: .cfi_endproc

; There must be 1 CIE and 3 FDEs

; DEBUG_FRAME: .debug_frame contents

; DEBUG_FRAME: CIE
; DEBUG_FRAME: DW_CFA_def_cfa
; DEBUG_FRAME: DW_CFA_offset

; DEBUG_FRAME: FDE cie=
; DEBUG_FRAME: DW_CFA_def_cfa_offset
; DEBUG_FRAME: DW_CFA_offset
; DEBUG_FRAME: DW_CFA_def_cfa_register

; DEBUG_FRAME: FDE cie=
; DEBUG_FRAME: DW_CFA_def_cfa
; DEBUG_FRAME: DW_CFA_offset

; DEBUG_FRAME: FDE cie=
; DEBUG_FRAME: DW_CFA_def_cfa
; DEBUG_FRAME: DW_CFA_offset

; Function Attrs: noinline optnone uwtable
define dso_local void @_Z2f3b(i1 zeroext %b) #0 {
entry:
  %b.addr = alloca i8, align 1
  %frombool = zext i1 %b to i8
  store i8 %frombool, i8* %b.addr, align 1
  %0 = load i8, i8* %b.addr, align 1
  %tobool = trunc i8 %0 to i1
  br i1 %tobool, label %if.then, label %if.end

if.then:                                          ; preds = %entry
  call void @_Z2f1v()
  br label %if.end

if.end:                                           ; preds = %if.then, %entry
  ret void
}

declare dso_local void @_Z2f1v()

attributes #0 = { "frame-pointer"="all" }
