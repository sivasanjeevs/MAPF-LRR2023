#!/usr/bin/env python3
"""
MAPF Server Continuous Client Example

This script demonstrates continuous task assignment and execution
where the server automatically assigns tasks to free agents and
tracks task completion. This version runs continuously to keep the
server session active.
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

def run_continuous_simulation(server_url="http://localhost:8080"):
    """Run a continuous simulation that keeps the session active."""
    print("=== Starting Continuous MAPF Simulation (Persistent Session) ===")
    
    # Initial agent positions (these will be used to start the session)
    agents = [
        {"location": 2, "orientation": 0, "timestep": 0},
        {"location": 3, "orientation": 0, "timestep": 0},
        {"location": 4, "orientation": 0, "timestep": 0},
        {"location": 5, "orientation": 0, "timestep": 0},
        {"location": 6, "orientation": 0, "timestep": 0}
    ]
    
    timestep = 0
    
    try:
        # --- Initial Setup ---
        print("1. Checking server health...")
        response = requests.get(f"{server_url}/health")
        if response.status_code != 200:
            print("✗ Server is not healthy. Please start the server first.")
            return False
        print("✓ Server is healthy.")

        print("\n2. Resetting simulation...")
        response = requests.post(f"{server_url}/reset")
        if response.status_code != 200:
            print("✗ Failed to reset simulation.")
            return False
        print("✓ Simulation reset.")
        
        print("\n3. Sending initial planning request to start the session...")
        response = requests.post(
            f"{server_url}/plan",
            json={"agents": agents},
            headers={"Content-Type": "application/json"}
        )
        if response.status_code != 200 or response.json().get("status") != "success":
            print("✗ Failed to start the session.")
            return False
        print("✓ Session started successfully.")
        
        # Update agents based on initial plan
        result = response.json()
        planned_actions = result.get("actions", [])
        if planned_actions:
            for i, action_data in enumerate(planned_actions):
                if i < len(agents):
                    agents[i]["location"] = action_data.get("location", agents[i]["location"])
                    agents[i]["orientation"] = action_data.get("orientation", agents[i]["orientation"])
        
        print("\n4. Now running continuously. Add tasks from another terminal.")
        print("   Example: python3 examples/add_task_client.py --location 29")
        print("   Press Ctrl+C to stop the client.")
        
        # --- Continuous Loop ---
        while True:
            timestep += 1
            for agent in agents:
                agent["timestep"] = timestep

            response = requests.post(
                f"{server_url}/plan",
                json={"agents": agents},
                headers={"Content-Type": "application/json"}
            )
            
            if response.status_code != 200:
                print(f"Communication error at timestep {timestep}.")
                time.sleep(2)
                continue

            result = response.json()
            if result.get("status") != "success":
                print(f"Planning failed at timestep {timestep}: {result.get('message', 'Unknown error')}")
                time.sleep(2)
                continue

            # Update agent positions for the next step
            planned_actions = result.get("actions", [])
            if planned_actions:
                for i, action_data in enumerate(planned_actions):
                    if i < len(agents):
                        agents[i]["location"] = action_data.get("location", agents[i]["location"])
                        agents[i]["orientation"] = action_data.get("orientation", agents[i]["orientation"])

            # Display status
            total_completed = result.get("total_tasks_completed", 0)
            tasks_remaining = result.get("tasks_remaining", 0)
            
            print(f"\rTimestep: {timestep} | Tasks Completed: {total_completed} | Tasks in Queue: {tasks_remaining}", end="")
            
            time.sleep(1) # Poll every second

    except requests.exceptions.ConnectionError as e:
        print(f"\nConnection lost: {e}")
    except KeyboardInterrupt:
        print("\n\nClient stopped by user.")
    except Exception as e:
        print(f"\nAn error occurred: {e}")
        
    print("=== Simulation Client Finished ===")
    return True

def main():
    if len(sys.argv) > 1:
        server_url = sys.argv[1]
    else:
        server_url = "http://localhost:8080"
    
    run_continuous_simulation(server_url)

if __name__ == "__main__":
    main()