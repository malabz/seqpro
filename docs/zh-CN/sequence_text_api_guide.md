# SequenceText API 手册

[English](../sequence_text_api_guide.md) · [快速上手](getting_started.md) ·
[SequenceText 契约](sequence_text_layout.md)

`SequenceTextLayout` 是独立可选组件，面向后缀数组、FM-index 和逐轮排除已处理区域的流程。
普通 FASTA 用户只需要 `SeqPro::seqpro`，无需阅读或链接本组件。

## 启用和链接组件

源码树或离线 FetchContent：

```cmake
set(SEQPRO_BUILD_SEQUENCE_TEXT ON)
add_subdirectory(path/to/seqpro)
target_link_libraries(my_app PRIVATE SeqPro::sequence_text)
```

安装包：

```cmake
find_package(SeqPro 0.2 CONFIG REQUIRED COMPONENTS SequenceText)
target_link_libraries(my_app PRIVATE SeqPro::sequence_text)
```

显式包含扩展头：

```cpp
#include "seqpro/indexed_fasta.h"
#include "seqpro/sequence_text_layout.h"
```

核心聚合头 `seqpro/seqpro.h` 有意不包含 SequenceText。

## 概念模型

组件使用三种 0-based 坐标：

| 类型 | 含义 |
|---|---|
| `SequencePosition` | 原始 FASTA 记录位置 |
| `ActiveSequencePosition` | 删除 excluded base 后的一条记录内压缩位置 |
| `SequenceTextPosition` | 拼接文本位置，包含控制字节 |

原始区间使用 `[sequence_start_position, sequence_end_position)`；文本区间使用
`text_start_position` 和 `text_length`。排除区间和文本区间都不允许为空。

每个非空 active run 后写 `kSeparatorByte`（`0x01`），全文最后写唯一
`kTerminatorByte`（`0x00`）：

```text
run0 0x01 run1 0x01 ... runN 0x01 0x00
```

剩余 `B` 个碱基和 `R` 个 run 时，`text_size() == B + R + 1`。完全排除的序列不产生空
run 或 separator；全部排除时文本只有一个 terminator。

公开区间和 location 字段：

| 类型 | 字段 |
|---|---|
| `OriginalSequenceInterval` | `sequence_start_position`、`sequence_end_position` |
| `ExcludedSequenceInterval` | `sequence_id`、`sequence_start_position`、`sequence_end_position` |
| `SequenceTextInterval` | `text_start_position`、`text_length` |
| `SequenceTextBaseLocation` | `sequence_id`、`sequence_run_index`、`original_sequence_position`、`active_sequence_position` |
| `SequenceTextSeparatorLocation` | `preceding_sequence_id`、`preceding_run_index` |
| `SequenceTextTerminatorLocation` | 无字段 |
| `LocatedSequenceInterval` | `sequence_id`、`sequence_run_index`、`original_sequence_start_position`、`active_sequence_start_position`、`interval_length` |
| `MaterializedSequenceText` | `sequence_text_bytes`、`layout_generation` |

`SequenceTextLength` 是 64 位文本字节数量，`SequenceTextGeneration` 是 64 位布局版本，
`SequenceRunIndex` 是一条序列内的 32 位 run index。

## 构造 layout

```cpp
seqpro::SequenceTextLayout layout(indexed_fasta);
```

空 `selected_sequence_order` 按 FAI 顺序选择全部记录。非空 vector 选择唯一子集并固定顺序：

```cpp
const auto chr1_id = *indexed_fasta.FindSequenceId("chr1");
const auto chr2_id = *indexed_fasta.FindSequenceId("chr2");
seqpro::SequenceTextLayout layout(indexed_fasta, {chr2_id, chr1_id});
```

非法或重复 ID 会抛错；未选择序列不能添加排除区间。构造已完成初始无排除定案，因此
`is_finalized()` 为 true，`layout_generation()` 为 1。

类只能 move。`indexed_fasta()` 返回内部保留的不可变 reader，`sequence_order()` 返回布局
顺序中的选择 ID。

## 状态和 generation

```text
构造完成/finalized
        |
        | Exclude* 或有效 Clear*
        v
      dirty
        |
        | Finalize()
        v
下一 generation 的 finalized
```

dirty 状态只允许：

- `is_finalized()` 和 `layout_generation()`。
- 原始坐标 `ExcludeInterval()` 和 `ExcludeIntervals()`。
- `ClearExcludedIntervals()` 和 `ClearAllExcludedIntervals()`。
- `Finalize()`。

坐标查询、文本读取、物化和文本坐标排除在 `Finalize()` 成功前抛
`ErrorCode::kInvalidArgument`。clean 状态调用 `Finalize()` 是 no-op。dirty 定案即使合并后
有效区间相同，也增加 generation；旧 generation 的文本坐标不能复用。

## 排除原始坐标区间

### `ExcludeInterval()`

两个重载按 ID 或名称选择：

```cpp
layout.ExcludeInterval(sequence_id, 1000, 1500);
layout.ExcludeInterval("chr2", 200, 900);
```

区间为 `[start, end)`，必须非空、位于选择序列内。调用成功后 layout 变 dirty，不会自动
`Finalize()`。

### `ExcludeIntervals()`

```cpp
layout.ExcludeIntervals({
    {chr1_id, 1000, 1500},
    {chr1_id, 3000, 3500},
    {chr2_id, 200, 900},
});
layout.Finalize();
```

批量输入先全部验证，一个非法区间会拒绝整个调用。`Finalize()` 会排序并合并重叠、相邻、
嵌套和重复区间。

### 清空排除区间

`ClearExcludedIntervals(sequence_id)` 和名称重载清空一条选择记录；
`ClearAllExcludedIntervals()` 清空全部。原集合为空时是 no-op。没有局部 restore API；需要
恢复时先清空该序列，再重新添加仍应保留的排除区间。

## 排除当前文本坐标

`ExcludeTextIntervals()` 把当前 generation 的命中原子转换回原 FASTA 区间：

```cpp
const seqpro::MaterializedSequenceText text = layout.Materialize();
layout.ExcludeTextIntervals(
    text.layout_generation,
    {{1000, 250}, {5000, 100}});
layout.Finalize();
```

要求：layout 当前 finalized；generation 匹配；每个区间非空且完全位于一个 active run。
separator、terminator、跨 run 或越界会拒绝整个 batch。同一轮后缀索引的命中必须一次批量
提交；成功后 layout 已 dirty，不能继续逐条使用旧文本坐标。

## 定案后的区间和计数

| 方法 | 结果 |
|---|---|
| `text_size()` | active base + 每 run 一个 separator + terminator |
| `active_base_count()` | 全部剩余 FASTA 字符数 |
| `active_run_count()` | 非空 active run 数 |
| `ActiveSequenceLength(id)` | 一条序列的压缩剩余长度 |
| `ExcludedBaseCount(id)` | 一条序列排除的原始碱基数 |
| `ActiveIntervalsById(id)` | 原始坐标 active run |
| `ExcludedIntervalsById(id)` | 排序并合并后的 excluded interval |

ID 必须属于当前 layout 的选择集合。返回的 interval vector 是调用方拥有的副本，修改它不会
改变 layout。

## 原始和 active 坐标转换

`FindActiveSequencePosition()` 把原始位置转成压缩 active 位置；原始碱基被排除时返回
`std::nullopt`。非法 ID 或越界原始位置抛错。

`OriginalSequencePosition()` 把有效 active 位置转回原始位置；active 位置必须严格小于
`ActiveSequenceLength(id)`。

```cpp
const auto active_position =
    layout.FindActiveSequencePosition(chr1_id, original_position);
if (active_position) {
  const seqpro::SequencePosition round_trip =
      layout.OriginalSequencePosition(chr1_id, *active_position);
}
```

所有未排除碱基都满足 original → active → original 恒等。

## 转换到 sequence-text 坐标

`FindTextPosition()` 把原始位置转成拼接文本位置；碱基被排除时返回 `std::nullopt`。

`TextPositionFromActive()` 把有效 active 位置转成文本位置，不会返回 separator 或 terminator。

`LocateTextPosition()` 返回 tagged `SequenceTextLocation`：

- `SequenceTextBaseLocation`：sequence ID、run index、原始位置和 active 位置。
- `SequenceTextSeparatorLocation`：前一 sequence ID 和 run index。
- `SequenceTextTerminatorLocation`：不伪造 sequence ID。

```cpp
const seqpro::SequenceTextLocation location =
    layout.LocateTextPosition(text_position);

if (const auto* base_location =
        std::get_if<seqpro::SequenceTextBaseLocation>(&location)) {
  ConsumeBase(base_location->sequence_id,
              base_location->original_sequence_position);
} else if (const auto* separator_location =
               std::get_if<seqpro::SequenceTextSeparatorLocation>(&location)) {
  ConsumeSeparator(separator_location->preceding_sequence_id,
                   separator_location->preceding_run_index);
} else {
  ConsumeTerminator();
}
```

`LocateTextInterval()` 只在非空文本区间完全位于一个 active run 时返回
`LocatedSequenceInterval`，其中包含 sequence ID、run index、原始起点、active 起点和长度。
从控制字节开始、跨 separator/run 或超过末端返回 `std::nullopt`；零长度属于非法参数并抛错。

## 读取和导出文本

`ReadTextByte()` 返回一个 active FASTA 字节、separator 或 terminator，位置必须小于
`text_size()`。active FASTA 中的 `0x00` 和 `0x01` 是保留字节，会被拒绝。

`Materialize()` 精确分配 `text_size()` 字节，返回包含 `sequence_text_bytes` 和
`layout_generation` 的 `MaterializedSequenceText`。字符串含最终 NUL，必须使用
`sequence_text_bytes.size()`，不能使用 `strlen()`。

`CopyTextTo()` 复制完整文本到调用方缓冲区，`destination_size_bytes` 必须精确等于
`text_size()`，非空缓冲区必须为非空指针。

`WriteTo()` 使用有界工作内存流式写完整文本；`transfer_buffer_size_bytes` 必须大于零，输出
失败抛 `ErrorCode::kIoError`。stream 得到原始控制字节，不是可打印 FASTA。

## 完整迭代流程

```cpp
seqpro::IndexedFasta indexed_fasta =
    seqpro::IndexedFasta::Open("reference.fa");
seqpro::SequenceTextLayout layout(indexed_fasta);

for (;;) {
  const seqpro::MaterializedSequenceText text = layout.Materialize();
  const std::vector<seqpro::SequenceTextInterval> hits =
      FindNewMatches(text.sequence_text_bytes);
  if (hits.empty()) {
    break;
  }
  layout.ExcludeTextIntervals(text.layout_generation, hits);
  layout.Finalize();
}
```

SequenceText 不实现后缀数组、BWT 或 FM-index 本身。

## 并发

采用阶段式并发：

```text
单线程修改：Exclude* / Clear* / Finalize
多线程查询：finalized 后的 const 操作
```

类内部没有 mutex。一个 finalized layout 的 const 查询无锁并发安全。不支持并发 mutation、
mutation/query 重叠或两个同时修改者。每次 `Materialize()` 拥有自己的字符串；调用方缓冲区和
stream 仍需要正常外部同步。

精确溢出、保留字节、dirty 错误和控制字节布局见
[SequenceText 契约](sequence_text_layout.md)。

可构建的 C++17 示例：

- [完整排除与 Finalize 流程](../../extensions/sequence_text/examples/sequence_text_usage.cc)
- [坐标与 tagged location 查询](../../extensions/sequence_text/examples/sequence_text_coordinates.cc)
