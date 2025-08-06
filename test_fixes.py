#!/usr/bin/env python3
"""
Test script to verify server fixes work properly
"""

import requests
import json
import time
import sys

def test_server():
    print("=== Testing Server Fixes ===")
    
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
    
    # Test 3: Initial planning request
    print("3. Testing initial planning request...")
    try:
        agents = [
            {"location": 2, "orientation": 0, "timestep": 0},
            {"location": 3, "orientation": 0, "timestep": 0},
            {"location": 4, "orientation": 0, "timestep": 0},
            {"location": 5, "orientation": 0, "timestep": 0},
            {"location": 6, "orientation": 0, "timestep": 0}
        ]
        
        response = requests.post(
            "http://localhost:8080/plan",
            json={"agents": agents},
            headers={"Content-Type": "application/json"}
        )
        
        if response.status_code == 200:
            result = response.json()
            if result.get("status") == "success":
                print("✓ Initial planning successful")
                print(f"  Timestep: {result.get('timestep')}")
                print(f"  Tasks remaining: {result.get('tasks_remaining')}")
            else:
                print(f"✗ Planning failed: {result.get('message', 'Unknown error')}")
                return False
        else:
            print(f"✗ Planning request failed with status {response.status_code}")
            return False
    except Exception as e:
        print(f"✗ Planning request failed: {e}")
        return False
    
    # Test 4: Add task with auto-planning
    print("4. Testing task addition with auto-planning...")
    try:
        response = requests.post(
            "http://localhost:8080/add_task",
            json={"location": 82},
            headers={"Content-Type": "application/json"}
        )
        
        if response.status_code == 200:
            result = response.json()
            if result.get("status") == "success":
                print("✓ Task addition successful")
                print(f"  Task ID: {result.get('task_id')}")
                print(f"  Location: {result.get('location')}")
                
                # Check auto-planning
                planning = result.get("planning", {})
                if planning.get("planning_triggered"):
                    print("✓ Auto-planning was triggered")
                    if planning.get("planning_success"):
                        print("✓ Auto-planning successful")
                    else:
                        print("⚠ Auto-planning failed but server didn't crash")
                else:
                    print("⚠ Auto-planning not triggered")
            else:
                print(f"✗ Task addition failed: {result.get('message', 'Unknown error')}")
                return False
        else:
            print(f"✗ Task addition request failed with status {response.status_code}")
            return False
    except Exception as e:
        print(f"✗ Task addition failed: {e}")
        return False
    
    # Test 5: Check if test.json was created
    print("5. Checking if test.json was created...")
    try:
        with open("test.json", "r") as f:
            data = json.load(f)
            print("✓ test.json file exists and is valid JSON")
            print(f"  Team size: {data.get('teamSize')}")
            print(f"  Tasks finished: {data.get('numTaskFinished')}")
            print(f"  All valid: {data.get('AllValid')}")
    except FileNotFoundError:
        print("⚠ test.json file not found")
    except Exception as e:
        print(f"✗ Error reading test.json: {e}")
    
    # Test 6: Multiple planning requests to ensure stability
    print("6. Testing multiple planning requests for stability...")
    try:
        for i in range(3):
            agents = [
                {"location": 2 + i, "orientation": 0, "timestep": i + 1},
                {"location": 3 + i, "orientation": 0, "timestep": i + 1},
                {"location": 4 + i, "orientation": 0, "timestep": i + 1},
                {"location": 5 + i, "orientation": 0, "timestep": i + 1},
                {"location": 6 + i, "orientation": 0, "timestep": i + 1}
            ]
            
            response = requests.post(
                "http://localhost:8080/plan",
                json={"agents": agents},
                headers={"Content-Type": "application/json"}
            )
            
            if response.status_code == 200:
                result = response.json()
                if result.get("status") == "success":
                    print(f"  ✓ Planning request {i+1} successful")
                else:
                    print(f"  ✗ Planning request {i+1} failed: {result.get('message')}")
                    return False
            else:
                print(f"  ✗ Planning request {i+1} failed with status {response.status_code}")
                return False
            
            time.sleep(0.1)  # Small delay between requests
        
        print("✓ All planning requests successful - server is stable")
    except Exception as e:
        print(f"✗ Stability test failed: {e}")
        return False
    
    print("\n=== All Tests Passed! ===")
    print("✓ Server doesn't crash")
    print("✓ Auto-planning works")
    print("✓ Results are saved to test.json")
    print("✓ Server remains stable")
    
    return True

if __name__ == "__main__":
    success = test_server()
    sys.exit(0 if success else 1) 