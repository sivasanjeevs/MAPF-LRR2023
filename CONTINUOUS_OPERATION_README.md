# MAPF Server Continuous Operation Guide

This guide explains how to use the MAPF server for continuous task assignment and execution, where the server automatically assigns tasks to free agents and tracks task completion.

## Overview

The MAPF server now supports **continuous operation** where:

1. **Server runs continuously** - Never stops unless explicitly terminated
2. **Automatic task assignment** - Free agents automatically get new tasks from the queue
3. **Task completion tracking** - Server detects when agents reach task locations
4. **Session persistence** - State is maintained across multiple planning requests
5. **Real-time status monitoring** - Check task status at any time

## Key Features

### 🔄 Continuous Task Assignment
- When an agent completes a task, they automatically become available for new tasks
- The server assigns tasks from the queue to free agents based on the configured strategy
- No manual intervention required for task assignment

### 📊 Task Status Tracking
- Real-time monitoring of which agents have tasks assigned
- Track task completion progress
- Monitor remaining tasks in the queue

### 🎯 Multiple Assignment Strategies
- **Greedy**: Tasks assigned to first available agent
- **Round Robin**: Tasks distributed evenly among agents
- **Round Robin Fixed**: Tasks pre-assigned to specific agents

## API Endpoints

### POST `/plan`
Main endpoint for continuous operation. Sends agent states and receives planned actions.

**Request:**
```json
{
  "agents": [
    {"location": 2, "orientation": 0, "timestep": 0},
    {"location": 3, "orientation": 0, "timestep": 0}
  ]
}
```

**Response:**
```json
{
  "status": "success",
  "actions": [...],
  "timestep": 5,
  "task_status": [
    {
      "agent_id": 0,
      "has_task": true,
      "current_task": {
        "task_id": 1,
        "location": 96,
        "assigned_at": 2
      },
      "tasks_completed": 2
    }
  ],
  "tasks_remaining": 8,
  "total_tasks_completed": 12,
  "all_tasks_finished": false
}
```

### GET `/task_status`
Get current task status for all agents without planning.

**Response:**
```json
[
  {
    "agent_id": 0,
    "has_task": true,
    "current_task": {
      "task_id": 1,
      "location": 96,
      "assigned_at": 2
    },
    "tasks_completed": 2
  }
]
```

### POST `/reset`
Reset the simulation session and reinitialize the task system.

### GET `/health`
Check if the server is running.

### GET `/status`
Get server status information.

### GET `/report`
Generate a comprehensive report of the simulation.

## Usage Examples

### 1. Start the Server
```bash
# Compile the server
./compile.sh

# Start the server
./build/mapf_server \
  --mapFile ./example_problems/custom_domain/maps/mymap.map \
  --configFile ./configs/mymap.json \
  --port 8080
```

### 2. Run Continuous Simulation (Python)
```bash
# Run the continuous client example
python3 examples/continuous_client_example.py

# Or with custom server URL
python3 examples/continuous_client_example.py http://localhost:9090
```

### 3. Run Continuous Simulation (Shell)
```bash
# Run the curl-based example
./examples/continuous_curl_example.sh
```

### 4. Manual Testing with curl
```bash
# Reset simulation
curl -X POST http://localhost:8080/reset

# Send planning request
curl -X POST http://localhost:8080/plan \
  -H "Content-Type: application/json" \
  -d '{"agents": [{"location": 2, "orientation": 0, "timestep": 0}]}'

# Check task status
curl http://localhost:8080/task_status
```

## Configuration

### Task Assignment Strategy
Configure the task assignment strategy in your problem configuration file:

```json
{
  "teamSize": 5,
  "taskAssignmentStrategy": "greedy",
  "numTasksReveal": 1,
  "agentFile": "myagents.agents",
  "taskFile": "mytask.tasks"
}
```

**Available strategies:**
- `"greedy"`: Tasks assigned to first available agent
- `"roundrobin"`: Tasks distributed evenly among agents  
- `"roundrobin_fixed"`: Tasks pre-assigned to specific agents

### Problem Configuration
The server loads tasks and agent start positions from:
- `./example_problems/custom_domain/myproblem.json`
- Agent positions: `./example_problems/custom_domain/agents/myagents.agents`
- Task locations: `./example_problems/custom_domain/task/mytask.tasks`

## How It Works

### 1. Initialization
When the server starts or `/reset` is called:
- Loads agent start positions and task locations
- Initializes task queue based on assignment strategy
- Sets up tracking structures for task assignment and completion

### 2. Continuous Operation Loop
For each `/plan` request:

1. **Task Completion Check**: Check if any agents have reached their task locations
2. **Task Assignment**: Assign new tasks to free agents from the queue
3. **Path Planning**: Plan paths for agents to reach their assigned tasks
4. **State Update**: Update agent positions and task status
5. **Response**: Return planned actions and current task status

### 3. Task Lifecycle
```
Task Queue → Assigned to Agent → Agent Moves to Task → Task Completed → Agent Free → New Task Assigned
```

## Monitoring and Debugging

### Server Logs
The server provides detailed logging:
```
=== Task Update at Timestep 5 ===
Tasks in queue: 8
✓ Agent 0 completed task 1 at location 96 (timestep 5)
→ Assigned task 9 to agent 0 at location 45 (timestep 5)
Current task assignments:
  Agent 0: Task 9 at location 45 (assigned at timestep 5)
  Agent 1: No assigned task
Tasks remaining in queue: 7
Total tasks completed: 12
=====================================
```

### Task Status Monitoring
Use the `/task_status` endpoint to monitor:
- Which agents have tasks assigned
- Current task details (ID, location, assignment time)
- Number of tasks completed by each agent

### Progress Tracking
The `/plan` response includes:
- `tasks_remaining`: Number of tasks left in queue
- `total_tasks_completed`: Total tasks completed so far
- `all_tasks_finished`: Whether all tasks are done

## Troubleshooting

### Common Issues

1. **No tasks being assigned**
   - Check if task queue is empty
   - Verify problem configuration is loaded correctly
   - Check server logs for initialization errors

2. **Tasks not completing**
   - Verify agent positions are being updated correctly
   - Check if agents are actually reaching task locations
   - Ensure action model is working properly

3. **Server not responding**
   - Check if server is running on correct port
   - Verify firewall settings
   - Check server logs for errors

### Debug Commands
```bash
# Check server health
curl http://localhost:8080/health

# Get server status
curl http://localhost:8080/status

# Check task status
curl http://localhost:8080/task_status

# Reset and start fresh
curl -X POST http://localhost:8080/reset
```

## Advanced Usage

### Custom Task Assignment
You can implement custom task assignment strategies by modifying the `update_tasks_lifelong()` function in `src/MAPFServer.cpp`.

### Integration with External Systems
The server can be integrated with external systems by:
- Polling `/task_status` for real-time updates
- Using webhooks for task completion events
- Implementing custom clients for specific use cases

### Performance Optimization
For high-performance scenarios:
- Use connection pooling for HTTP requests
- Implement batch processing for multiple agents
- Consider using WebSocket for real-time updates 