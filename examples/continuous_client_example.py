#!/usr/bin/env python3
"""
MAPF Server Continuous Client Example

This script demonstrates continuous task assignment and execution
where the server automatically assigns tasks to free agents and
tracks task completion.
"""

import requests
import json
import time
import sys

class ContinuousMAPFClient:
    def __init__(self, server_url="http://localhost:8080"):
        self.server_url = server_url
        self.current_agents = []
        self.timestep = 0
        
    def health_check(self):
        """Check if the server is healthy"""
        try:
            response = requests.get(f"{self.server_url}/health")
            return response.json()
        except requests.exceptions.RequestException as e:
            print(f"Health check failed: {e}")
            return None
    
    def reset_simulation(self):
        """Reset the simulation session"""
        try:
            response = requests.post(f"{self.server_url}/reset")
            return response.json()
        except requests.exceptions.RequestException as e:
            print(f"Reset failed: {e}")
            return None
    
    def get_task_status(self):
        """Get current task status for all agents"""
        try:
            response = requests.get(f"{self.server_url}/task_status")
            return response.json()
        except requests.exceptions.RequestException as e:
            print(f"Task status request failed: {e}")
            return None
    
    def plan_step(self, agents):
        """
        Request path planning for agents for a single timestep
        
        Args:
            agents: List of agent states [{"location": int, "orientation": int, "timestep": int}]
        
        Returns:
            JSON response with planned actions and task status
        """
        request_data = {
            "agents": agents
        }
        
        try:
            response = requests.post(
                f"{self.server_url}/plan",
                json=request_data,
                headers={"Content-Type": "application/json"}
            )
            return response.json()
        except requests.exceptions.RequestException as e:
            print(f"Planning request failed: {e}")
            return None
    
    def simulate_continuous_operation(self, max_timesteps=100):
        """Simulate continuous operation with automatic task assignment"""
        print("=== Starting Continuous MAPF Simulation ===")
        
        # Check server health
        print("Checking server health...")
        health = self.health_check()
        if not health:
            print("Server is not responding")
            return False
        
        # Reset simulation
        print("Resetting simulation...")
        reset_result = self.reset_simulation()
        if not reset_result:
            print("Failed to reset simulation")
            return False
        
        # Initialize agent positions (will be overridden by server's initial states)
        self.current_agents = [
            {"location": 0, "orientation": 0, "timestep": 0},
            {"location": 1, "orientation": 0, "timestep": 0},
            {"location": 2, "orientation": 0, "timestep": 0},
            {"location": 3, "orientation": 0, "timestep": 0},
            {"location": 4, "orientation": 0, "timestep": 0}
        ]
        
        print(f"Running simulation for up to {max_timesteps} timesteps...")
        print("The server will automatically:")
        print("- Assign tasks to free agents")
        print("- Track task completion")
        print("- Plan paths for agents to reach their tasks")
        print()
        
        for timestep in range(max_timesteps):
            print(f"\n--- Timestep {timestep + 1} ---")
            
            # Request planning
            result = self.plan_step(self.current_agents)
            
            if not result or result.get("status") != "success":
                print(f"Planning failed at timestep {timestep + 1}")
                if result:
                    print(f"Error: {result.get('message', 'Unknown error')}")
                break
            
            # Update agent positions based on the planned actions
            actions = result.get("actions", [])
            for action_data in actions:
                agent_id = action_data["agent_id"]
                new_location = action_data["location"]
                new_orientation = action_data["orientation"]
                
                # Update agent state
                if agent_id < len(self.current_agents):
                    self.current_agents[agent_id]["location"] = new_location
                    self.current_agents[agent_id]["orientation"] = new_orientation
                    self.current_agents[agent_id]["timestep"] = timestep + 1
            
            # Display task status
            task_status = result.get("task_status", [])
            print("Task Status:")
            for agent_status in task_status:
                agent_id = agent_status["agent_id"]
                has_task = agent_status["has_task"]
                tasks_completed = agent_status["tasks_completed"]
                
                if has_task:
                    current_task = agent_status["current_task"]
                    task_id = current_task["task_id"]
                    task_location = current_task["location"]
                    print(f"  Agent {agent_id}: Task {task_id} at location {task_location} (completed: {tasks_completed})")
                else:
                    print(f"  Agent {agent_id}: No task assigned (completed: {tasks_completed})")
            
            # Display simulation progress
            tasks_remaining = result.get("tasks_remaining", 0)
            total_completed = result.get("total_tasks_completed", 0)
            all_finished = result.get("all_tasks_finished", False)
            
            print(f"Progress: {total_completed} tasks completed, {tasks_remaining} tasks remaining")
            
            if all_finished:
                print("\n🎉 All tasks completed! Simulation finished successfully.")
                break
            
            # Small delay to make output readable
            time.sleep(0.1)
        
        print(f"\n=== Simulation Complete ===")
        print(f"Final timestep: {timestep + 1}")
        print(f"Total tasks completed: {total_completed}")
        return True

    def add_task(self, location):
        """
        Add a new task at the specified location
        
        Args:
            location: Linearized position (0-99 for 10x10 map)
        
        Returns:
            JSON response from server
        """
        request_data = {
            "location": location
        }
        
        try:
            response = requests.post(
                f"{self.server_url}/add_task",
                json=request_data,
                headers={"Content-Type": "application/json"}
            )
            return response.json()
        except requests.exceptions.RequestException as e:
            print(f"Add task request failed: {e}")
            return None
    
    def add_task_with_planning(self, location):
        """
        Add a new task and show auto-planning results
        
        Args:
            location: Linearized position (0-99 for 10x10 map)
        
        Returns:
            JSON response from server
        """
        result = self.add_task(location)
        if result and result.get("status") == "success":
            print(f"✓ Task added successfully!")
            print(f"  Task ID: {result.get('task_id')}")
            print(f"  Location: {result.get('location')}")
            print(f"  Tasks in queue: {result.get('tasks_in_queue')}")
            
            # Show auto-planning information
            planning_info = result.get("planning", {})
            if planning_info.get("planning_triggered"):
                print("🔄 Auto-planning was triggered!")
                if planning_info.get("planning_success"):
                    print("✓ Auto-planning successful")
                    print(f"  Planning time: {planning_info.get('planning_time', 'N/A')} seconds")
                    print(f"  Actions planned: {planning_info.get('actions_planned', 'N/A')}")
                    
                    # Show task assignments after auto-planning
                    task_status = planning_info.get("task_status", [])
                    print("  Task assignments after auto-planning:")
                    for agent_status in task_status:
                        if isinstance(agent_status, dict):
                            agent_id = agent_status.get("agent_id")
                            has_task = agent_status.get("has_task")
                            if has_task and agent_status.get("current_task"):
                                current_task = agent_status["current_task"]
                                task_id = current_task.get("task_id")
                                task_location = current_task.get("location")
                                print(f"    Agent {agent_id}: Task {task_id} at location {task_location}")
                            else:
                                print(f"    Agent {agent_id}: No task assigned")
                else:
                    print("✗ Auto-planning failed")
                    print(f"  Error: {planning_info.get('error', 'Unknown error')}")
            else:
                print("⚠ Auto-planning was not triggered")
                print(f"  Reason: {planning_info.get('message', 'Unknown')}")
        else:
            print("✗ Failed to add task")
            if result:
                print(f"Error: {result.get('message', 'Unknown error')}")
        
        return result

def run_continuous_simulation(max_timesteps=200):
    """Run a continuous simulation until all tasks are completed or max timesteps reached"""
    print("=== Starting Continuous MAPF Simulation ===")
    
    # Check server health
    print("Checking server health...")
    try:
        response = requests.get("http://localhost:8080/health")
        if response.status_code != 200:
            print("Server is not healthy. Please start the server first.")
            return False
    except Exception as e:
        print(f"Cannot connect to server: {e}")
        return False
    
    # Reset simulation
    print("Resetting simulation...")
    try:
        response = requests.post("http://localhost:8080/reset")
        if response.status_code != 200:
            print("Failed to reset simulation")
            return False
    except Exception as e:
        print(f"Reset failed: {e}")
        return False
    
    print(f"Running simulation for up to {max_timesteps} timesteps...")
    print("The server will automatically:")
    print("- Assign tasks to free agents")
    print("- Track task completion")
    print("- Plan paths for agents to reach their tasks")
    print()
    
    # Initial agent positions
    agents = [
        {"location": 2, "orientation": 0, "timestep": 0},
        {"location": 3, "orientation": 0, "timestep": 0},
        {"location": 4, "orientation": 0, "timestep": 0},
        {"location": 5, "orientation": 0, "timestep": 0},
        {"location": 6, "orientation": 0, "timestep": 0}
    ]
    
    total_tasks_completed = 0
    all_tasks_finished = False
    
    for timestep in range(1, max_timesteps + 1):
        try:
            # Update agent timesteps
            for agent in agents:
                agent["timestep"] = timestep
            
            # Send planning request
            response = requests.post(
                "http://localhost:8080/plan",
                json={"agents": agents},
                headers={"Content-Type": "application/json"}
            )
            
            if response.status_code != 200:
                print(f"Planning request failed at timestep {timestep}")
                return False
            
            result = response.json()
            
            if result.get("status") != "success":
                print(f"Planning failed at timestep {timestep}: {result.get('message', 'Unknown error')}")
                return False
            
            # Extract information
            current_timestep = result.get("timestep", timestep)
            tasks_remaining = result.get("tasks_remaining", 0)
            total_tasks_completed = result.get("total_tasks_completed", 0)
            all_tasks_finished = result.get("all_tasks_finished", False)
            
            # Update agent positions based on planned actions
            planned_actions = result.get("actions", [])
            if planned_actions:
                for i, action_data in enumerate(planned_actions):
                    if i < len(agents):
                        new_location = action_data.get("location", agents[i]["location"])
                        new_orientation = action_data.get("orientation", agents[i]["orientation"])
                        agents[i]["location"] = new_location
                        agents[i]["orientation"] = new_orientation
            
            # Display task status
            print(f"--- Timestep {timestep} ---")
            task_status = result.get("task_status", [])
            print("Task Status:")
            for agent_status in task_status:
                if isinstance(agent_status, dict):
                    agent_id = agent_status.get("agent_id", "?")
                    has_task = agent_status.get("has_task", False)
                    tasks_completed = agent_status.get("tasks_completed", 0)
                    
                    if has_task:
                        current_task = agent_status.get("current_task", {})
                        task_id = current_task.get("task_id", "?")
                        location = current_task.get("location", "?")
                        print(f"  Agent {agent_id}: Task {task_id} at location {location} (completed: {tasks_completed})")
                    else:
                        print(f"  Agent {agent_id}: No task (completed: {tasks_completed})")
            
            print(f"Progress: {total_tasks_completed} tasks completed, {tasks_remaining} tasks remaining")
            
            # Check if all tasks are finished
            if all_tasks_finished:
                print(f"\n🎉 All tasks completed at timestep {timestep}!")
                break
            
            # Check if no more tasks and all agents are free
            if tasks_remaining == 0 and all(not status.get("has_task", True) for status in task_status if isinstance(status, dict)):
                print(f"\n✅ No more tasks to assign at timestep {timestep}")
                break
            
        except requests.exceptions.ConnectionError as e:
            print(f"Connection lost at timestep {timestep}: {e}")
            return False
        except Exception as e:
            print(f"Error at timestep {timestep}: {e}")
            return False
    
    print("\n=== Simulation Complete ===")
    print(f"Final timestep: {timestep}")
    print(f"Total tasks completed: {total_tasks_completed}")
    print(f"All tasks finished: {all_tasks_finished}")
    print("Continuous simulation completed successfully!")
    return True

def main():
    if len(sys.argv) > 1:
        server_url = sys.argv[1]
    else:
        server_url = "http://localhost:8080"
    
    client = ContinuousMAPFClient(server_url)
    
    # Run continuous simulation
    success = run_continuous_simulation(max_timesteps=200)
    
    if success:
        print("Continuous simulation completed successfully!")
    else:
        print("Continuous simulation failed!")
        sys.exit(1)

if __name__ == "__main__":
    main() 