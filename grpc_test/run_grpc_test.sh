#!/bin/bash

# gRPC Performance Test Runner
echo "=== gRPC Performance Test Results ==="
echo "Date: $(date)"
echo ""

cd /home/zero/work/repo/github/dde-cooperation/grpc_test/build

for i in {1..3}; do
    echo "=== Test Run $i ==="
    
    # Start server in background
    ./grpc_benchmark server &
    SERVER_PID=$!
    
    # Wait for server to start
    sleep 2
    
    # Run client test
    ./grpc_benchmark
    
    # Stop server
    kill $SERVER_PID
    wait $SERVER_PID 2>/dev/null
    
    echo ""
    
    # Wait a bit between runs
    sleep 1
done

echo "All gRPC tests completed."
