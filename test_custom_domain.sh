#!/bin/bash

# Test script for custom domain MAPF server
# This script tests the complete workflow

echo "=== Testing Custom Domain MAPF Server ==="
echo

# Check if server binary exists
if [ ! -f "./build/mapf_server" ]; then
    echo "✗ Server binary not found. Please compile first:"
    echo "  ./compile.sh"
    exit 1
fi

# Check if custom domain files exist
if [ ! -f "./example_problems/custom_domain/myproblem.json" ]; then
    echo "✗ Custom domain files not found"
    exit 1
fi

echo "✓ Custom domain files found"
echo

# Start server in background
echo "Starting MAPF server..."
./build/mapf_server \
  --mapFile ./example_problems/custom_domain/maps/mymap.map \
  --configFile ./configs/mymap.json \
  --port 8080 > server.log 2>&1 &

SERVER_PID=$!
echo "Server started with PID: $SERVER_PID"

# Wait for server to start
echo "Waiting for server to start..."
sleep 3

# Test server health
echo "Testing server health..."
health_response=$(curl -s http://localhost:8080/health)
if [[ $? -eq 0 ]]; then
    echo "✓ Server is healthy"
else
    echo "✗ Server health check failed"
    kill $SERVER_PID 2>/dev/null
    exit 1
fi

# Test task status
echo "Testing task status..."
status_response=$(curl -s http://localhost:8080/task_status)
if [[ $? -eq 0 ]]; then
    echo "✓ Task status endpoint working"
else
    echo "✗ Task status endpoint failed"
fi

# Test adding a task
echo "Testing task addition..."
add_response=$(curl -s -X POST http://localhost:8080/add_task \
  -H "Content-Type: application/json" \
  -d '{"location": 82}')

if [[ $? -eq 0 ]]; then
    status=$(echo "$add_response" | jq -r '.status')
    if [[ "$status" == "success" ]]; then
        echo "✓ Task addition working"
        task_id=$(echo "$add_response" | jq -r '.task_id')
        location=$(echo "$add_response" | jq -r '.location')
        echo "  Added task $task_id at location $location"
    else
        echo "✗ Task addition failed"
        echo "  Response: $add_response"
    fi
else
    echo "✗ Task addition request failed"
fi

# Test continuous operation
echo "Testing continuous operation..."
plan_response=$(curl -s -X POST http://localhost:8080/plan \
  -H "Content-Type: application/json" \
  -d '{"agents": [{"location": 2, "orientation": 0, "timestep": 0}, {"location": 3, "orientation": 0, "timestep": 0}, {"location": 4, "orientation": 0, "timestep": 0}, {"location": 5, "orientation": 0, "timestep": 0}, {"location": 6, "orientation": 0, "timestep": 0}]}')

if [[ $? -eq 0 ]]; then
    status=$(echo "$plan_response" | jq -r '.status')
    if [[ "$status" == "success" ]]; then
        echo "✓ Planning endpoint working"
        timestep=$(echo "$plan_response" | jq -r '.timestep')
        tasks_remaining=$(echo "$plan_response" | jq -r '.tasks_remaining')
        echo "  Timestep: $timestep, Tasks remaining: $tasks_remaining"
    else
        echo "✗ Planning failed"
        echo "  Response: $plan_response"
    fi
else
    echo "✗ Planning request failed"
fi

# Test Python client
echo "Testing Python client..."
if command -v python3 &> /dev/null; then
    python3 examples/add_task_client.py --status > /dev/null 2>&1
    if [[ $? -eq 0 ]]; then
        echo "✓ Python client working"
    else
        echo "✗ Python client failed"
    fi
else
    echo "⚠ Python3 not found, skipping Python client test"
fi

# Show final status
echo
echo "Final task status:"
curl -s http://localhost:8080/task_status | jq .

# Stop server
echo
echo "Stopping server..."
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

echo
echo "=== Test Complete ==="
echo "Server log saved to: server.log"
echo
echo "To run the server manually:"
echo "  ./build/mapf_server --mapFile ./example_problems/custom_domain/maps/mymap.map --configFile ./configs/mymap.json"
echo
echo "To add tasks:"
echo "  python3 examples/add_task_client.py --location 82"
echo "  ./examples/add_task_curl.sh --location 82" 