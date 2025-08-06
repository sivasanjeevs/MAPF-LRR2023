# Custom Domain MAPF Server Guide

This guide explains how to run the MAPF server with your custom domain and how to dynamically add new tasks.

## Your Custom Domain Structure

Your custom domain is located in `example_problems/custom_domain/` with the following structure:

```
custom_domain/
├── myproblem.json          # Problem configuration
├── agents/
│   └── myagents.agents     # Agent start positions
├── maps/
│   └── mymap.map          # Map file
└── task/
    └── mytask.tasks       # Task locations
```

### File Formats

#### 1. **Map File** (`mymap.map`)
- `@` represents blocked cells (obstacles)
- `.` represents free cells where agents can move
- Format: `type octile`, `height 10`, `width 10`, then the map grid

#### 2. **Agent File** (`myagents.agents`)
- First line: Number of agents
- Following lines: Linearized positions of agents
- Example: `2` means agent at position (0,2) in a 10x10 map

#### 3. **Task File** (`mytask.tasks`)
- First line: Number of tasks
- Following lines: Linearized positions of task goals
- Example: `82` means task at position (8,2) in a 10x10 map

#### 4. **Problem Configuration** (`myproblem.json`)
```json
{
    "mapFile": "maps/mymap.map",
    "agentFile": "agents/myagents.agents",
    "teamSize": 5,
    "taskFile": "task/mytask.tasks",
    "numTasksReveal": 1,
    "taskAssignmentStrategy": "greedy"
}
```

## How to Run the Server

### 1. **Compile the Server**
```bash
./compile.sh
```

### 2. **Start the Server**
```bash
./build/mapf_server \
  --mapFile ./example_problems/custom_domain/maps/mymap.map \
  --configFile ./configs/mymap.json \
  --port 8080
```

The server will:
- Load your custom domain configuration
- Initialize 5 agents at positions 2, 3, 4, 5, 6
- Load 13 tasks from your task file
- Start the HTTP server on port 8080

## How to Add New Tasks Dynamically

The server now supports **dynamic task addition**! You can add new tasks while the server is running, and they will be automatically assigned to free agents.

### Method 1: Using Python Script (Recommended)

```bash
# Add task by linearized location
python3 examples/add_task_client.py --location 82

# Add task by coordinates (row, col)
python3 examples/add_task_client.py --row 8 --col 2

# Show current task status
python3 examples/add_task_client.py --status

# Add task to different server
python3 examples/add_task_client.py --server http://localhost:9090 --location 45
```

### Method 2: Using Shell Script

```bash
# Add task by linearized location
./examples/add_task_curl.sh --location 82

# Add task by coordinates
./examples/add_task_curl.sh --row 8 --col 2

# Show current task status
./examples/add_task_curl.sh --status
```

### Method 3: Using curl directly

```bash
# Add task at location 82
curl -X POST http://localhost:8080/add_task \
  -H "Content-Type: application/json" \
  -d '{"location": 82}'

# Check task status
curl http://localhost:8080/task_status
```

## How It Works

### 1. **Server Initialization**
When the server starts:
- Loads agent positions: [2, 3, 4, 5, 6] (5 agents)
- Loads task locations: [96, 95, 91, 94, 93, 2, 3, 4, 5, 6, 52, 57, 61] (13 tasks)
- Initializes task queue with greedy assignment strategy

### 2. **Continuous Operation**
- Server runs continuously and accepts planning requests
- When you call `/plan`, the server:
  - Checks for completed tasks
  - Assigns new tasks to free agents
  - Plans paths for agents to reach their tasks
  - Returns planned actions

### 3. **Dynamic Task Addition**
When you add a new task:
- Task is added to the `mytask.tasks` file
- File is updated with new task count
- Server reloads tasks from file
- New task is added to the queue
- Free agents can be assigned to the new task

## Coordinate System

Your 10x10 map uses this coordinate system:

```
  0  1  2  3  4  5  6  7  8  9  (col)
0  0  1  2  3  4  5  6  7  8  9
1 10 11 12 13 14 15 16 17 18 19
2 20 21 22 23 24 25 26 27 28 29
3 30 31 32 33 34 35 36 37 38 39
4 40 41 42 43 44 45 46 47 48 49
5 50 51 52 53 54 55 56 57 58 59
6 60 61 62 63 64 65 66 67 68 69
7 70 71 72 73 74 75 76 77 78 79
8 80 81 82 83 84 85 86 87 88 89
9 90 91 92 93 94 95 96 97 98 99
(row)
```

**Linearized location = row × 10 + col**

Examples:
- Location 82 = (8,2)
- Location 96 = (9,6)
- Location 2 = (0,2)

## Complete Workflow Example

### 1. **Start the Server**
```bash
./build/mapf_server \
  --mapFile ./example_problems/custom_domain/maps/mymap.map \
  --configFile ./configs/mymap.json
```

### 2. **Run Continuous Simulation**
```bash
python3 examples/continuous_client_example.py
```

### 3. **Add New Tasks While Running**
In another terminal:
```bash
# Add task at position (8,2)
python3 examples/add_task_client.py --row 8 --col 2

# Add task at position (5,5)
python3 examples/add_task_client.py --location 55

# Check current status
python3 examples/add_task_client.py --status
```

### 4. **Monitor Progress**
```bash
# Check task status
curl http://localhost:8080/task_status

# Get detailed report
curl http://localhost:8080/report
```

## Your Current Setup

Based on your files:

### **Agents** (5 agents):
- Agent 0: Position 2 (0,2)
- Agent 1: Position 3 (0,3)
- Agent 2: Position 4 (0,4)
- Agent 3: Position 5 (0,5)
- Agent 4: Position 6 (0,6)

### **Initial Tasks** (13 tasks):
- Task 0: Position 96 (9,6)
- Task 1: Position 95 (9,5)
- Task 2: Position 91 (9,1)
- Task 3: Position 94 (9,4)
- Task 4: Position 93 (9,3)
- Task 5: Position 2 (0,2)
- Task 6: Position 3 (0,3)
- Task 7: Position 4 (0,4)
- Task 8: Position 5 (0,5)
- Task 9: Position 6 (0,6)
- Task 10: Position 52 (5,2)
- Task 11: Position 57 (5,7)
- Task 12: Position 61 (6,1)

### **Map** (10x10):
```
@@......@@  (row 0: positions 0-9)
..........  (row 1: positions 10-19)
..........  (row 2: positions 20-29)
..........  (row 3: positions 30-39)
..........  (row 4: positions 40-49)
..........  (row 5: positions 50-59)
..........  (row 6: positions 60-69)
..........  (row 7: positions 70-79)
..........  (row 8: positions 80-89)
..........  (row 9: positions 90-99)
```

## Troubleshooting

### **Server won't start**
- Check if all files exist in the correct locations
- Verify file permissions
- Check console output for error messages

### **Tasks not being assigned**
- Check if task file is readable
- Verify task locations are within map bounds
- Check server logs for initialization errors

### **Can't add new tasks**
- Ensure server is running
- Check if task file is writable
- Verify location is not blocked (not an `@` in the map)

### **Agents not moving**
- Check if agent positions are valid
- Verify map file format
- Check planning algorithm configuration

## Advanced Usage

### **Modifying Task Assignment Strategy**
Edit `myproblem.json`:
```json
{
    "taskAssignmentStrategy": "roundrobin"  // or "greedy", "roundrobin_fixed"
}
```

### **Adding Multiple Tasks**
```bash
# Add several tasks at once
for loc in 25 35 45 55; do
    python3 examples/add_task_client.py --location $loc
done
```

### **Monitoring in Real-time**
```bash
# Watch task status continuously
watch -n 1 'curl -s http://localhost:8080/task_status | jq .'
```

The server is now fully configured for your custom domain with dynamic task addition capabilities! 