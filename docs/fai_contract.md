# SeqPro v0.1.0 FASTA/FAI 契约

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

## 验证级别

`kFast` 验证 FAI 结构、名称、关键物理 offset、记录边界、文件大小/mtime、FAI hash 和
sidecar 计数，不扫描全部序列正文。

`kFull` 额外重新顺序扫描 FASTA，验证所有 header、行宽、换行和 XXH3-128。
如果文件内容和 FAI 完全未变、只有复制或移动造成 mtime 改变，`kFull` 可通过内容指纹确认
其身份；该只读验证不会改写 sidecar，因此后续 `kFast` 仍会按旧 mtime 报陈旧索引。

`FastaIndexValidationReport::is_fasta_fingerprint_current` 只有在本次 `kFull` 验证实际重算
并匹配 sidecar 中的 FASTA XXH3-128 时才为 true；`kFast` 不宣称完成内容 hash 验证。

## 查询坐标

查询统一使用 0-based 半开区间 `[sequence_start, sequence_start + length)`。非空区间必须
完全位于序列内；长度为零时允许 `sequence_start == sequence_length`。所有检查先使用减法
形式，避免无符号加法溢出。
