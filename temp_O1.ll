; Function Declarations
declare i32 @getint()
declare i32 @getch()
declare i32 @getarray(ptr)
declare float @getfloat()
declare i32 @getfarray(ptr)
declare void @putint(i32)
declare void @putch(i32)
declare void @putarray(i32, ptr)
declare void @putfloat(float)
declare void @putfarray(i32, ptr)
declare void @_sysy_starttime(i32)
declare void @_sysy_stoptime(i32)
declare void @llvm.memset.p0.i32(ptr, i8, i32, i1)

; Global Variable Declarations


; Function Definitions
define i32 @main()
{
Block0: ; main.entry
	%reg_3 = add i32 1, 0
	%reg_5 = add i32 0, 0
	%reg_6 = icmp sgt i32 %reg_3, %reg_5
	br i1 %reg_6, label %Block1, label %Block3
Block1: ; if.then
	%reg_7 = add i32 1, 0
	br label %Block2
Block2: ; if.end
	%reg_11 = phi i32 [ %reg_7, %Block1 ], [ %reg_9, %Block3 ]
	ret i32 %reg_11
Block3: ; if.else
	%reg_8 = add i32 1, 0
	%reg_9 = sub i32 0, %reg_8
	br label %Block2
}
