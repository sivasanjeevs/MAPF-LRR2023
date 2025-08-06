#!/usr/bin/env python3
"""
MAPF Server Add Task Client

This script helps you add new pickup and delivery tasks to the MAPF server dynamically.
"""

import requests
import json
import sys
import argparse

class AddTaskClient:
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
    
    def add_task(self, start_location, goal_location):
        """
        Add a new pickup and delivery task.
        
        Args:
            start_location: The pickup location.
            goal_location: The delivery location.
        
        Returns:
            JSON response from server.
        """
        request_data = {
            "start_location": start_location,
            "goal_location": goal_location
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
    
    def convert_location_to_coordinates(self, location, map_width=10):
        """Convert linearized location to (row, col) coordinates"""
        row = location // map_width
        col = location % map_width
        return row, col

def main():
    parser = argparse.ArgumentParser(description="Add pickup and delivery tasks to MAPF server")
    parser.add_argument("--server", default="http://localhost:8080", 
                       help="Server URL (default: http://localhost:8080)")
    # New arguments for start and goal
    parser.add_argument("--start", type=int, 
                       help="Linearized start (pickup) location")
    parser.add_argument("--goal", type=int, 
                       help="Linearized goal (delivery) location")
    parser.add_argument("--status", action="store_true", 
                       help="Show current task status")
    
    args = parser.parse_args()
    
    client = AddTaskClient(args.server)
    
    print("Checking server health...")
    health = client.health_check()
    if not health or health.get("status") != "healthy":
        print("✗ Server is not responding or not healthy.")
        return 1
    
    print("✓ Server is healthy.")
    
    if args.status:
        print("\nCurrent task status:")
        status = client.get_task_status()
        if status and isinstance(status, list):
            for agent_status in status:
                agent_id = agent_status.get("agent_id")
                has_task = agent_status.get("has_task")
                is_carrying = agent_status.get("is_carrying_task", False) # Check if agent is carrying task
                tasks_completed = agent_status.get("tasks_completed")
                
                if has_task and agent_status.get("current_task"):
                    current_task = agent_status["current_task"]
                    task_id = current_task.get("task_id")
                    start_loc = current_task.get("start_location")
                    goal_loc = current_task.get("goal_location")
                    
                    if is_carrying:
                        print(f"  Agent {agent_id}: Delivering task {task_id} to {goal_loc} [completed: {tasks_completed}]")
                    else:
                        print(f"  Agent {agent_id}: Picking up task {task_id} at {start_loc} [completed: {tasks_completed}]")

                else:
                    print(f"  Agent {agent_id}: Idle [completed: {tasks_completed}]")
        else:
            print("Could not retrieve task status.")
        return 0
    
    if args.start is None or args.goal is None:
        print("Error: Both --start and --goal must be specified to add a task.")
        return 1
    
    # Validate locations
    if not (0 <= args.start < 100 and 0 <= args.goal < 100): # Assuming 10x10 map
        print(f"Error: Locations must be between 0 and 99.")
        return 1
    
    start_coords = client.convert_location_to_coordinates(args.start)
    goal_coords = client.convert_location_to_coordinates(args.goal)
    print(f"\nAdding task from {args.start} {start_coords} to {args.goal} {goal_coords}")
    
    result = client.add_task(args.start, args.goal)
    
    if result and result.get("status") == "success":
        print("✓ Task added successfully!")
        print(f"  Task ID: {result.get('task_id')}")
    else:
        print("✗ Failed to add task.")
        if result:
            print(f"  Error: {result.get('error', 'Unknown error')}")
        return 1
    
    return 0

if __name__ == "__main__":
    sys.exit(main())