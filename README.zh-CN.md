# SeqPro

[![质量检查](https://github.com/malabz/seqpro/actions/workflows/quality.yml/badge.svg)](https://github.com/malabz/seqpro/actions/workflows/quality.yml)

[English](README.md)

SeqPro 是一个使用 C++17 编写的未压缩 FASTA 索引随机访问库。核心库可以创建或读取标准
五列 FAI，并通过只读内存映射按需读取碱基，不需要把完整序列加载到堆内存。已有索引的
打开开销随 FASTA 记录数增长，而不随碱基总数增长。

SeqPro 0.2.0 是首次公开发布候选。默认构建只提供通用 FASTA 访问库。独立、默认关闭的
`SequenceTextLayout` 组件可以把选定序列的 active run 组织为后缀数组或 FM-index
输入文本。只需要 FASTA 索引访问的用户不会编译、安装或链接该组件。

## 支持环境

- x86_64、64 位 Linux 或 WSL。
- GCC 9+，或 Clang 10+ 与系统 libstdc++ ABI。
- CMake 3.20+。
- 未压缩且结构合法的 FASTA 输入。

在相同平台、编译器 ABI、C++ runtime ABI 和构建模式下，0.2.x 补丁版本计划保持源码和
二进制兼容。不同小版本、不同 C++ runtime ABI，或者不同
`_GLIBCXX_USE_CXX11_ABI` 配置之间不承诺二进制兼容，跨越这些边界时应重新编译消费者。

## 构建与测试

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DSEQPRO_BUILD_TOOLS=ON

cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

SeqPro 不覆盖父工程的全局 C++ 标准、warning 策略、构建类型、RPATH 或安装前缀。

## 创建与验证索引

```bash
build/seqpro-index build reference.fa
build/seqpro-index validate reference.fa --full
build/seqpro-index info reference.fa
```

默认生成：

```text
reference.fa
reference.fa.fai
reference.fa.fai.seqpro.meta
```

`.fai` 始终是 Samtools/HTSlib 兼容的标准五列格式。`.seqpro.meta` sidecar 保存版本化
源文件指纹、文件元数据、记录数和总碱基数，不扩展或改变 FAI。

`build` 是显式写操作；`validate` 和 `IndexedFasta::Open()` 只读。没有 SeqPro
sidecar 的合法外部 FAI 可以直接打开。以后执行 `build` 时，SeqPro 会先完整验证并保留
该 FAI，再补充 sidecar。

## 使用 CMake 链接

源码树方式：

```cmake
add_subdirectory(path/to/seqpro)
target_link_libraries(my_app PRIVATE SeqPro::seqpro)
```

安装后：

```cmake
find_package(SeqPro 0.2 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE SeqPro::seqpro)
```

导出的 target 会声明 `cxx_std_17`，但不会设置全局 `CMAKE_CXX_STANDARD`。

## 随机访问示例

```cpp
#include <iostream>
#include <string>
#include <vector>

#include "seqpro/seqpro.h"

int main() {
  const seqpro::IndexedFasta reference =
      seqpro::IndexedFasta::Open("reference.fa");
  const seqpro::FastaSequenceView chromosome =
      reference.SequenceByName("chr1");

  const char sequence_base = chromosome.ReadBase(42);
  const std::string sequence_region =
      chromosome.ReadSubsequence(1'000, 500);

  std::vector<char> destination_buffer(1'024);
  chromosome.CopySubsequenceTo(
      1'000, destination_buffer.data(), destination_buffer.size());
  chromosome.WriteSubsequenceTo(1'000, 1'000'000, std::cout);

  std::cout << sequence_base << '\n' << sequence_region << '\n';
}
```

`SubsequenceChunks()` 返回直接指向 mmap、且不包含 FASTA 换行符的连续
`std::string_view`：

```cpp
for (const seqpro::SequenceChunk sequence_chunk :
     chromosome.SubsequenceChunks(1'000, 500)) {
  Consume(sequence_chunk.sequence_start_position,
          sequence_chunk.sequence_bases);
}
```

chunk range 会维持映射生命周期；单独保存的 `sequence_bases` view 不会独立延长生命周期。

## 坐标与错误语义

序列坐标都是 0-based，区间都是半开区间：

```text
[sequence_start_position,
 sequence_start_position + subsequence_length)
```

- `ReadBase(position)` 要求 `position < sequence_length()`。
- 长度为零的区间可以从序列末端开始。
- 请求不会被静默截断。
- 非法 ID、缺失名称、错误输入、陈旧索引、溢出和越界都会抛出
  `seqpro::SeqProError`，并携带明确的 `ErrorCode`。
- 返回字节保持 FASTA 中的原始大小写和字符；SeqPro 不做字母表归一化，也不替换为 `N`。

## 可选 SequenceText 组件

在源码树或 FetchContent 构建中显式启用：

```cmake
set(SEQPRO_BUILD_SEQUENCE_TEXT ON)
add_subdirectory(path/to/seqpro)
target_link_libraries(my_app PRIVATE SeqPro::sequence_text)
```

从安装包中显式请求：

```cmake
find_package(SeqPro 0.2 CONFIG REQUIRED COMPONENTS SequenceText)
target_link_libraries(my_app PRIVATE SeqPro::sequence_text)
```

普通的 `find_package(SeqPro CONFIG REQUIRED)` 只定义 `SeqPro::seqpro`。如果安装包未构建
可选组件，请求 `SequenceText` 会在 CMake 配置阶段明确失败。

典型迭代流程：

```cpp
#include "seqpro/sequence_text_layout.h"

seqpro::IndexedFasta indexed_fasta =
    seqpro::IndexedFasta::Open("reference.fa");
seqpro::SequenceTextLayout sequence_text_layout(indexed_fasta);

// 构造后已生成无排除区间的 generation 1。
seqpro::MaterializedSequenceText materialized_text =
    sequence_text_layout.Materialize();

sequence_text_layout.ExcludeTextIntervals(
    materialized_text.layout_generation,
    {{1'000, 250}, {5'000, 100}});
sequence_text_layout.Finalize();

seqpro::MaterializedSequenceText next_generation_text =
    sequence_text_layout.Materialize();
```

每个非空 active run 后写入 `0x01`，全文由唯一的 `0x00` 终止。因此
`MaterializedSequenceText::sequence_text_bytes` 含有最终 NUL，必须通过 `size()` 使用，
不能传给 `strlen()`。tagged location 会明确区分碱基、separator 和 terminator。

修改和 `Finalize()` 属于单线程阶段。成功定案后，同一个 layout 的 const 坐标查询和文本
访问可以无锁并发。完整状态机、generation 和坐标规则见
[SequenceText 契约](docs/zh-CN/sequence_text_layout.md)。

## mmap、内存与并发

映射文件不等于把完整文件读入内存。mmap 首先保留虚拟地址空间；页面通常在实际访问时才
进入 RSS 或 page cache。因此：

- VIRT 接近 FASTA 文件大小是正常现象，不代表堆内存用量。
- 打开后的堆内存随记录数和名称总长度增长，不随总碱基数增长。
- 已访问页面进入 RSS 属于正常的操作系统缓存行为。
- 64 位地址空间是明确要求。

`IndexedFasta`、`FastaSequenceView` 和 finalized 的 SequenceText 查询都是不可变的，
不创建隐藏线程，也不在查询路径加锁。sequence view 和 chunk range 共享映射所有权。
任何对应 reader 或 view 存活时，都不得原地修改、替换或截断 FASTA、FAI 或 metadata。

在 WSL 中，大型 Linux 工作负载通常放在 WSL Linux 文件系统比放在 `/mnt/c` 或
`/mnt/d` 更快；两种路径都受支持。

## 可选工程 target

除顶层命令行工具外，非核心 target 都需要显式启用：

| CMake 选项 | 默认值 | 用途 |
|---|---:|---|
| `SEQPRO_BUILD_TOOLS` | 顶层 ON | 构建 `seqpro-index` |
| `SEQPRO_BUILD_SEQUENCE_TEXT` | OFF | 构建独立扩展 |
| `SEQPRO_BUILD_EXAMPLES` | OFF | 构建示例 |
| `SEQPRO_BUILD_BENCHMARKS` | OFF | 构建微基准 |
| `SEQPRO_BUILD_DOCUMENTATION` | OFF | 构建严格 Doxygen HTML |
| `SEQPRO_BUILD_FUZZERS` | OFF | 构建不安装的 Clang libFuzzer target |
| `SEQPRO_WARNINGS_AS_ERRORS` | OFF | 将 SeqPro 自有代码 warning 视为错误 |

benchmark 不会被 CTest 自动运行，也不承诺脱离硬件环境的绝对吞吐。

## 文档

- [FASTA、FAI 与 metadata 契约](docs/zh-CN/fai_contract.md)
- [SequenceText 字节布局与坐标契约](docs/zh-CN/sequence_text_layout.md)
- [贡献与兼容性政策](CONTRIBUTING.md)
- [安全政策](SECURITY.md)
- [发布候选与正式发布流程](docs/releasing.md)
- [变更记录](CHANGELOG.md)

SeqPro 使用 MIT 许可证。内置 xxHash 实现保留 BSD-2-Clause 许可证，详见
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
