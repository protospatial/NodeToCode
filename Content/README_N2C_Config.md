# NodeToCode 配置层优化变更说明

本文件记录针对 NodeToCode 生成结果进行的“配置层”优化，以实现：
1) 蓝图事件拆分为独立 C++ 方法，而非汇总到单一大函数；
2) 自动生成类成员变量声明（含 UPROPERTY 修饰符）；
3) 输出必要的 `#include` 与蓝图/资产的伪 include 注释。

## 变更内容
- 编辑 Prompt：`Plugins/NodeToCode/Content/Prompting/CodeGen_CPP.md`
  - 新增章节：`Event Structure Preservation - STRICT`
    - 每个蓝图事件对应独立 C++ 方法，禁止合并为单一大函数。
    - 引擎事件以 `override` 形式生成并调用 `Super::`。
    - 自定义事件以 `UFUNCTION(BlueprintCallable)` 形式生成，参数与引脚一一对应。
    - 事件分发器使用 `DECLARE_DYNAMIC_*` 与 `UPROPERTY(BlueprintAssignable)`，通过 `.Broadcast(...)` 触发。
    - `.h` 与 `.cpp` 分别生成声明与实现。
  - 新增章节：`Variable Declaration Synthesis - STRICT`
    - 基于 Blueprint 使用自动推断并在 `.h` 中生成 `UPROPERTY(...)` 成员（精确类型、容器、TODO 注释）。
  - 新增章节：`UPROPERTY Specifiers Mapping - STRICT`
    - 基于使用语义选择 `EditAnywhere/VisibleAnywhere/BlueprintReadWrite/Transient/SaveGame` 等修饰符与 `meta` 标签。
  - 新增章节：`Includes and External References - STRICT`
    - 在实现顶部输出必要 `#include`；为蓝图/资产输出 `// include: /Game/...` 伪包含注释；未知头文件输出占位注释。
  - 新增章节：`Networking & Replication - STRICT`
    - 变量：`UPROPERTY(Replicated)` / `UPROPERTY(ReplicatedUsing=OnRep_Var)` 与 `OnRep_Var()` 生成；构造函数 `SetIsReplicatedByDefault(true)`。
    - 生命周期：声明并实现 `GetLifetimeReplicatedProps(...)`，在 `.cpp` 使用 `DOREPLIFETIME(ThisClass, Var)`；包含 `#include "Net/UnrealNetwork.h"`。
    - RPC：按语义生成 `UFUNCTION(Server|Client|NetMulticast, Reliable|Unreliable)`，并在实现中添加必要的权限校验（如 `HasAuthority()`）。

- 新增参考源文件（仅用于 LLM 参考上下文，不参与编译）：
  - `Plugins/NodeToCode/Content/NodeToCode_Refs/N2C_EventStyleGuide.h`
  - `Plugins/NodeToCode/Content/NodeToCode_Refs/N2C_EventStyleGuide.cpp`

## 使用步骤
1. 在 Project Settings → Plugins → Node to Code 中：
   - 将 `Target Language` 设为 `C++`
   - 将 `Max Translation Depth`（TranslationDepth）调至 `1` 或 `2`
   - 根据偏好选择 Provider/Model（建议：Anthropic Sonnet 或 OpenAI o4-mini）
   - 在 `Reference Source Files` 中添加：
     - `Plugins/NodeToCode/Content/NodeToCode_Refs/N2C_EventStyleGuide.h`
     - `Plugins/NodeToCode/Content/NodeToCode_Refs/N2C_EventStyleGuide.cpp`

2. 重新执行翻译，并在输出目录（默认 `Saved/NodeToCode/Translations` 或自定义目录）对比结果结构。

## 期望效果
- 多个蓝图事件将被转换为多个独立的 C++ 方法（引擎事件 `BeginPlay`/`Tick` 等为 `override`，自定义事件为 `UFUNCTION`）。
- 事件分发器将正确以动态多播代理形式生成与触发。
- 带网络语义的变量与调用会自动输出：`Replicated/ReplicatedUsing`、`OnRep_`、`GetLifetimeReplicatedProps`（含 `DOREPLIFETIME`）、必要的 `#include "Net/UnrealNetwork.h"`，以及 Server/Client/Multicast RPC 声明与实现骨架。
- Blueprint 成员变量会在模型输入中以集中列表的形式提供，便于在同一个 `.h`/`.cpp` 片段中统一生成变量声明，而不是为每个变量拆散成多个脚本片段。
- 函数局部变量现在也会被包含在模型中，以确保正确的声明和默认值。

## Translate Entire Blueprint 行为说明

- 使用工具栏上的 `Translate Entire Blueprint` 时，现在会按「图」拆分为多次请求：
  - 事件图、每个函数图、每个宏图分别序列化为独立的 N2C JSON。
  - 每个 JSON 单独发送给 LLM 服务，降低单次请求的上下文长度，避免触发 DeepSeek/OpenAI 等 Provider 的最大上下文限制错误。
- 输出落盘行为调整为“同一批次集中在一个时间戳目录下”：
  - 每次执行 `Translate Entire Blueprint` 会为当前 Blueprint 生成一个带时间戳的根目录（例如 `Saved/NodeToCode/Translations/MyBP_2025-11-23-16.30.00/`）。
  - 这一轮批次内的所有图（事件图/函数图/宏图）翻译结果都会写入该目录下的子文件夹中（按图名划分子目录），便于统一查看和管理。
 - C++ 生成时的文件命名规则：
   - 每个图依然有自己的子目录：`<时间戳根目录>/<GraphName>/`。
   - 但当目标语言为 C++ 且图类型为 `EventGraph` 时，会使用 Blueprint 类名（`GraphClass`/`metadata.BlueprintClass`）作为 `.h/.cpp` 文件名：
     - 例如：`AMyActor.h` / `AMyActor.cpp`，其中会集中放置：类声明、UPROPERTY 成员、组件声明与构造、构造函数/析构函数等。
   - 其他图（函数图/宏图等）仍旧以图名作为文件前缀，保持原有拆分粒度不变。

## Blueprint 组件（Components）默认值覆盖

- 从现在开始，N2C JSON 顶层会额外包含一个 `components[]` 数组，用于描述当前 Blueprint 中挂载的组件实例，以及**相对于组件类默认值被修改过的属性**：
  - `ComponentName`：该组件在 Blueprint 中的变量名（例如 `MyMesh`）。
  - `ComponentClassName`：组件的类名（例如 `StaticMeshComponent`）。
  - `AttachParentName`：可选，挂载到的父组件/根（例如 `RootComponent` 或某个 SceneComponent 名）。
  - `OverriddenProperties[]`：只包含那些**被蓝图修改过**的属性，结构与 `variables[]` 类似（`Name / Type / TypeName / DefaultValue` 等）。
- 生成 C++ 时，LLM 会参考 `components[]`：
  - 在构造函数中使用 `CreateDefaultSubobject` 创建对应组件，并按 `AttachParentName` 做 `SetupAttachment`；
  - 对每个组件仅对 `OverriddenProperties[]` 中出现的属性生成赋值代码（例如位置、旋转、布尔开关等），不会尝试覆盖所有引擎默认值；
  - 这样可以在保持上下文体积可控的前提下，尽量复现 Blueprint 中对组件默认参数的修改。

## C++ 生成约定补充（构造函数 / 析构函数 / override）

- 在 CodeGen 的 Prompt 中，新增了针对 C++ 类骨架的强约束：
  - 对于每个 Blueprint Class（例如 `AMCMagicCardBase`），要求 LLM 在生成 C++ 时：
    - 为该类补全/生成默认构造函数，用于创建组件（`CreateDefaultSubobject`）并初始化成员变量；
    - 在合适的情况下生成虚析构函数，保证继承链安全；
    - 对于引擎事件（`BeginPlay` / `Tick` / `OnConstruction` 等），使用 `override` 关键字声明，并在实现中调用 `Super::Xxx(...)`；
    - 对 Blueprint 自定义函数，使用 `UFUNCTION(BlueprintCallable, Category="Auto")` 等 native 方式声明，必要时在重写父类虚函数时同样加上 `override`。
- 当用户提供已有的 .h/.cpp 文件时，LLM 会按 Prompt 要求将这些声明/实现**合并进现有类**，而不是重新定义一个重复的类。

## 备注
- `Reference Source Files` 的示例文件仅用于提示 LLM 遵循风格，不会影响项目编译逻辑。
- 如果已有你自己的事件风格示例，建议优先加入你自己的 .h/.cpp 作为参考上下文。

## C++ 生成增强方案与设计说明（对社区贡献摘要）

本节用于向社区简要说明当前针对 Blueprint → C++ 的增强设计与实现状态，便于在开源仓库中讨论与演进。

### 1. N2C JSON 模型扩展：components[]（已实现）

- 在 `FN2CBlueprint` 顶层新增 `components[]` 数组，描述 Blueprint 中挂载的组件实例以及**相对于组件类默认值被修改过的属性**：
  - `ComponentName`：组件在 Blueprint 中的变量名。
  - `ComponentClassName`：组件类名（如 `StaticMeshComponent`、`TextRenderComponent` 等）。
  - `AttachParentName`：可选，挂载到的父组件/根名称（某些版本可能为空）。
  - `OverriddenProperties[]`：仅包含相对于对应组件类 CDO 被修改过的属性，结构沿用 `variables[]`（`Name / Type / TypeName / DefaultValue` 等）。
- 在 `FN2CNodeTranslator::CollectComponentOverrides` 中：
  - 遍历 Blueprint 的 `SimpleConstructionScript`，对每个 `USCS_Node` 的 `ComponentTemplate` 与类 CDO 做属性对比。
  - 仅当某个属性相对 CDO 发生变更时，才将其写入 `OverriddenProperties[]`，从而控制上下文长度并聚焦“真正被修改过的默认值”。

### 2. Prompt 侧 C++ 生成约束（已实现）

对应文件：`Content/Prompting/CodeGen_CPP.md`。

- **事件与函数结构（Event Structure Preservation - STRICT）**：
  - 每个 Blueprint 事件转为独立 C++ 方法，禁止合并为单一大函数。
  - 引擎事件统一以 `override` 形式声明并在实现中调用 `Super::Xxx(...)`。
  - 自定义事件 / Blueprint 函数以 `UFUNCTION(BlueprintCallable, Category="Auto")` 等 native 风格声明。
  - 事件分发器使用 `DECLARE_DYNAMIC_*` + `UPROPERTY(BlueprintAssignable)`，并通过 `.Broadcast(...)` 触发。
  - `.h` 只放声明、`.cpp` 只放实现，保持 Unreal 代码风格。

- **类骨架 / 构造 / 析构（Class Skeleton, Constructor, and Destructor - STRICT）**：
  - 对每个 Blueprint 类（`metadata.BlueprintClass`），要求生成或补全对应的 C++ 类：
    - 默认构造函数：用于创建组件并初始化成员变量。
    - 适当的虚析构函数：保证继承链安全（AActor/APawn/UObject 子类）。
  - 将 `graph_type == "EventGraph"` 对应的翻译视为**该 Blueprint 类的主 C++ 类骨架承载者**：
    - 其 `graphDeclaration` 必须包含：
      - `UCLASS()` 宏。
      - `class <BlueprintClass> : public <BaseClass>` 声明。
      - 成员字段，包括：
        - 由顶层 `variables[]` 推断出的 UPROPERTY 成员。
        - 由 `components[]` 推断出的组件 UPROPERTY 成员。
      - 所有事件 / 函数的 UFUNCTION 声明。
    - 其 `graphImplementation` 必须包含：
      - 构造函数实现（组件 `CreateDefaultSubobject`、`SetupAttachment`、成员初始化）。
      - 需要时的析构函数实现。
      - 与类级别初始化相关的其它逻辑。
  - 当用户提供参考 `.h/.cpp` 时：
    - 不新建重复类，而是将上述声明/实现合并进既有类体和 `.cpp` 文件中，并补齐缺失的构造/析构/方法声明。

- **变量与组件映射（Variable + Component Synthesis - STRICT）**：
  - 顶层 `variables[]`：
    - 视为类成员变量，在 `.h` 中生成统一的 UPROPERTY 成员区块，而不是为每个变量拆开多个脚本片段。
  - `graphs[i].LocalVariables[]`：
    - 视为函数局部变量，在 `graphImplementation` 中的函数体前部声明，并尽可能根据 `DefaultValue` 进行安全初始化。
  - `components[]`：
    - `.h` 中声明组件 UPROPERTY 成员（`USceneComponent*` / `UStaticMeshComponent*` / `UTextRenderComponent*` 等）。
    - 构造函数中使用 `CreateDefaultSubobject` 创建组件并根据 `AttachParentName` 调用 `SetupAttachment`，必要时退化为挂到 `RootComponent`。
    - 仅对 `OverriddenProperties[]` 中出现的属性生成赋值代码，优先使用语义化 `SetXxx` 接口，其次直接属性赋值。

- **网络与包含（Networking & Includes）**：
  - 根据语义推断自动生成：
    - `UPROPERTY(Replicated)` / `UPROPERTY(ReplicatedUsing=OnRep_)`。
    - `OnRep_` 函数及实现。
    - `GetLifetimeReplicatedProps` + `DOREPLIFETIME(ThisClass, Var)`。
    - RPC：`UFUNCTION(Server|Client|NetMulticast, Reliable|Unreliable)` + 权限检查逻辑。
  - 保证 `.cpp` 包含 `#include "Net/UnrealNetwork.h"`，并在文件顶部集中输出伪 include 注释，便于后续人工补完头文件。

### 3. 输出目录与 C++ 文件命名（已实现）

- **批次根目录**：
  - `Translate Entire Blueprint` 时，仍按图拆分为多个请求，每个图各自生成一份 N2C JSON。
  - 同一轮请求的所有图共享一个带时间戳的根目录，例如：
    - `Saved/NodeToCode/Translations/MyBP_2025-11-23-16.30.00/`。
  - 从实现层面看，会先对整个 Blueprint 调用一次 `GenerateFromBlueprint` 生成完整的 `FN2CBlueprint`（包含所有图以及合成出来的 `ClassItSelf` 图），然后按图切片构造“每图一个 Blueprint 视图”的 N2C JSON 发送给 LLM，因此 ClassItSelf 也会参与整蓝图批量翻译流程。

- **每个图的子目录与文件名**：
  - 每个 graph 有自己的子目录：`<时间戳根目录>/<GraphName>/`，内部存放该图的 `.h/.cpp` 以及 `_Notes.txt` 等辅助文件。
  - 实际写盘时会对 `GraphName` 做一次**轻量的文件名安全清洗**：
    - 去掉首尾空格；
    - 将 Windows 不支持的文件名字符（如 `< > : " / \\ | ? *` 等）替换为下划线 `_`；
    - JSON 中的 `graph_name` 保持原样不变，仅文件系统路径使用清洗后的名字。
  - 当前实现中，无论图类型（EventGraph/Function/Macro 等），生成的 C++ 文件均使用**清洗后的 `GraphName`** 作为文件名前缀：
    - 例如：`EventGraph/EventGraph.h`、`SomeFunction/SomeFunction.cpp`。
  - 对于 C++ 的 `ClassItSelf` 图（`graph_type == "ClassItSelf"`）且其 `graph_class` 非空时，除了按图输出 `ClassItSelf/ClassItSelf.*` 以外，还会额外生成一对以类名命名的脚本：
    - `<时间戳根目录>/<GraphClass>/<GraphClass>.h`
    - `<时间戳根目录>/<GraphClass>/<GraphClass>.cpp`
  - 这两份类名脚本仅承载类骨架：
    - UCLASS 与完整 class 声明；
    - 所有 Blueprint 变量对应的 UPROPERTY 成员；
    - 所有 Blueprint 组件（来自 `components[]`）对应的 UPROPERTY 组件指针成员；
    - 构造/析构函数声明与实现；
    - 在构造函数中通过 `CreateDefaultSubobject` 创建组件、根据 `AttachParentName` 调用 `SetupAttachment`、以及对 `OverriddenProperties[]` 中属性的初始化逻辑。
  - 组件的 UPROPERTY 成员与构造阶段的创建/挂接/属性初始化**只会出现在 `<GraphClass>.h/.cpp` 这对类骨架文件中**，不会出现在 `EventGraph` 等其他按图拆分的 `.h/.cpp` 里；
  - `EventGraph` 图本身只负责事件/函数的声明与实现，不再在类目录下生成额外副本，从而保持“类结构 + 组件 + 构造/析构”和“事件逻辑”在输出层面上的清晰分离。

### 4. override/native 风格可配置化（规划中）

- 目前 Prompt 中对事件与函数声明风格是“写死”的：
  - 引擎事件统一使用 `override`。
  - 自定义函数统一使用 `UFUNCTION(BlueprintCallable, Category="Auto")` 等 native 风格。
- 计划在 `UN2CSettings` 中增加配置项（例如 `bUseNativeOverrideStyleForFunctions` 或 `EN2CFunctionStyle`），并在 Prompt 组装时读取：
  - 开启：维持当前“强 native + override”风格。
  - 关闭：使用更简化的函数声明风格，弱化 `override`/`UFUNCTION` 约束，方便某些项目做轻量级迁移。

这一系列设计在不改变 NodeToCode 核心数据模型（`FN2CBlueprint`/`FN2CGraph`）的前提下，通过 Prompt 约定与落盘命名策略，将“类骨架 + 组件 + 构造/析构”聚合到以 Blueprint 类名命名的 C++ 脚本中，同时保留按图拆分的优势，便于后续社区在此基础上迭代。
