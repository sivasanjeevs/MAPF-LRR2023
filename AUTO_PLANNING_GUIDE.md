# Auto-Planning Guide for MAPF Server

This guide explains the **auto-planning functionality** that automatically triggers the planner when new tasks are added to the system.

## 🎯 **What is Auto-Planning?**

Auto-planning is a feature that automatically calls the MAPF planner whenever a new task is added to the system. This ensures that:

1. **Immediate Planning**: New tasks are planned for immediately when added
2. **Free Agent Assignment**: Free agents are automatically assigned to new tasks
3. **Continuous Operation**: The server maintains continuous operation without manual intervention
4. **Real-time Response**: Planning happens in real-time as tasks are added

## 🔄 **How Auto-Planning Works**

### **1. Initial Planning**
When you first start the server and send a `/plan` request:
- Server loads initial tasks from `mytask.tasks` file
- Agents are assigned to initial tasks
- Planner creates paths for agents to reach their tasks

### **2. Dynamic Task Addition**
When you add a new task via `/add_task`:
- Task is added to the `mytask.tasks` file
- Server reloads tasks from the updated file
- **Auto-planning is triggered automatically**
- Planner creates new paths for agents to reach the new task
- Free agents are assigned to the new task

### **3. Continuous Operation**
The server keeps running and:
- Monitors for task completion
- Assigns new tasks to free agents
- Maintains continuous planning and execution

## 🚀 **How to Use Auto-Planning**

### **Step 1: Start the Server**
```bash
./build/mapf_server \
  --mapFile ./example_problems/custom_domain/maps/mymap.map \
  --configFile ./configs/mymap.json \
  --port 8080
```

### **Step 2: Initialize Session**
Send an initial planning request to start the session:
```bash
curl -X POST http://localhost:8080/plan \
  -H "Content-Type: application/json" \
  -d '{"agents": [{"location": 2, "orientation": 0, "timestep": 0}, {"location": 3, "orientation": 0, "timestep": 0}, {"location": 4, "orientation": 0, "timestep": 0}, {"location": 5, "orientation": 0, "timestep": 0}, {"location": 6, "orientation": 0, "timestep": 0}]}'
```

### **Step 3: Add New Tasks (Auto-Planning Triggered)**
```bash
# Add task at location 82 (coordinates 8,2)
curl -X POST http://localhost:8080/add_task \
  -H "Content-Type: application/json" \
  -d '{"location": 82}'
```

**Response will include auto-planning information:**
```json
{
  "status": "success",
  "message": "Task added successfully",
  "task_id": 13,
  "location": 82,
  "tasks_in_queue": 14,
  "planning": {
    "planning_triggered": true,
    "planning_success": true,
    "planning_time": 0.045,
    "actions_planned": 5,
    "task_status": [
      {
        "agent_id": 0,
        "has_task": true,
        "current_task": {
          "task_id": 13,
          "location": 82,
          "assigned_at": 5
        },
        "tasks_completed": 2
      }
    ],
    "tasks_remaining": 13,
    "total_tasks_completed": 2
  }
}
```

## 📋 **Auto-Planning Response Fields**

When auto-planning is triggered, the response includes a `planning` object with:

- **`planning_triggered`**: `true` if planning was attempted
- **`planning_success`**: `true` if planning succeeded
- **`planning_time`**: Time taken for planning (seconds)
- **`actions_planned`**: Number of actions planned for agents
- **`task_status`**: Current task assignments after planning
- **`tasks_remaining`**: Number of tasks left in queue
- **`total_tasks_completed`**: Total tasks completed so far

## 🛠 **Using the Python Client**

### **Basic Task Addition**
```python
from examples.add_task_client import AddTaskClient

client = AddTaskClient()
result = client.add_task(82)
```

### **Task Addition with Auto-Planning Info**
```python
from examples.continuous_client_example import ContinuousMAPFClient

client = ContinuousMAPFClient()
result = client.add_task_with_planning(82)
```

### **Test Auto-Planning**
```bash
python3 examples/test_auto_planning.py
```

## 🔧 **Server Logs for Auto-Planning**

When auto-planning is triggered, you'll see these logs:

```
✓ Added new task at location 82 (task ID: 13)
🔄 Auto-triggering planning for new task...
Using current agent positions for planning
=== Task Update at Timestep 5 ===
Tasks in queue: 13
→ Assigned task 13 to agent 0 at location 82 (timestep 5)
Current task assignments:
  Agent 0: Task 13 at location 82 (assigned at timestep 5)
  Agent 1: No assigned task
Tasks remaining in queue: 12
Total tasks completed: 2
=====================================
✓ Planning completed in 0.045 seconds
```

## 📊 **Complete Workflow Example**

### **1. Start Server and Initialize**
```bash
# Start server
./build/mapf_server --mapFile ./example_problems/custom_domain/maps/mymap.map --configFile ./configs/mymap.json

# In another terminal, initialize session
python3 examples/continuous_client_example.py
```

### **2. Add Tasks with Auto-Planning**
```bash
# Add multiple tasks - each will trigger auto-planning
python3 examples/add_task_client.py --location 82
python3 examples/add_task_client.py --location 45
python3 examples/add_task_client.py --location 67
```

### **3. Monitor Progress**
```bash
# Check task status
curl http://localhost:8080/task_status

# Get detailed report
curl http://localhost:8080/report
```

## 🎯 **Key Benefits of Auto-Planning**

### **1. Real-time Response**
- Planning happens immediately when tasks are added
- No need to manually trigger planning
- Agents start moving toward new tasks right away

### **2. Automatic Assignment**
- Free agents are automatically assigned to new tasks
- No manual intervention required
- Optimal task distribution based on assignment strategy

### **3. Continuous Operation**
- Server maintains state across task additions
- Planning uses current agent positions
- Seamless integration with existing task queue

### **4. Error Handling**
- Graceful handling of planning failures
- Detailed error reporting
- Fallback to manual planning if needed

## 🔍 **Troubleshooting Auto-Planning**

### **Auto-Planning Not Triggered**
- Check if session is active (send initial `/plan` request first)
- Verify server is running and healthy
- Check server logs for error messages

### **Planning Fails**
- Verify task location is valid and not blocked
- Check if agents can reach the task location
- Review planning algorithm configuration

### **No Agents Assigned**
- Check if there are free agents available
- Verify task assignment strategy
- Review task queue status

## 📈 **Performance Considerations**

### **Planning Time**
- Auto-planning typically takes 0.01-0.1 seconds
- Time depends on map complexity and number of agents
- Planning time is included in the response

### **Memory Usage**
- Server maintains current agent positions
- Task queue is updated in real-time
- Minimal memory overhead for auto-planning

### **Concurrent Requests**
- Auto-planning is synchronous (blocks until complete)
- Multiple task additions are processed sequentially
- Consider batching multiple tasks if needed

## 🎉 **Summary**

Auto-planning provides a seamless experience for dynamic task management:

1. **Add tasks** → **Auto-planning triggered** → **Agents assigned** → **Paths planned**
2. **Server keeps running** continuously
3. **No manual intervention** required
4. **Real-time response** to new tasks
5. **Automatic optimization** of agent assignments

The system now works exactly as you requested - when you add a new task, the planner is automatically called and free agents are assigned to the new task immediately! 