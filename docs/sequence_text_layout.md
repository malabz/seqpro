# SequenceTextLayout 契约

本文固定 SeqPro v0.2.0 可选 `SequenceText` 组件的字节布局、坐标系统、状态机、错误语义和
并发边界。核心 FASTA/FAI 契约仍见 `fai_contract.md`。

## 1. 组件边界

`SequenceTextLayout` 只依赖 v1 公共 `IndexedFasta` API，不读取核心库私有头文件。它不解析
区间文件，不保存排除状态，也不实现后缀数组、BWT 或 FM-index。

组件默认不构建：

```cmake
set(SEQPRO_BUILD_SEQUENCE_TEXT ON)
add_subdirectory(path/to/seqpro)
target_link_libraries(app PRIVATE SeqPro::sequence_text)
```

安装后必须显式请求：

```cmake
find_package(SeqPro 0.2 CONFIG REQUIRED COMPONENTS SequenceText)
target_link_libraries(app PRIVATE SeqPro::sequence_text)
```

核心聚合头 `seqpro/seqpro.h` 不包含本组件；使用者显式包含
`seqpro/sequence_text_layout.h`。

## 2. 三种坐标

- `SequencePosition`：v1 原始 FASTA 序列内的 0-based 碱基位置。
- `ActiveSequencePosition`：同一条序列删除全部 excluded interval 后的压缩位置，不含控制
  字节。
- `SequenceTextPosition`：最终拼接文本中的 0-based 位置，包含 separator 和 terminator。

所有输入区间均为半开区间。原始区间表示为 `[sequence_start, sequence_end)`，要求
`sequence_start < sequence_end <= sequence_length`。文本区间表示为
`[text_start, text_start + text_length)`，要求 `text_length > 0`。

## 3. 字节布局

每个非空 active run 后写入一个 separator `0x01`；所有 run 之后写入唯一的 terminator
`0x00`：

```text
run0 0x01 run1 0x01 ... runR-1 0x01 0x00
```

设 active 碱基数为 `B`、run 数为 `R`：

```text
text_size = B + R + 1
```

最后一个 run 后仍有 separator。完全排除的序列不产生空 run 或 separator；全部序列都完全
排除时，文本恰好是一个 `0x00`。

例如原序列 `ABCDEFG` 排除 `[2, 4)` 后，active run 为 `AB` 和 `EFG`，文本为：

```text
AB 0x01 EFG 0x01 0x00
```

这种布局禁止后缀匹配跨越已排除区间。

## 4. tagged 位置

`LocateTextPosition()` 返回 `SequenceTextLocation`：

- 碱基：`SequenceTextBaseLocation`，同时给出 sequence ID、run index、原始位置和 active
  位置。
- separator：`SequenceTextSeparatorLocation`，只指明它前面的 sequence ID 和 run index。
- terminator：`SequenceTextTerminatorLocation`，不伪造 sequence ID。

`LocateTextInterval()` 只接受完全位于同一个 active run 内的非空文本区间。起于控制字节、
跨过 separator、超过文本末端或跨 run 时返回 `std::nullopt`。长度为零属于参数错误并抛出
`SeqProError`。

## 5. 选择和顺序

构造函数的 `sequence_order` 为空时使用全部 FAI 记录顺序。非空时只选择指定 ID，并严格按
传入顺序布局。ID 必须存在且唯一。未选择序列不能添加排除区间，也不参与任何坐标映射。

构造完成后已经定案，初始 generation 为 1；仅需要无排除的拼接坐标时无需再次调用
`Finalize()`。

## 6. 修改和 Finalize

`ExcludeInterval()` 与 `ExcludeIntervals()` 使用原始坐标。批量调用先验证全部输入，再追加；
任一输入无效时不会留下部分区间。

`ExcludeTextIntervals()` 用于同一轮后缀索引产生的文本坐标：

1. layout 必须 finalized。
2. `source_generation` 必须等于当前 generation。
3. 每个区间必须完全位于一个 active run。
4. 全部区间先转换成原始坐标，再一次性追加。
5. 成功后 layout 进入 dirty 状态。

同一轮的多个命中必须一次批量传入。对象 dirty 后不能继续使用旧文本坐标逐条修改。

`Finalize()` 对每条序列排序排除区间，合并重叠和相邻区间，计算补集 active run、active
前缀和与全局文本前缀。所有新索引先在临时容器完成，成功后一次发布。成功时 generation
增加；即使新区间与已有区间完全重合，只要经历 dirty 状态也保守增加。clean 状态重复调用
是 no-op。

`ClearExcludedIntervals()` 清空一条选择序列，`ClearAllExcludedIntervals()` 清空全部。清空
本来就为空的集合是 no-op。v0.2 不提供局部恢复；需要恢复时先清空对应序列，再重新添加应
保留的排除区间。

## 7. dirty 状态

dirty 状态允许：

- `is_finalized()` 和 `layout_generation()`；
- 原始坐标 `ExcludeInterval()` / `ExcludeIntervals()`；
- `ClearExcludedIntervals()` / `ClearAllExcludedIntervals()`；
- `Finalize()`。

坐标查询、区间查询、文本读取、物化和输出均抛出
`SeqProError(ErrorCode::kInvalidArgument, ...)`，错误信息要求先调用 `Finalize()`。库不会
自动定案。

`indexed_fasta()` 和 `sequence_order()` 返回构造后不再变化的身份信息，也可用于诊断
dirty 对象。

## 8. 文本访问

- `ReadTextByte()` 读取一个碱基或控制字节。
- `Materialize()` 一次分配精确长度，返回 bytes 和当前 generation，不建立隐藏缓存。
- `CopyTextTo()` 要求非空 destination 且大小严格等于 `text_size()`。
- `WriteTo()` 使用固定传输缓冲区流式输出。

物化结果包含最终 NUL；必须使用 `bytes.size()`，禁止使用 `strlen()`。FASTA active 区域若
包含 `0x00` 或 `0x01`，文本访问抛出 `kUnsupportedFileFormat`。库不转义、不替换，也不扫描
被排除区间中的保留字节。

## 9. 生命周期和并发

`SequenceTextLayout` 按值持有共享只读 `IndexedFasta` 句柄，因此原 reader 销毁后 layout
仍保持 mmap 生命周期。该类 move-only，不隐式复制区间元数据。

并发采用明确阶段：

```text
修改阶段：单线程 Exclude / Clear / Finalize
查询阶段：Finalize 成功后，多线程 const 查询
```

内部无 mutex、无线程池、无可变文本缓存。多个线程可同时执行坐标映射、字节读取、物化或
输出；每次物化有独立输出内存。并发修改、两个线程同时修改，或 mutation/query 重叠，不在
线程安全契约内。

## 10. 复杂度

设选择序列数为 `S`、排除区间数为 `M`、active run 数为 `R`：

- 单区间追加：摊销 O(1)。
- 批量追加：O(k)，加一次按序列预留。
- Finalize：O(M log M + S + R)。
- 原始/active 坐标转换：O(log 每序列 run 数)。
- 文本位置或区间定位：O(log R)。
- 物化和输出：O(active base 数 + R)。
- 元数据内存：O(S + M + R)，不随 FASTA 总碱基数线性增长。

默认 FAI 顺序直接以连续 SequenceId 访问紧凑数组，不进行哈希；显式子集或重排使用只含
选择 ID 的哈希表。批量文本输出的保留字节检测使用 libc `memchr`，避免逐碱基函数调用，
同时保留首个非法字节的精确原始位置。

## 11. 文件生命周期和错误

layout 存活期间不得原地修改或截断 FASTA、FAI 或 metadata。无效 ID、未选择序列、非法
区间、generation 不匹配、坐标溢出、输出流失败和 dirty 查询均显式失败；不会截断区间、
跳过序列、把 separator 映射到相邻 run，或吞掉异常。
