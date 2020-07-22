; RUN: llc < %s -mtriple=x86_64-unknown-linux-gnu -split-machine-functions | FileCheck %s -check-prefix=FUNCTION-SPLIT

define void @foo(i1 zeroext) nounwind !section_prefix !1 !prof !2 {
; FUNCTION-SPLIT: .section        .text.unlikely.foo,"ax",@progbits
; FUNCTION-SPLIT:foo.cold: 
  %2 = alloca i8, align 1
  %3 = zext i1 %0 to i8
  store i8 %3, i8* %2, align 1
  %4 = load i8, i8* %2, align 1
  %5 = trunc i8 %4 to i1
  br i1 %5, label %6, label %8, !prof !3

6:                                                ; preds = %1
  %7 = call i32 @bar()
  br label %10

8:                                                ; preds = %1
  %9 = call i32 @baz()
  br label %10

10:                                               ; preds = %8, %6
  %11 = call i32 @qux()
  ret void
}

declare i32 @bar() #1
declare i32 @baz() #1
declare i32 @qux() #1

!1 = !{!"function_section_prefix", !".hot"}
!2 = !{!"function_entry_count", i64 7}
!3 = !{!"branch_weights", i32 7, i32 0}
!4 = !{!"branch_weights", i32 4, i32 3} 
!5 = !{!"branch_weights", i32 3, i32 0} 
