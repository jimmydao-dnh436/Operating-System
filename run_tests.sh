#!/bin/bash
export PATH=/c/msys64/mingw64/bin:/usr/bin:$PATH
mkdir -p output_test
for t in os_0_mlq_paging os_1_mlq_paging os_1_mlq_paging_small_1K os_1_mlq_paging_small_4K os_1_singleCPU_mlq os_1_singleCPU_mlq_paging os_2_mlq_paging os_2_singleCPU_mlq_paging os_sc os_syscall os_syscall_list sched sched_0 sched_1; do
    echo "Running $t..."
    ./os "$t" > output_test/"$t".output 2>&1
done
