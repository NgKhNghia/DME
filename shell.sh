#!/bin/bash

trap 'kill ${pids[@]}; exit' SIGINT

# g++ application/lamport.cpp -o application/lamport -lpthread -Iframework -Ialgorithm
# g++ application/tokenRing.cpp -o application/tokenRing -lpthread -Iframework -Ialgorithm -no-pie
g++ application/naimiTrehel.cpp -o application/naimiTrehel -lpaho-mqtt3c -lpthread -Iframework -Ialgorithm

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














# #!/bin/bash

# trap 'shutdown' SIGINT

# # Biên dịch ứng dụng
# g++ application/mainTokenRing.cpp -o application/mainTokenRing -lpthread -Iframework -Ialgorithm

# num_params=${1:-3}
# params=()
# pids=()  

# for ((i=1; i<=num_params; i++)); do
#     params+=("$i")
# done

# for param in "${params[@]}"; do
#     ./application/mainTokenRing "$param" &
#     pids+=("$!")  
#     usleep 1
# done

# wait

# shutdown() {
#     for pid in "${pids[@]}"; do
#         kill "$pid"  # Dừng tiến trình
#         usleep 1  # Đợi 100 nanoseconds
#     done
#     exit
# }
