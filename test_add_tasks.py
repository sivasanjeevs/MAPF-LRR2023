#!/usr/bin/env python3

import requests
import json
import time

def test_add_tasks():
    base_url = "http://localhost:8080"
    
    print("Testing MAPF Server with dynamic task addition...")
    
    # Reset the server
    try:
        response = requests.post(f"{base_url}/reset")
        print(f"Reset: {response.json()}")
    except Exception as e:
        print(f"Reset failed: {e}")
        return
    
    # Initial agent states
    agents = [
        {"location": 2, "orientation": 0, "timestep": 0},
        {"location": 3, "orientation": 0, "timestep": 0},
        {"location": 4, "orientation": 0, "timestep": 0},
        {"location": 5, "orientation": 0, "timestep": 0},
        {"location": 6, "orientation": 0, "timestep": 0}
    ]
    
    # Run a few planning steps to get started
    print("\n=== Initial Planning Steps ===")
    for step in range(5):
        try:
            response = requests.post(f"{base_url}/plan", json={"agents": agents})
            if response.status_code == 200:
                result = response.json()
                print(f"Step {step + 1}: {result['status']}")
                
                # Update agent positions
                for action_data in result['actions']:
                    agent_id = action_data['agent_id']
                    if agent_id < len(agents):
                        agents[agent_id]['location'] = action_data['location']
                        agents[agent_id]['orientation'] = action_data['orientation']
                        agents[agent_id]['timestep'] = step + 1
            else:
                print(f"Planning failed: {response.text}")
                break
        except Exception as e:
            print(f"Planning request failed: {e}")
            break
    
    # Check current status
    try:
        response = requests.get(f"{base_url}/report")
        if response.status_code == 200:
            report = response.json()
            print(f"\nCurrent status:")
            print(f"  Tasks finished: {report.get('numTaskFinished', 0)}")
            print(f"  Total tasks: {len(report.get('tasks', []))}")
    except Exception as e:
        print(f"Report request failed: {e}")
    
    # Add new tasks
    print("\n=== Adding New Tasks ===")
    new_goals = [
        {"location": 20},  # New goal location
        {"location": 25},  # Another new goal
        {"location": 30}   # Third new goal
    ]
    
    try:
        response = requests.post(f"{base_url}/add_tasks", json={"goals": new_goals})
        if response.status_code == 200:
            result = response.json()
            print(f"Add tasks result: {result}")
        else:
            print(f"Add tasks failed: {response.text}")
    except Exception as e:
        print(f"Add tasks request failed: {e}")
    
    # Continue planning to see if new tasks are assigned
    print("\n=== Continuing Planning with New Tasks ===")
    for step in range(10):
        try:
            response = requests.post(f"{base_url}/plan", json={"agents": agents})
            if response.status_code == 200:
                result = response.json()
                print(f"Step {step + 6}: {result['status']}")
                
                # Update agent positions
                for action_data in result['actions']:
                    agent_id = action_data['agent_id']
                    if agent_id < len(agents):
                        agents[agent_id]['location'] = action_data['location']
                        agents[agent_id]['orientation'] = action_data['orientation']
                        agents[agent_id]['timestep'] = step + 6
                
                # Check if new tasks are being assigned
                try:
                    response = requests.get(f"{base_url}/report")
                    if response.status_code == 200:
                        report = response.json()
                        tasks_finished = report.get('numTaskFinished', 0)
                        total_tasks = len(report.get('tasks', []))
                        print(f"  Tasks finished: {tasks_finished}/{total_tasks}")
                        
                        # Show some events to see task assignments
                        events = report.get('events', [])
                        for i, agent_events in enumerate(events):
                            if agent_events:
                                latest_event = agent_events[-1]
                                print(f"  Agent {i} latest event: {latest_event}")
                except Exception as e:
                    print(f"Report request failed inside planning loop: {e}")
            
            else:
                print(f"Planning failed: {response.text}")
                break
        except Exception as e:
            print(f"Planning request failed: {e}")
            break
    
    # Final report
    print("\n=== Final Report ===")
    try:
        response = requests.get(f"{base_url}/report")
        if response.status_code == 200:
            report = response.json()
            print(f"Final tasks finished: {report.get('numTaskFinished', 0)}")
            print(f"Total tasks: {len(report.get('tasks', []))}")
            print(f"Team size: {report.get('teamSize', 0)}")
            print(f"All valid: {report.get('AllValid', 'Unknown')}")
            
            # Show all tasks
            tasks = report.get('tasks', [])
            print(f"\nAll tasks:")
            for task in tasks:
                print(f"  Task {task[0]}: ({task[1]}, {task[2]})")
            
            # Show events
            events = report.get('events', [])
            print(f"\nEvents:")
            for i, agent_events in enumerate(events):
                if agent_events:
                    print(f"  Agent {i}: {len(agent_events)} events")
                    for event in agent_events:
                        print(f"    {event}")
        else:
            print(f"Final report failed: {response.text}")
    except Exception as e:
        print(f"Final report request failed: {e}")

if __name__ == "__main__":
    test_add_tasks() 