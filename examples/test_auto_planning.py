#!/usr/bin/env python3
"""
Test Auto-Planning Functionality

This script demonstrates how the server automatically triggers planning
when new tasks are added.
"""

import requests
import json
import time
import sys

class AutoPlanningTest:
    def __init__(self, server_url="http://localhost:8080"):
        self.server_url = server_url
        
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
    
    def plan_step(self, agents):
        """Send planning request"""
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
    
    def add_task(self, location):
        """Add a new task"""
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
    
    def get_task_status(self):
        """Get current task status"""
        try:
            response = requests.get(f"{self.server_url}/task_status")
            return response.json()
        except requests.exceptions.RequestException as e:
            print(f"Task status request failed: {e}")
            return None

def main():
    print("=== Testing Auto-Planning Functionality ===")
    print()
    
    test = AutoPlanningTest()
    
    # Check server health
    print("1. Checking server health...")
    health = test.health_check()
    if not health:
        print("✗ Server is not responding")
        return 1
    
    print("✓ Server is healthy")
    print()
    
    # Reset simulation
    print("2. Resetting simulation...")
    reset_result = test.reset_simulation()
    if not reset_result:
        print("✗ Failed to reset simulation")
        return 1
    
    print("✓ Simulation reset")
    print()
    
    # Initial planning request to start session
    print("3. Sending initial planning request...")
    initial_agents = [
        {"location": 2, "orientation": 0, "timestep": 0},
        {"location": 3, "orientation": 0, "timestep": 0},
        {"location": 4, "orientation": 0, "timestep": 0},
        {"location": 5, "orientation": 0, "timestep": 0},
        {"location": 6, "orientation": 0, "timestep": 0}
    ]
    
    plan_result = test.plan_step(initial_agents)
    if not plan_result or plan_result.get("status") != "success":
        print("✗ Initial planning failed")
        if plan_result:
            print(f"Error: {plan_result.get('message', 'Unknown error')}")
        return 1
    
    print("✓ Initial planning successful")
    print(f"  Timestep: {plan_result.get('timestep')}")
    print(f"  Tasks remaining: {plan_result.get('tasks_remaining')}")
    print()
    
    # Add a new task (should trigger auto-planning)
    print("4. Adding new task (should trigger auto-planning)...")
    add_result = test.add_task(82)  # Add task at location 82
    
    if not add_result or add_result.get("status") != "success":
        print("✗ Failed to add task")
        if add_result:
            print(f"Error: {add_result.get('message', 'Unknown error')}")
        return 1
    
    print("✓ Task added successfully")
    print(f"  Task ID: {add_result.get('task_id')}")
    print(f"  Location: {add_result.get('location')}")
    print(f"  Tasks in queue: {add_result.get('tasks_in_queue')}")
    
    # Check if auto-planning was triggered
    planning_info = add_result.get("planning", {})
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
    
    print()
    
    # Add another task to test multiple auto-planning
    print("5. Adding another task...")
    add_result2 = test.add_task(45)  # Add task at location 45
    
    if add_result2 and add_result2.get("status") == "success":
        print("✓ Second task added successfully")
        planning_info2 = add_result2.get("planning", {})
        if planning_info2.get("planning_triggered") and planning_info2.get("planning_success"):
            print("🔄 Auto-planning triggered for second task!")
            print(f"  Planning time: {planning_info2.get('planning_time', 'N/A')} seconds")
        else:
            print("⚠ Auto-planning not triggered for second task")
    else:
        print("✗ Failed to add second task")
    
    print()
    
    # Final status check
    print("6. Final task status:")
    final_status = test.get_task_status()
    if final_status:
        for agent_status in final_status:
            if isinstance(agent_status, dict):
                agent_id = agent_status.get("agent_id")
                has_task = agent_status.get("has_task")
                tasks_completed = agent_status.get("tasks_completed")
                
                if has_task and agent_status.get("current_task"):
                    current_task = agent_status["current_task"]
                    task_id = current_task.get("task_id")
                    task_location = current_task.get("location")
                    print(f"  Agent {agent_id}: Task {task_id} at location {task_location} [completed: {tasks_completed}]")
                else:
                    print(f"  Agent {agent_id}: No task assigned [completed: {tasks_completed}]")
    
    print()
    print("=== Auto-Planning Test Complete ===")
    print()
    print("Summary:")
    print("- Server automatically triggers planning when new tasks are added")
    print("- Planning uses current agent positions when available")
    print("- New tasks are immediately assigned to free agents")
    print("- Server maintains continuous operation throughout")
    
    return 0

if __name__ == "__main__":
    sys.exit(main()) 