#!/usr/bin/env python3
"""
MAPF Server Add Task Client

This script helps you add new tasks to the MAPF server dynamically.
It can add tasks by location coordinates or by linearized position.
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
    
    def get_task_status(self):
        """Get current task status"""
        try:
            response = requests.get(f"{self.server_url}/task_status")
            return response.json()
        except requests.exceptions.RequestException as e:
            print(f"Task status request failed: {e}")
            return None
    
    def convert_coordinates_to_location(self, row, col, map_width=10):
        """Convert (row, col) coordinates to linearized location"""
        return row * map_width + col
    
    def convert_location_to_coordinates(self, location, map_width=10):
        """Convert linearized location to (row, col) coordinates"""
        row = location // map_width
        col = location % map_width
        return row, col

def main():
    parser = argparse.ArgumentParser(description="Add tasks to MAPF server")
    parser.add_argument("--server", default="http://localhost:8080", 
                       help="Server URL (default: http://localhost:8080)")
    parser.add_argument("--location", type=int, 
                       help="Linearized location (0-99 for 10x10 map)")
    parser.add_argument("--row", type=int, 
                       help="Row coordinate (0-9 for 10x10 map)")
    parser.add_argument("--col", type=int, 
                       help="Column coordinate (0-9 for 10x10 map)")
    parser.add_argument("--status", action="store_true", 
                       help="Show current task status")
    
    args = parser.parse_args()
    
    client = AddTaskClient(args.server)
    
    # Check server health
    print("Checking server health...")
    health = client.health_check()
    if not health:
        print("Server is not responding")
        return 1
    
    print("✓ Server is healthy")
    
    # Show task status if requested
    if args.status:
        print("\nCurrent task status:")
        status = client.get_task_status()
        if status:
            for agent_status in status:
                agent_id = agent_status["agent_id"]
                has_task = agent_status["has_task"]
                tasks_completed = agent_status["tasks_completed"]
                
                if has_task:
                    current_task = agent_status["current_task"]
                    task_id = current_task["task_id"]
                    task_location = current_task["location"]
                    row, col = client.convert_location_to_coordinates(task_location)
                    print(f"  Agent {agent_id}: Task {task_id} at ({row},{col}) [completed: {tasks_completed}]")
                else:
                    print(f"  Agent {agent_id}: No task assigned [completed: {tasks_completed}]")
        return 0
    
    # Determine location
    location = None
    if args.location is not None:
        location = args.location
    elif args.row is not None and args.col is not None:
        location = client.convert_coordinates_to_location(args.row, args.col)
    else:
        print("Error: Must specify either --location or both --row and --col")
        return 1
    
    # Validate location
    if location < 0 or location >= 100:  # Assuming 10x10 map
        print(f"Error: Location {location} is out of bounds (0-99)")
        return 1
    
    # Show location info
    row, col = client.convert_location_to_coordinates(location)
    print(f"Adding task at location {location} (coordinates: {row},{col})")
    
    # Add task
    result = client.add_task(location)
    
    if result and result.get("status") == "success":
        print("✓ Task added successfully!")
        print(f"  Task ID: {result.get('task_id')}")
        print(f"  Location: {result.get('location')} (coordinates: {row},{col})")
        print(f"  Tasks in queue: {result.get('tasks_in_queue')}")
        
        # Show updated task status
        print("\nUpdated task status:")
        status = client.get_task_status()
        if status:
            for agent_status in status:
                if isinstance(agent_status, dict):  # Check if it's a dictionary
                    agent_id = agent_status.get("agent_id")
                    has_task = agent_status.get("has_task")
                    tasks_completed = agent_status.get("tasks_completed")
                    
                    if has_task and agent_status.get("current_task"):
                        current_task = agent_status["current_task"]
                        task_id = current_task.get("task_id")
                        task_location = current_task.get("location")
                        task_row, task_col = client.convert_location_to_coordinates(task_location)
                        print(f"  Agent {agent_id}: Task {task_id} at ({task_row},{task_col}) [completed: {tasks_completed}]")
                    else:
                        print(f"  Agent {agent_id}: No task assigned [completed: {tasks_completed}]")
                else:
                    print(f"  Invalid agent status format: {agent_status}")
    else:
        print("✗ Failed to add task")
        if result:
            print(f"Error: {result.get('message', 'Unknown error')}")
        return 1
    
    return 0

if __name__ == "__main__":
    sys.exit(main()) 