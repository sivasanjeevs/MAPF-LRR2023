#!/bin/bash

# MAPF Server Continuous Operation Example using curl
# This script demonstrates how the server continuously assigns tasks to free agents

SERVER_URL="http://localhost:8080"

echo "=== MAPF Server Continuous Operation Example ==="
echo "Server URL: $SERVER_URL"
echo

# Health check
echo "1. Health Check:"
curl -s "$SERVER_URL/health" | jq .
echo

# Reset simulation
echo "2. Reset Simulation:"
curl -s -X POST "$SERVER_URL/reset" | jq .
echo

# Initial planning request (agents will be assigned tasks automatically)
echo "3. Initial Planning Request (Task Assignment):"
cat << EOF > initial_request.json
{
  "agents": [
    {"location": 2, "orientation": 0, "timestep": 0},
    {"location": 3, "orientation": 0, "timestep": 0},
    {"location": 4, "orientation": 0, "timestep": 0},
    {"location": 5, "orientation": 0, "timestep": 0},
    {"location": 6, "orientation": 0, "timestep": 0}
  ]
}
EOF

echo "Sending initial planning request..."
response=$(curl -s -X POST "$SERVER_URL/plan" \
  -H "Content-Type: application/json" \
  -d @initial_request.json)

echo "Response:"
echo "$response" | jq .

# Extract task status from response
echo
echo "4. Task Status After Initial Assignment:"
echo "$response" | jq '.task_status'

# Check task status endpoint
echo
echo "5. Task Status Endpoint:"
curl -s "$SERVER_URL/task_status" | jq .

# Simulate a few more timesteps
echo
echo "6. Simulating Continuous Operation (3 more timesteps):"

for step in {1..3}; do
    echo
    echo "--- Timestep $step ---"
    
    # Create request with updated agent positions (simplified - in reality you'd track actual movements)
    cat << EOF > step_${step}_request.json
{
  "agents": [
    {"location": 2, "orientation": 0, "timestep": $step},
    {"location": 3, "orientation": 0, "timestep": $step},
    {"location": 4, "orientation": 0, "timestep": $step},
    {"location": 5, "orientation": 0, "timestep": $step},
    {"location": 6, "orientation": 0, "timestep": $step}
  ]
}
EOF
    
    response=$(curl -s -X POST "$SERVER_URL/plan" \
      -H "Content-Type: application/json" \
      -d @step_${step}_request.json)
    
    echo "Timestep $step response:"
    echo "$response" | jq '.task_status'
    echo "Tasks remaining: $(echo "$response" | jq '.tasks_remaining')"
    echo "Total completed: $(echo "$response" | jq '.total_tasks_completed')"
done

# Final task status
echo
echo "7. Final Task Status:"
curl -s "$SERVER_URL/task_status" | jq .

# Clean up
rm -f initial_request.json step_*.json

echo
echo "=== Continuous Operation Example Completed ==="
echo
echo "Key Features Demonstrated:"
echo "- Server automatically assigns tasks to free agents"
echo "- Task completion is tracked automatically"
echo "- Task status is available via /task_status endpoint"
echo "- Server maintains state across multiple requests"
echo "- Continuous operation until all tasks are completed" 