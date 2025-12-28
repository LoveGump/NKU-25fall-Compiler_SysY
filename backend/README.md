# 编译器后端实现说明

下文把具体的目标平台称为一个 Target（例如 `targets/riscv64`、`targets/aarch64`）。本 README 旨在帮助你迅速理解后端的目录、流水线、关键抽象与各阶段职责，并给出新增与实现 Target 的步骤与文件导航。

## 目录总览

- `backend/common`: 通用基础设施
- `backend/dag`: SelectionDAG 及相关工具
- `backend/isel`: 指令选择基础框架
- `backend/mir`: 目标无关的机器 IR（Machine IR）定义与数据结构（如 `m_instruction.*`、`m_function.*`、`m_block.*`、`m_frame_info.h`）
- `backend/ra`: 目标无关的寄存器分配算法实现
- `backend/target`: 目标无关抽象（`target.h`、`target_reg_info.h`、`target_instr_adapter.h`、`registry.*`）
- `backend/targets/<arch>`: 目标相关实现
  - 代码生成 `*_codegen.*`
  - 指令选择 `isel/*`
  - 各阶段 Pass`passes/*`
  - 寄存器/指令适配 `*_reg_info.*`、`*_instr_adapter.*`

## 通用抽象与接口（它们在哪里被用到）

- `backend/target/target_reg_info.h`（TargetRegInfo）
  - 描述目标的寄存器集合与调用约定相关集合（参数寄存器、被调者保存寄存器、保留寄存器等）。
  - 使用位置：寄存器分配阶段需要它来构造可分配寄存器池、识别被调者保存寄存器；栈降低阶段需要其通用信息（如通用临时寄存器）。
- `backend/target/target_instr_adapter.h`（TargetInstrAdapter）
  - 统一抽象目标指令的“语义接口”，如判断是否是调用/返回/分支、枚举使用/定义的寄存器、替换寄存器、插入 reload/spill 等。
  - 使用位置：CFG 构建（识别控制流）、寄存器分配（枚举 USE/DEF、替换虚拟/物理寄存器、插入 reload/spill）。
- `backend/target/registry.h`
  - Target 工厂注册/获取接口。各架构在全局对象构造期注册自身的构造函数，主流程通过名称获取具体 Target。
- `backend/mir/m_frame_info.h`（MFrameInfo）
  - 维护帧内对象的宽度、对齐、偏移；
  - 使用位置：帧降低（计算局部对象偏移）、寄存器分配（创建溢出槽）、栈降低（查询溢出槽偏移、计算最终栈大小）。
- `backend/common/cfg_builder.*`
  - 基于 `TargetInstrAdapter` 搭建 MIR 层的控制流图，为若干分析/清理/RA 提供基础。
- `backend/dag/*`
  - DAGISel 使用的 DAG 构建、合法化与可视化工具。

## 后端流水线

完成后端代码生成需要的最小流水线仅包含四个步骤：

- Instruction Selection（DAG ISel）
- Frame Lowering（Pre‑RA）
- Register Allocation（Linear Scan）
- Stack Lowering（Post‑RA）

各步骤会在后文中再做说明。当前架构下为了不让一个具体的 Target 实现暴露在通用抽象的实现中而使用来注册与工厂类，用于通过 Target 名获取对应的后端实现。

## 栈与帧

- **栈（Stack）**：运行时整个函数的栈使用区间；
- **帧（Frame）**：函数在本次调用中使用的栈框架。由本函数的“传出参数区（Outgoing Args）+ 局部对象（Local Variables）+ 溢出槽（Spill Slots）+ 保存的被调者寄存器（Saved Registers）”构成。

在本实现中，`backend/mir/m_frame_info.h` 的 `BE::MFrameInfo` 负责帧对象的记录与偏移计算：

- 对象类型：`LocalVar`（alloca）、`SpillSlot`（RA 产生）、`OutArg`（传出参数区大小）
- `paramSize_` 表示传出参数区的最大需要空间（由调用点决定）
- `calculateOffsets()` 计算各对象偏移，按顺序：`Outgoing Args` → `Local Variables` → `Spill Slots`；最终对齐到 16 字节
- 最终运行时栈大小：`stackSize = frameSize + savedRegSize`

RISC-V 与 ARMv8 的函数调用约定均要求 Callee 在 Caller 的 栈顶，也就是 sp 寄存器指向的位置开始获取按 8 字节对齐的栈上参数，因此在完成后端实现时可以使用这样的内存布局：
![栈上内存布局][StackImage]

其中前三者（Local Variables，Spill Slots，Saved Registers）本质上都是局部使用到的内存对象，你实际上可以任意调整它们的顺序。

注意：Caller 的 Outgoing Args 同时也是 Callee 的 incoming stack params。当需要在一个函数内使用栈上参数时，一般做法为调整寄存器 sp 前将其值赋给寄存器 fp，之后使用 fp 寄存器来访问栈上参数。

## 各阶段职责与关键点

### 指令选择（DAG ISel）

- 将中端 IR 降低为目标相关的 Machine IR；针对局部对象与栈访问，产生带抽象帧引用（如 `FrameIndexOperand`）的地址/访存节点，把具体偏移的决定延迟到帧降低。
- 当需要访问局部对象（alloca）时，生成带 `FrameIndexOperand` 的地址计算或访存，延迟具体偏移到 Frame Lowering 决定。

实现选项：

- 使用提供的 DAG ISel 流程（`backend/dag/*` 与 `backend/targets/<arch>/isel/*`），按步骤构建与匹配 SelectionDAG；
- 跳过 DAG：直接遍历 IR Module，按语义生成 MIR（不经 DAG）。两种路径的目标都是产出一致的 MIR 与后续可用的 `FrameIndexOperand`。

### 帧降低（Frame Lowering，Pre‑RA）

文件：`backend/targets/<arch>/passes/lowering/frame_lowering.*`

- **处理函数参数**：
  - 根据目标的调用约定，将寄存器传参搬入虚拟寄存器；
  - 超出寄存器的参数位于 Caller 的 Outgoing Args（Callee 的 incoming stack params）。此时需从栈上加载到虚拟寄存器；
- **计算局部对象偏移**：调用 `func->frameInfo.calculateOffsets()`，确定 `LocalVar` 起始偏移（以 `SP + paramSize_` 为基准）；
- **解析 `FrameIndexOperand`**：
  - 若偏移可用单条目标指令的立即数字段表达，则直接用 `SP + offset`；
  - 否则展开为目标相关的多指令地址计算序列（如装载高位 + 加法）。

要点：

- 这里不会处理溢出槽（还没做 RA），因此 `SpillSlot` 的偏移要在 Post‑RA 再定；
- 是否使用 `fp`：仅当存在 incoming stack params 时，在 prologue 中设置 `fp = sp`（见下文 Stack Lowering）。

### 寄存器分配（Register Allocation，Linear Scan）

文件：`backend/ra/linear_scan.cpp`

- 构建 CFG、做 USE/DEF 收集，计算活跃区间；
- 优先选择不与已用物理寄存器活跃区间冲突的寄存器，跨调用的虚寄存器应被分配到被调者保存寄存器；
- 不能分配时：
  - 创建溢出槽（`SpillSlot`） 并插入伪指令 `FILoadInst`/`FIStoreInst` 完成 reload/spill，溢出槽的具体位置交给栈降低计算；
  - 使用 TargetInstrAdapter 在合适位置插入、并将虚寄存器使用/定义替换为物理寄存器或临时物理寄存器。

实现要求与选项：

- RA 的职责：为所有虚寄存器分配物理寄存器；必要时生成 spill/reload；保持调用约定约束；配合后续栈降低完成最终栈大小与保存/恢复。
- 你可以基于当前提供的 `backend/ra/linear_scan.h` 框架补全实现（发布版本可能仅保留骨架/伪代码）；
- 也可以自行实现其他分配器（如图着色），并复用 `TargetRegInfo`、`TargetInstrAdapter`、`CFGBuilder`、`MFrameInfo` 等通用接口。

### 栈降低（Stack Lowering，Post‑RA）

文件：`backend/targets/<arch>/passes/lowering/stack_lowering.*`

- **重新计算帧大小**：再次调用 `frameInfo.calculateOffsets()`，此时包含 RA 创造的 `SpillSlot`；
- **确定需要保存的被调者寄存器**：扫描使用情况，确定需要保存哪些寄存器，并生成对应的保存 / 恢复指令以及调整栈大小；
- **函数前言**：
  - 先以相对当前 `sp` 的负偏移保存需要的 callee‑saved；
  - 再将 `sp` 下调 `stackSize` 以移动到新的栈顶；
- **函数尾声**：
  - 先恢复 `sp`（上调 `stackSize`）；
  - 再按保存顺序以负偏移加载回 callee‑saved；
- **解析溢出伪指令**：将 `FILoadInst`/`FIStoreInst` 替换为具体的目标访存指令。

注意：

- Outgoing Args 固定位于 `SP + 0`（调用约定要求）；其它空间的顺序可以任意调整；
- 只有 Post‑RA 才能知道最终 `SpillSlot` 数量与 `Saved Registers` 集合，因此 `stackSize` 须在此阶段最终确定。

## 关键抽象

- **MFrameInfo（`backend/mir/m_frame_info.h`）**
  - `ObjectKind = LocalVar | SpillSlot | OutArg`
  - `setParamAreaSize()` / `getParamAreaSize()`：传出参数区大小（按 16 对齐）
  - `createLocalObject()` / `createSpillSlot()`：创建对象
  - `getObjectOffset()` / `getSpillSlotOffset()`：查询偏移
  - `calculateOffsets()`：计算帧内所有对象的偏移，返回帧大小（不含 Saved Registers）

- **FrameIndexOperand vs FILoad/FIStore**
  - `FrameIndexOperand`：ISel 阶段用于抽象局部对象地址（LocalVar），在 Frame Lowering（Pre‑RA）具体化为 `SP + offset`
  - `FILoadInst`/`FIStoreInst`：RA 阶段插入的伪指令，表示对溢出槽（SpillSlot）的访存，在 Stack Lowering（Post‑RA）具体化
  - 二者索引空间不同：前者对应 IR 寄存器的局部对象，后者对应 RA 分配的溢出槽

## 如何新增一个 Target

1) 在 `backend/targets/<arch>/` 下创建子目录，需实现一个继承自 BaseTarget 的后端类。随后视你是否需要使用公共接口来决定是否实现上述其它类；
2) 创建一个注册类，并创建一个全局对象用于向工厂类注册你新实现的 Target。
3) 实现后端流水线，通过传入的 LLVM IR Module 来生成对应的目标代码。

## 特别说明（对 ARM）

当前框架在“类型系统与寄存器分配”两个方面对 ARMv8 的支持仍有改进空间。这里给出一些实践建议；RISC‑V 后端同学也可参考，但影响较小。

### 类型系统

核心差异在于操作数位宽与寄存器别名：

- RISC‑V 64 仅有 64 位寄存器，不存在“低 32 位别名”；
- ARMv8 64 存在 `w`（32 位）作为 `x`（64 位）的低 32 位别名；
- 部分指令对不同位宽的行为不同，如 `str x0, [addr]` 写入 8 字节，`str w0, [addr]` 写入 4 字节；
- ARMv8 不允许 `mov w1, x0` 或 `mov x1, w0` 之类的跨位宽直接移动。

一个可行的折中是：遇到位宽不匹配时显式插入类型转换。

- I32 → I64：可使用 ARMv8 提供的零扩展/符号扩展指令（如 `uxtw`/`sxtw`），先扩展到 64 位的临时虚拟寄存器，再参与后续 64 位运算；
- I64 → I32：ARMv8 通常通过使用同一物理寄存器的 `w` 别名来表示截断。鉴于当前通用 RA 接口无法保证将 `v_a_i64` 与其 32 位视图分配到同一对 `xN/wN`，折中做法是预留一个物理寄存器（如 `x10`）专门用于截断：插入 `mov x10, v_a_i64`，随后用 `w10` 参与 32 位运算。

如果你能从根因上解决类型视图与寄存器别名协同的问题（例如在 RA 中引入别名约束），当然更佳。

### 寄存器分配

当前通用 RA 接口未显式建模“寄存器别名与位宽视图”，因此：

- 它不知道 `w10` 与 `x10` 是同一个物理寄存器的不同视图；
- 无法强制 `v_a_i64` 与 `v_a_i32` 分配到同一对 `xN/wN`；
- 由于 `Register` 的比较中包含类型宽度，`v_a_i64` 与 `v_a_i32` 会被视为不同虚拟寄存器，若活跃区间重叠就会被分配到不同位置。

采用前述“显式类型转换”的折中策略，通常可以避免活跃区间层面上的别名问题；若你希望进一步优化（例如在 I64 → I32 时不经由中转寄存器，直接让两种视图绑定到同一物理寄存器），需要在 RA 里引入别名建模或等价约束来解决。额外的，如果你只是想为 ARM 实现一个能解决这个问题的 RA，可以先在 ARM 的 targets 目录下新建一个其专用的分配器，这样可以更灵活的使用 ARM target 的定义。
