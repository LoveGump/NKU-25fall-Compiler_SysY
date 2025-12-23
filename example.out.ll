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
@W = global i32 12
@H = global i32 12
@N = global i32 24
@PI = global float 0x400921fb60000000
@TWO_PI = global float 0x401921fb60000000
@MAX_STEP = global i32 10
@MAX_DISTANCE = global float 0x4000000000000000
@EPSILON = global float 0x3eb0c6f7a0000000
@RAND_MAX = global i32 100000006
@seed = global i32 0

; Function Definitions
define i32 @rand()
{
Block0: ; rand.entry
	%reg_1 = load i32, ptr @seed
	%reg_2 = add i32 19980130, 0
	%reg_3 = mul i32 %reg_1, %reg_2
	%reg_4 = add i32 23333, 0
	%reg_5 = add i32 %reg_3, %reg_4
	%reg_6 = add i32 100000007, 0
	%reg_7 = srem i32 %reg_5, %reg_6
	store i32 %reg_7, ptr @seed
	%reg_8 = load i32, ptr @seed
	%reg_9 = add i32 0, 0
	%reg_10 = icmp slt i32 %reg_8, %reg_9
	br i1 %reg_10, label %Block1, label %Block2
Block1: ; if.then
	%reg_11 = load i32, ptr @seed
	%reg_12 = add i32 100000007, 0
	%reg_13 = add i32 %reg_11, %reg_12
	store i32 %reg_13, ptr @seed
	br label %Block2
Block2: ; if.end
	%reg_14 = load i32, ptr @seed
	ret i32 %reg_14
}

define float @my_fabs(float %reg_1)
{
Block0: ; my_fabs.entry
	%reg_4 = add i32 0, 0
	%reg_5 = sitofp i32 %reg_4 to float
	%reg_6 = fcmp ogt float %reg_1, %reg_5
	br i1 %reg_6, label %Block1, label %Block2
Block1: ; if.then
	br label %Block5
Block2: ; if.end
	%reg_9 = fsub float 0x0, %reg_1
	br label %Block5
Block5:
	%reg_10 = phi float [ %reg_1, %Block1 ], [ %reg_9, %Block2 ]
	ret float %reg_10
}

define float @my_sqrt(float %reg_1)
{
Block0: ; my_sqrt.entry
	%reg_5 = add i32 8, 0
	%reg_6 = sitofp i32 %reg_5 to float
	%reg_7 = fdiv float %reg_1, %reg_6
	%reg_8 = fadd float 0x3fe0000000000000, 0x0
	%reg_9 = fadd float %reg_7, %reg_8
	%reg_10 = add i32 2, 0
	%reg_12 = sitofp i32 %reg_10 to float
	%reg_13 = fmul float %reg_12, %reg_1
	%reg_14 = add i32 4, 0
	%reg_16 = sitofp i32 %reg_14 to float
	%reg_17 = fadd float %reg_16, %reg_1
	%reg_18 = fdiv float %reg_13, %reg_17
	%reg_19 = fadd float %reg_9, %reg_18
	%reg_21 = add i32 10, 0
	br label %Block1
Block1: ; while.cond
	%reg_37 = phi i32 [ %reg_34, %Block2 ], [ %reg_21, %Block0 ]
	%reg_36 = phi float [ %reg_31, %Block2 ], [ %reg_19, %Block0 ]
	%reg_23 = icmp ne i32 %reg_37, 0
	br i1 %reg_23, label %Block2, label %Block3
Block2: ; while.body
	%reg_27 = fdiv float %reg_1, %reg_36
	%reg_28 = fadd float %reg_36, %reg_27
	%reg_29 = add i32 2, 0
	%reg_30 = sitofp i32 %reg_29 to float
	%reg_31 = fdiv float %reg_28, %reg_30
	%reg_33 = add i32 1, 0
	%reg_34 = sub i32 %reg_37, %reg_33
	br label %Block1
Block3: ; while.end
	ret float %reg_36
}

define float @p(float %reg_1)
{
Block0: ; p.entry
	%reg_3 = add i32 3, 0
	%reg_5 = sitofp i32 %reg_3 to float
	%reg_6 = fmul float %reg_5, %reg_1
	%reg_7 = add i32 4, 0
	%reg_9 = sitofp i32 %reg_7 to float
	%reg_10 = fmul float %reg_9, %reg_1
	%reg_12 = fmul float %reg_10, %reg_1
	%reg_14 = fmul float %reg_12, %reg_1
	%reg_15 = fsub float %reg_6, %reg_14
	ret float %reg_15
}

define float @my_sin_impl(float %reg_1)
{
Block0: ; my_sin_impl.entry
	%reg_4 = call float @my_fabs(float %reg_1)
	%reg_5 = load float, ptr @EPSILON
	%reg_6 = fcmp ole float %reg_4, %reg_5
	br i1 %reg_6, label %Block1, label %Block2
Block1: ; if.then
	br label %Block5
Block2: ; if.end
	%reg_9 = fadd float 0x4008000000000000, 0x0
	%reg_10 = fdiv float %reg_1, %reg_9
	%reg_11 = call float @my_sin_impl(float %reg_10)
	%reg_12 = call float @p(float %reg_11)
	br label %Block5
Block5:
	%reg_13 = phi float [ %reg_1, %Block1 ], [ %reg_12, %Block2 ]
	ret float %reg_13
}

define float @my_sin(float %reg_1)
{
Block0: ; my_sin.entry
	%reg_4 = load float, ptr @TWO_PI
	%reg_5 = fcmp ogt float %reg_1, %reg_4
	br i1 %reg_5, label %Block4, label %Block3
Block1: ; if.then
	%reg_13 = load float, ptr @TWO_PI
	%reg_14 = fdiv float %reg_1, %reg_13
	%reg_15 = fptosi float %reg_14 to i32
	%reg_18 = load float, ptr @TWO_PI
	%reg_19 = sitofp i32 %reg_15 to float
	%reg_20 = fmul float %reg_19, %reg_18
	%reg_21 = fsub float %reg_1, %reg_20
	br label %Block2
Block2: ; if.end
	%reg_38 = phi float [ %reg_21, %Block1 ], [ %reg_1, %Block4 ]
	%reg_23 = load float, ptr @PI
	%reg_24 = fcmp ogt float %reg_38, %reg_23
	br i1 %reg_24, label %Block5, label %Block6
Block3: ; or.rhs
	%reg_7 = load float, ptr @TWO_PI
	%reg_8 = fsub float 0x0, %reg_7
	%reg_9 = fcmp olt float %reg_1, %reg_8
	br label %Block4
Block4: ; or.end
	%reg_10 = phi i1 [ %reg_9, %Block3 ], [ %reg_5, %Block0 ]
	br i1 %reg_10, label %Block1, label %Block2
Block5: ; if.then
	%reg_26 = load float, ptr @TWO_PI
	%reg_27 = fsub float %reg_38, %reg_26
	br label %Block6
Block6: ; if.end
	%reg_39 = phi float [ %reg_38, %Block2 ], [ %reg_27, %Block5 ]
	%reg_29 = load float, ptr @PI
	%reg_30 = fsub float 0x0, %reg_29
	%reg_31 = fcmp olt float %reg_39, %reg_30
	br i1 %reg_31, label %Block7, label %Block8
Block7: ; if.then
	%reg_33 = load float, ptr @TWO_PI
	%reg_34 = fadd float %reg_39, %reg_33
	br label %Block8
Block8: ; if.end
	%reg_40 = phi float [ %reg_39, %Block6 ], [ %reg_34, %Block7 ]
	%reg_36 = call float @my_sin_impl(float %reg_40)
	ret float %reg_36
}

define float @my_cos(float %reg_1)
{
Block0: ; my_cos.entry
	%reg_4 = load float, ptr @PI
	%reg_5 = add i32 2, 0
	%reg_6 = sitofp i32 %reg_5 to float
	%reg_7 = fdiv float %reg_4, %reg_6
	%reg_8 = fadd float %reg_1, %reg_7
	%reg_9 = call float @my_sin(float %reg_8)
	ret float %reg_9
}

define float @circle_sdf(float %reg_1, float %reg_3, float %reg_5, float %reg_7, float %reg_9)
{
Block0: ; circle_sdf.entry
	%reg_14 = fsub float %reg_1, %reg_5
	%reg_18 = fsub float %reg_3, %reg_7
	%reg_21 = fmul float %reg_14, %reg_14
	%reg_24 = fmul float %reg_18, %reg_18
	%reg_25 = fadd float %reg_21, %reg_24
	%reg_26 = call float @my_sqrt(float %reg_25)
	%reg_28 = fsub float %reg_26, %reg_9
	ret float %reg_28
}

define void @scene(float %reg_1, float %reg_3, ptr %reg_5)
{
Block0: ; scene.entry
	%reg_9 = fadd float 0x3fd99999a0000000, 0x0
	%reg_10 = fadd float 0x3fd99999a0000000, 0x0
	%reg_11 = fadd float 0x3fb99999a0000000, 0x0
	%reg_12 = call float @circle_sdf(float %reg_1, float %reg_3, float %reg_9, float %reg_10, float %reg_11)
	%reg_16 = fadd float 0x3fe3333340000000, 0x0
	%reg_17 = fadd float 0x3fe3333340000000, 0x0
	%reg_18 = fadd float 0x3fa99999a0000000, 0x0
	%reg_19 = call float @circle_sdf(float %reg_1, float %reg_3, float %reg_16, float %reg_17, float %reg_18)
	%reg_22 = fcmp olt float %reg_12, %reg_19
	br i1 %reg_22, label %Block1, label %Block3
Block1: ; if.then
	%reg_23 = add i32 0, 0
	%reg_24 = getelementptr float, ptr %reg_5, i32 %reg_23
	store float %reg_12, ptr %reg_24
	%reg_26 = add i32 1, 0
	%reg_27 = getelementptr float, ptr %reg_5, i32 %reg_26
	%reg_28 = fadd float 0x4008000000000000, 0x0
	store float %reg_28, ptr %reg_27
	br label %Block2
Block2: ; if.end
	ret void
Block3: ; if.else
	%reg_29 = add i32 0, 0
	%reg_30 = getelementptr float, ptr %reg_5, i32 %reg_29
	store float %reg_19, ptr %reg_30
	%reg_32 = add i32 1, 0
	%reg_33 = getelementptr float, ptr %reg_5, i32 %reg_32
	%reg_34 = fadd float 0x0, 0x0
	store float %reg_34, ptr %reg_33
	br label %Block2
}

define float @trace(float %reg_1, float %reg_3, float %reg_5, float %reg_7)
{
Block0: ; trace.entry
	%reg_20 = alloca [2 x float]
	%reg_10 = fadd float 0x0, 0x0
	%reg_12 = add i32 0, 0
	br label %Block1
Block1: ; while.cond
	%reg_52 = phi i32 [ %reg_12, %Block0 ], [ %reg_48, %Block7 ]
	%reg_51 = phi float [ %reg_10, %Block0 ], [ %reg_45, %Block7 ]
	%reg_14 = load i32, ptr @MAX_STEP
	%reg_15 = icmp slt i32 %reg_52, %reg_14
	br i1 %reg_15, label %Block4, label %Block5
Block2: ; while.body
	%reg_24 = fmul float %reg_5, %reg_51
	%reg_25 = fadd float %reg_1, %reg_24
	%reg_29 = fmul float %reg_7, %reg_51
	%reg_30 = fadd float %reg_3, %reg_29
	%reg_32 = getelementptr [2 x float], ptr %reg_20, i32 0
	call void @scene(float %reg_25, float %reg_30, ptr %reg_32)
	%reg_33 = add i32 0, 0
	%reg_34 = getelementptr [2 x float], ptr %reg_20, i32 0, i32 %reg_33
	%reg_35 = load float, ptr %reg_34
	%reg_36 = load float, ptr @EPSILON
	%reg_37 = fcmp olt float %reg_35, %reg_36
	br i1 %reg_37, label %Block6, label %Block7
Block3: ; while.end
	%reg_49 = fadd float 0x0, 0x0
	br label %Block10
Block4: ; and.rhs
	%reg_17 = load float, ptr @MAX_DISTANCE
	%reg_18 = fcmp olt float %reg_51, %reg_17
	br label %Block5
Block5: ; and.end
	%reg_19 = phi i1 [ %reg_15, %Block1 ], [ %reg_18, %Block4 ]
	br i1 %reg_19, label %Block2, label %Block3
Block6: ; if.then
	%reg_38 = add i32 1, 0
	%reg_39 = getelementptr [2 x float], ptr %reg_20, i32 0, i32 %reg_38
	%reg_40 = load float, ptr %reg_39
	br label %Block10
Block7: ; if.end
	%reg_42 = add i32 0, 0
	%reg_43 = getelementptr [2 x float], ptr %reg_20, i32 0, i32 %reg_42
	%reg_44 = load float, ptr %reg_43
	%reg_45 = fadd float %reg_51, %reg_44
	%reg_47 = add i32 1, 0
	%reg_48 = add i32 %reg_52, %reg_47
	br label %Block1
Block10:
	%reg_50 = phi float [ %reg_49, %Block3 ], [ %reg_40, %Block6 ]
	ret float %reg_50
}

define float @sample(float %reg_1, float %reg_3)
{
Block0: ; sample.entry
	%reg_6 = fadd float 0x0, 0x0
	%reg_8 = add i32 0, 0
	br label %Block1
Block1: ; while.cond
	%reg_47 = phi i32 [ %reg_39, %Block2 ], [ %reg_8, %Block0 ]
	%reg_46 = phi float [ %reg_36, %Block2 ], [ %reg_6, %Block0 ]
	%reg_10 = load i32, ptr @N
	%reg_11 = icmp slt i32 %reg_47, %reg_10
	br i1 %reg_11, label %Block2, label %Block3
Block2: ; while.body
	%reg_13 = call i32 @rand()
	%reg_14 = sitofp i32 %reg_13 to float
	%reg_16 = load float, ptr @TWO_PI
	%reg_19 = load i32, ptr @RAND_MAX
	%reg_20 = sitofp i32 %reg_19 to float
	%reg_21 = fdiv float %reg_14, %reg_20
	%reg_22 = sitofp i32 %reg_47 to float
	%reg_23 = fadd float %reg_22, %reg_21
	%reg_24 = fmul float %reg_16, %reg_23
	%reg_25 = load i32, ptr @N
	%reg_26 = sitofp i32 %reg_25 to float
	%reg_27 = fdiv float %reg_24, %reg_26
	%reg_32 = call float @my_cos(float %reg_27)
	%reg_34 = call float @my_sin(float %reg_27)
	%reg_35 = call float @trace(float %reg_1, float %reg_3, float %reg_32, float %reg_34)
	%reg_36 = fadd float %reg_46, %reg_35
	%reg_38 = add i32 1, 0
	%reg_39 = add i32 %reg_47, %reg_38
	br label %Block1
Block3: ; while.end
	%reg_41 = load i32, ptr @N
	%reg_42 = sitofp i32 %reg_41 to float
	%reg_43 = fdiv float %reg_46, %reg_42
	ret float %reg_43
}

define void @write_pgm()
{
Block0: ; write_pgm.entry
	%reg_1 = add i32 80, 0
	call void @putch(i32 %reg_1)
	%reg_2 = add i32 50, 0
	call void @putch(i32 %reg_2)
	%reg_3 = add i32 10, 0
	call void @putch(i32 %reg_3)
	%reg_4 = load i32, ptr @W
	call void @putint(i32 %reg_4)
	%reg_5 = add i32 32, 0
	call void @putch(i32 %reg_5)
	%reg_6 = load i32, ptr @H
	call void @putint(i32 %reg_6)
	%reg_7 = add i32 32, 0
	call void @putch(i32 %reg_7)
	%reg_8 = add i32 255, 0
	call void @putint(i32 %reg_8)
	%reg_9 = add i32 10, 0
	call void @putch(i32 %reg_9)
	%reg_11 = add i32 0, 0
	br label %Block1
Block1: ; while.cond
	%reg_61 = phi i32 [ %reg_11, %Block0 ], [ %reg_51, %Block6 ]
	%reg_58 = phi float [ 0, %Block0 ], [ %reg_57, %Block6 ]
	%reg_56 = phi float [ 0, %Block0 ], [ %reg_55, %Block6 ]
	%reg_54 = phi i32 [ 0, %Block0 ], [ %reg_52, %Block6 ]
	%reg_13 = load i32, ptr @H
	%reg_14 = icmp slt i32 %reg_61, %reg_13
	br i1 %reg_14, label %Block2, label %Block3
Block2: ; while.body
	%reg_16 = add i32 0, 0
	br label %Block4
Block3: ; while.end
	ret void
Block4: ; while.cond
	%reg_60 = phi i32 [ %reg_16, %Block2 ], [ %reg_47, %Block8 ]
	%reg_57 = phi float [ %reg_58, %Block2 ], [ %reg_22, %Block8 ]
	%reg_55 = phi float [ %reg_56, %Block2 ], [ %reg_25, %Block8 ]
	%reg_52 = phi i32 [ %reg_54, %Block2 ], [ %reg_53, %Block8 ]
	%reg_18 = load i32, ptr @W
	%reg_19 = icmp slt i32 %reg_60, %reg_18
	br i1 %reg_19, label %Block5, label %Block6
Block5: ; while.body
	%reg_22 = sitofp i32 %reg_60 to float
	%reg_25 = sitofp i32 %reg_61 to float
	%reg_28 = load i32, ptr @W
	%reg_29 = sitofp i32 %reg_28 to float
	%reg_30 = fdiv float %reg_22, %reg_29
	%reg_32 = load i32, ptr @H
	%reg_33 = sitofp i32 %reg_32 to float
	%reg_34 = fdiv float %reg_25, %reg_33
	%reg_35 = call float @sample(float %reg_30, float %reg_34)
	%reg_36 = fadd float 0x406fe00000000000, 0x0
	%reg_37 = fmul float %reg_35, %reg_36
	%reg_38 = fptosi float %reg_37 to i32
	%reg_40 = add i32 255, 0
	%reg_41 = icmp sgt i32 %reg_38, %reg_40
	br i1 %reg_41, label %Block7, label %Block8
Block6: ; while.end
	%reg_48 = add i32 10, 0
	call void @putch(i32 %reg_48)
	%reg_50 = add i32 1, 0
	%reg_51 = add i32 %reg_61, %reg_50
	br label %Block1
Block7: ; if.then
	%reg_42 = add i32 255, 0
	br label %Block8
Block8: ; if.end
	%reg_53 = phi i32 [ %reg_38, %Block5 ], [ %reg_42, %Block7 ]
	call void @putint(i32 %reg_53)
	%reg_44 = add i32 32, 0
	call void @putch(i32 %reg_44)
	%reg_46 = add i32 1, 0
	%reg_47 = add i32 %reg_60, %reg_46
	br label %Block4
}

define i32 @main()
{
Block0: ; main.entry
	call void @write_pgm()
	%reg_1 = add i32 0, 0
	ret i32 %reg_1
}
