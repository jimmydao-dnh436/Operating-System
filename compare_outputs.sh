#!/bin/bash
export PATH=/usr/bin:$PATH
for t in os_0_mlq_paging os_1_mlq_paging os_1_mlq_paging_small_1K os_1_mlq_paging_small_4K os_1_singleCPU_mlq os_1_singleCPU_mlq_paging os_2_mlq_paging os_2_singleCPU_mlq_paging os_sc os_syscall os_syscall_list sched sched_0 sched_1; do
    echo "=========================================="
    echo "Comparing $t..."
    diff -w output_test/"$t".output output_fixed/"$t".output
done
