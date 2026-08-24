# FASTA、FAI 与 metadata 契约

[English](../fai_contract.md)

本文规定 SeqPro 0.2.x 核心库使用的磁盘格式、验证和坐标契约。标准 FAI 始终保持外部
互操作；所有 SeqPro 扩展状态都保存在独立 metadata sidecar 中。

## 标准 FAI

每条记录恰好包含五个 TAB 分隔字段：

```text
NAME  LENGTH  OFFSET  LINEBASES  LINEWIDTH
```

含义：

- `NAME`：header 中 `>` 后跳过初始 ASCII 空白的第一个 token。
- `LENGTH`：序列碱基总数。
- `OFFSET`：第一个碱基在 FASTA 中的零起始字节偏移。
- `LINEBASES`：标准序列行中的碱基数。
- `LINEWIDTH`：标准序列行包含换行符后的字节数。

为兼容 HTSlib，当最后一条记录只有一个序列行且文件没有末尾换行时，`LINEWIDTH` 仍按
`LINEBASES + 1` 写出；该虚拟换行宽度不会参与 row 0 之外的查询，因为该记录没有下一行。

所有数字为十进制无符号 64 位整数。解析拒绝符号、尾随字符和溢出。FAI 不允许注释、状态
行或扩展列。

以 `YES` 或 `NO` 开头、后接六列记录的历史 RaMAx 私有格式不是标准 FAI。SeqPro 会识别并
拒绝它，并提示从原 FASTA 重新构建。

## FASTA 约束

支持 LF、CRLF、较短的最后一行以及无文件末尾换行。同一序列的非最后序列行必须等长，
序列行换行形式必须一致。不同序列可以使用不同合法行宽和换行形式。

以下输入被拒绝：

- gzip/BGZF magic bytes。
- 空文件、空名称、重复名称和空序列。
- 第一条 header 前的正文。
- 序列内部空行。
- 序列行内 ASCII 空白或 NUL。
- 短序列行后继续出现序列行。
- 同一序列混用 LF 和 CRLF。
- 任何长度、偏移或坐标算术溢出。

SeqPro 不限制 DNA 字母表，不改变大小写，也不替换 IUPAC、蛋白或标点字符。

## sidecar schema 1

默认路径为 `<fai>.seqpro.meta`，内容严格为：

```text
SEQPRO_META\t1
fasta_size_bytes\t<UINT64>
fasta_mtime_ns\t<UINT64>
fasta_xxh3_128\t<32 lowercase hex digits>
fai_xxh3_128\t<32 lowercase hex digits>
record_count\t<UINT64>
total_bases\t<UINT64>
```

sidecar 不保存绝对路径。未知 schema、字段缺失、额外行和 hash 不匹配均被拒绝。没有
sidecar 的标准 FAI 仍可作为 external index 打开。

设置 `IndexedFastaOptions::require_seqpro_metadata` 可以让需要严格来源证明的流水线拒绝
external index。SequenceText 组件不会修改 schema 1，也不会把排除区间或拼接坐标写入
FAI/metadata。

## 构建与原子发布

`BuildFastaIndex()` 对 FASTA 进行一次顺序扫描，不物化完整序列，并在同一遍扫描中计算
XXH3-128。扫描前后对同一个已打开文件执行 `fstat`；文件身份、大小或 mtime 在构建期间发生
变化时拒绝发布。

FAI 和 metadata 先写入目标目录中的唯一临时文件，完成 flush、同步、关闭和重新解析后再
原子 rename。先发布标准 FAI，再发布与最终 FAI 字节绑定的 metadata。进程如果在两次发布
之间退出，留下的 FAI 仍可作为 external index 验证。

- FAI 和 sidecar 都匹配时复用。
- 合法 external FAI 保持原文件并可通过补建 metadata 接管。
- malformed/stale 索引默认拒绝。
- 只有 `FastaIndexBuildOptions::force_rebuild = true` 才允许替换。
- `ValidateFastaIndex()` 永远只读。

## 验证级别

`kFast` 验证 FAI 结构、名称、关键物理 offset、记录边界、文件大小/mtime、FAI hash 和
sidecar 计数，不扫描全部序列正文。

`kFull` 额外重新顺序扫描 FASTA，验证所有 header、行宽、换行和 XXH3-128。
如果文件内容和 FAI 完全未变、只有复制或移动造成 mtime 改变，`kFull` 可通过内容指纹确认
其身份；该只读验证不会改写 sidecar，因此后续 `kFast` 仍会按旧 mtime 报陈旧索引。

`FastaIndexValidationReport::is_fasta_fingerprint_current` 只有在本次 `kFull` 验证实际重算
并匹配 sidecar 中的 FASTA XXH3-128 时才为 true；`kFast` 不宣称完成内容 hash 验证。

## 查询坐标

查询统一使用 0-based 半开区间：

```text
[sequence_start_position,
 sequence_start_position + subsequence_length)
```

非空区间必须满足 `sequence_start_position < sequence_length` 和
`subsequence_length <= sequence_length - sequence_start_position`。长度为零时允许
`sequence_start_position == sequence_length`。所有检查先使用减法形式，避免无符号加法
溢出；越界请求抛出 `SeqProError`，不会静默截断。

所有物理行乘法、文件 offset 加法和文件大小检查均使用 checked arithmetic。

## mmap 与文件生命周期

reader 使用 `mmap(PROT_READ, MAP_PRIVATE)`。映射成功不表示完整文件进入堆内存；映射和索引
在打开后不可变，可以无内部查询锁并发读取。

任何对应的 `IndexedFasta`、`FastaSequenceView` 或 `SequenceChunkRange` 存活期间，不得原地
修改、替换或截断 FASTA、FAI 或 metadata。更新文件前必须先销毁所有 reader，然后显式
重新打开新一代文件。
