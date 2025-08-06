#!/usr/bin/env python3
"""
MAPF Server Client Example

This script demonstrates how to interact with the MAPF server
to request path planning for agents and generate a comprehensive report.
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
    
    def reset_simulation(self):
        """Reset the simulation session"""
        try:
            response = requests.post(f"{self.server_url}/reset")
            return response.json()
        except requests.exceptions.RequestException as e:
            print(f"Reset failed: {e}")
            return None
    
    def plan_step(self, agents, goals):
        """
        Request path planning for agents for a single timestep
        
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
    
    def get_report(self):
        """Get the simulation report"""
        try:
            response = requests.get(f"{self.server_url}/report")
            return response.json()
        except requests.exceptions.RequestException as e:
            print(f"Report request failed: {e}")
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
    
    # Reset the simulation for a fresh start
    print("\nResetting simulation...")
    reset_result = client.reset_simulation()
    if reset_result:
        print(f"Reset result: {reset_result}")
    
    # Example multi-step simulation
    print("\nStarting multi-step simulation...")
    
    # Initial agent positions (5 agents)
    current_agents = [
        {"location": 2, "orientation": 0, "timestep": 0},   # Agent 0
        {"location": 3, "orientation": 0, "timestep": 0},   # Agent 1
        {"location": 4, "orientation": 0, "timestep": 0},   # Agent 2
        {"location": 5, "orientation": 0, "timestep": 0},   # Agent 3
        {"location": 6, "orientation": 0, "timestep": 0}    # Agent 4
    ]
    
    # Initial goals
    goals = [
        {"location": 96, "timestep": 0},  # Goal for agent 0
        {"location": 95, "timestep": 0},  # Goal for agent 1
        {"location": 91, "timestep": 0},  # Goal for agent 2
        {"location": 94, "timestep": 0},  # Goal for agent 3
        {"location": 93, "timestep": 0}   # Goal for agent 4
    ]
    
    # Run simulation for multiple timesteps
    max_timesteps = 50
    for timestep in range(max_timesteps):
        print(f"\nTimestep {timestep + 1}/{max_timesteps}")
        
        # Request planning
        result = client.plan_step(current_agents, goals)
        
        if result and result.get("status") == "success":
            # Update agent positions based on the planned actions
            actions = result.get("actions", [])
            for action_data in actions:
                agent_id = action_data["agent_id"]
                action = action_data["action"]
                new_location = action_data["location"]
                new_orientation = action_data["orientation"]
                
                # Update agent state (simplified - in reality you'd use the action model)
                current_agents[agent_id]["location"] = new_location
                current_agents[agent_id]["orientation"] = new_orientation
                current_agents[agent_id]["timestep"] = timestep + 1
                
                print(f"  Agent {agent_id}: {action} -> location {new_location}")
            
            # Check if all agents have reached their goals (simplified check)
            all_at_goals = True
            for i, agent in enumerate(current_agents):
                if agent["location"] != goals[i]["location"]:
                    all_at_goals = False
                    break
            
            if all_at_goals:
                print("All agents reached their goals!")
                break
        else:
            print(f"Planning failed at timestep {timestep + 1}")
            break
    
    # Get the final report
    print("\nGenerating simulation report...")
    report = client.get_report()
    
    if report:
        print("Report generated successfully!")
        print(f"Team size: {report.get('teamSize', 'N/A')}")
        print(f"Tasks finished: {report.get('numTaskFinished', 'N/A')}")
        print(f"Sum of cost: {report.get('sumOfCost', 'N/A')}")
        print(f"Makespan: {report.get('makespan', 'N/A')}")
        
        # Save report to file
        with open("test_output.json", "w") as f:
            json.dump(report, f, indent=4)
        print("Report saved to test_output.json")
    else:
        print("Failed to generate report")

if __name__ == "__main__":
    main() 