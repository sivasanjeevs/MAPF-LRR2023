#!/bin/bash

# MAPF Server Startup Script
# This script provides an easy way to start the MAPF server with common configurations

# Default values
MAP_FILE="./example_problems/custom_domain/maps/mymap.map"
CONFIG_FILE="./configs/mymap.json"
PORT=8080
PREPROCESS_TIME=30

# Function to show usage
show_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo
    echo "Options:"
    echo "  -m, --map FILE       Map file path (default: $MAP_FILE)"
    echo "  -c, --config FILE    Config file path (default: $CONFIG_FILE)"
    echo "  -p, --port PORT      Server port (default: $PORT)"
    echo "  -t, --time SECONDS   Preprocessing time limit (default: $PREPROCESS_TIME)"
    echo "  -h, --help           Show this help message"
    echo
    echo "Examples:"
    echo "  $0                                    # Use defaults"
    echo "  $0 -m ./maps/warehouse.map -p 9090   # Custom map and port"
    echo "  $0 --config ./configs/brc202d.json   # Custom config"
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -m|--map)
            MAP_FILE="$2"
            shift 2
            ;;
        -c|--config)
            CONFIG_FILE="$2"
            shift 2
            ;;
        -p|--port)
            PORT="$2"
            shift 2
            ;;
        -t|--time)
            PREPROCESS_TIME="$2"
            shift 2
            ;;
        -h|--help)
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

# Check if build directory exists
if [ ! -d "build" ]; then
    echo "Build directory not found. Compiling..."
    ./compile.sh
fi

# Check if server executable exists
if [ ! -f "build/mapf_server" ]; then
    echo "Server executable not found. Compiling..."
    ./compile.sh
fi

# Check if map file exists
if [ ! -f "$MAP_FILE" ]; then
    echo "Error: Map file not found: $MAP_FILE"
    exit 1
fi

# Check if config file exists
if [ ! -f "$CONFIG_FILE" ]; then
    echo "Error: Config file not found: $CONFIG_FILE"
    exit 1
fi

echo "Starting MAPF Server..."
echo "Map file: $MAP_FILE"
echo "Config file: $CONFIG_FILE"
echo "Port: $PORT"
echo "Preprocessing time: $PREPROCESS_TIME seconds"
echo

# Start the server
./build/mapf_server \
    --mapFile "$MAP_FILE" \
    --configFile "$CONFIG_FILE" \
    --port "$PORT" \
    --preprocessTimeLimit "$PREPROCESS_TIME" 