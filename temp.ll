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
	%reg_1 = alloca i32
	store i32 0, ptr %reg_1
	%reg_2 = alloca i32
	%reg_3 = add i32 1, 0
	store i32 %reg_3, ptr %reg_2
	%reg_4 = load i32, ptr %reg_2
	%reg_5 = add i32 0, 0
	%reg_6 = icmp sgt i32 %reg_4, %reg_5
	br i1 %reg_6, label %Block1, label %Block3
Block1: ; if.then
	%reg_7 = add i32 1, 0
	store i32 %reg_7, ptr %reg_1
	br label %Block2
Block2: ; if.end
	%reg_10 = load i32, ptr %reg_1
	ret i32 %reg_10
Block3: ; if.else
	%reg_8 = add i32 1, 0
	%reg_9 = sub i32 0, %reg_8
	store i32 %reg_9, ptr %reg_1
	br label %Block2
Block4: ; return.dead
	ret i32 0
}
