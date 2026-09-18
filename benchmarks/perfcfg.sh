# some explainer notes for myself
#stli: store to load interlock, where the CPU store queue cannot store to load forward to the load queue, so it has to commit to l1
# first before the load can proceed

export perf_basic=(
  perf stat --control fifo:/tmp/perf.ctl -D -1 -e L1-dcache-prefetches \
  -e "{cycles,ls_stlf,ls_bad_status2.stli_other,de_dis_dispatch_token_stalls1.store_queue_rsrc_stall,de_dis_dispatch_token_stalls1.load_queue_rsrc_stall,de_dis_dispatch_token_stalls2.int_sch0_token_stall,de_dis_dispatch_token_stalls2.int_sch1_token_stall,de_dis_dispatch_token_stalls2.int_sch2_token_stall,de_dis_dispatch_token_stalls2.int_sch3_token_stall,de_dis_dispatch_token_stalls2.retire_token_stall}" \
  -M "lpm_itlb_ov_insn_bt_l1_miss,lpm_itlb_ov_insn_bt_l2_miss,l1d_miss_rate"
)

export perf_basic2=(
  perf stat --control fifo:/tmp/perf.ctl -D -1 -e L1-dcache-prefetches \
  -e "{cycles,ls_stlf,ls_bad_status2.stli_other,de_dis_dispatch_token_stalls1.store_queue_rsrc_stall,de_dis_dispatch_token_stalls1.load_queue_rsrc_stall,de_dis_dispatch_token_stalls2.int_sch0_token_stall,de_dis_dispatch_token_stalls2.int_sch1_token_stall,de_dis_dispatch_token_stalls2.retire_token_stall}" \
  -M "lpm_itlb_ov_insn_bt_l1_miss,lpm_itlb_ov_insn_bt_l2_miss,l1d_miss_rate"
)

export perf_mem=(perf mem record -g --call-graph dwarf)

export perf_basic_4=(
  perf stat --control fifo:/tmp/perf.ctl -D -1 \
    -e "{cycles,ls_stlf,ls_bad_status2.stli_other,L1-dcache-prefetches,ls_dmnd_fills_from_sys.all,ls_hw_pf_dc_fills.all}" \
    -e "{cycles,de_dis_dispatch_token_stalls2.retire_token_stall,de_dis_dispatch_token_stalls1.store_queue_rsrc_stall,de_dis_dispatch_token_stalls1.load_queue_rsrc_stall,de_dis_dispatch_token_stalls2.int_sch1_token_stall}" \
    -M "lpm_itlb_ov_insn_bt_l1_miss,lpm_itlb_ov_insn_bt_l2_miss,l1d_miss_rate"
)

export stalls=(
  perf stat --control fifo:/tmp/perf.ctl -D -1 \
    -e "{cycles,de_dis_dispatch_token_stalls2.retire_token_stall,de_dis_dispatch_token_stalls1.store_queue_rsrc_stall,de_dis_dispatch_token_stalls1.load_queue_rsrc_stall,de_dis_dispatch_token_stalls1.taken_brnch_buffer_rsrc}" \
    -e "{de_dis_dispatch_token_stalls1.fp_flush_recovery_stall,de_dis_dispatch_token_stalls1.fp_reg_file_rsrc_stall,de_dis_dispatch_token_stalls1.fp_sch_rsrc_stall,de_dis_dispatch_token_stalls1.int_phy_reg_file_rsrc_stall,de_dis_dispatch_token_stalls2.int_sch1_token_stall}" \
    -M "lpm_br_cond_retired,lpm_br_taken_mispred,dtlb_miss_rate,itlb_miss_rate"
)

export record_1=(
perf record --control fifo:/tmp/perf.ctl -D -1 -g --call-graph dwarf \
  -e '{cpu/cycles,period=1000003,name=cycles/,
      cpu/ls_stlf,period=50021,name=stlf/,
      cpu/ls_bad_status2.stli_other,period=10007/,
      cpu/de_dis_dispatch_token_stalls1.store_queue_rsrc_stall,period=250007/,
      cpu/de_dis_dispatch_token_stalls1.load_queue_rsrc_stall,period=100003/,
      cpu/de_dis_dispatch_token_stalls2.int_sch1_token_stall,period=200003/}'
)

export record_2=(
  perf record --control fifo:/tmp/perf.ctl -D -1 -g --call-graph dwarf \
    -e 'cpu/cycles,period=1000003,name=cycles/' \
    -e '{cpu/ls_stlf,period=50021,name=stlf/,
        cpu/ls_bad_status2.stli_other,period=10007,name=stli/,
        cpu/de_dis_dispatch_token_stalls1.store_queue_rsrc_stall,period=250007,name=store_queue_stall/,
        cpu/de_dis_dispatch_token_stalls1.load_queue_rsrc_stall,period=100003,name=load_queue_stall/,
        cpu/de_dis_dispatch_token_stalls2.int_sch1_token_stall,period=200003,name=int_sch1_stall  /}'
  )