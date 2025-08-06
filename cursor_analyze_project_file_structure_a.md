# Analyze project file structure and flow
_Exported on 8/1/2025 at 12:10:14 GMT+5:30 from Cursor (1.3.6)_

---

**User**

 @official_docs/ @custom_domain/ @test.json  @LaCAM2/ @MAPFPlanner.cpp @driver.cpp @common.cpp @CompetitionSystem.cpp @ActionModel.cpp 
understand the project and analyse it completely, understand the flow of how each file is used (what is defined where) the command i use is ./build/lifelong --inputFile ./example_problems/my.domain/new_usecase.json -o test.json
put in your memory about the project and tell me. i'll tell you how to proceed

---

**Cursor**

I'll analyze this MAPF (Multi-Agent Path Finding) project comprehensively to understand its structure and flow.


Now let me examine the example problem structure to understand the input format:


Now let me examine the config files to understand the solver configuration:


Now let me examine the LaCAM2 solver to understand the core algorithm:


Based on my comprehensive analysis of the MAPF (Multi-Agent Path Finding) project, here's my complete understanding:

## **Project Overview**

This is a **MAPF-LRR2023** (Multi-Agent Path Finding - League of Robot Runners 2023) competition framework for lifelong multi-agent pathfinding. The system simulates robots navigating in grid environments while continuously receiving and completing tasks.

## **Core Architecture & Flow**

### **1. Entry Point & Command Flow**
```
./build/lifelong --inputFile ./example_problems/my.domain/new_usecase.json -o test.json
```

**Flow:**
1. `driver.cpp` (main entry point) → parses command line arguments
2. Loads JSON input file → creates `Grid`, `ActionModelWithRotate`, and `MAPFPlanner`
3. Creates appropriate `BaseSystem` subclass based on `taskAssignmentStrategy`
4. Calls `system_ptr->simulate(simulation_time)`

### **2. System Components**

#### **Core Classes:**
- **`MAPFPlanner`**: Main planner interface with multiple solver backends (LaCAM2, LNS, RHCR)
- **`SharedEnvironment`**: Contains current state, goals, map info shared between planner and simulator
- **`BaseSystem`**: Abstract simulation system with three implementations:
  - `TaskAssignSystem`: Greedy task assignment
  - `InfAssignSystem`: Round-robin infinite task assignment  
  - `FixedAssignSystem`: Pre-assigned tasks per agent
- **`ActionModelWithRotate`**: Handles action validation and state transitions

#### **Key Data Structures:**
- **`State`**: `{location, timestep, orientation}` (0=E, 1=S, 2=W, 3=N)
- **`Action`**: `{FW, CR, CCR, W, NA}` (Forward, Clockwise, Counter-clockwise, Wait, Not Applicable)
- **`Task`**: `{task_id, location, t_assigned, t_completed, agent_assigned}`

### **3. Simulation Loop**

```cpp
// In CompetitionSystem.cpp::simulate()
for (; timestep < simulation_time; ) {
    sync_shared_env();           // Update env with current state
    vector<Action> actions = plan();  // Get actions from planner
    list<Task> finished = move(actions);  // Execute actions, check task completion
    update_tasks();              // Assign new tasks to agents
}
```

### **4. Planner Architecture**

The system supports multiple solver backends:

#### **LaCAM2 Solver** (Primary):
- **Location**: `src/LaCAM2/LaCAM2Solver.cpp`
- **Algorithm**: LaCAM2 (Lazy Constraints for Multi-Agent Pathfinding)
- **Features**: Planning windows, external executor, rotation support
- **Configuration**: Via `configs/*.json` files

#### **LNS Solver** (Large Neighborhood Search):
- **Location**: `src/LNS/LNSSolver.cpp` 
- **Algorithm**: Large Neighborhood Search with LaCAM2 as sub-solver
- **Features**: Adaptive destruction strategies, replanning

#### **RHCR Solver** (Receding Horizon Conflict Resolution):
- **Location**: `src/RHCR/`
- **Algorithm**: Various MAPF solvers (ECBS, PBS, WHCA, LRA)
- **Features**: Conflict resolution, suboptimal bounds

### **5. Input/Output Format**

#### **Input JSON Structure:**
```json
{
    "mapFile": "maps/mymap.map",
    "agentFile": "agents/myagents.agents", 
    "taskFile": "task/mytask.tasks",
    "teamSize": 5,
    "numTasksReveal": 1,
    "taskAssignmentStrategy": "greedy"
}
```

#### **Map Format:**
```
type octile
height 10
width 10
map
@@......@@
..........
```
- `@` or `T`: Obstacles
- `.`: Free space
- `E`: Emitter points
- `S`: Service points

#### **Output JSON Structure:**
```json
{
    "actionModel": "MAPF_T",
    "AllValid": "Yes",
    "teamSize": 5,
    "start": [[0,2,"E"], ...],
    "numTaskFinished": 13,
    "sumOfCost": 214,
    "makespan": 48,
    "actualPaths": ["W,F,F,W,...", ...],
    "plannerPaths": ["W,F,F,W,...", ...],
    "plannerTimes": [0.001886799, ...],
    "errors": [],
    "events": [...],
    "tasks": [...]
}
```

### **6. Configuration System**

- **Config files**: `configs/*.json` (mymap.json, etc.)
- **Conditional values**: Based on number of agents
- **Solver-specific settings**: LaCAM2, LNS, RHCR parameters
- **Map weights**: Optional cost modifications for different actions

### **7. Key Features**

#### **Lifelong Planning:**
- Continuous task assignment and completion
- Dynamic replanning as new tasks arrive
- Multiple task assignment strategies

#### **Multi-Solver Support:**
- LaCAM2: Fast, rotation-aware planning
- LNS: Large neighborhood search for complex scenarios  
- RHCR: Various conflict resolution algorithms

#### **Validation & Error Handling:**
- Action validation (vertex/edge conflicts)
- Movement validation (obstacles, boundaries)
- Comprehensive error reporting

#### **Performance Monitoring:**
- Planning time tracking
- Solution cost metrics
- Event logging

### **8. Build System**

- **CMake-based**: `CMakeLists.txt` with conditional compilation
- **Compilation**: `compile.sh` script
- **Dependencies**: Boost, OpenMP, spdlog, nlohmann/json
- **Python support**: Optional pybind11 integration

### **9. File Organization**

```
src/           # Core implementation
├── driver.cpp              # Main entry point
├── MAPFPlanner.cpp         # Planner interface
├── CompetitionSystem.cpp   # Simulation system
├── ActionModel.cpp         # Action validation
├── common.cpp              # Utilities
├── LaCAM2/                 # LaCAM2 solver
├── LNS/                    # LNS solver  
├── RHCR/                   # RHCR solver
└── util/                   # Utilities

inc/           # Headers
├── MAPFPlanner.h
├── SharedEnv.h
├── States.h
├── ActionModel.h
└── CompetitionSystem.h

configs/       # Configuration files
example_problems/  # Test problems
```

This is a sophisticated, production-ready MAPF framework designed for competition use with multiple algorithmic approaches, comprehensive validation, and extensive configuration options. The system is highly modular and supports both C++ and Python implementations.

**I'm ready to proceed with any specific questions or modifications you'd like to make to the project!**

```plaintext
./build/lifelong --inputFile ./example_problems/my.domain/new_usecase.json -o test.json
```

```cpp
// In CompetitionSystem.cpp::simulate()
for (; timestep < simulation_time; ) {
    sync_shared_env();           // Update env with current state
    vector<Action> actions = plan();  // Get actions from planner
    list<Task> finished = move(actions);  // Execute actions, check task completion
    update_tasks();              // Assign new tasks to agents
}
```

```json
{
    "mapFile": "maps/mymap.map",
    "agentFile": "agents/myagents.agents", 
    "taskFile": "task/mytask.tasks",
    "teamSize": 5,
    "numTasksReveal": 1,
    "taskAssignmentStrategy": "greedy"
}
```

```plaintext
type octile
height 10
width 10
map
@@......@@
..........
```

```json
{
    "actionModel": "MAPF_T",
    "AllValid": "Yes",
    "teamSize": 5,
    "start": [[0,2,"E"], ...],
    "numTaskFinished": 13,
    "sumOfCost": 214,
    "makespan": 48,
    "actualPaths": ["W,F,F,W,...", ...],
    "plannerPaths": ["W,F,F,W,...", ...],
    "plannerTimes": [0.001886799, ...],
    "errors": [],
    "events": [...],
    "tasks": [...]
}
```

```plaintext
src/           # Core implementation
├── driver.cpp              # Main entry point
├── MAPFPlanner.cpp         # Planner interface
├── CompetitionSystem.cpp   # Simulation system
├── ActionModel.cpp         # Action validation
├── common.cpp              # Utilities
├── LaCAM2/                 # LaCAM2 solver
├── LNS/                    # LNS solver  
├── RHCR/                   # RHCR solver
└── util/                   # Utilities

inc/           # Headers
├── MAPFPlanner.h
├── SharedEnv.h
├── States.h
├── ActionModel.h
└── CompetitionSystem.h

configs/       # Configuration files
example_problems/  # Test problems
```

