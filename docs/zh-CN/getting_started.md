# SeqPro 快速上手

[English](../getting_started.md)

本文帮助具备基本 C++17 和 CMake 经验的使用者，从一个 FASTA 文件开始完成索引、单碱基和
区间查询。需要完整行为时继续阅读 [Core API 手册](core_api_guide.md)。普通 FASTA 访问不需要
可选的 [SequenceText API 手册](sequence_text_api_guide.md)。

## 环境要求

- x86_64、64 位 Linux 或 WSL。
- GCC 9+、Clang 10+ 和 CMake 3.20+。
- 未压缩且结构合法的 FASTA。
- 消费者 target 使用 C++17 或更高版本。

SeqPro 原样保留 FASTA 字节，不自动大写、不限制字母表，也不把字符替换成 `N`。

## 1. 使用 CMake 链接 SeqPro

选择一种引入方式。

### 源码树 `add_subdirectory`

```cmake
add_subdirectory(path/to/seqpro)
target_link_libraries(my_app PRIVATE SeqPro::seqpro)
```

### 离线 `FetchContent`

```cmake
include(FetchContent)

FetchContent_Declare(
  SeqPro
  SOURCE_DIR /absolute/path/to/seqpro
)
FetchContent_MakeAvailable(SeqPro)

target_link_libraries(my_app PRIVATE SeqPro::seqpro)
```

### 安装后的 package

先安装 SeqPro：

```bash
cmake -S /path/to/seqpro -B /path/to/seqpro-build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF \
  -DSEQPRO_BUILD_TOOLS=ON \
  -DCMAKE_INSTALL_PREFIX=/path/to/seqpro-install
cmake --build /path/to/seqpro-build --parallel
cmake --install /path/to/seqpro-build
```

消费者使用：

```cmake
find_package(SeqPro 0.2 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE SeqPro::seqpro)
```

配置消费者时指定：

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/seqpro-install
```

导出的 target 会请求 `cxx_std_17`，但不会修改父工程的全局 C++ 标准。

## 2. 准备 FASTA

创建 `reference.fa`：

```text
>chr1 example chromosome
ACGTACGTACGT
>plasmid
TTGCAACC
```

记录名是 `>` 后第一个由空白分隔的 token，因此索引名为 `chr1` 和 `plasmid`。

## 3. 创建并验证索引

使用命令行工具：

```bash
seqpro-index build reference.fa
seqpro-index validate reference.fa --full
seqpro-index info reference.fa
```

默认生成：

```text
reference.fa
reference.fa.fai
reference.fa.fai.seqpro.meta
```

`.fai` 是 Samtools 兼容的标准五列索引。SeqPro metadata sidecar 保存来源指纹，但不改变
FAI。也可以在 C++ 中使用 `BuildFastaIndex()`，或使用
`IndexedFasta::OpenOrBuildIndex()` 显式完成“创建后打开”。

## 4. 编写第一个完整程序

创建 `main.cc`：

```cpp
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

#include "seqpro/seqpro.h"

int main(int argument_count, char** argument_values) {
  if (argument_count != 2) {
    std::cerr << "usage: fasta_reader FASTA\n";
    return 2;
  }

  try {
    const seqpro::IndexedFasta indexed_fasta =
        seqpro::IndexedFasta::OpenOrBuildIndex(
            std::filesystem::path(argument_values[1]));
    const seqpro::FastaSequenceView chromosome =
        indexed_fasta.SequenceByName("chr1");

    const char sequence_base = chromosome.ReadBase(4);
    const std::string sequence_region =
        chromosome.ReadSubsequence(2, 6);

    std::cout << chromosome.sequence_name() << '\t'
              << chromosome.sequence_length() << '\n'
              << "base[4]\t" << sequence_base << '\n'
              << "region[2,8)\t" << sequence_region << '\n';
    return 0;
  } catch (const seqpro::SeqProError& seqpro_error) {
    std::cerr << "SeqPro error "
              << static_cast<unsigned>(seqpro_error.error_code())
              << ": " << seqpro_error.what() << '\n';
    return 1;
  }
}
```

为安装后的 SeqPro 创建 `CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.20)
project(FastaReader LANGUAGES CXX)

find_package(SeqPro 0.2 CONFIG REQUIRED)

add_executable(fasta_reader main.cc)
target_link_libraries(fasta_reader PRIVATE SeqPro::seqpro)
target_compile_features(fasta_reader PRIVATE cxx_std_17)
```

构建并运行：

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/seqpro-install
cmake --build build --parallel
./build/fasta_reader reference.fa
```

预期输出：

```text
chr1    12
base[4] A
region[2,8)    GTACGT
```

## 5. 理解坐标

所有序列位置均为 0-based，所有区间均为半开区间：

```text
[sequence_start_position,
 sequence_start_position + subsequence_length)
```

`ReadSubsequence(2, 6)` 读取位置 2 到 7。越界时 SeqPro 抛错，不会静默截断。零长度区间
可以从 `sequence_length()` 开始。

## 6. 选择读取 API

| 需求 | API | 结果内存 |
|---|---|---|
| 一个字符 | `ReadBase()` | 返回一个字节 |
| 方便地拥有一个区间 | `ReadSubsequence()` | 分配一个 `std::string` |
| 复用已有缓冲区 | `CopySubsequenceTo()` | 调用方拥有缓冲区 |
| 导出超大区间 | `WriteSubsequenceTo()` | 固定传输缓冲区 |
| 直接消费 mmap span | `SubsequenceChunks()` | 零复制 `string_view` span |

完整的所有权、生命周期、错误和复杂度见
[选择读取 API](core_api_guide.md#选择读取-api)。

## 7. 处理错误

确定性的库错误会抛出 `SeqProError`。程序逻辑读取 `error_code()`，面向用户的日志使用
`what()`：

```cpp
try {
  const auto sequence = indexed_fasta.SequenceByName("missing");
  (void)sequence;
} catch (const seqpro::SeqProError& error) {
  if (error.error_code() == seqpro::ErrorCode::kSequenceNotFound) {
    // 报告记录不存在，不要把它误判为索引损坏。
  }
}
```

当名称不存在属于预期分支时，使用不抛异常的 `FindSequenceId()`。

## 8. mmap、生命周期和线程

- 建立映射不会把完整 FASTA 复制到堆内存。
- 被访问的 file-backed page 进入 RSS 属于正常 mmap/page-cache 行为。
- `IndexedFasta` 和 `FastaSequenceView` 是不可变共享句柄，const 查询可以并发，也没有隐藏线程。
- 原始 `IndexedFasta` 销毁后，已有 `FastaSequenceView` 仍维持映射生命周期。
- `SequenceChunkRange` 存活时维持映射；单独保存的 `string_view` 不会。
- reader 或 view 存活期间不得替换、截断或修改 FASTA、FAI 和 metadata。

## 后续阅读

- [Core API 手册](core_api_guide.md)：全部索引、reader、view、chunk 和错误 API。
- [SequenceText API 手册](sequence_text_api_guide.md)：可选的后缀索引文本布局。
- [FASTA、FAI 与 metadata 契约](fai_contract.md)：权威文件格式行为。
- [SequenceText 契约](sequence_text_layout.md)：权威坐标和状态规则。
- [完整示例](../../examples)：可编译的 Core 示例。
