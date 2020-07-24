; RUN: llc < %s -mtriple=x86_64-unknown-linux-gnu -split-machine-functions | FileCheck %s -check-prefix=FUNCTION-SPLIT

define void @foo(i1 zeroext %0) nounwind !prof !1 !section_prefix !2 {
; FUNCTION-SPLIT: .section        .text.unlikely.foo,"ax",@progbits
; FUNCTION-SPLIT:foo.cold: 
  br i1 %0, label %2, label %4, !prof !3

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
!3 = !{!"branch_weights", i32 7, i32 0}
