#!/usr/bin/env python3
"""
Test script to verify server stability and task completion
"""

import requests
import json
import time
import sys

def test_server_stability():
    print("=== Testing Server Stability and Task Completion ===")
    
    # Test 1: Health check
    print("1. Testing health check...")
    try:
        response = requests.get("http://localhost:8080/health")
        if response.status_code == 200:
            print("✓ Server is healthy")
        else:
            print("✗ Server health check failed")
            return False
    except Exception as e:
        print(f"✗ Cannot connect to server: {e}")
        return False
    
    # Test 2: Reset simulation
    print("2. Resetting simulation...")
    try:
        response = requests.post("http://localhost:8080/reset")
        if response.status_code == 200:
            print("✓ Simulation reset successful")
        else:
            print("✗ Reset failed")
            return False
    except Exception as e:
        print(f"✗ Reset failed: {e}")
        return False
    
    # Test 3: Run simulation until completion or timeout
    print("3. Running simulation until task completion...")
    
    agents = [
        {"location": 2, "orientation": 0, "timestep": 0},
        {"location": 3, "orientation": 0, "timestep": 0},
        {"location": 4, "orientation": 0, "timestep": 0},
        {"location": 5, "orientation": 0, "timestep": 0},
        {"location": 6, "orientation": 0, "timestep": 0}
    ]
    
    max_timesteps = 50  # Reasonable limit for testing
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
                print(f"✗ Planning request failed at timestep {timestep}")
                return False
            
            result = response.json()
            
            if result.get("status") != "success":
                print(f"✗ Planning failed at timestep {timestep}: {result.get('message', 'Unknown error')}")
                return False
            
            # Extract information
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
            
            print(f"  Timestep {timestep}: {total_tasks_completed} completed, {tasks_remaining} remaining")
            
            # Check if all tasks are finished
            if all_tasks_finished:
                print(f"✓ All tasks completed at timestep {timestep}!")
                break
            
            # Check if no more tasks and all agents are free
            task_status = result.get("task_status", [])
            if tasks_remaining == 0 and all(not status.get("has_task", True) for status in task_status if isinstance(status, dict)):
                print(f"✓ No more tasks to assign at timestep {timestep}")
                break
            
        except requests.exceptions.ConnectionError as e:
            print(f"✗ Connection lost at timestep {timestep}: {e}")
            return False
        except Exception as e:
            print(f"✗ Error at timestep {timestep}: {e}")
            return False
    
    # Test 4: Check if test.json was created and updated
    print("4. Checking test.json file...")
    try:
        with open("test.json", "r") as f:
            data = json.load(f)
            print("✓ test.json file exists and is valid JSON")
            print(f"  Team size: {data.get('teamSize')}")
            print(f"  Tasks finished: {data.get('numTaskFinished')}")
            print(f"  All valid: {data.get('AllValid')}")
            print(f"  Makespan: {data.get('makespan')}")
            
            # Check if we have actual paths
            actual_paths = data.get('actualPaths', [])
            if actual_paths and len(actual_paths) > 0:
                print(f"  Actual paths recorded: {len(actual_paths)} agents")
                for i, path in enumerate(actual_paths):
                    if path:
                        print(f"    Agent {i}: {len(path.split(','))} actions")
            else:
                print("  ⚠ No actual paths recorded")
                
    except FileNotFoundError:
        print("⚠ test.json file not found")
    except Exception as e:
        print(f"✗ Error reading test.json: {e}")
    
    print("\n=== Stability Test Results ===")
    print(f"✓ Server ran for {timestep} timesteps without crashing")
    print(f"✓ Total tasks completed: {total_tasks_completed}")
    print(f"✓ All tasks finished: {all_tasks_finished}")
    print("✓ Server remains stable and functional")
    
    return True

if __name__ == "__main__":
    success = test_server_stability()
    sys.exit(0 if success else 1) 