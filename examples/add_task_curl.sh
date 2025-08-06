#!/bin/bash

# MAPF Server Add Task Script using curl
# This script helps you add new tasks to the MAPF server

SERVER_URL="http://localhost:8080"

# Function to convert coordinates to linearized location
coordinates_to_location() {
    local row=$1
    local col=$2
    echo $((row * 10 + col))
}

# Function to convert location to coordinates
location_to_coordinates() {
    local location=$1
    local row=$((location / 10))
    local col=$((location % 10))
    echo "$row $col"
}

# Function to show usage
show_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --location LOC    Add task at linearized location (0-99)"
    echo "  --row ROW --col COL  Add task at coordinates (row, col)"
    echo "  --status          Show current task status"
    echo "  --server URL      Server URL (default: http://localhost:8080)"
    echo "  --help            Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0 --location 82          # Add task at location 82 (coordinates 8,2)"
    echo "  $0 --row 8 --col 2        # Add task at coordinates (8,2)"
    echo "  $0 --status               # Show current task status"
    echo "  $0 --server http://localhost:9090 --location 45"
}

# Parse command line arguments
LOCATION=""
ROW=""
COL=""
SHOW_STATUS=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --location)
            LOCATION="$2"
            shift 2
            ;;
        --row)
            ROW="$2"
            shift 2
            ;;
        --col)
            COL="$2"
            shift 2
            ;;
        --status)
            SHOW_STATUS=true
            shift
            ;;
        --server)
            SERVER_URL="$2"
            shift 2
            ;;
        --help)
            show_usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            show_usage
            exit 1
            ;;
    esac
done

echo "=== MAPF Server Add Task Script ==="
echo "Server URL: $SERVER_URL"
echo

# Check server health
echo "1. Checking server health..."
health_response=$(curl -s "$SERVER_URL/health")
if [[ $? -ne 0 ]]; then
    echo "✗ Server is not responding"
    exit 1
fi
echo "✓ Server is healthy"
echo

# Show task status if requested
if [[ "$SHOW_STATUS" == true ]]; then
    echo "2. Current task status:"
    status_response=$(curl -s "$SERVER_URL/task_status")
    if [[ $? -eq 0 ]]; then
        echo "$status_response" | jq .
    else
        echo "✗ Failed to get task status"
        exit 1
    fi
    exit 0
fi

# Determine location
if [[ -n "$LOCATION" ]]; then
    TASK_LOCATION=$LOCATION
elif [[ -n "$ROW" && -n "$COL" ]]; then
    TASK_LOCATION=$(coordinates_to_location $ROW $COL)
else
    echo "✗ Error: Must specify either --location or both --row and --col"
    show_usage
    exit 1
fi

# Validate location
if [[ $TASK_LOCATION -lt 0 || $TASK_LOCATION -gt 99 ]]; then
    echo "✗ Error: Location $TASK_LOCATION is out of bounds (0-99)"
    exit 1
fi

# Show location info
coords=$(location_to_coordinates $TASK_LOCATION)
read row col <<< "$coords"
echo "2. Adding task at location $TASK_LOCATION (coordinates: $row,$col)"

# Add task
echo "3. Sending add task request..."
add_response=$(curl -s -X POST "$SERVER_URL/add_task" \
  -H "Content-Type: application/json" \
  -d "{\"location\": $TASK_LOCATION}")

if [[ $? -eq 0 ]]; then
    echo "Response:"
    echo "$add_response" | jq .
    
    # Check if successful
    status=$(echo "$add_response" | jq -r '.status')
    if [[ "$status" == "success" ]]; then
        echo
        echo "✓ Task added successfully!"
        
        # Show updated task status
        echo
        echo "4. Updated task status:"
        updated_status=$(curl -s "$SERVER_URL/task_status")
        if [[ $? -eq 0 ]]; then
            echo "$updated_status" | jq .
        fi
    else
        echo "✗ Failed to add task"
        exit 1
    fi
else
    echo "✗ Failed to send request"
    exit 1
fi

echo
echo "=== Task Addition Complete ===" 