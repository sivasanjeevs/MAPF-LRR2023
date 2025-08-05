#!/usr/bin/env python3

import requests
import json
import time

def test_server():
    base_url = "http://localhost:8080"
    
    print("Testing MAPF Server...")
    
    # Test health endpoint
    try:
        response = requests.get(f"{base_url}/health")
        print(f"Health check: {response.json()}")
    except Exception as e:
        print(f"Health check failed: {e}")
        return
    
    # Test status endpoint
    try:
        response = requests.get(f"{base_url}/status")
        print(f"Status: {response.json()}")
    except Exception as e:
        print(f"Status check failed: {e}")
        return
    
    # Test reset endpoint
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
    
    request_data = {"agents": agents}
    
    try:
        print("Sending planning request...")
        response = requests.post(f"{base_url}/plan", json=request_data)
        if response.status_code == 200:
            result = response.json()
            print(f"Planning successful: {result}")
            
            # Test report endpoint
            try:
                response = requests.get(f"{base_url}/report")
                if response.status_code == 200:
                    report = response.json()
                    print(f"Report generated successfully")
                    print(f"Tasks finished: {report.get('numTaskFinished', 0)}")
                    print(f"Team size: {report.get('teamSize', 0)}")
                else:
                    print(f"Report failed: {response.text}")
            except Exception as e:
                print(f"Report request failed: {e}")
        else:
            print(f"Planning failed: {response.text}")
    except Exception as e:
        print(f"Planning request failed: {e}")

if __name__ == "__main__":
    test_server() 