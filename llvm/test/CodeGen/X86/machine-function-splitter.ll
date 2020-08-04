; RUN: llc < %s -mtriple=x86_64 -split-machine-functions | FileCheck %s

define void @foo1(i1 zeroext %0) nounwind !prof !1 !section_prefix !2 {
;; Check that cold block is moved to .text.unlikely.
; CHECK-LABEL: foo1:
; CHECK:       .section        .text.unlikely.foo1
; CHECK-NEXT:  foo1.cold:
; CHECK-NOT:   callq   bar
  br i1 %0, label %2, label %4, !prof !4

2:                                                ; preds = %1
  %3 = call i32 @bar()
  br label %6

4:                                                ; preds = %1
  %5 = call i32 @baz()
  br label %6

6:                                                ; preds = %4, %2
  %7 = tail call i32 @qux()
  ret void
}

define void @foo2(i1 zeroext %0) nounwind !prof !1 !section_prefix !3 {
;; Check that function marked unlikely is not split.
; CHECK-LABEL: foo2:
; CHECK-NOT:   foo2.cold:
  br i1 %0, label %2, label %4, !prof !4

2:                                                ; preds = %1
  %3 = call i32 @bar()
  br label %6

4:                                                ; preds = %1
  %5 = call i32 @baz()
  br label %6

6:                                                ; preds = %4, %2
  %7 = tail call i32 @qux()
  ret void
}

define void @foo3(i1 zeroext %0) nounwind !section_prefix !2 {
;; Check that function without profile data is not split.
; CHECK-LABEL: foo3:
; CHECK-NOT:   foo3.cold:
  br i1 %0, label %2, label %4

2:                                                ; preds = %1
  %3 = call i32 @bar()
  br label %6

4:                                                ; preds = %1
  %5 = call i32 @baz()
  br label %6

6:                                                ; preds = %4, %2
  %7 = tail call i32 @qux()
  ret void
}

declare i32 @bar()
declare i32 @baz()
declare i32 @qux()

!1 = !{!"function_entry_count", i64 7}
!2 = !{!"function_section_prefix", !".hot"}
!3 = !{!"function_section_prefix", !".unlikely"}
!4 = !{!"branch_weights", i32 7, i32 0}
