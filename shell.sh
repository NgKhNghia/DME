#!/bin/bash


# handle_exit() {
#     kill ${pids[@]} 
#     g++ sort.cpp -o sort
#     ./sort
#     exit
# }

# trap handle_exit SIGINT
trap 'kill ${pids[@]}; exit' SIGINT

# g++ application/lamport.cpp -o application/lamport -lpthread -Iframework -Ialgorithm
# g++ application/tokenRing.cpp -o application/tokenRing -lpthread -Iframework -Ialgorithm -no-pie
g++ application/naimiTrehel.cpp -o application/naimiTrehel -lpaho-mqttpp3 -lpaho-mqtt3a -lpthread -Iframework -Ialgorithm

num_params=${1:-3}
params=()
pids=()  

for ((i=1; i<=num_params; i++)); do
    params+=("$i")
done

for param in "${params[@]}"; do
    # ./application/lamport "$param" &
    # ./application/tokenRing "$param" &
    ./application/naimiTrehel "$param" &

    pids+=("$!")  
    sleep 0.000001 
done

wait
