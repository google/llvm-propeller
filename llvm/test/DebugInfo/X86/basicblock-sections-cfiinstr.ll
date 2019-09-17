; RUN: llc -O3 %s -mtriple=x86_64-unknown-linux-gnu -filetype=asm --basicblock-sections=all  -stop-after=cfi-instr-inserter  -o - | FileCheck --check-prefix=CFI_INSTR %s

; CFI_INSTR: _Z7computebiiiiiiiiiiii
; CFI_INSTR: bb.0
; CFI_INSTR: CFI_INSTRUCTION def_cfa_offset
; CFI_INSTR: bb.1
; CFI_INSTR: CFI_INSTRUCTION def_cfa $rsp
; CFI_INSTR-NEXT: CFI_INSTRUCTION offset
; CFI_INSTR-NEXT: CFI_INSTRUCTION offset
; CFI_INSTR-NEXT: CFI_INSTRUCTION offset
; CFI_INSTR: bb.2
; CFI_INSTR: CFI_INSTRUCTION def_cfa $rsp
; CFI_INSTR-NEXT: CFI_INSTRUCTION offset
; CFI_INSTR-NEXT: CFI_INSTRUCTION offset
; CFI_INSTR-NEXT: CFI_INSTRUCTION offset
; CFI_INSTR: bb.3
; CFI_INSTR: CFI_INSTRUCTION def_cfa $rsp
; CFI_INSTR-NEXT: CFI_INSTRUCTION offset
; CFI_INSTR-NEXT: CFI_INSTRUCTION offset
; CFI_INSTR-NEXT: CFI_INSTRUCTION offset

; From:
; int compute(bool k, int p1, int p2, int p3, int p4, int p5, int p6, int p7, int p8, int p9, int pa, int pb, int pc) {
;  int result;
;  if (k)
;    result = p1 * p2 + p3 / p4 - p5 * p6 + p7 / p8 - p9 * pa + pb / pc;
;  else
;    result = p1 / p2 - p3 * p4 + p5 / p6 - p7 * p8 + p9 / pa - pb * pc;
;  return result;
; }

; ModuleID = 'use_regs.tmp.bc'
source_filename = "use_regs.cc"
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; Function Attrs: norecurse nounwind readnone uwtable
define dso_local i32 @_Z7computebiiiiiiiiiiii(i1 zeroext %0, i32 %1, i32 %2, i32 %3, i32 %4, i32 %5, i32 %6, i32 %7, i32 %8, i32 %9, i32 %10, i32 %11, i32 %12) local_unnamed_addr #0 !dbg !7 {
  call void @llvm.dbg.value(metadata i1 %0, metadata !13, metadata !DIExpression()), !dbg !27
  call void @llvm.dbg.value(metadata i32 %1, metadata !14, metadata !DIExpression()), !dbg !27
  call void @llvm.dbg.value(metadata i32 %2, metadata !15, metadata !DIExpression()), !dbg !27
  call void @llvm.dbg.value(metadata i32 %3, metadata !16, metadata !DIExpression()), !dbg !27
  call void @llvm.dbg.value(metadata i32 %4, metadata !17, metadata !DIExpression()), !dbg !27
  call void @llvm.dbg.value(metadata i32 %5, metadata !18, metadata !DIExpression()), !dbg !27
  call void @llvm.dbg.value(metadata i32 %6, metadata !19, metadata !DIExpression()), !dbg !27
  call void @llvm.dbg.value(metadata i32 %7, metadata !20, metadata !DIExpression()), !dbg !27
  call void @llvm.dbg.value(metadata i32 %8, metadata !21, metadata !DIExpression()), !dbg !27
  call void @llvm.dbg.value(metadata i32 %9, metadata !22, metadata !DIExpression()), !dbg !27
  call void @llvm.dbg.value(metadata i32 %10, metadata !23, metadata !DIExpression()), !dbg !27
  call void @llvm.dbg.value(metadata i32 %11, metadata !24, metadata !DIExpression()), !dbg !27
  call void @llvm.dbg.value(metadata i32 %12, metadata !25, metadata !DIExpression()), !dbg !27
  br i1 %0, label %14, label %22, !dbg !28

14:                                               ; preds = %13
  %15 = mul nsw i32 %2, %1, !dbg !29
  %16 = sdiv i32 %3, %4, !dbg !31
  %.neg28 = mul i32 %6, %5
  %17 = sdiv i32 %7, %8, !dbg !32
  %.neg29 = mul i32 %10, %9
  %18 = sdiv i32 %11, %12, !dbg !33
  %reass.add30 = add i32 %.neg29, %.neg28
  %19 = sub i32 %15, %reass.add30, !dbg !34
  %20 = add i32 %19, %16, !dbg !35
  %21 = add i32 %20, %17, !dbg !36
  br label %28, !dbg !37

22:                                               ; preds = %13
  %23 = sdiv i32 %1, %2, !dbg !38
  %.neg = mul i32 %4, %3
  %24 = sdiv i32 %5, %6, !dbg !39
  %.neg25 = mul i32 %8, %7
  %25 = sdiv i32 %9, %10, !dbg !40
  %.neg26 = mul i32 %12, %11
  %reass.add = add i32 %.neg25, %.neg
  %reass.add27 = add i32 %reass.add, %.neg26
  %26 = sub i32 %23, %reass.add27, !dbg !41
  %27 = add i32 %26, %24, !dbg !42
  call void @llvm.dbg.value(metadata i32 %29, metadata !26, metadata !DIExpression()), !dbg !27
  br label %28

28:                                               ; preds = %22, %14
  %.sink32 = phi i32 [ %25, %22 ], [ %18, %14 ]
  %.sink = phi i32 [ %27, %22 ], [ %21, %14 ]
  %29 = add i32 %.sink, %.sink32, !dbg !43
  call void @llvm.dbg.value(metadata i32 %29, metadata !26, metadata !DIExpression()), !dbg !27
  ret i32 %29, !dbg !44
}

; Function Attrs: nounwind readnone speculatable willreturn
declare void @llvm.dbg.value(metadata, metadata, metadata) #1

attributes #0 = { norecurse nounwind readnone uwtable "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nounwind readnone speculatable willreturn }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!3, !4, !5}
!llvm.ident = !{!6}

!0 = distinct !DICompileUnit(language: DW_LANG_C_plus_plus, file: !1, producer: "clang version 10.0.0 (git@github.com:google/llvm-propeller.git e414756c805463af90acd9bff57e6b1c805b7925)", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, enums: !2, nameTableKind: None)
!1 = !DIFile(filename: "use_regs.cc", directory: "/g/tmsriram/Projects_2019/github_repo/Examples")
!2 = !{}
!3 = !{i32 2, !"Dwarf Version", i32 4}
!4 = !{i32 2, !"Debug Info Version", i32 3}
!5 = !{i32 1, !"wchar_size", i32 4}
!6 = !{!"clang version 10.0.0 (git@github.com:google/llvm-propeller.git e414756c805463af90acd9bff57e6b1c805b7925)"}
!7 = distinct !DISubprogram(name: "compute", linkageName: "_Z7computebiiiiiiiiiiii", scope: !1, file: !1, line: 1, type: !8, scopeLine: 1, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !12)
!8 = !DISubroutineType(types: !9)
!9 = !{!10, !11, !10, !10, !10, !10, !10, !10, !10, !10, !10, !10, !10, !10}
!10 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!11 = !DIBasicType(name: "bool", size: 8, encoding: DW_ATE_boolean)
!12 = !{!13, !14, !15, !16, !17, !18, !19, !20, !21, !22, !23, !24, !25, !26}
!13 = !DILocalVariable(name: "k", arg: 1, scope: !7, file: !1, line: 1, type: !11)
!14 = !DILocalVariable(name: "p1", arg: 2, scope: !7, file: !1, line: 1, type: !10)
!15 = !DILocalVariable(name: "p2", arg: 3, scope: !7, file: !1, line: 1, type: !10)
!16 = !DILocalVariable(name: "p3", arg: 4, scope: !7, file: !1, line: 1, type: !10)
!17 = !DILocalVariable(name: "p4", arg: 5, scope: !7, file: !1, line: 1, type: !10)
!18 = !DILocalVariable(name: "p5", arg: 6, scope: !7, file: !1, line: 1, type: !10)
!19 = !DILocalVariable(name: "p6", arg: 7, scope: !7, file: !1, line: 1, type: !10)
!20 = !DILocalVariable(name: "p7", arg: 8, scope: !7, file: !1, line: 1, type: !10)
!21 = !DILocalVariable(name: "p8", arg: 9, scope: !7, file: !1, line: 1, type: !10)
!22 = !DILocalVariable(name: "p9", arg: 10, scope: !7, file: !1, line: 1, type: !10)
!23 = !DILocalVariable(name: "pa", arg: 11, scope: !7, file: !1, line: 1, type: !10)
!24 = !DILocalVariable(name: "pb", arg: 12, scope: !7, file: !1, line: 1, type: !10)
!25 = !DILocalVariable(name: "pc", arg: 13, scope: !7, file: !1, line: 1, type: !10)
!26 = !DILocalVariable(name: "result", scope: !7, file: !1, line: 2, type: !10)
!27 = !DILocation(line: 0, scope: !7)
!28 = !DILocation(line: 3, column: 7, scope: !7)
!29 = !DILocation(line: 4, column: 17, scope: !30)
!30 = distinct !DILexicalBlock(scope: !7, file: !1, line: 3, column: 7)
!31 = !DILocation(line: 4, column: 27, scope: !30)
!32 = !DILocation(line: 4, column: 47, scope: !30)
!33 = !DILocation(line: 4, column: 67, scope: !30)
!34 = !DILocation(line: 4, column: 32, scope: !30)
!35 = !DILocation(line: 4, column: 42, scope: !30)
!36 = !DILocation(line: 4, column: 52, scope: !30)
!37 = !DILocation(line: 4, column: 5, scope: !30)
!38 = !DILocation(line: 6, column: 17, scope: !30)
!39 = !DILocation(line: 6, column: 37, scope: !30)
!40 = !DILocation(line: 6, column: 57, scope: !30)
!41 = !DILocation(line: 6, column: 42, scope: !30)
!42 = !DILocation(line: 6, column: 52, scope: !30)
!43 = !DILocation(line: 0, scope: !30)
!44 = !DILocation(line: 7, column: 3, scope: !7)
