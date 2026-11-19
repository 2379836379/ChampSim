Part 4: Crafting a Hard Trace Benchmark
for Branch Prediction
In this part, you will flip your role from a hardware designer to a software/workload
developer. You are required to design, compile, and generate a custom ChampSim trace that
serves as a stress test for branch predictors.
Background & Objective
Standard benchmarks like sorting or matrix multiplication often exhibit regular loops and
repetitive control flow patterns. However, real-world software—such as database indexing,
cryptosystems, binary parsers, and graph analytics—contains complex, data-dependent, and
highly unpredictable branch behavior.
The workload must be written in C only. Your goal is to write a C program featuring intricate
control flow that maximizes branch prediction difficulty, compile it, and collect a trace file
using ChampSim 's trace collection tools.
Guidelines for Designing Unpredictable Workloads
To create a high-difficulty trace that forces frequent mispredictions across standard
predictors, consider incorporating:
Data-Dependent Branches: Branches whose outcomes depend on unpredictable data
(e.g., pseudo-random number generators, cryptographic hashes, or Collatz conjecture
sequences).
Long-History Dependencies: Control flow patterns where branch outcomes correlate
with events that happened beyond typical global history lengths (k > 64).
Irregular Memory & Pointer Chasing: Unstructured graph traversals (e.g., BFS/DFS on
sparse graphs, skip lists, or unbalanced tree searches).
High-Entropy Conditional Statements: Interleaved conditional logic that induces
heavy aliasing (collisions) in branch target/predictor tables.
Installing Intel PIN
If you are using the course-provided Docker image, Intel PIN is already installed at /pin￾external-4.3-99850-gce5652921-gcc-linux . Set PIN_ROOT as follows and skip the
remaining installation steps in this section:
export PIN_ROOT=/pin-external-4.3-99850-gce5652921-gcc-linux
Set PIN_ROOT again whenever you open a new terminal.
Use the course-provided Linux x86-64 ChampSim package. The commands below require
GCC ( gcc for the C workload and g++ for the tracer), GNU Make, wget , tar , and xz .
Create the installation directory before downloading Intel PIN so that the archive and
extraction commands use the same directory:
mkdir -p "$HOME/tools"
cd "$HOME/tools"
wget https://software.intel.com/sites/landingpage/pintool/downloads/pin￾external-4.3-99850-gce5652921-gcc-linux.tar.gz
tar -xzf pin-external-4.3-99850-gce5652921-gcc-linux.tar.gz
export PIN_ROOT="$HOME/tools/pin-external-4.3-99850-gce5652921-gcc-linux"
test -x "$PIN_ROOT/pin"
PIN_ROOT must point to the extracted package directory containing pin and source/ . If
your downloaded package has a different directory name, adjust PIN_ROOT accordingly. Set
this variable again when opening a new terminal.
Building the ChampSim Tracer
Return to your ChampSim root directory. Replace /path/to/ChampSim below with the actual
path to your course-provided checkout:
cd /path/to/ChampSim
make -C tracer/pin
ls -l tracer/pin/obj-intel64/champsim_tracer.so
The shared library tracer/pin/obj-intel64/champsim_tracer.so is the tool that PIN loads
to record ChampSim traces. The build command follows the official tracer instructions. Keep
the ChampSim root directory as your working directory for all remaining commands.
Compiling Your Workload
Save your workload as hard_workload.c in the ChampSim root directory, then compile and
run it:
gcc -O2 -std=c11 hard_workload.c -o hard_workload
./hard_workload
If your program requires arguments, supply them both in this test run and after
./hard_workload in the tracing command below.
Verify that the program performs its intended computation and runs without crashing. It
must execute enough instructions to produce a full 200M-instruction trace. Compiler
optimizations may remove unused computations or replace conditional branches, so check
that the compiled workload retains the control flow you intend to evaluate. Record your
compiler options and program arguments in the report for reproducibility.
Capturing and Compressing the Trace
A 200M-instruction trace in the course format occupies 12.8 GB before compression.
Ensure that the trace output location has enough free disk space for the raw trace and
compressed output.
Capture and submit 200 million instructions ( 200,000,000 / 200M). Evaluation uses 2M
warmup instructions followed by 100M simulation instructions from this trace; grading
metrics exclude warmup.
Set STUDENT_ID to your actual student ID. The following example writes the trace directly
into traces/ :
STUDENT_ID=123456789
mkdir -p traces
"$PIN_ROOT/pin" -t tracer/pin/obj-intel64/champsim_tracer.so -o
"traces/${STUDENT_ID}_hard.champsimtrace" -s 0 -t 200000000 -- ./hard_workload
Here, the first -t loads the tracer library; the second -t 200000000 sets the number of
instructions to capture. -s 0 starts recording without skipping instructions. The --
separates PIN/tracer options from your workload command. Check that tracing completes at
the requested instruction count; a program that exits early may produce a shorter trace and
must be adjusted before submission.
Compress the trace and check the compressed file's integrity:
xz -z -T0 "traces/${STUDENT_ID}_hard.champsimtrace"
xz -t "traces/${STUDENT_ID}_hard.champsimtrace.xz"
ls -lh "traces/${STUDENT_ID}_hard.champsimtrace.xz"
xz -z replaces the uncompressed trace with its compressed .xz file. xz -t checks
compression integrity; it does not check the instruction count or whether ChampSim can
simulate the trace. The final filename must be <student_id>_hard.champsimtrace.xz , with
<student_id> replaced by your actual student ID.
Validating with ChampSim
In lab2_config.json , set "branch_predictor": "bimodal" , then configure and build the
simulator from the ChampSim root directory:
./config.sh ./lab2_config.json
make -j $(nproc)
bin/champsim --warmup-instructions 2000000 --simulation-instructions 100000000
"traces/${STUDENT_ID}_hard.champsimtrace.xz"
Ensure that the simulation completes without crashing. Record MPKI, Prediction Accuracy,
and CPU IPC from the 100M simulation phase after 2M warmup instructions. This
validation uses the same predictor and instruction lengths as the Part 4 hardness
evaluation. Submit the full compressed 200M-instruction trace, even though grading uses
only 100M simulation instructions.
Grading Policy & Trace Hardness
All submitted traces will be benchmarked against the baseline predictor
( bimodal ) using 2M warmup + 100M simulation instructions. Only statistics
from the 100M simulation phase are used for grading. Part 4 is worth 5 points in
total:
Source Code & Trace Validity (2 pts): Valid C source code and a properly
formatted 200M-instruction trace file ( .champsimtrace.xz ) that runs
without crashing.
Trace Hardness (up to 3 pts): Measured by Branch Mispredictions Per
Kilo-Instructions (MPKI) on bimodal :
MPKI >= 150: 3 points.
MPKI < 150: 3 × MPKI / 150 points, proportional to the achieved MPKI.
Fairness & Validity: Random-number-driven workloads and loops
deliberately designed to create hard-to-predict branches are allowed,
including synthetic stress tests. Empty loops that perform no meaningful
computation, inline assembly traps, and code causing segmentation faults or
other crashes during tracing will be disqualified.

---

本程序使用的分支预测干扰机制与算法（对应 `hard_workload.c`）

程序实现了一个由伪随机位驱动的加权子集校验和算法：每轮产生一个 32 位伪随机数，将它的每一位作为一个独立条件语句的判断依据；第 i 位为 1 时，就把对应的固定权重加入校验和。所有轮次的结果累加后输出。设计目标是让 `bimodal` 预测器持续遇到方向接近均衡、难以通过近期结果判断的分支，同时提高这些分支在指令流中的密度，以增加每千条指令的预测错误次数（MPKI）。

伪随机数生成使用 xorshift32。设内部状态为无符号 32 位整数 `x`，每轮依次执行：

```c
x ^= x << 13;
x ^= x >> 17;
x ^= x << 5;
```

这三个步骤通过移位和异或混合状态位，生成下一轮状态。对于非零初始状态，这组参数的周期为 `2^32 - 1`；在完整周期内，各位的 0、1 出现次数近乎相等。程序拒绝零种子，因为零状态会一直保持为零，使所有数据分支退化为固定结果。默认种子是 `0x6d2b79f5`，也可以通过命令行指定，因此相同参数会产生相同的分支序列和校验和，便于复现实验。

每轮的 32 个条件语句分别检查不同的状态位，其形式为：

```c
if (random_state & (UINT32_C(1) << bit))
    checksum += weight;
```

仓库中的 `bimodal` 根据分支指令地址索引一张二位饱和计数器表：实际跳转时增加计数器，不跳转时减少计数器，并根据计数器所在的高、低区间预测下一次方向。它没有用来识别 xorshift 状态演化的历史机制，也不能直接读取程序的随机数状态。面对这些长期接近 50%/50% 的分支，计数器难以形成持续有效的方向偏好。如果分支结果近似不可由该计数器预测，其错误率就会接近 50%。即使编译器把条件实现为“条件不成立时跳过加法”，方向取反也不会改变这种均衡性。

这里的 xorshift32 是确定性的线性伪随机算法，并不提供真正独立的随机位；不同位之间、不同轮次之间存在数学关联。因此，接近 50% 的错误率是针对简单 `bimodal` 的预期，不能据此声称所有高级历史预测器都无法学习该序列。程序也没有专门构造预测表地址冲突，主要依靠数据分支本身的预测难度。

为了提高 MPKI，程序用宏显式展开 32 次条件判断，而不是在一个逐位循环中反复执行同一条条件语句。这样可以形成 32 个不同地址的数据分支，并省去逐位循环的计数、索引和循环控制指令。一次 xorshift 更新为全部 32 个分支提供输入，每个条件成立后只执行一次权重加法，随机数生成开销得到分摊。每轮外层循环虽然容易预测，但只占一个循环分支。MPKI 的分母是全部动态指令，因此减少辅助指令，与提高数据分支错误率同样重要。

保留机器码中的条件跳转也是设计的一部分。普通的短 `if` 语句可能在 `-O2` 下被 GCC 转换成条件移动或其他无分支运算，导致源代码虽然包含很多条件语句，trace 中却没有对应的难预测分支。为此，`run_workload` 使用 GCC 函数属性 `optimize("no-if-conversion", "no-if-conversion2")` 禁止相关转换，并使用 `noinline` 保持函数边界。本次 GCC 编译后的反汇编检查确认，核心函数保留了 32 个数据条件跳转和 1 个循环条件跳转。该属性针对 GCC；更换编译器或编译选项后应重新检查机器码。

所有条件分支都参与实际计算：被选中的固定权重累加到 `uint32_t` 校验和中，溢出按无符号整数规则对 `2^32` 取模，最终结果通过 `printf` 输出。因此，分支会影响可观察的程序结果，计算不是无意义的空循环。程序不使用内联汇编或非法内存访问，也不依赖运行时输入输出制造开销。

复现实验时使用 `gcc -O2 -std=c11 hard_workload.c -o hard_workload` 编译，然后以 `./hard_workload` 运行默认的 10,000,000 轮。当前构建的默认输出校验和为 `0x0afa670e`，执行长度足够采集 200M 条指令。上述机制说明了设计预期，实际 MPKI、Prediction Accuracy 和 CPU IPC 应以 `bimodal` 在 2M 条预热指令之后、100M 条模拟指令上的统计为准，不能用理论估计代替实测结果。
