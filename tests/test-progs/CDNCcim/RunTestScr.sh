#!/bin/bash


inCim=0
var=10
EndAddress=0x12000018
FileSizes=(1)
optimization="-O3 -Wall -Wconversion -Wno-sign-conversion -DSTD_PRINT_OUTPUT" #-DCHECKPOINT_FI -DSTD_PRINT_OUTPUT
tag=O3stt

echo -e "\t${bold}${blue}Compiling the source code...${normal}"

cd /

fout=app1_bit_indexing
# fout=app2_aes_ebc_enc



g++ $optimization \
	/nv-gem5/tests/test-progs/CDNCcim/$fout.cpp \
	/nv-gem5/tests/test-progs/CDNCcim/cim_api.cpp \
	-o /nv-gem5/tests/test-progs/CDNCcim/$fout.exe \
	-I nv-gem5/include/ -lm5 -L nv-gem5/util/m5/build/x86/out \
	-DNUM_ROWS=$var \
	-DNUM_WEEKS=$var \
	-DinCIM=$inCim \
	-DEndAddress=$EndAddress

cd /nv-gem5/

echo -e "\t${bold}${blue}Running Simulation...${normal}"

build/X86/gem5.opt --stdout-file=out.txt --stderr-file=err.txt --stats-file=stat.txt --debug-flags=CIMDBG --debug-file=debug.txt \
	configs/CDNCcim/system_design_$tag.py "/nv-gem5/tests/test-progs/CDNCcim/$fout.exe" \
	--EndAddress $EndAddress

echo -e "\n\t${bold}${blue}$fout ${blink}is Finished.${normal}"

rm "/nv-gem5/tests/test-progs/CDNCcim/$fout.exe"

echo -e "---------------------------------------"
