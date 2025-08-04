#!/usr/bin/env python3
"""
MAPF Server Client Example

This script demonstrates how to interact with the MAPF server
to request path planning for agents.
"""

import requests
import json
import time

class MAPFClient:
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
    
    def get_status(self):
        """Get server status"""
        try:
            response = requests.get(f"{self.server_url}/status")
            return response.json()
        except requests.exceptions.RequestException as e:
            print(f"Status check failed: {e}")
            return None
    
    def plan_path(self, agents, goals):
        """
        Request path planning for agents
        
        Args:
            agents: List of agent states [{"location": int, "orientation": int, "timestep": int}]
            goals: List of goal locations [{"location": int, "timestep": int}]
        
        Returns:
            JSON response with planned actions
        """
        request_data = {
            "agents": agents,
            "goals": goals
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

def main():
    # Create client
    client = MAPFClient()
    
    # Check server health
    print("Checking server health...")
    health = client.health_check()
    if health:
        print(f"Server health: {health}")
    else:
        print("Server is not responding")
        return
    
    # Get server status
    print("\nGetting server status...")
    status = client.get_status()
    if status:
        print(f"Server status: {json.dumps(status, indent=2)}")
    
    # Example planning request
    print("\nSubmitting planning request...")
    
    # Example agents (5 agents at different locations)
    agents = [
        {"location": 2, "orientation": 0, "timestep": 0},   # Agent 0
        {"location": 3, "orientation": 0, "timestep": 0},   # Agent 1
        # {"location": 4, "orientation": 0, "timestep": 0},   # Agent 2
        # {"location": 5, "orientation": 0, "timestep": 0},   # Agent 3
        # {"location": 6, "orientation": 0, "timestep": 0}    # Agent 4
    ]
    
    # Example goals (different target locations)
    goals = [
        {"location": 96, "timestep": 0},  # Goal for agent 0
        {"location": 95, "timestep": 0},  # Goal for agent 1
        # {"location": 91, "timestep": 0},  # Goal for agent 2
        # {"location": 94, "timestep": 0},  # Goal for agent 3
        # {"location": 93, "timestep": 0}   # Goal for agent 4
    ]
    
    # Request planning
    result = client.plan_path(agents, goals)
    
    if result:
        print("Planning result:")
        print(json.dumps(result, indent=2))
        
        # Parse and display actions
        if "actions" in result:
            print("\nPlanned actions:")
            for action_data in result["actions"]:
                agent_id = action_data["agent_id"]
                action = action_data["action"]
                location = action_data["location"]
                orientation = action_data["orientation"]
                print(f"Agent {agent_id}: {action} at location {location}, orientation {orientation}")
    else:
        print("Planning failed")

if __name__ == "__main__":
    main() 