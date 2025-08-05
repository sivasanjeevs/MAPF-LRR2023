#!/usr/bin/env python3

import requests
import json
import time

def test_multiple_runs():
    base_url = "http://localhost:8080"
    
    print("Testing multiple MAPF Server planning steps...")
    
    # Reset once at the beginning
    try:
        response = requests.post(f"{base_url}/reset")
        print(f"Reset: {response.json()}")
    except Exception as e:
        print(f"Reset failed: {e}")
        return
    
    # Test planning with initial agent states
    agents = [
        {"location": 2, "orientation": 0, "timestep": 0},
        {"location": 3, "orientation": 0, "timestep": 0},
        {"location": 4, "orientation": 0, "timestep": 0},
        {"location": 5, "orientation": 0, "timestep": 0},
        {"location": 6, "orientation": 0, "timestep": 0}
    ]
    
    # Run multiple planning steps in sequence
    for step in range(50):  # Run up to 50 steps or until all tasks are done
        print(f"\n=== Planning Step {step + 1} ===")
        
        try:
            print("Sending planning request...")
            response = requests.post(f"{base_url}/plan", json={"agents": agents})
            if response.status_code == 200:
                result = response.json()
                print(f"Planning successful: {result['status']}")
                print(f"Planning time: {result['planning_time']:.6f}s")
                
                # Update agent positions for next step
                for action_data in result['actions']:
                    agent_id = action_data['agent_id']
                    if agent_id < len(agents):
                        agents[agent_id]['location'] = action_data['location']
                        agents[agent_id]['orientation'] = action_data['orientation']
                        agents[agent_id]['timestep'] = step + 1
                
                print(f"Updated agent positions:")
                for i, agent in enumerate(agents):
                    print(f"  Agent {i}: location {agent['location']}, orientation {agent['orientation']}")
                
                # Check if we should continue
                try:
                    response = requests.get(f"{base_url}/report")
                    if response.status_code == 200:
                        report = response.json()
                        tasks_finished = report.get('numTaskFinished', 0)
                        total_tasks = len(report.get('tasks', []))
                        print(f"Tasks finished: {tasks_finished}/{total_tasks}")
                        
                        # Stop if all tasks are finished
                        if tasks_finished >= total_tasks and total_tasks > 0:
                            print("All tasks completed!")
                            break
                    else:
                        print(f"Report failed: {response.text}")
                except Exception as e:
                    print(f"Report request failed: {e}")
            else:
                print(f"Planning failed: {response.text}")
                break
        except Exception as e:
            print(f"Planning request failed: {e}")
            break
        
        time.sleep(0.1)  # Small delay between steps
    
    # Final report
    print("\n=== Final Report ===")
    try:
        response = requests.get(f"{base_url}/report")
        if response.status_code == 200:
            report = response.json()
            print(f"Final tasks finished: {report.get('numTaskFinished', 0)}")
            print(f"Team size: {report.get('teamSize', 0)}")
            print(f"All valid: {report.get('AllValid', 'Unknown')}")
            print(f"Sum of cost: {report.get('sumOfCost', 0)}")
            print(f"Makespan: {report.get('makespan', 0)}")
            
            # Show path lengths
            actual_paths = report.get('actualPaths', [])
            planner_paths = report.get('plannerPaths', [])
            print(f"\nPath lengths:")
            for i in range(len(actual_paths)):
                actual_len = len(actual_paths[i].split(',')) if actual_paths[i] else 0
                planner_len = len(planner_paths[i].split(',')) if planner_paths[i] else 0
                print(f"  Agent {i}: actual={actual_len}, planner={planner_len}")
        else:
            print(f"Final report failed: {response.text}")
    except Exception as e:
        print(f"Final report request failed: {e}")

if __name__ == "__main__":
    test_multiple_runs() 