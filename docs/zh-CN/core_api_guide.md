# SeqPro Core API 手册

[English](../core_api_guide.md) · [快速上手](getting_started.md) ·
[FASTA/FAI 契约](fai_contract.md)

Core API 负责构建和验证标准 FAI、打开不可变 mmap FASTA reader，并提供五种读取方式。所有
序列坐标均为 0-based，所有区间均为半开区间。除非本文明确说明返回 `optional`，失败时函数
抛出 `SeqProError`。

## 坐标和标识符类型

| 类型 | 表示 | 含义 |
|---|---:|---|
| `SequencePosition` | `uint64_t` | 一条 FASTA 记录内的 0-based 位置 |
| `SequenceLength` | `uint64_t` | 序列字符数量 |
| `SequenceId` | `uint32_t` | 按 FAI 顺序分配的连续 ID |

`FastaIndexEntry` 表示一条五列 FAI 记录及其 `sequence_id`：

| 字段 | 单位和含义 |
|---|---|
| `sequence_id` | 0-based FAI 记录顺序 |
| `sequence_name` | FASTA header 的第一个空白分隔 token |
| `sequence_length` | 序列字符数 |
| `first_base_offset_bytes` | FASTA 文件内字节偏移 |
| `bases_per_line` | 每个完整序列行的字符数 |
| `bytes_per_line` | 每个完整物理行的字节数，包含 LF 或 CRLF |

应用通常只读取这些字段，不应自行复刻物理 offset 算法。

## 构建和验证索引

### `BuildFastaIndex()`

```cpp
seqpro::FastaIndexBuildReport BuildFastaIndex(
    const std::filesystem::path& fasta_path,
    const seqpro::FastaIndexBuildOptions& build_options = {});
```

- **何时使用：** 应用明确允许创建或更新索引文件。
- **输入：** 未压缩 FASTA 和可选目标路径/参数。
- **返回：** 路径、序列/碱基计数和实际执行的 `FastaIndexBuildAction`。
- **写入：** 标准 FAI，默认还写 `<fai>.seqpro.meta`。
- **错误：** I/O、非法 FASTA/FAI、陈旧索引、重复名称、不支持格式和溢出。
- **复杂度：** 创建或重建时顺序扫描 FASTA 一次；索引当前时直接复用。
- **线程：** 指向同一目标的多个 builder 必须由调用方串行化。

```cpp
seqpro::FastaIndexBuildOptions build_options;
build_options.fasta_index_path = "indexes/reference.fai";
build_options.force_rebuild = false;
build_options.write_seqpro_metadata = true;

const seqpro::FastaIndexBuildReport build_report =
    seqpro::BuildFastaIndex("reference.fa", build_options);
```

`FastaIndexBuildAction`：

| 值 | 含义 |
|---|---|
| `kCreated` | 原来没有索引，新建成功 |
| `kReused` | FAI 和 metadata 已经是当前版本 |
| `kAdoptedExternalIndex` | 保留合法 external FAI，并补充 metadata |
| `kRebuilt` | `force_rebuild` 允许替换已有索引 |

安全默认值是 `force_rebuild=false`：非法或陈旧文件会报错，不会静默覆盖。
`write_seqpro_metadata=false` 只关闭 sidecar，FAI 仍是标准格式。

`FastaIndexBuildReport` 字段：

| 字段 | 含义 |
|---|---|
| `build_action` | 一个 `FastaIndexBuildAction` 值 |
| `fasta_path` | 调用方提供的来源路径 |
| `fasta_index_path` | 发布的标准 FAI 路径 |
| `metadata_path` | sidecar 路径；关闭 metadata 时为空 |
| `sequence_count` | 索引记录数 |
| `total_base_count` | 全部序列长度之和 |

### `ValidateFastaIndex()`

```cpp
seqpro::FastaIndexValidationReport ValidateFastaIndex(
    const std::filesystem::path& fasta_path,
    const std::filesystem::path& fasta_index_path = {},
    seqpro::IndexVerificationMode verification_mode =
        seqpro::IndexVerificationMode::kFast);
```

- **何时使用：** 验证必须完全只读。
- **返回：** 来源、最强验证级别、记录计数和指纹状态。
- **写入：** 无；不会修复或接管索引。
- **复杂度：** `kFast` 随记录数增长；`kFull` 扫描并 hash 完整 FASTA。

`IndexVerificationMode`：

- `kFast`：检查 metadata、FAI 结构、计数、已有 hash 和关键物理 offset，不 hash 完整正文。
- `kFull`：额外扫描 FASTA 并重新计算 XXH3-128。

`FastaIndexOrigin`：`kSeqProVerified` 表示 metadata 匹配；`kExternalStandardFai` 表示合法标准
FAI 没有 SeqPro metadata。

`IndexVerificationStatus` 从弱到强为 `kStructureValidated`、`kMetadataValidated` 和
`kFullContentValidated`。

`FastaIndexValidationReport` 的 `has_seqpro_metadata` 表示 sidecar 是否存在；
`is_fasta_fingerprint_current` 只有在当前操作确实建立了内容指纹匹配时才为 true，详见
[验证契约](fai_contract.md#验证级别)。
其他字段为 `index_origin`、`verification_status`、`sequence_count` 和 `total_base_count`，与
上面的枚举和计数直接对应。

## 打开 FASTA

### `IndexedFasta::Open()`

```cpp
static seqpro::IndexedFasta Open(
    const std::filesystem::path& fasta_path,
    const seqpro::IndexedFastaOptions& open_options = {});
```

- **何时使用：** 索引必须已经存在，打开过程必须只读。
- **返回：** 可复制、不可变、共享 mmap 和索引元数据的句柄。
- **错误：** 索引缺失/陈旧/非法、不支持输入、I/O 或溢出。
- **复杂度：** 已有索引时为 O(记录数)，不是 O(碱基数)。
- **线程：** 打开后全部 const 操作可安全并发。

### `IndexedFasta::OpenOrBuildIndex()`

这是 `BuildFastaIndex()` 后调用 `Open()` 的显式便利流程，可能写文件。必须只读的库接口应
暴露 `Open()`。

### `IndexedFastaOptions`

| 字段 | 默认值 | 用途 |
|---|---|---|
| `fasta_index_path` | `<fasta>.fai` | 选择自定义 FAI |
| `file_access_pattern` | `kOperatingSystemDefault` | mmap 页面访问提示 |
| `index_verification_mode` | `kFast` | 打开时快速或完整验证 |
| `require_seqpro_metadata` | `false` | 拒绝没有 metadata 的 external FAI |

`FileAccessPattern::kRandom` 适合稀疏随机查询，`kSequential` 适合批量导出；它们只是操作系统
提示，不改变正确性，也不是缓存保证。

## 查看 `IndexedFasta`

以下访问器不会分配序列正文：

| 方法 | 返回值和生命周期 |
|---|---|
| `fasta_path()` | 打开的 FASTA 路径引用 |
| `fasta_index_path()` | FAI 路径引用 |
| `fasta_index_origin()` | `FastaIndexOrigin` |
| `index_verification_status()` | 最强验证状态 |
| `sequence_count()` | FAI 记录数 |
| `fasta_index_entries()` | 按 FAI 顺序排列的不可变记录引用 |

所有对应 `IndexedFasta` 句柄销毁后，不得继续使用这些返回引用。

## 查找记录和创建 view

### `FindSequenceId()`

```cpp
std::optional<seqpro::SequenceId> FindSequenceId(
    std::string_view sequence_name) const noexcept;
```

名称不存在时返回 `std::nullopt`，不创建临时名称、不抛异常，平均复杂度 O(1)。适合“名称
不存在是正常分支”的逻辑。

### `IndexEntryById()` 和 `IndexEntryByName()`

返回不可变 `FastaIndexEntry&`。ID 非法或名称不存在时抛出
`ErrorCode::kSequenceNotFound`。ID 查询 O(1)，名称查询平均 O(1)。

### `SequenceById()` 和 `SequenceByName()`

返回共享映射生命周期的 `FastaSequenceView`。高频查询时只解析名称一次，然后复用 view：

```cpp
const seqpro::FastaSequenceView chromosome =
    indexed_fasta.SequenceByName("chr1");

for (seqpro::SequencePosition position : query_positions) {
  Consume(chromosome.ReadBase(position));
}
```

## `FastaSequenceView` 元数据

| 方法 | 含义 |
|---|---|
| `sequence_id()` | 连续 FAI-order ID |
| `sequence_name()` | 由不可变 reader 状态支持的非拥有名称 view |
| `sequence_length()` | 序列字符数 |
| `fasta_index_entry()` | 不可变 FAI 记录引用 |

原始 `IndexedFasta` 变量销毁后 view 仍然有效，因为它共享映射所有权。

## 选择读取 API

| 方法 | 是否分配结果 | 适用场景 | 长度 k 的复杂度 |
|---|---:|---|---|
| `ReadBase()` | 否 | 一个随机字符 | O(1) |
| `ReadSubsequence()` | 一个字符串 | 方便地拥有区间 | O(k + 跨行数) |
| `CopySubsequenceTo()` | 否 | 复用调用方缓冲区 | O(k + 跨行数) |
| `WriteSubsequenceTo()` | 固定缓冲区 | 超大区间或流式导出 | O(k + 跨行数) |
| `SubsequenceChunks()` | 不复制序列 | 直接处理 mmap span | 迭代 O(跨行数) |

所有方法原样保留 FASTA 字节；越界时抛错，不会静默截断。

### `ReadBase()`

要求 `sequence_position < sequence_length()`，返回一个原始 FASTA 字节。非法坐标抛
`kSequenceRangeOutOfBounds`。const 并发调用安全。

### `ReadSubsequence()`

分配请求的逻辑长度，移除物理换行。零长度区间的起点允许等于 `sequence_length()`。

### `CopySubsequenceTo()`

`destination_size_bytes` 同时是请求的序列长度；非空目标必须非空指针。每个线程应使用独立
可写缓冲区。

```cpp
std::vector<char> destination_buffer(4096);
chromosome.CopySubsequenceTo(
    1000, destination_buffer.data(), destination_buffer.size());
```

### `WriteSubsequenceTo()`

只写序列字符，不写 header 或物理换行。传输缓冲区大小不能为零；stream 失败抛
`ErrorCode::kIoError`。当完整 `std::string` 过大时优先使用。

### `SubsequenceChunks()`

每个 `SequenceChunk` 包含：

- `sequence_start_position`：chunk 第一个字节的逻辑位置。
- `sequence_bases`：直接指向一个 mmap 物理 span 的 `string_view`，不含换行。

```cpp
const seqpro::SequenceChunkRange chunks =
    chromosome.SubsequenceChunks(1000, 5000);

if (!chunks.empty()) {
  ReserveForApproximately(chunks.estimated_chunk_count());
}

for (const seqpro::SequenceChunk sequence_chunk : chunks) {
  Consume(sequence_chunk.sequence_start_position,
          sequence_chunk.sequence_bases);
}
```

`begin()`/`end()` 提供 forward iterator。`estimated_chunk_count()` 适合容量预留，但不是验证
结果。range 维持 mmap 生命周期；把 `sequence_bases` 单独复制出去不会独立延长生命周期。
range-for 会使用 `SequenceChunkRange::Iterator::operator*`、前置/后置 `operator++` 以及
`operator==`/`operator!=`；调用方通常不需要直接调用这些协议方法。

## 错误

`SeqProError` 继承 `std::runtime_error`。`what()` 尽可能包含操作、路径、记录、行号或坐标；
程序分支读取稳定的 `error_code()`：

| ErrorCode | 典型处理 |
|---|---|
| `kInvalidArgument` | 修正冲突参数、空缓冲区或错误状态 |
| `kIoError` | 报告路径/stream 失败并保留消息 |
| `kInvalidFasta` | 修复或替换非法 FASTA |
| `kInvalidFastaIndex` | 重建非法或物理不一致的 FAI |
| `kStaleFastaIndex` | 重建，或对当前 FASTA 完整验证 |
| `kDuplicateSequenceName` | 重命名重复 FASTA 记录 |
| `kSequenceNotFound` | 处理缺失名称/ID，或改用 `FindSequenceId()` |
| `kSequenceRangeOutOfBounds` | 修正序列、active 或 text 坐标 |
| `kIntegerOverflow` | 拒绝不可表示的文件、区间或缓冲区大小 |
| `kUnsupportedFileFormat` | 解压 gzip/BGZF，或拒绝保留/不支持字节 |

应先捕获 `SeqProError`，再捕获更宽泛的 `std::exception`。不要解析 `what()` 决定程序逻辑。

## 生命周期和并发

- `IndexedFasta` 是可复制的不可变共享句柄。
- `FastaSequenceView` 和 `SequenceChunkRange` 共享映射所有权。
- const 访问无锁，可跨线程安全调用。
- 查询路径没有隐藏线程，也不缓存整条序列。
- 可写缓冲区和输出流属于调用方，需要外部同步。
- 替换、截断或修改 FASTA、FAI、metadata 前，必须销毁全部 reader/view/range。

权威 FASTA 语法、metadata、陈旧索引和物理 offset 规则见
[FASTA、FAI 与 metadata 契约](fai_contract.md)。

可构建的 C++17 示例：

- [索引管理](../../examples/index_management.cc)
- [五种读取方式](../../examples/sequence_read_modes.cc)
- [并发共享读取](../../examples/concurrent_reading.cc)
