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