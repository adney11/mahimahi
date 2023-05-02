#!/bin/bash -x


runtime=10



path="/newhome/mahimahi/src/tests"
num_flows=1
port_base=44444
kbs=1000

downlink_advlogpath="/newhome/mahimahi/src/tests/log_advlink_advdownlink_"
downlink_logpath="/newhome/mahimahi/src/tests/log_advlink_downlink_"
trace_postfix=".tr"
advlog_postfix=".csv"
stats_postfix=".stats"
plots_postfix=".svg"


$path/test_advlinkshell $path $num_flows $port_base $kbs $runtime

for i in `seq 0 $((num_flows-1))`;
do
    mm-throughput-graph 500 "${downlink_logpath}${i}${trace_postfix}" 2>"${downlink_logpath}${i}${stats_postfix}" 1> "${downlink_logpath}${i}${plots_postfix}"
    mm-adv-graph "${downlink_advlogpath}${i}${advlog_postfix}" 1> "${downlink_advlogpath}${i}${plots_postfix}"
done

echo "Done"

