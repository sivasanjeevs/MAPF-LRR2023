#include "MAPFServer.h"
#include "Logger.h"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>
#include <iostream>
#include <thread>
#include <sstream>
#include <chrono>
#include <numeric> // Required for std::accumulate
#include <set>

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

// Helper function to convert Action enum to string
std::string action_to_string_local(Action action) {
    switch (action) {
        case Action::FW:  return "F";
        case Action::CR:  return "R";
        case Action::CCR: return "C";
        case Action::W:   return "W";
        case Action::NA:  return "T";
        default:          return "?";
    }
}

// Helper function to convert orientation int to string
std::string orientation_to_string_local(int orientation) {
    switch (orientation) {
        case 0: return "E";
        case 1: return "S";
        case 2: return "W";
        case 3: return "N";
        default: return "";
    }
}

MAPFServer::MAPFServer(const std::string& map_file, const std::string& config_file, int port)
    : map_file(map_file), config_file(config_file), port(port), running(false) {
    io_context = std::make_unique<asio::io_context>();
}

MAPFServer::~MAPFServer() {
    stop();
}

bool MAPFServer::initialize() {
    try {
        std::cout << "Initializing MAPF Server..." << std::endl;
        std::cout << "Map file: " << map_file << std::endl;
        std::cout << "Config file: " << config_file << std::endl;
        
        // Initialize grid
        std::cout << "Creating grid..." << std::endl;
        grid = std::make_unique<Grid>(map_file);
        std::cout << "Grid created: " << grid->rows << "x" << grid->cols << std::endl;
        
        // Initialize action model
        std::cout << "Creating action model..." << std::endl;
        action_model = std::make_unique<ActionModelWithRotate>(*grid);
        
        // Initialize environment
        std::cout << "Creating shared environment..." << std::endl;
        env = std::make_unique<SharedEnvironment>();
        env->rows = grid->rows;
        env->cols = grid->cols;
        env->map = grid->map;
        env->map_name = grid->map_name;
        env->num_of_agents = 5; // Default to 5 agents, will be updated after loading problem config
        std::cout << "Environment created" << std::endl;
        
        std::string problem_file = "./example_problems/custom_domain/myproblem.json";
        std::cout << "Looking for problem file: " << problem_file << std::endl;
        
        if (load_problem_configuration(problem_file)) {
            std::cout << "Problem configuration loaded successfully" << std::endl;
            // Update environment with loaded team size BEFORE initializing planner
            env->num_of_agents = team_size;
            std::cout << "Environment updated with team size: " << team_size << std::endl;
        } else {
            std::cout << "No problem configuration found, using default settings" << std::endl;
            // Use the config file provided to the server
            std::cout << "Using config file: " << config_file << std::endl;
        }

        // Initialize planner
        std::cout << "Creating planner..." << std::endl;
        planner = std::make_unique<MAPFPlanner>(env.get());
        setenv("CONFIG_PATH", config_file.c_str(), 1);
        
        // Initialize planner first
        std::cout << "Initializing planner..." << std::endl;
        try {
            // Ensure environment is properly set up before planner initialization
            env->curr_states = std::vector<State>(env->num_of_agents, State(0, 0, 0));
            env->curr_timestep = 0;
            env->goal_locations.resize(env->num_of_agents);
            for (int i = 0; i < env->num_of_agents; i++) {
                env->goal_locations[i].clear();
            }
            
            planner->initialize(30);
            std::cout << "Planner initialized" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Error initializing planner: " << e.what() << std::endl;
            return false;
        }

        // Initialize task system after planner is ready
        if (!task_locations.empty()) {
            initialize_task_system();
        }
        
        // Set up initial environment state
        if (!initial_states.empty()) {
            env->curr_states = initial_states;
            env->curr_timestep = 0;
            env->goal_locations.resize(team_size);
            for (size_t i = 0; i < team_size; i++) {
                env->goal_locations[i].clear();
            }
        }
        
        std::cout << "MAPF Server initialized successfully" << std::endl;
        std::cout << "Map: " << map_file << " (" << grid->rows << "x" << grid->cols << ")" << std::endl;
        std::cout << "Port: " << port << std::endl;
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
        std::cout << "Available endpoints:" << std::endl;
        std::cout << "  POST /plan   - Submit planning request" << std::endl;
        std::cout << "  GET  /status  - Get server status" << std::endl;
        std::cout << "  GET  /health  - Health check" << std::endl;
        std::cout << "  POST /reset   - Reset the simulation history" << std::endl;
        std::cout << "  GET  /report  - Generate a JSON report of the simulation" << std::endl;
        std::cout << "  POST /add_tasks - Add new tasks to the task file" << std::endl;
        std::cout << "Starting HTTP server..." << std::endl;

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
                } catch (const std::exception& e) {
                    std::cerr << "Error handling request: " << e.what() << std::endl;
                } catch (...) {
                    std::cerr << "Unknown error handling request" << std::endl;
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
            response = handle_plan_request(nlohmann::json::parse(body));
        } else if (method == "GET" && path == "/status") {
            response = handle_status_request();
        } else if (method == "GET" && path == "/health") {
            response = handle_health_request();
        } else if (method == "GET" && path == "/report") {
            response = handle_report_request();
        } else if (method == "POST" && path == "/reset") {
            response = handle_reset_request();
        } else if (method == "POST" && path == "/add_tasks") {
            response = handle_add_tasks_request(nlohmann::json::parse(body));
        } else {
            nlohmann::json error_response = {{"error", "Not Found"}, {"message", "Endpoint not found: " + method + " " + path}};
            response = error_response.dump(4);
        }
    } catch (const std::exception& e) {
        nlohmann::json error_response = {{"error", "Internal Server Error"}, {"message", e.what()}};
        response = error_response.dump(4);
    }
}

std::string MAPFServer::handle_reset_request() {
    session_active = false;
    team_size = 0;
    timestep = 0;
    initial_states.clear();
    history_of_actions.clear();
    history_of_planning_times.clear();
    
    // Reset task tracking structures
    finished_tasks.clear();
    assigned_tasks.clear();
    events.clear();
    all_tasks.clear();
    solution_costs.clear();
    actual_movements.clear();
    planner_movements.clear();
    num_of_task_finish = 0;
    task_id = 0;
    fast_mover_feasible = true;
    current_agent_states.clear();
    
    // Always reinitialize task system on reset to ensure clean state
    if (!task_locations.empty()) {
        // Clear existing task queue
        task_queue.clear();
        // Reset task_id to start from beginning
        task_id = 0;
        // Reinitialize with fresh task queue
        initialize_task_system();
    }
    
    nlohmann::json response = {
        {"status", "success"},
        {"message", "Simulation history has been reset."}
    };
    return response.dump(4);
}

std::string MAPFServer::handle_plan_request(const nlohmann::json& request) {
    try {
        if (!validate_planning_request(request)) {
            return nlohmann::json({{"error", "Invalid Request"}, {"message", "Invalid planning request format"}}).dump(4);
        }

        auto agents = parse_agent_states(request["agents"]);
        
        // Safety check: ensure we have valid agents
        if (agents.empty()) {
            return nlohmann::json({{"error", "Invalid Request"}, {"message", "No valid agents provided"}}).dump(4);
        }

        if (!session_active) {
            session_active = true;
            team_size = agents.size();
            history_of_actions.clear();
            history_of_planning_times.clear();
            
            // Initialize task tracking structures like BaseSystem
            finished_tasks.resize(team_size);
            assigned_tasks.resize(team_size);
            events.resize(team_size);
            solution_costs.resize(team_size, 0);
            actual_movements.resize(team_size);
            planner_movements.resize(team_size);
            timestep = 0;
            num_of_task_finish = 0;
            fast_mover_feasible = true;
            
            // Initialize with initial states from problem configuration
            if (!initial_states.empty()) {
                // Use initial states from problem configuration
                std::cout << "Using initial states from problem configuration" << std::endl;
                // Override the agents with initial states for the first planning step
                agents = initial_states;
            } else {
                // Use states from request
                initial_states = agents;
            }
            
            // Initialize current agent states
            current_agent_states = agents;
        } else {
            // Use stored current states for subsequent planning steps
            agents = current_agent_states;
        }

        // Update tasks like the lifelong system does
        update_tasks_lifelong(agents);

        std::cout << "Current agent positions:" << std::endl;
        for (size_t i = 0; i < agents.size(); i++) {
            std::cout << "  Agent " << i << ": location " << agents[i].location << ", orientation " << agents[i].orientation << std::endl;
        }

        // Set up the environment properly like the lifelong system does
        env->curr_states = agents;
        env->curr_timestep = timestep;
        env->num_of_agents = static_cast<int>(agents.size());
        
        // Clear and set up goal locations properly
        env->goal_locations.clear();
        env->goal_locations.resize(team_size);
        for (size_t i = 0; i < team_size; i++) {
            env->goal_locations[i].clear();
            for (auto& task: assigned_tasks[i]) {
                // Only add goals that are different from current location to avoid immediate completion
                if (task.location != agents[i].location) {
                    env->goal_locations[i].push_back({task.location, task.t_assigned});
                }
            }
        }
        
        // Additional safety check: ensure all agent states are valid
        for (size_t i = 0; i < agents.size(); i++) {
            if (agents[i].location < 0 || agents[i].location >= grid->rows * grid->cols) {
                std::cerr << "Invalid agent location: agent " << i << " at location " << agents[i].location << std::endl;
                return nlohmann::json({{"error", "Invalid State"}, {"message", "Agent location out of bounds"}}).dump(4);
            }
            if (agents[i].orientation < 0 || agents[i].orientation > 3) {
                std::cerr << "Invalid agent orientation: agent " << i << " orientation " << agents[i].orientation << std::endl;
                return nlohmann::json({{"error", "Invalid State"}, {"message", "Agent orientation out of bounds"}}).dump(4);
            }
        }

        auto start_time = std::chrono::high_resolution_clock::now();
        std::vector<Action> actions;
        try {
            // Safety check: ensure planner is properly initialized
            if (!planner) {
                throw std::runtime_error("Planner not initialized");
            }
            
            // Safety check: ensure environment is properly set up
            if (env->curr_states.size() != team_size) {
                throw std::runtime_error("Environment states size mismatch");
            }
            
            // Additional safety check: ensure planner state is consistent
            if (env->num_of_agents != team_size) {
                std::cerr << "Environment agent count mismatch, resetting planner" << std::endl;
                try {
                    env->num_of_agents = team_size;
                    planner->initialize(30);
                } catch (const std::exception& e) {
                    std::cerr << "Failed to reset planner: " << e.what() << std::endl;
                    throw std::runtime_error("Planner reset failed");
                }
            }
            
            // Check if any agents have goals to plan for
            bool has_goals = false;
            for (const auto& goals : env->goal_locations) {
                if (!goals.empty()) {
                    has_goals = true;
                    break;
                }
            }
            
            if (!has_goals) {
                // No goals to plan for, use wait actions
                actions = std::vector<Action>(team_size, Action::W);
                std::cout << "No goals to plan for, using wait actions" << std::endl;
            } else {
                // Add timeout and error handling for planner
                try {
                    planner->plan(5.0, actions);
                } catch (const std::exception& e) {
                    std::cerr << "Planner error: " << e.what() << std::endl;
                    // Use wait actions as fallback
                    actions = std::vector<Action>(team_size, Action::W);
                    std::cout << "Using wait actions due to planner error" << std::endl;
                } catch (...) {
                    std::cerr << "Unknown planner error" << std::endl;
                    // Use wait actions as fallback
                    actions = std::vector<Action>(team_size, Action::W);
                    std::cout << "Using wait actions due to unknown planner error" << std::endl;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error during planning: " << e.what() << std::endl;
            // Return error response
            return nlohmann::json({{"error", "Planning Failed"}, {"message", e.what()}}).dump(4);
        }
        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> planning_duration = end_time - start_time;
        
        std::cout << "Planned actions:" << std::endl;
        for (size_t i = 0; i < actions.size(); i++) {
            std::cout << "  Agent " << i << ": " << action_to_string_local(actions[i]) << std::endl;
        }

        if (actions.size() != agents.size()) {
             std::cerr << "CRITICAL: Planner returned " << actions.size() 
                       << " actions for " << agents.size() << " agents. Planning may have failed." << std::endl;
            
            // Try to recover by resetting planner and using wait actions
            std::cerr << "Attempting planner recovery..." << std::endl;
            try {
                planner->initialize(30);
                std::cout << "Planner recovery successful" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "Planner recovery failed: " << e.what() << std::endl;
            }
            
            // Use wait actions as fallback
            actions = std::vector<Action>(team_size, Action::W);
            std::cout << "Using wait actions due to planner failure" << std::endl;
        }

        // Update solution costs for agents with goals
        for (int a = 0; a < team_size; a++) {
            if (!env->goal_locations[a].empty()) {
                solution_costs[a]++;
            }
        }

        // Execute actions like BaseSystem::move()
        std::vector<Action> final_actions = actions;
        
        // Track planner movements
        for (int k = 0; k < team_size; k++) {
            if (k < actions.size()) {
                planner_movements[k].push_back(actions[k]);
            } else {
                fast_mover_feasible = false;
                planner_movements[k].push_back(Action::NA);
            }
        }
        
        // Check if moves are valid and update actual movements
        bool actions_valid = false;
        try {
            // Additional safety check before validation
            if (agents.size() != actions.size()) {
                std::cerr << "Agent/action count mismatch during validation" << std::endl;
                actions_valid = false;
            } else {
                actions_valid = action_model->is_valid(agents, actions);
            }
        } catch (const std::exception& e) {
            std::cerr << "Error validating actions: " << e.what() << std::endl;
            actions_valid = false;
        }
        
        if (!actions_valid) {
            fast_mover_feasible = false;
            std::cerr << "Planned actions are not valid, using wait actions" << std::endl;
            // Use wait actions if invalid
            final_actions = std::vector<Action>(team_size, Action::W);
        }
        
        // Execute the actions and update states
        std::vector<State> new_states;
        try {
            // Safety check before computing result states
            if (agents.size() != final_actions.size()) {
                std::cerr << "Agent/action count mismatch during result computation" << std::endl;
                new_states = agents; // Keep current states
            } else {
                new_states = action_model->result_states(agents, final_actions);
            }
            
            // Validate new states before using them
            for (size_t i = 0; i < new_states.size(); i++) {
                if (new_states[i].location < 0 || new_states[i].location >= grid->rows * grid->cols) {
                    std::cerr << "Invalid new state location: agent " << i << " at location " << new_states[i].location << std::endl;
                    new_states[i] = agents[i]; // Keep current state
                }
                if (new_states[i].orientation < 0 || new_states[i].orientation > 3) {
                    std::cerr << "Invalid new state orientation: agent " << i << " orientation " << new_states[i].orientation << std::endl;
                    new_states[i] = agents[i]; // Keep current state
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error computing result states: " << e.what() << std::endl;
            // Use current states as fallback
            new_states = agents;
        }
        
        // Track actual movements
        for (int k = 0; k < team_size; k++) {
            actual_movements[k].push_back(final_actions[k]);
        }
        
        // Check for finished tasks
        for (int k = 0; k < team_size; k++) {
            if (!assigned_tasks[k].empty() && new_states[k].location == assigned_tasks[k].front().location) {
                Task task = assigned_tasks[k].front();
                assigned_tasks[k].pop_front();
                task.t_completed = timestep;
                finished_tasks[k].push_back(task);
                num_of_task_finish++;
                log_event_finished(k, task.task_id, timestep);
                
                std::cout << "Agent " << k << " completed task " << task.task_id << " at timestep " << timestep << std::endl;
            }
        }

        history_of_actions.push_back(final_actions);
        history_of_planning_times.push_back(planning_duration.count());
        
        // Store the new states for the next planning step
        current_agent_states = new_states;
        
        // Periodically reset planner state to prevent corruption (every 50 steps)
        if (timestep > 0 && timestep % 50 == 0) {
            std::cout << "Resetting planner state at timestep " << timestep << " to prevent corruption" << std::endl;
            try {
                // Reinitialize planner with current environment state
                planner->initialize(30);
                std::cout << "Planner state reset successful" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "Failed to reset planner state: " << e.what() << std::endl;
            }
        }
        
        timestep++;

        nlohmann::json response = {
            {"status", "success"},
            {"actions", serialize_path(final_actions, agents)},
            {"num_agents", agents.size()},
            {"planning_time", planning_duration.count()}
        };
        return response.dump(4);

    } catch (const std::exception& e) {
        return nlohmann::json({{"error", "Planning Failed"}, {"message", e.what()}}).dump(4);
    }
}

std::string MAPFServer::handle_report_request() {
    if (!session_active) {
        return nlohmann::json({{"error", "No Active Session"}, {"message", "No simulation data to report. Send a /plan request first."}}).dump(4);
    }

    nlohmann::json report;
    report["actionModel"] = "MAPF_T";
    report["AllValid"] = fast_mover_feasible ? "Yes" : "No";
    report["teamSize"] = team_size;
    
    // "start" - initial agent positions
    nlohmann::json starts = nlohmann::json::array();
    for(const auto& state : initial_states) {
        starts.push_back({state.location / grid->cols, state.location % grid->cols, orientation_to_string_local(state.orientation)});
    }
    report["start"] = starts;

    // "numTaskFinished", "sumOfCost", "makespan"
    report["numTaskFinished"] = num_of_task_finish;
    int sum_of_cost = 0;
    int makespan = 0;
    if (team_size > 0) {
        sum_of_cost = solution_costs[0];
        makespan = solution_costs[0];
        for (int a = 1; a < team_size; a++) {
            sum_of_cost += solution_costs[a];
            if (solution_costs[a] > makespan) {
                makespan = solution_costs[a];
            }
        }
    }
    report["sumOfCost"] = sum_of_cost;
    report["makespan"] = makespan;

    // "actualPaths" and "plannerPaths"
    std::vector<std::string> actual_paths(team_size, "");
    std::vector<std::string> planner_paths(team_size, "");
    
    // Build paths from actual and planned movements
    for (int i = 0; i < team_size; i++) {
        bool first = true;
        for (const auto action : actual_movements[i]) {
            if (!first) {
                actual_paths[i] += ",";
            } else {
                first = false;
            }
            actual_paths[i] += action_to_string_local(action);
        }
        
        first = true;
        for (const auto action : planner_movements[i]) {
            if (!first) {
                planner_paths[i] += ",";
            } else {
                first = false;
            }
            planner_paths[i] += action_to_string_local(action);
        }
    }
    report["actualPaths"] = actual_paths;
    report["plannerPaths"] = planner_paths;

    // "plannerTimes"
    report["plannerTimes"] = history_of_planning_times;
    
    // "errors" - empty for now as the server doesn't track errors
    report["errors"] = nlohmann::json::array();

    // "events" - task assignment and completion events
    nlohmann::json events_json = nlohmann::json::array();
    for (int i = 0; i < team_size; i++) {
        nlohmann::json agent_events = nlohmann::json::array();
        for(auto e: events[i]) {
            nlohmann::json ev = nlohmann::json::array();
            int task_id, event_timestep;
            std::string event_msg;
            std::tie(task_id, event_timestep, event_msg) = e;
            ev.push_back(task_id);
            ev.push_back(event_timestep);
            ev.push_back(event_msg);
            agent_events.push_back(ev);
        }
        events_json.push_back(agent_events);
    }
    report["events"] = events_json;

    // "tasks" - all tasks that were created during the simulation
    nlohmann::json tasks = nlohmann::json::array();
    for (auto t: all_tasks) {
        nlohmann::json task = nlohmann::json::array();
        task.push_back(t.task_id);
        task.push_back(t.location / grid->cols);
        task.push_back(t.location % grid->cols);
        tasks.push_back(task);
    }
    report["tasks"] = tasks;

    return report.dump(4);
}

std::string MAPFServer::handle_add_tasks_request(const nlohmann::json& request) {
    try {
        // Validate request format
        if (!request.contains("goals") || !request["goals"].is_array()) {
            return nlohmann::json({{"error", "Invalid Request"}, {"message", "Request must contain 'goals' array"}}).dump(4);
        }
        
        std::vector<int> new_goals;
        for (const auto& goal : request["goals"]) {
            if (goal.is_object() && goal.contains("location")) {
                int location = goal["location"].get<int>();
                new_goals.push_back(location);
            } else if (goal.is_number()) {
                // Allow direct location numbers
                new_goals.push_back(goal.get<int>());
            }
        }
        
        if (new_goals.empty()) {
            return nlohmann::json({{"error", "Invalid Request"}, {"message", "No valid goals provided"}}).dump(4);
        }
        
        // Add goals to the task file
        std::string task_file_path = "./example_problems/custom_domain/task/mytask.tasks";
        std::ifstream task_file(task_file_path);
        
        if (!task_file.is_open()) {
            return nlohmann::json({{"error", "File Error"}, {"message", "Cannot open task file: " + task_file_path}}).dump(4);
        }
        
        // Read existing tasks
        std::vector<int> existing_tasks;
        std::string line;
        std::getline(task_file, line);
        
        // Skip comments
        while (!task_file.eof() && line[0] == '#') {
            std::getline(task_file, line);
        }
        
        int num_existing_tasks = std::atoi(line.c_str());
        
        for (int i = 0; i < num_existing_tasks; i++) {
            std::getline(task_file, line);
            while (!task_file.eof() && line[0] == '#') {
                std::getline(task_file, line);
            }
            if (!task_file.eof() && !line.empty()) {
                // Trim whitespace
                line.erase(0, line.find_first_not_of(" \t\r\n"));
                line.erase(line.find_last_not_of(" \t\r\n") + 1);
                if (!line.empty()) {
                    existing_tasks.push_back(std::atoi(line.c_str()));
                }
            }
        }
        task_file.close();
        
        // Append new tasks
        std::ofstream task_file_out(task_file_path, std::ios::trunc);
        if (!task_file_out.is_open()) {
            return nlohmann::json({{"error", "File Error"}, {"message", "Cannot write to task file: " + task_file_path}}).dump(4);
        }
        
        // Write updated count
        task_file_out << (num_existing_tasks + new_goals.size()) << std::endl;
        
        // Write existing tasks
        for (int task : existing_tasks) {
            task_file_out << task << std::endl;
        }
        
        // Write new tasks
        for (int goal : new_goals) {
            task_file_out << goal << std::endl;
        }
        task_file_out.close();
        
        // Reload task configuration to update internal state
        if (load_problem_configuration("./example_problems/custom_domain/myproblem.json")) {
            // Reinitialize task system with new tasks
            if (!task_locations.empty()) {
                initialize_task_system();
            }
            
            nlohmann::json response = {
                {"status", "success"},
                {"message", "Added " + std::to_string(new_goals.size()) + " new tasks"},
                {"added_goals", new_goals},
                {"total_tasks", task_locations.size()}
            };
            return response.dump(4);
        } else {
            return nlohmann::json({{"error", "Configuration Error"}, {"message", "Failed to reload task configuration"}}).dump(4);
        }
        
    } catch (const std::exception& e) {
        return nlohmann::json({{"error", "Internal Server Error"}, {"message", e.what()}}).dump(4);
    }
}

std::string MAPFServer::handle_status_request() {
    nlohmann::json response = {
        {"status", "running"},
        {"map_file", map_file},
        {"map_size", {grid->rows, grid->cols}},
        {"port", port},
        {"uptime", 0}
    };
    return response.dump();
}

std::string MAPFServer::handle_health_request() {
    nlohmann::json response = {
        {"status", "healthy"},
        {"timestamp", std::time(nullptr)}
    };
    return response.dump();
}

bool MAPFServer::validate_planning_request(const nlohmann::json& request) {
    return request.contains("agents") && request["agents"].is_array();
}

std::vector<State> MAPFServer::parse_agent_states(const nlohmann::json& agents) {
    std::vector<State> states;
    for (const auto& agent : agents) {
        int location = agent["location"];
        int orientation = agent.value("orientation", 0);
        int timestep = agent.value("timestep", 0);
        states.emplace_back(location, timestep, orientation);
    }
    return states;
}

std::vector<std::pair<int, int>> MAPFServer::parse_goals(const nlohmann::json& goals) {
    std::vector<std::pair<int, int>> goal_pairs;
    for (const auto& goal : goals) {
        int location = goal["location"];
        int timestep = goal.value("timestep", 0);
        goal_pairs.emplace_back(location, timestep);
    }
    return goal_pairs;
}

nlohmann::json MAPFServer::serialize_path(const std::vector<Action>& actions, const std::vector<State>& states) {
    nlohmann::json path_data = nlohmann::json::array();
    for (size_t i = 0; i < actions.size(); ++i) {
        nlohmann::json agent_path = {
            {"agent_id", i},
            {"action", action_to_string_local(actions[i])},
            {"location", states[i].location},
            {"orientation", states[i].orientation}
        };
        path_data.push_back(agent_path);
    }
    return path_data;
}

void MAPFServer::update_tasks_lifelong(const std::vector<State>& current_states) {
    // Safety check: ensure task system is initialized
    if (team_size <= 0 || assigned_tasks.size() != team_size || events.size() != team_size) {
        std::cerr << "Task system not properly initialized. Skipping task assignment." << std::endl;
        return;
    }
    
    // Check for new tasks in the file
    check_for_new_tasks();
    
    // Update tasks like TaskAssignSystem does
    for (int k = 0; k < team_size; k++) {
        // Check if agent is free (no assigned tasks) and has tasks available
        if (assigned_tasks[k].empty() && !task_queue.empty()) {
            // Find a task that is not at the same location as the agent
            bool found_task = false;
            for (size_t i = 0; i < task_queue.size(); i++) {
                Task task = task_queue.front();
                task_queue.pop_front();
                
                if (task.location != current_states[k].location) {
                    // Found a suitable task
                    task.t_assigned = timestep;
                    task.agent_assigned = k;
                    assigned_tasks[k].push_back(task);
                    events[k].push_back(std::make_tuple(task.task_id, timestep, "assigned"));
                    all_tasks.push_back(task);
                    
                    std::cout << "Assigned task " << task.task_id << " to agent " << k << " at timestep " << timestep << std::endl;
                    found_task = true;
                    break;
                } else {
                    // Put task back at the end of the queue
                    task_queue.push_back(task);
                }
            }
            
            // If no suitable task found, assign the first available task
            if (!found_task && !task_queue.empty()) {
                Task task = task_queue.front();
                task.t_assigned = timestep;
                task.agent_assigned = k;
                task_queue.pop_front();
                assigned_tasks[k].push_back(task);
                events[k].push_back(std::make_tuple(task.task_id, timestep, "assigned"));
                all_tasks.push_back(task);
                
                std::cout << "Assigned task " << task.task_id << " to agent " << k << " at timestep " << timestep << std::endl;
            }
        }
        
        // Also assign tasks if agent has fewer than num_tasks_reveal tasks
        while (assigned_tasks[k].size() < num_tasks_reveal && !task_queue.empty()) {
            Task task = task_queue.front();
            // Skip tasks that are at the same location as the agent to avoid immediate completion
            if (task.location == current_states[k].location) {
                task_queue.pop_front();
                task_queue.push_back(task); // Put it back at the end of the queue
                continue;
            }
            task.t_assigned = timestep;
            task.agent_assigned = k;
            task_queue.pop_front();
            assigned_tasks[k].push_back(task);
            events[k].push_back(std::make_tuple(task.task_id, timestep, "assigned"));
            all_tasks.push_back(task);
            
            std::cout << "Assigned additional task " << task.task_id << " to agent " << k << " at timestep " << timestep << std::endl;
        }
        
        // Check if agent has completed their current task
        if (!assigned_tasks[k].empty()) {
            Task& current_task = assigned_tasks[k].front();
            if (current_states[k].location == current_task.location) {
                // Task completed
                Task completed_task = assigned_tasks[k].front();
                assigned_tasks[k].pop_front();
                completed_task.t_completed = timestep;
                finished_tasks[k].push_back(completed_task);
                num_of_task_finish++;
                events[k].push_back(std::make_tuple(completed_task.task_id, timestep, "finished"));
                
                std::cout << "Agent " << k << " completed task " << completed_task.task_id << " at timestep " << timestep << std::endl;
                
                // Don't assign new tasks immediately after completion to avoid immediate re-completion
                // The next planning step will handle new task assignment
            }
        }
    }
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

bool MAPFServer::load_problem_configuration(const std::string& problem_file) {
    try {
        std::cout << "Loading problem configuration from: " << problem_file << std::endl;
        
        std::ifstream f(problem_file);
        if (!f.is_open()) {
            std::cerr << "Failed to open problem file: " << problem_file << std::endl;
            return false;
        }
        
        std::cout << "Parsing JSON configuration..." << std::endl;
        nlohmann::json data = nlohmann::json::parse(f);
        
        // Load basic configuration
        team_size = data["teamSize"].get<int>();
        num_tasks_reveal = data.value("numTasksReveal", 1);
        task_assignment_strategy = data.value("taskAssignmentStrategy", "greedy");
        
        std::cout << "Basic configuration loaded:" << std::endl;
        std::cout << "  Team size: " << team_size << std::endl;
        std::cout << "  Num tasks reveal: " << num_tasks_reveal << std::endl;
        std::cout << "  Task assignment strategy: " << task_assignment_strategy << std::endl;
        
        // Load agent start locations
        std::string agent_file = data["agentFile"].get<std::string>();
        std::string base_path = "./example_problems/custom_domain/";
        std::string full_agent_path = base_path + agent_file;
        
        std::cout << "Loading agent file: " << full_agent_path << std::endl;
        std::ifstream agent_f(full_agent_path);
        if (!agent_f.is_open()) {
            std::cerr << "Failed to open agent file: " << full_agent_path << std::endl;
            return false;
        }
        
        std::string line;
        std::getline(agent_f, line);
        while (!agent_f.eof() && line[0] == '#') {
            std::getline(agent_f, line);
        }
        
        // Parse agent file format: first line is number of agents, then one location per line
        int num_agents = std::atoi(line.c_str());
        std::cout << "Number of agents in file: " << num_agents << std::endl;
        agent_start_locations.resize(num_agents);
        
        for (int i = 0; i < num_agents; i++) {
            std::getline(agent_f, line);
            while (!agent_f.eof() && line[0] == '#') {
                std::getline(agent_f, line);
            }
            if (agent_f.eof()) {
                std::cerr << "Unexpected end of file while reading agent " << i << std::endl;
                return false;
            }
            agent_start_locations[i] = std::atoi(line.c_str());
            std::cout << "Agent " << i << " start location: " << agent_start_locations[i] << std::endl;
        }
        
        // Load task locations
        std::string task_file = data["taskFile"].get<std::string>();
        std::string full_task_path = base_path + task_file;
        
        std::cout << "Loading task file: " << full_task_path << std::endl;
        std::ifstream task_f(full_task_path);
        if (!task_f.is_open()) {
            std::cerr << "Failed to open task file: " << full_task_path << std::endl;
            return false;
        }
        
        // Read number of tasks (first line)
        std::getline(task_f, line);
        while (!task_f.eof() && line[0] == '#') {
            std::getline(task_f, line);
        }
        int num_tasks = std::atoi(line.c_str());
        std::cout << "Number of tasks in file: " << num_tasks << std::endl;
        
        task_locations.clear();
        task_locations.reserve(num_tasks);
        
        for (int i = 0; i < num_tasks; i++) {
            std::getline(task_f, line);
            while (!task_f.eof() && line[0] == '#') {
                std::getline(task_f, line);
            }
            if (task_f.eof() && line.empty()) {
                std::cerr << "Unexpected end of file while reading task " << i << std::endl;
                break; // Don't return false, just break and use what we have
            }
            // Trim whitespace from the line
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);
            if (!line.empty()) {
                task_locations.push_back(std::atoi(line.c_str()));
                std::cout << "Task " << i << " location: " << task_locations.back() << std::endl;
            } else {
                // Skip empty lines and try to get the next line
                i--; // Retry this index
                continue;
            }
        }
        
        // Verify we loaded the correct number of tasks
        if (task_locations.size() != num_tasks) {
            std::cerr << "Warning: Expected " << num_tasks << " tasks but loaded " << task_locations.size() << std::endl;
        }
        
        // Ensure we have at least some tasks
        if (task_locations.empty()) {
            std::cerr << "No tasks loaded, using default tasks" << std::endl;
            // Add some default tasks if none were loaded
            for (int i = 0; i < 5; i++) {
                task_locations.push_back(agent_start_locations[i]);
            }
        }
        
        std::cout << "Loaded problem configuration:" << std::endl;
        std::cout << "  Team size: " << team_size << std::endl;
        std::cout << "  Agent start locations: " << agent_start_locations.size() << std::endl;
        std::cout << "  Task locations: " << task_locations.size() << std::endl;
        std::cout << "  Task assignment strategy: " << task_assignment_strategy << std::endl;
        
        // Debug: Show agent and task positions in row/column format
        std::cout << "Agent start positions (row, col):" << std::endl;
        for (size_t i = 0; i < agent_start_locations.size(); i++) {
            int row = agent_start_locations[i] / grid->cols;
            int col = agent_start_locations[i] % grid->cols;
            std::cout << "  Agent " << i << ": (" << row << ", " << col << ") [linearized: " << agent_start_locations[i] << "]" << std::endl;
        }
        
        std::cout << "Task positions (row, col):" << std::endl;
        for (size_t i = 0; i < task_locations.size(); i++) {
            int row = task_locations[i] / grid->cols;
            int col = task_locations[i] % grid->cols;
            std::cout << "  Task " << i << ": (" << row << ", " << col << ") [linearized: " << task_locations[i] << "]" << std::endl;
        }
        
        // Debug: Show complete map layout
        std::cout << "Complete map layout (10x10):" << std::endl;
        for (int r = 0; r < grid->rows; r++) {
            for (int c = 0; c < grid->cols; c++) {
                int loc = r * grid->cols + c;
                bool is_agent = false;
                int agent_id = -1;
                for (size_t i = 0; i < agent_start_locations.size(); i++) {
                    if (agent_start_locations[i] == loc) {
                        is_agent = true;
                        agent_id = i;
                        break;
                    }
                }
                if (is_agent) {
                    std::cout << "A" << agent_id;
                } else if (grid->map[loc] == 1) {
                    std::cout << "@";
                } else {
                    std::cout << ".";
                }
            }
            std::cout << std::endl;
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error loading problem configuration: " << e.what() << std::endl;
        return false;
    }
}

void MAPFServer::initialize_task_system() {
    std::cout << "Initializing task system..." << std::endl;
    std::cout << "Task assignment strategy: " << task_assignment_strategy << std::endl;
    std::cout << "Number of task locations: " << task_locations.size() << std::endl;
    
    // Initialize task queue based on assignment strategy
    if (task_assignment_strategy == "greedy") {
        // For greedy assignment, all tasks go into a single queue
        std::cout << "Using greedy assignment strategy" << std::endl;
        for (size_t i = 0; i < task_locations.size(); i++) {
            task_queue.emplace_back(task_id++, task_locations[i], 0, -1);
        }
    } else if (task_assignment_strategy == "roundrobin") {
        // For roundrobin, tasks are assigned in round-robin fashion
        std::cout << "Using roundrobin assignment strategy" << std::endl;
        for (size_t i = 0; i < task_locations.size(); i++) {
            int agent_id = i % team_size;
            task_queue.emplace_back(task_id++, task_locations[i], 0, agent_id);
        }
    } else if (task_assignment_strategy == "roundrobin_fixed") {
        // For roundrobin_fixed, tasks are pre-assigned to agents
        std::cout << "Using roundrobin_fixed assignment strategy" << std::endl;
        for (size_t i = 0; i < task_locations.size(); i++) {
            int agent_id = i % team_size;
            task_queue.emplace_back(task_id++, task_locations[i], 0, agent_id);
        }
    }
    
    // Initialize agent states from start locations
    initial_states.clear();
    for (size_t i = 0; i < agent_start_locations.size(); i++) {
        initial_states.emplace_back(agent_start_locations[i], 0, 0);
    }
    
    std::cout << "Initialized task system with " << task_queue.size() << " tasks" << std::endl;
    std::cout << "Initial states created for " << initial_states.size() << " agents" << std::endl;
}

void MAPFServer::check_for_new_tasks() {
    try {
        std::string task_file_path = "./example_problems/custom_domain/task/mytask.tasks";
        std::ifstream task_file(task_file_path);
        
        if (!task_file.is_open()) {
            std::cerr << "Cannot open task file for checking new tasks: " << task_file_path << std::endl;
            return;
        }
        
        // Read current tasks from file
        std::vector<int> file_tasks;
        std::string line;
        std::getline(task_file, line);
        
        // Skip comments
        while (!task_file.eof() && line[0] == '#') {
            std::getline(task_file, line);
        }
        
        int num_file_tasks = std::atoi(line.c_str());
        
        for (int i = 0; i < num_file_tasks; i++) {
            std::getline(task_file, line);
            while (!task_file.eof() && line[0] == '#') {
                std::getline(task_file, line);
            }
            if (!task_file.eof() && !line.empty()) {
                // Trim whitespace
                line.erase(0, line.find_first_not_of(" \t\r\n"));
                line.erase(line.find_last_not_of(" \t\r\n") + 1);
                if (!line.empty()) {
                    file_tasks.push_back(std::atoi(line.c_str()));
                }
            }
        }
        task_file.close();
        
        // Check if there are new tasks that are not in our queue
        std::set<int> queue_task_locations;
        for (const auto& task : task_queue) {
            queue_task_locations.insert(task.location);
        }
        
        // Also check assigned tasks
        for (const auto& agent_tasks : assigned_tasks) {
            for (const auto& task : agent_tasks) {
                queue_task_locations.insert(task.location);
            }
        }
        
        // Add new tasks to queue
        int new_tasks_added = 0;
        for (int file_task : file_tasks) {
            if (queue_task_locations.find(file_task) == queue_task_locations.end()) {
                // This is a new task, add it to the queue
                task_queue.emplace_back(task_id++, file_task, 0, -1);
                queue_task_locations.insert(file_task);
                new_tasks_added++;
                std::cout << "Added new task " << (task_id-1) << " at location " << file_task << " to queue" << std::endl;
            }
        }
        
        if (new_tasks_added > 0) {
            std::cout << "Added " << new_tasks_added << " new tasks to queue. Total tasks in queue: " << task_queue.size() << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error checking for new tasks: " << e.what() << std::endl;
    }
}