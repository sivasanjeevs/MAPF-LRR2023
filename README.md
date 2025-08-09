compile
./compile.sh

run server
./build/mapf_server --mapFile ./example_problems/custom_domain/maps/mymap.map --configFile ./configs/mymap.json --port 8080

run client
python3 examples/continuous_client_example.py

visualisation
python live_visual.py

add new task
python3 examples/add_task_client.py --start 3 --goal 49
