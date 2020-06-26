; RUN: llc -O0 %s --basicblock-sections=all -mtriple=x86_64-unknown-linux-gnu -filetype=asm -dedup-fde-to-cie -o - | FileCheck --check-prefix=SECTIONS_CFI %s
; RUN: llc -O0 %s --basicblock-sections=all -mtriple=x86_64-unknown-linux-gnu -filetype=obj -dedup-fde-to-cie -o - | llvm-dwarfdump --debug-frame  - | FileCheck --check-prefix=DEBUG_FRAME %s

; From:
; int foo(int a) {
;   if (a > 20)
;     return 2;
;   else
;     return 0;
; }

; SECTIONS_CFI: _Z3fooi
; SECTIONS_CFI: .cfi_startproc
; SECTIONS_CFI: .cfi_def_cfa_offset
; SECTIONS_CFI: .cfi_def_cfa_register
; SECTIONS_CFI: .cfi_endproc

; SECTIONS_CFI: _Z3fooi.1
; SECTIONS_CFI-NEXT: .cfi_startproc
; SECTIONS_CFI-NEXT: .cfi_def_cfa
; SECTIONS_CFI-NEXT: .cfi_offset
; SECTIONS_CFI: .cfi_endproc

; SECTIONS_CFI: _Z3fooi.2
; SECTIONS_CFI-NEXT: .cfi_startproc
; SECTIONS_CFI-NEXT: .cfi_def_cfa
; SECTIONS_CFI-NEXT: .cfi_offset
; SECTIONS_CFI: .cfi_endproc

; SECTIONS_CFI: _Z3fooi.3
; SECTIONS_CFI-NEXT: .cfi_startproc
; SECTIONS_CFI-NEXT: .cfi_def_cfa
; SECTIONS_CFI-NEXT: .cfi_offset
; SECTIONS_CFI: .cfi_def_cfa
; SECTIONS_CFI: .cfi_endproc

; There must be 2 CIEs and 4 FDEs

; DEBUG_FRAME: .debug_frame contents

; DEBUG_FRAME: CIE
; DEBUG_FRAME: DW_CFA_def_cfa
; DEBUG_FRAME: DW_CFA_offset

; DEBUG_FRAME: FDE cie=
; DEBUG_FRAME: DW_CFA_def_cfa_offset
; DEBUG_FRAME: DW_CFA_def_cfa_register

; DEBUG_FRAME: CIE
; DEBUG_FRAME: DW_CFA_def_cfa
; DEBUG_FRAME: DW_CFA_def_cfa

; DEBUG_FRAME: FDE cie=

; DEBUG_FRAME: FDE cie=

; DEBUG_FRAME: FDE cie=
; DEBUG_FRAME: DW_CFA_def_cfa


source_filename = "debuginfo.cc"
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @_Z3fooi(i32 %0) #0 !dbg !7 {
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  store i32 %0, i32* %3, align 4
  call void @llvm.dbg.declare(metadata i32* %3, metadata !11, metadata !DIExpression()), !dbg !12
  %4 = load i32, i32* %3, align 4, !dbg !13
  %5 = icmp sgt i32 %4, 20, !dbg !15
  br i1 %5, label %6, label %7, !dbg !16

6:                                                ; preds = %1
  store i32 2, i32* %2, align 4, !dbg !17
  br label %8, !dbg !17

7:                                                ; preds = %1
  store i32 0, i32* %2, align 4, !dbg !18
  br label %8, !dbg !18

8:                                                ; preds = %7, %6
  %9 = load i32, i32* %2, align 4, !dbg !19
  ret i32 %9, !dbg !19
}

; Function Attrs: nounwind readnone speculatable willreturn
declare void @llvm.dbg.declare(metadata, metadata, metadata) #1

attributes #0 = { noinline nounwind optnone uwtable  "frame-pointer"="all"  "target-cpu"="x86-64" }

attributes #1 = { nounwind readnone speculatable willreturn }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!3, !4, !5}
!llvm.ident = !{!6}

!0 = distinct !DICompileUnit(language: DW_LANG_C_plus_plus, file: !1, producer: "clang version 10.0.0 (git@github.com:google/llvm-propeller.git f9421ebf4b3d8b64678bf6c49d1607fdce3f50c5)", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, enums: !2, nameTableKind: None)
!1 = !DIFile(filename: "debuginfo.cc", directory: "/g/tmsriram/Projects_2019/github_repo/Examples")
!2 = !{}
!3 = !{i32 2, !"Dwarf Version", i32 4}
!4 = !{i32 2, !"Debug Info Version", i32 3}
!5 = !{i32 1, !"wchar_size", i32 4}
!6 = !{!"clang version 10.0.0 (git@github.com:google/llvm-propeller.git f9421ebf4b3d8b64678bf6c49d1607fdce3f50c5)"}
!7 = distinct !DISubprogram(name: "foo", linkageName: "_Z3fooi", scope: !1, file: !1, line: 1, type: !8, scopeLine: 1, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !0, retainedNodes: !2)
!8 = !DISubroutineType(types: !9)
!9 = !{!10, !10}
!10 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!11 = !DILocalVariable(name: "a", arg: 1, scope: !7, file: !1, line: 1, type: !10)
!12 = !DILocation(line: 1, column: 13, scope: !7)
!13 = !DILocation(line: 2, column: 7, scope: !14)
!14 = distinct !DILexicalBlock(scope: !7, file: !1, line: 2, column: 7)
!15 = !DILocation(line: 2, column: 9, scope: !14)
!16 = !DILocation(line: 2, column: 7, scope: !7)
!17 = !DILocation(line: 3, column: 5, scope: !14)
!18 = !DILocation(line: 5, column: 5, scope: !14)
!19 = !DILocation(line: 6, column: 1, scope: !7)
