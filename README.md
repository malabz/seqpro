# SeqPro

SeqPro 是一个 C++17 未压缩 FASTA 随机访问库。它创建或读取标准五列 FAI，并通过 64 位
Linux/WSL 的只读 mmap 按需访问碱基。打开已有索引时不会扫描 FASTA 正文，也不会把完整
序列加载到堆内存。

当前版本为 `0.1.0`，只负责 FASTA 索引与局部序列读取。全局坐标、序列拼接和遮蔽区间不在
本版本中。

在 `0.x` 阶段，公共 API 会尽量保持源码兼容，但不承诺不同小版本共享库之间的稳定 ABI；
升级后建议重新编译消费者。标准五列 FAI 的外部互操作格式不受这一限制。

## 环境

- 64 位 Linux 或 WSL。
- GCC 9+，或 Clang 10+ 与相应 C++17 标准库。
- CMake 3.20+。
- 输入必须是未压缩、结构规范的 FASTA。

## 构建

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DSEQPRO_BUILD_TOOLS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

SeqPro 不修改父工程的全局 C++ 标准、warning、构建类型或安装前缀。

## 创建和验证索引

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

`.fai` 始终是 Samtools/HTSlib 可读的标准五列文件。`.seqpro.meta` 保存版本、文件大小、
mtime、XXH3-128、记录数和总碱基数，不改变 FAI 格式。

`build` 是显式写操作。`validate` 和 `IndexedFasta::Open()` 只读。已经存在但没有 sidecar
的标准 FAI 可以直接打开；再次执行 `build` 会在完整验证后保留该 FAI 并补建 sidecar。

## CMake 引入

源码树方式：

```cmake
add_subdirectory(path/to/seqpro)
target_link_libraries(my_app PRIVATE SeqPro::seqpro)
```

安装后：

```cmake
find_package(SeqPro 0.1 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE SeqPro::seqpro)
```

SeqPro 的公共 target 会为消费者声明 `cxx_std_17`，但不会设置全局
`CMAKE_CXX_STANDARD`。

## 基本读取

```cpp
#include <iostream>
#include "seqpro/seqpro.h"

int main() {
  const seqpro::IndexedFasta reference =
      seqpro::IndexedFasta::Open("reference.fa");
  const seqpro::FastaSequenceView chromosome =
      reference.SequenceByName("chr1");

  const char base = chromosome.ReadBase(42);
  const std::string region = chromosome.ReadSubsequence(1000, 500);
  std::cout << base << '\n' << region << '\n';
}
```

对于大区间，使用调用方缓冲区或流式接口，避免返回字符串分配：

```cpp
std::vector<char> destination(1024);
chromosome.CopySubsequenceTo(1000, destination.data(), destination.size());
chromosome.WriteSubsequenceTo(1000, 1'000'000, std::cout);
```

`SubsequenceChunks()` 返回不含 FASTA 换行符的 mmap `std::string_view` 块。chunk 不复制
碱基，但独立保存的 `string_view` 不能超过其 reader、sequence view 或 chunk range 的
生命周期。

## 坐标和错误语义

所有序列坐标为 0-based 半开区间 `[start, start + length)`：

- `ReadBase(position)` 要求 `position < sequence_length()`。
- 空区间允许位于序列末端。
- 越界一律抛出 `seqpro::SeqProError`，不静默截断。
- 缺失名称和非法 ID 抛出 `kSequenceNotFound`。
- 返回字节保持 FASTA 原始大小写和字符，不自动清洗或转成 `N`。

## mmap 与内存

映射整个文件不等于读取整个文件。mmap 首先保留虚拟地址范围；实际访问的页面才会进入
RSS/page cache。因此：

- VIRT 可以接近 FASTA 文件大小，这是正常现象。
- 打开时的常驻堆内存只随序列记录数和名称总长度增长。
- 已访问页面进入 RSS 是正常的操作系统缓存行为。
- 64 位地址空间是 v0.1.0 的明确要求。

在 WSL 中，大型 Linux 工作负载放在 WSL Linux 文件系统通常比 `/mnt/c` 或 `/mnt/d`
更快；SeqPro 同时支持 Windows 挂载路径，并对索引发布后的可见性进行有限重试。

可选的微基准只在显式设置 `-DSEQPRO_BUILD_BENCHMARKS=ON` 时构建。使用方式为：

```bash
build/seqpro-benchmark reference.fa chr1 1000000
```

它不会由 CTest 自动运行，也不承诺脱离硬件和文件系统环境的绝对 QPS。

## 线程安全和文件生命周期

打开完成后，reader、索引和映射均不可变。同一个 `IndexedFasta` 或
`FastaSequenceView` 可以被多个线程同时读取，库内部不加查询锁，也不创建线程池。

`FastaSequenceView` 和 `SequenceChunkRange` 共享映射生命周期；原 reader 销毁后 view
仍可读取。reader 存活期间不得原地修改、替换或截断 FASTA/FAI/sidecar。需要更新文件时，
先销毁所有 reader，再显式重建并重新打开。

## 文件格式契约

完整规则见 [docs/fai_contract.md](docs/fai_contract.md)。
