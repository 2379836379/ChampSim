make clean
./config.sh ./lab2_config.json
make -j $(nproc)
bin/champsim --warmup-instructions 2000000 --simulation-instructions 100000000 traces/shell_sort.champsimtrace.xz
bin/champsim --warmup-instructions 2000000 --simulation-instructions 100000000 traces/spfa.champsimtrace.xz