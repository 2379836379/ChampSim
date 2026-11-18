export PIN_ROOT=/pin-external-4.3-99850-gce5652921-gcc-linux

gcc -O2 -std=c11 hard_workload.c -o hard_workload
./hard_workload

STUDENT_ID=2400013518

"$PIN_ROOT/pin" \
    -t tracer/pin/obj-intel64/champsim_tracer.so \
    -o "traces/${STUDENT_ID}_hard.champsimtrace" \
    -s 0 \
    -t 200000000 \
    -- ./hard_workload

stat -c '%s' "traces/${STUDENT_ID}_hard.champsimtrace"
xz -z -T0 "traces/${STUDENT_ID}_hard.champsimtrace"
xz -t "traces/${STUDENT_ID}_hard.champsimtrace.xz"
ls -lh "traces/${STUDENT_ID}_hard.champsimtrace.xz"

./config.sh ./lab2_config.json
make -j $(nproc)
bin/champsim --warmup-instructions 2000000 --simulation-instructions 100000000 "traces/${STUDENT_ID}_hard.champsimtrace.xz" | tee ./result/part4.txt

