#include "MAPFServer.h"
#include "Utils.h" // Include the shared utility file
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>
#include <iostream>
#include <thread>
#include <sstream>
#include <chrono>
#include <numeric>
#include <cstdlib>
#include <limits>

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

// Helper functions are now in Utils.h and removed from here

MAPFServer::MAPFServer(const std::string& map_file, const std::string& config_file, int port)
    : map_file(map_file), config_file(config_file), port(port) {
    
    signal(SIGSEGV, [](int sig) {
        std::cerr << "Segmentation fault caught!" << std::endl;
        exit(1);
    });
    
    io_context = std::make_unique<asio::io_context>();
}

MAPFServer::~MAPFServer() {
    stop();
}

bool MAPFServer::initialize() {
    try {
        grid = std::make_unique<Grid>(map_file);
        action_model = std::make_unique<ActionModelWithRotate>(*grid);
        
        env = std::make_unique<SharedEnvironment>();
        env->rows = grid->rows;
        env->cols = grid->cols;
        env->map = grid->map;
        
        std::string problem_file = "./example_problems/custom_domain/myproblem.json";
        if (load_problem_configuration(problem_file)) {
            env->num_of_agents = team_size;
        } else {
            env->num_of_agents = 5;
        }

        planner = std::make_unique<MAPFPlanner>(env.get());
        setenv("CONFIG_PATH", config_file.c_str(), 1);
        planner->initialize(30);

        if (!task_locations.empty()) {
            initialize_task_system();
        }
        
        std::cout << "MAPF Server initialized successfully" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize MAPF Server: " << e.what() << std::endl;
        return false;
    }
}

void MAPFServer::run() {
    if (!running) {
        running = true;
        start_http_server();
    }
}

void MAPFServer::stop() {
    if (running) {
        running = false;
        if (io_context) {
            io_context->stop();
        }
    }
}

void MAPFServer::start_http_server() {
    try {
        asio::ip::tcp::acceptor acceptor(*io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), static_cast<unsigned short>(port)));
        std::cout << "HTTP Server started on port " << port << std::endl;

        while (running) {
            tcp::socket socket(*io_context);
            acceptor.accept(socket);
            std::thread([this, socket = std::move(socket)]() mutable {
                try {
                    beast::flat_buffer buffer;
                    http::request<http::string_body> req;
                    http::read(socket, buffer, req);
                    std::string response_body;
                    handle_http_request(std::string(req.method_string()), std::string(req.target()), req.body(), response_body);
                    http::response<http::string_body> res;
                    res.result(http::status::ok);
                    res.set(http::field::server, "MAPF-Server");
                    res.set(http::field::content_type, "application/json");
                    res.body() = response_body;
                    res.prepare_payload();
                    http::write(socket, res);
                } catch (...) {
                    // Ignore client disconnects
                }
            }).detach();
        }
    } catch (const std::exception& e) {
        std::cerr << "HTTP Server error: " << e.what() << std::endl;
    }
}

void MAPFServer::handle_http_request(const std::string& method, const std::string& path,
                                     const std::string& body, std::string& response) {
    try {
        if (method == "POST" && path == "/plan") {
            response = handle_plan_request(body);
        } else if (method == "GET" && path == "/report") {
            response = handle_report_request();
        } else if (method == "POST" && path == "/reset") {
            response = handle_reset_request();
        } else if (method == "POST" && path == "/add_task") {
            response = handle_add_task_request(body);
        } else if (method == "GET" && path == "/health") {
            response = handle_health_request();
        } else if (method == "GET" && path == "/task_status") {
            response = handle_task_status_request();
        } else {
            response = nlohmann::json({{"error", "Not Found"}}).dump(4);
        }
    } catch (const std::exception& e) {
        response = nlohmann::json({{"error", "Internal Server Error"}, {"message", e.what()}}).dump(4);
    }
}

std::string MAPFServer::handle_health_request() {
    return nlohmann::json({{"status", "healthy"}}).dump();
}

std::string MAPFServer::handle_task_status_request() {
    if (!session_active) {
        return nlohmann::json({{"error", "No Active Session"}}).dump(4);
    }

    nlohmann::json task_status_report = nlohmann::json::array();
    for (int k = 0; k < team_size; k++) {
        nlohmann::json agent_status;
        agent_status["agent_id"] = k;
        agent_status["is_carrying_task"] = is_carrying_task[k];
        agent_status["has_task"] = !assigned_tasks[k].empty();
        if(agent_status["has_task"]) {
             Task& task = assigned_tasks[k].front();
             agent_status["current_task"] = {
                {"task_id", task.task_id},
                {"start_location", task.start_location},
                {"goal_location", task.goal_location}
            };
        }
        agent_status["tasks_completed"] = finished_tasks[k].size();
        task_status_report.push_back(agent_status);
    }
    return task_status_report.dump(4);
}


std::string MAPFServer::handle_reset_request() {
    session_active = false;
    team_size = 0;
    timestep = 0;
    initial_states.clear();
    history_of_actions.clear();
    history_of_planning_times.clear();
    
    finished_tasks.clear();
    assigned_tasks.clear();
    is_carrying_task.clear();
    events.clear();
    all_tasks.clear();
    solution_costs.clear();
    actual_movements.clear();
    // planner_movements.clear();
    num_of_task_finish = 0;
    task_id = 0;
    fast_mover_feasible = true;
    
    if (!task_locations.empty()) {
        initialize_task_system();
    }
    
    return nlohmann::json({{"status", "success"}}).dump(4);
}

std::string MAPFServer::handle_plan_request(const std::string& request_body) {
    try {
        nlohmann::json request = nlohmann::json::parse(request_body);
        
        if (!validate_planning_request(request)) {
            return nlohmann::json({{"error", "Invalid Request"}}).dump(4);
        }
        
        std::vector<State> agents = parse_agent_states(request["agents"]);
        
        if (!session_active) {
            session_active = true;
            team_size = agents.size();
            
            finished_tasks.assign(team_size, {});
            assigned_tasks.assign(team_size, {});
            is_carrying_task.assign(team_size, false);
            events.assign(team_size, {});
            solution_costs.assign(team_size, 0);
            actual_movements.assign(team_size, {});
            // planner_movements.assign(team_size, {});
            current_agent_states.resize(team_size);
            
            if (!initial_states.empty()) {
                agents = initial_states;
            } else {
                initial_states = agents;
            }
        }

        current_agent_states = agents;
        
        for (int k = 0; k < team_size; k++) {
            if (!assigned_tasks[k].empty()) {
                Task& current_task = assigned_tasks[k].front();
                if (!is_carrying_task[k] && current_agent_states[k].location == current_task.start_location) {
                    is_carrying_task[k] = true;
                }
                if (is_carrying_task[k] && current_agent_states[k].location == current_task.goal_location) {
                    assigned_tasks[k].pop_front();
                    current_task.t_completed = timestep;
                    finished_tasks[k].push_back(current_task);
                    num_of_task_finish++;
                    log_event_finished(k, current_task.task_id, timestep);
                    is_carrying_task[k] = false;
                }
            }
        }

        update_tasks_lifelong(agents);

        env->curr_states = agents;
        env->goal_locations.assign(team_size, {});
        
        for (int i = 0; i < team_size; i++) {
            if (!assigned_tasks[i].empty()) {
                int goal = is_carrying_task[i] ? assigned_tasks[i].front().goal_location : assigned_tasks[i].front().start_location;
                env->goal_locations[i].push_back({goal, timestep});
            } else {
                env->goal_locations[i].push_back({agents[i].location, timestep});
            }
        }
            
        std::vector<Action> actions;
        auto start_time = std::chrono::high_resolution_clock::now();
        planner->plan(5.0, actions);
        auto end_time = std::chrono::high_resolution_clock::now();
        
        if (actions.size() != agents.size()) {
            actions.assign(team_size, Action::W);
        }

        for (int a = 0; a < team_size; a++) {
            if (env->goal_locations[a].front().first != agents[a].location) {
                solution_costs[a]++;
            }
        }
        
        std::vector<State> new_states = action_model->result_states(agents, actions);
        
        history_of_actions.push_back(actions);
        history_of_planning_times.push_back(std::chrono::duration<double>(end_time - start_time).count());
        
        for (int k = 0; k < team_size; k++) {
            // planner_movements[k].push_back(actions[k]);
            actual_movements[k].push_back(actions[k]);
        }
        
        timestep++;
        save_results_to_file();

        return nlohmann::json({
            {"status", "success"},
            {"timestep", timestep},
            {"actions", serialize_path(actions, new_states)},
            {"total_tasks_completed", num_of_task_finish},
            {"tasks_remaining", task_queue.size()}
        }).dump(4);

    } catch (const std::exception& e) {
        return nlohmann::json({{"error", "Critical Error"}, {"message", e.what()}}).dump(4);
    }
}

std::string MAPFServer::handle_report_request() {
    if (!session_active) {
        return nlohmann::json({{"error", "No Active Session"}}).dump(4);
    }

    nlohmann::json report;
    report["actionModel"] = "MAPF_T";
    report["AllValid"] = fast_mover_feasible ? "Yes" : "No";
    report["teamSize"] = team_size;
    
    nlohmann::json starts = nlohmann::json::array();
    for(const auto& state : initial_states) {
        starts.push_back({state.location / grid->cols, state.location % grid->cols, orientation_to_string_local(state.orientation)});
    }
    report["start"] = starts;

    report["numTaskFinished"] = num_of_task_finish;
    int sum_of_cost = std::accumulate(solution_costs.begin(), solution_costs.end(), 0);
    int makespan = team_size > 0 ? *std::max_element(solution_costs.begin(), solution_costs.end()) : 0;
    report["sumOfCost"] = sum_of_cost;
    report["makespan"] = makespan;

    std::vector<std::string> actual_paths(team_size);
    // std::vector<std::string> planner_paths(team_size);
    
    for (int i = 0; i < team_size; i++) {
        std::string a_path; //, p_path;
        for (const auto action : actual_movements[i]) a_path += action_to_string_local(action) + ",";
        if(!a_path.empty()) a_path.pop_back();
        actual_paths[i] = a_path;
        
        // for (const auto action : planner_movements[i]) p_path += action_to_string_local(action) + ",";
        // if(!p_path.empty()) p_path.pop_back();
        // planner_paths[i] = p_path;
    }
    report["actualPaths"] = actual_paths;
    // report["plannerPaths"] = planner_paths;

    report["plannerTimes"] = history_of_planning_times;
    report["errors"] = nlohmann::json::array();

    nlohmann::json events_json = nlohmann::json::array();
    for (int i = 0; i < team_size; i++) {
        nlohmann::json agent_events = nlohmann::json::array();
        for(const auto& e: events[i]) {
            agent_events.push_back({std::get<0>(e), std::get<1>(e), std::get<2>(e)});
        }
        events_json.push_back(agent_events);
    }
    report["events"] = events_json;

    nlohmann::json tasks_json = nlohmann::json::array();
    for (const auto& t: all_tasks) {
        tasks_json.push_back({
            t.task_id,
            t.goal_location / grid->cols,
            t.goal_location % grid->cols
        });
    }
    report["tasks"] = tasks_json;

    return report.dump(4);
}

std::string MAPFServer::handle_add_task_request(const std::string& request_body) {
    try {
        nlohmann::json request = nlohmann::json::parse(request_body);
        int start_location = request["start_location"];
        int goal_location = request["goal_location"];
        
        if (start_location < 0 || start_location >= grid->rows * grid->cols || grid->map[start_location] == 1 ||
            goal_location < 0 || goal_location >= grid->rows * grid->cols || grid->map[goal_location] == 1) {
            return nlohmann::json({{"error", "Invalid Location"}}).dump(4);
        }
        
        task_queue.emplace_back(task_id, start_location, goal_location, 0, -1);
        add_task_to_file(start_location, goal_location);
        
        return nlohmann::json({
            {"status", "success"},
            {"task_id", task_id++}
        }).dump(4);
        
    } catch (const std::exception& e) {
        return nlohmann::json({{"error", "Add Task Failed"}, {"message", e.what()}}).dump(4);
    }
}

int MAPFServer::calculate_manhattan_distance(int location1, int location2) {
    // Convert linear indices to grid coordinates
    int row1 = location1 / grid->cols;
    int col1 = location1 % grid->cols;
    int row2 = location2 / grid->cols;
    int col2 = location2 % grid->cols;
    
    // Calculate Manhattan distance
    return abs(row1 - row2) + abs(col1 - col2);
}

int MAPFServer::find_nearest_free_agent(int task_start_location, const std::vector<State>& current_states) {
    int nearest_agent = -1;
    int min_distance = std::numeric_limits<int>::max();
    
    for (int k = 0; k < team_size; k++) {
        // Check if agent is free (no assigned tasks)
        if (assigned_tasks[k].empty()) {
            int distance = calculate_manhattan_distance(task_start_location, current_states[k].location);
            if (distance < min_distance) {
                min_distance = distance;
                nearest_agent = k;
            }
        }
    }
    
    return nearest_agent;
}

void MAPFServer::update_tasks_lifelong(const std::vector<State>& current_states) {
    if (team_size <= 0) return;
    
    // Process tasks one by one, assigning each to the nearest free agent
    while (!task_queue.empty()) {
        Task task = task_queue.front();
        
        // Find the nearest free agent to this task's start location
        int nearest_agent = find_nearest_free_agent(task.start_location, current_states);
        
        // If no free agent found, break (wait for next timestep)
        if (nearest_agent == -1) {
            break;
        }
        
        // Assign task to the nearest agent
        task_queue.pop_front();
        task.t_assigned = timestep;
        task.agent_assigned = nearest_agent;
        assigned_tasks[nearest_agent].push_back(task);
        all_tasks.push_back(task);
        log_event_assigned(nearest_agent, task.task_id, timestep);
    }
}

bool MAPFServer::load_problem_configuration(const std::string& problem_file) {
    try {
        std::ifstream f(problem_file);
        if (!f.is_open()) return false;
        
        nlohmann::json data = nlohmann::json::parse(f);
        
        team_size = data["teamSize"];
        
        std::string agent_file = "./example_problems/custom_domain/" + data["agentFile"].get<std::string>();
        std::ifstream agent_f(agent_file);
        int num_agents;
        agent_f >> num_agents;
        agent_start_locations.resize(num_agents);
        for (int i = 0; i < num_agents; i++) agent_f >> agent_start_locations[i];
        
        task_file_path = "./example_problems/custom_domain/" + data["taskFile"].get<std::string>();
        std::ifstream task_f(task_file_path);
        int num_tasks;
        task_f >> num_tasks;
        task_locations.resize(num_tasks);
        for (int i = 0; i < num_tasks; i++) {
            task_f >> task_locations[i].first >> task_locations[i].second;
        }
        
        return true;
    } catch (...) {
        return false;
    }
}

void MAPFServer::initialize_task_system() {
    task_queue.clear();
    task_id = 0;
    for (const auto& loc_pair : task_locations) {
        task_queue.emplace_back(task_id++, loc_pair.first, loc_pair.second, 0, -1);
    }
    
    initial_states.clear();
    for (int loc : agent_start_locations) {
        initial_states.emplace_back(loc, 0, 0);
    }
}

bool MAPFServer::add_task_to_file(int start_location, int goal_location) {
    try {
        std::vector<std::pair<int, int>> tasks;
        std::ifstream infile(task_file_path);
        if(infile.is_open()){
            int count = 0;
            infile >> count;
            tasks.resize(count);
            for(int i = 0; i < count; ++i) infile >> tasks[i].first >> tasks[i].second;
            infile.close();
        }

        tasks.push_back({start_location, goal_location});

        std::ofstream outfile(task_file_path);
        outfile << tasks.size() << std::endl;
        for(const auto& task_pair : tasks) outfile << task_pair.first << " " << task_pair.second << std::endl;
        outfile.close();
        return true;
    } catch (...) {
        return false;
    }
}

void MAPFServer::save_results_to_file() {
    try {
        std::ofstream file("test.json");
        file << handle_report_request();
    } catch (...) {
        // silent fail
    }
}

bool MAPFServer::validate_planning_request(const nlohmann::json& request) {
    return request.contains("agents") && request["agents"].is_array();
}

std::vector<State> MAPFServer::parse_agent_states(const nlohmann::json& agents) {
    std::vector<State> states;
    for (const auto& agent : agents) {
        states.emplace_back(agent["location"], agent.value("timestep", 0), agent.value("orientation", 0));
    }
    return states;
}

nlohmann::json MAPFServer::serialize_path(const std::vector<Action>& actions, const std::vector<State>& states) {
    nlohmann::json path_data = nlohmann::json::array();
    for (size_t i = 0; i < actions.size(); ++i) {
        path_data.push_back({
            {"agent_id", i},
            {"action", action_to_string_local(actions[i])},
            {"location", states[i].location},
            {"orientation", states[i].orientation}
        });
    }
    return path_data;
}

void MAPFServer::log_event_assigned(int agent_id, int task_id, int timestep) {
    if (agent_id < events.size()) {
        events[agent_id].push_back(std::make_tuple(task_id, timestep, "assigned"));
    }
}

void MAPFServer::log_event_finished(int agent_id, int task_id, int timestep) {
    if (agent_id < events.size()) {
        events[agent_id].push_back(std::make_tuple(task_id, timestep, "finished"));
    }
}