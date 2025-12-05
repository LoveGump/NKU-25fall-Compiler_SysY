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
define i32 @f(ptr %reg_1)
{
Block0: ; f.entry
	%reg_2 = add i32 1, 0
	%reg_3 = getelementptr i32, ptr %reg_1, i32 %reg_2
	%reg_4 = add i32 10, 0
	store i32 %reg_4, ptr %reg_3
	%reg_5 = add i32 2, 0
	ret i32 %reg_5
Block1: ; return.dead
	ret i32 0
}

define i32 @main()
{
Block0: ; main.entry
	%reg_1 = add i32 0, 0
	ret i32 %reg_1
Block1: ; return.dead
	ret i32 0
}
