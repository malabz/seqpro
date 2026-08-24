# SeqPro v0.2.0 SequenceTextLayout 实施规格与开发计划

## 1. 目标

v0.2.0 在不改变 v1 FASTA 随机访问核心的前提下，增加独立高级组件
`seqpro::SequenceTextLayout`。该组件把选定序列中尚需处理的 active run 虚拟拼成一段
包含 separator 和 terminator 的文本，为后缀数组、BWT、FM-index 和增量比对流程提供稳定
坐标。

本版本统一使用以下术语：

- excluded interval：当前不再参与后续处理的原始序列区间；
- active interval/run：excluded interval 的补集；
- sequence text：active run、separator 和 terminator 组成的索引输入文本。

不使用重复序列遮蔽的语义，也不引入区间文件解析或持久化。

## 2. 独立组件边界

源码位于：

```text
extensions/sequence_text/
├── include/seqpro/sequence_text_layout.h
├── include/seqpro/sequence_text_export.h
├── src/sequence_text_layout.cc
├── tests/
├── examples/
└── benchmarks/
```

CMake 选项 `SEQPRO_BUILD_SEQUENCE_TEXT` 在顶层和子项目中始终默认 `OFF`。关闭时不编译
扩展源码、不创建或安装扩展 target/头文件，也不改变 `libseqpro` 的源文件、符号、依赖和
include path。

启用后创建独立库 `seqpro_sequence_text`，导出为 `SeqPro::sequence_text`，并 PUBLIC 链接
`SeqPro::seqpro`。扩展只能包含 v1 公共头，不访问 `src/` 或核心 PIMPL。扩展有独立导出宏
`SEQPRO_SEQUENCE_TEXT_EXPORT`，核心聚合头不包含扩展头。

安装包只在调用方请求 `COMPONENTS SequenceText` 时加载扩展 export 文件。core-only 安装
请求该组件必须配置失败；未请求时，即使安装中包含扩展，也不定义高级 target。

## 3. 版本兼容

- 项目、版本头、CLI 和包版本升级为 0.2.0。
- `SeqPro::seqpro` v1 公共 API 保持源码兼容。
- 五列 FAI 和 `.fai.seqpro.meta` schema 1 不变。
- `seqpro-index` 命令和参数不变。
- v2 复用 `SeqProError` 与既有最接近错误码，不扩展核心 `ErrorCode`。
- 0.x 不承诺跨小版本共享库 ABI，消费者升级后重新编译。

## 4. 公共坐标和结果类型

公共类型包括：

```cpp
using SequenceTextPosition = std::uint64_t;
using SequenceTextLength = std::uint64_t;
using ActiveSequencePosition = std::uint64_t;
using SequenceTextGeneration = std::uint64_t;
using SequenceRunIndex = std::uint32_t;
```

原始位置、active 位置和 sequence text 位置全部 0-based。所有区间半开，原始区间要求
`start < end`，文本区间要求 `text_length > 0`。

全局位置通过 `SequenceTextLocation` variant 明确区分：

- `SequenceTextBaseLocation`；
- `SequenceTextSeparatorLocation`；
- `SequenceTextTerminatorLocation`。

`LocatedSequenceInterval` 仅表示完全位于一个 active run 的连续原始区间。物化结果
`MaterializedSequenceText` 同时返回 bytes 和 generation，避免旧 suffix array 坐标被用于
新 layout。

## 5. 字节布局

每个非空 active run 后追加 `0x01`，所有 run 后追加唯一 `0x00`：

```text
run0 + 0x01 + run1 + 0x01 + ... + runN + 0x01 + 0x00
```

总长度为 `active_base_count + active_run_count + 1`。完全排除的序列没有占位或 separator；
全部序列完全排除时只有 terminator。FASTA active 区域出现 `0x00` 或 `0x01` 时文本访问
失败，不做替换或转义。

## 6. 状态机和 generation

构造函数按值接收 `IndexedFasta`，可使用全部 FAI 顺序或显式唯一 ID 子集/顺序。构造后自动
完成初始无排除 layout，状态 finalized，generation 为 1。

添加或清空实际存在的排除区间后状态变 dirty。dirty 状态允许继续使用原始坐标修改并调用
`Finalize()`；其他坐标、文本和统计查询必须失败。clean 状态重复 Finalize 为 no-op。

Finalize：

1. 复制待处理区间到局部容器；
2. 按 start/end 排序；
3. 合并重叠、相邻、嵌套和重复区间；
4. 计算原序列补集并丢弃空 run；
5. 计算每序列 active 前缀和；
6. 按 sequence order 计算全局 text 前缀；
7. checked arithmetic 验证 64 位和容器容量；
8. 全部成功后 swap 发布并递增 generation。

失败时保持 dirty，旧查询索引不对外开放。只要经历 dirty 状态，最终 normalized 区间即使
未改变也保守增加 generation。

## 7. 坐标 API

实现：

- original → optional active；excluded base 返回空；
- active → original；
- original → optional text；excluded base 返回空；
- active → text；
- text → tagged location；
- text interval → optional continuous original interval。

每序列 active run 按原始 start 和 active start 排序；全局 run 表按 text start 排序。转换
使用 `upper_bound`，不构建固定步长的巨大采样缓存。

`ExcludeTextIntervals()` 只接受当前 finalized generation。批量内每个区间先验证并转换为
原始坐标；任一无效则对象逻辑状态不变。成功后统一追加并变 dirty。同一轮索引命中必须一次
批量传入。

## 8. 文本访问

- `ReadTextByte()`：O(log R) 定位并读取一个 byte。
- `Materialize()`：精确分配一次，返回 bytes/generation，不缓存结果。
- `CopyTextTo()`：调用方缓冲区大小必须严格等于 text size。
- `WriteTo()`：固定传输缓冲区流式读取 active run，并显式写控制字节。

全部路径校验保留字节、整数上限和流状态。包含 NUL 的物化字符串只能按 `size()` 使用。

## 9. 并发和生命周期

类使用 `std::unique_ptr<State>`，为 move-only。State 按值保存共享只读 `IndexedFasta` 句柄，
原 reader 销毁后 mmap 仍有效。每条选择序列缓存一个 v1 `FastaSequenceView`，热路径不重复
名称哈希或复制共享句柄。

修改阶段单线程串行；finalized 查询阶段可多线程 const 读取。内部不使用 mutex、线程池或
隐藏完整文本缓存。不支持 mutation/query 并发或两个线程同时修改。

## 10. 测试矩阵

功能测试覆盖：

- 单/多序列、默认顺序、子集和重排；
- 头、尾、中间、重叠、相邻、嵌套、重复和整条排除；
- 多个 active run、全部排除和 terminator-only；
- 四种文本访问逐字节一致；
- original/active/text round-trip；
- separator、terminator 和跨 run 区间；
- dirty、幂等 Finalize、保守 generation 和旧 generation 拒绝；
- 批量原始/文本区间的全有或全无语义；
- move 构造/赋值、保留字节、缓冲区和流错误。

随机测试以朴素字符串补集实现为 oracle，至少覆盖 3000 组区间布局和全部生成坐标。并发测试
在 1、2、8、32 线程下执行位置定位、区间定位、字节读取和小型物化。

打包测试覆盖：

- extension OFF/ON；
- static/shared；
- C++17/C++20 consumer；
- add_subdirectory 和 install-tree component find_package；
- core-only 安装缺少组件时的预期配置失败；
- v1 聚合头、target、导出符号和依赖隔离。

ASan、UBSan 和 TSan 分别验证内存、整数未定义行为和只读并发。Clang、clang-format 和
clang-tidy 在环境可用时执行；缺失时明确记录，不伪称通过。

## 11. 性能边界

设选择序列数 `S`、排除区间数 `M`、active run 数 `R`：

- 单条追加摊销 O(1)，批量追加 O(k)；
- Finalize 为 O(M log M + S + R)；
- 每序列坐标转换 O(log run count)；
- 全局文本定位 O(log R)；
- 文本输出 O(active bases + R)；
- 元数据内存 O(S + M + R)，不随 FASTA 总长度增长。

默认 FAI 顺序使用 SequenceId 直接访问紧凑数组；显式子集/重排才建立只包含所选 ID 的哈希
映射。保留字节检测使用 libc `memchr`，避免在物化和流式输出中逐碱基调用检查函数。两项
优化均不改变公共 API 或布局语义。

普通 CI 不使用硬件相关绝对 QPS 门槛。独立 benchmark 只在显式选项下构建，不由 CTest
执行。真实大型基因组和同机回归基线仍需用户单独授权。

## 12. 实施里程碑

### A. 契约和隔离骨架

建立默认 OFF 选项、独立 target/export、component package、公共头和契约文档。完成门是
core-only consumer 不出现扩展符号或头文件。

### B. 区间与 Finalize

实现 sequence order、批量验证、清空、dirty/finalized、合并补集、checked prefix 和
generation。完成门是所有确定性与随机补集结果与 oracle 一致。

### C. 坐标系统

实现 original/active/text 双向关系、tagged 全局位置、连续区间定位和 generation-bound 文本
排除。完成门是所有 active base round-trip，所有控制字节和跨 run 命中不伪造坐标。

### D. 文本访问

实现单字节、物化、精确缓冲区和流式输出。完成门是四条路径逐字节相同并正确检测保留字节
和输出错误。

### E. 并发、打包和文档

完成多线程、sanitizer、静态/共享、C++17/C++20、source/install consumer、README、示例
和 changelog。完成门是 v1 默认构建回归不受影响。

### F. 发布候选

在可用 GCC/Clang Debug/Release 中验证格式、静态分析和导出符号，记录性能基线。未经用户
明确授权，不 commit、push、打标签或运行大型实验。

## 13. 非目标

v0.2 不实现后缀数组/FM-index 本身、区间文件解析、排除状态持久化、局部 Restore、并发
mutation、压缩 FASTA、FASTQ、原生 Windows mmap、远程存储、FAI/metadata 扩展字段、
RaMAx 兼容层或自动迁移。

## 14. 实施状态

截至 2026-08-24，里程碑 A-E 的功能实现与首次公开发布候选工程加固已在工作树中完成：

- `SequenceTextLayout` 公共 API、PIMPL、generation 状态机、紧凑选择索引、active run、
  三套坐标转换及四条文本访问路径均已实现；
- 公共字段已冻结为带单位和坐标语义的名称，例如 `sequence_start_position`、
  `text_start_position` 和 `sequence_text_bytes`，核心、扩展、工具、示例、benchmark 与测试
  中的内部变量也完成领域化命名；
- `Finalize()` 的 active-run 容量估算只累计一次排除区间数量，并使用 checked `size_t`
  算术；随机 oracle、generation、跨 run 拒绝及 1/2/8/32 线程共享查询测试均通过；
- SequenceText 仍为默认关闭的独立 component；extension OFF/ON、静态/共享、C++17/C++20、
  add_subdirectory、离线 FetchContent、安装、安装前缀迁移、缺失组件和 Samtools 互操作均有
  自动门禁；安装后的扩展通过 `$ORIGIN` 解析同目录核心库，不修改父工程的全局 RPATH；
- 项目版本只有 CMake `0.2.0` 一个来源；核心与扩展 SONAME 分别为 `libseqpro.so.0.2` 和
  `libseqpro_sequence_text.so.0.2`，CMake package 使用 `SameMinorVersion`；
- 共享库采用独立 version script、`--no-undefined`、RELRO/NOW 和 stack protector；核心与扩展
  的符号边界、跨 DSO `SeqProError` 捕获及 libabigail v0.2.0 ABI 基线均已验证；
- 本地最终验证已覆盖 GCC/Clang 构建、全量静态与共享 CTest、ASan/UBSan、TSan、clang-tidy、
  GCC analyzer、严格 Doxygen、clang-format、ShellCheck 和四个 fuzzer 的短烟雾运行；完整
  LSan、每个 fuzzer 至少 300 秒以及可复现源码包/API 文档资产均保留为本地发布候选门禁；
- 仓库托管平台的自动化任务已按项目策略移除；质量、fuzz、ABI 和发布候选能力继续由
  CMake、CTest、本地分析工具及 `scripts/` 中的可复现源码包脚本提供；
- 示例和 benchmark 目标已编译，但 benchmark 和大型真实基因组实验没有执行。

当前没有执行 commit、push、版本标签、GitHub Release 或任何下游项目修改。由于源码包脚本
按设计拒绝脏工作树，完整的 300 秒 fuzz 和 commit 归档演练需要在用户授权提交后从本地执行；
正式标签和 Release 仍需单独授权。
