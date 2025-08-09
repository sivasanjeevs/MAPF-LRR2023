#!/bin/bash

# MAPF Server Client Examples using curl
# Make sure the MAPF server is running on localhost:8080

SERVER_URL="http://localhost:8080"

echo "=== MAPF Server Client Examples ==="
echo "Server URL: $SERVER_URL"
echo

# Health check
echo "1. Health Check:"
curl -s "$SERVER_URL/health" | jq .
echo

# Server status
echo "2. Server Status:"
curl -s "$SERVER_URL/status" | jq .
echo

# Planning request example
echo "3. Planning Request:"
cat << EOF > planning_request.json
{
  "agents": [
    {"location": 2, "orientation": 0, "timestep": 0},
    {"location": 3, "orientation": 0, "timestep": 0},
    {"location": 4, "orientation": 0, "timestep": 0},
    {"location": 5, "orientation": 0, "timestep": 0},
    {"location": 6, "orientation": 0, "timestep": 0}
  ],
  "goals": [
    {"location": 96, "timestep": 0},
    {"location": 95, "timestep": 0},
    {"location": 91, "timestep": 0},
    {"location": 94, "timestep": 0},
    {"location": 93, "timestep": 0}
  ]
}
EOF

curl -s -X POST "$SERVER_URL/plan" \
  -H "Content-Type: application/json" \
  -d @planning_request.json | jq .

echo

# Clean up
rm -f planning_request.json

echo "=== Examples completed ===" 