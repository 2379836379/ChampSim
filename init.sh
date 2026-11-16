make clean
./config.sh ./lab2_config.json
make -j $(nproc)
python3 ./auto-run.py --output-file ./result/result-11170726.json