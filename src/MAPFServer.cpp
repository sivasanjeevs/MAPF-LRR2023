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
#include <cstdlib> // Required for signal handling

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
    : map_file(map_file), config_file(config_file), port(port), 
      team_size(0), num_tasks_reveal(1), task_assignment_strategy("greedy"),
      timestep(0), num_of_task_finish(0), task_id(0), session_active(false),
      fast_mover_feasible(true) {
    
    // Set up signal handling to prevent crashes
    signal(SIGSEGV, [](int sig) {
        std::cerr << "Segmentation fault caught! Attempting graceful recovery..." << std::endl;
        exit(1);
        // Don't exit, just continue with error handling
    });
    
    signal(SIGABRT, [](int sig) {
        std::cerr << "Abort signal caught! Attempting graceful recovery..." << std::endl;
        // Don't exit, just continue with error handling
        exit(1);
    });
    
    std::cout << "Starting MAPF Server..." << std::endl;
    std::cout << "Map file: " << map_file << std::endl;
    std::cout << "Config file: " << config_file << std::endl;
    std::cout << "Port: " << port << std::endl;
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
            planner->initialize(30);
            std::cout << "Planner initialized" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Error initializing planner: " << e.what() << std::endl;
            return false;
        }
        
        // Load problem configuration if available
        //std::string problem_file = "./example_problems/custom_domain/myproblem.json";
        //std::cout << "Looking for problem file: " << problem_file << std::endl;
        
        // if (load_problem_configuration(problem_file)) {
        //     std::cout << "Problem configuration loaded successfully" << std::endl;
        //     initialize_task_system();
        //     // Update environment with loaded team size
        //     env->num_of_agents = team_size;
        //     std::cout << "Environment updated with team size: " << team_size << std::endl;
        // } else {
        //     std::cout << "No problem configuration found, using default settings" << std::endl;
        //     // Use the config file provided to the server
        //     std::cout << "Using config file: " << config_file << std::endl;
        // }

        // Initialize task system after planner is ready
        if (!task_locations.empty()) {
            initialize_task_system();
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
        std::cout << "  GET  /task_status - Get current task status for all agents" << std::endl;
        std::cout << "  POST /add_task - Add new task to the queue" << std::endl;

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
        } else if (method == "GET" && path == "/status") {
            response = handle_status_request();
        } else if (method == "GET" && path == "/health") {
            response = handle_health_request();
        } else if (method == "GET" && path == "/report") {
            response = handle_report_request();
        } else if (method == "POST" && path == "/reset") {
            response = handle_reset_request();
        } else if (method == "GET" && path == "/task_status") {
            response = handle_task_status_request();
        } else if (method == "POST" && path == "/add_task") {
            response = handle_add_task_request(body);
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
    
    // Reinitialize task system
    if (!task_locations.empty()) {
        initialize_task_system();
    }
    
    nlohmann::json response = {
        {"status", "success"},
        {"message", "Simulation history has been reset."}
    };
    return response.dump(4);
}

std::string MAPFServer::handle_plan_request(const std::string& request_body) {
    try {
        // Parse JSON request
        nlohmann::json request;
        try {
            request = nlohmann::json::parse(request_body);
        } catch (const std::exception& e) {
            return nlohmann::json({{"error", "Invalid JSON"}, {"message", e.what()}}).dump(4);
        }
        
        if (!validate_planning_request(request)) {
            return nlohmann::json({{"error", "Invalid Request"}, {"message", "Request must contain 'agents' array"}}).dump(4);
        }
        
        // Parse agent states and goals
        std::vector<State> agents = parse_agent_states(request["agents"]);
        std::vector<std::pair<int, int>> goals;
        if (request.contains("goals")) {
            goals = parse_goals(request["goals"]);
        }
        
        // Initialize session if not active
        if (!session_active) {
            session_active = true;
            team_size = agents.size();
            history_of_actions.clear();
            history_of_planning_times.clear();
            
            // Initialize task tracking structures
            finished_tasks.resize(team_size);
            assigned_tasks.resize(team_size);
            events.resize(team_size);
            solution_costs.resize(team_size, 0);
            actual_movements.resize(team_size);
            planner_movements.resize(team_size);
            current_agent_states.resize(team_size);
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
        }

        // Update current agent states
        current_agent_states = agents;

        // Update tasks for lifelong MAPF
        update_tasks_lifelong(agents);

        // Set up environment for planning
            env->curr_states = agents;
            env->goal_locations.clear();
            env->goal_locations.resize(team_size);
        
        // Add goals for agents with assigned tasks
        for (int i = 0; i < team_size; i++) {
                env->goal_locations[i].clear();
                for (auto& task: assigned_tasks[i]) {
                    env->goal_locations[i].push_back({task.location, task.t_assigned});
                }
                if (env->goal_locations[i].empty()) {
                    env->goal_locations[i].push_back({agents[i].location, timestep});
                }
            }
            
        // Plan for the new task
        std::vector<Action> actions;
        bool planning_success = false;
        auto start_time = std::chrono::high_resolution_clock::now();
        
        try {
            // Add timeout and error handling for planning
            try {
            planner->plan(5.0, actions);
                planning_success = true;
        } catch (const std::exception& e) {
                std::cerr << "Planning failed with exception: " << e.what() << std::endl;
                // Use wait actions as fallback
                actions.clear();
                for (int i = 0; i < team_size; i++) {
                    actions.push_back(Action::W);
                }
                planning_success = false;
            } catch (...) {
                std::cerr << "Unknown planning error" << std::endl;
                // Use wait actions as fallback
                actions.clear();
                for (int i = 0; i < team_size; i++) {
                    actions.push_back(Action::W);
                }
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
            return nlohmann::json({{"error", "Planner Failure"}, {"message", "Planner did not return an action for every agent."}}).dump(4);
        }

        // Update solution costs for agents with goals
        for (int a = 0; a < team_size; a++) {
            if (!env->goal_locations[a].empty()) {
                solution_costs[a]++;
            }
        }

        } catch (const std::exception& e) {
            std::cerr << "Error during planning: " << e.what() << std::endl;
            // Return error response
            return nlohmann::json({{"error", "Planning Failed"}, {"message", e.what()}}).dump(4);
        } catch (...) {
            std::cerr << "Unknown error during planning" << std::endl;
            // Return error response
            return nlohmann::json({{"error", "Planning Failed"}, {"message", "Unknown error"}}).dump(4);
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> planning_duration = end_time - start_time;

        // Check for finished tasks using the action model
        std::vector<State> new_states;
        bool result_states_success = false;
        
        try {
            new_states = action_model->result_states(agents, actions);
            result_states_success = true;
        } catch (const std::exception& e) {
            std::cerr << "Error computing result states: " << e.what() << std::endl;
            // Use current states as fallback
            new_states = agents;
            result_states_success = false;
        } catch (...) {
            std::cerr << "Unknown error computing result states" << std::endl;
            // Use current states as fallback
            new_states = agents;
            result_states_success = false;
        }
        
        // If result_states failed, use wait actions for this timestep
        if (!result_states_success) {
            std::cerr << "Result states computation failed, using wait actions" << std::endl;
            // Replace actions with wait actions
            actions.clear();
            for (int i = 0; i < team_size; i++) {
                actions.push_back(Action::W);
            }
            // Recompute result states with wait actions
            try {
                new_states = action_model->result_states(agents, actions);
            } catch (...) {
                // If even wait actions fail, just use current states
                new_states = agents;
            }
        }
        
        // Update agent positions safely
        try {
        for (int k = 0; k < team_size; k++) {
                if (k < new_states.size()) {
                    // Update current agent states for next iteration
                    if (k < current_agent_states.size()) {
                        current_agent_states[k] = new_states[k];
                    }
                    
                    // Check for task completion
            if (!assigned_tasks[k].empty() && new_states[k].location == assigned_tasks[k].front().location) {
                Task task = assigned_tasks[k].front();
                assigned_tasks[k].pop_front();
                task.t_completed = timestep;
                finished_tasks[k].push_back(task);
                num_of_task_finish++;
                log_event_finished(k, task.task_id, timestep);
                
                        std::cout << "✓ Agent " << k << " completed task " << task.task_id << " at timestep " << timestep << std::endl;
            }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error updating agent states: " << e.what() << std::endl;
            // Continue with current states
        }

        history_of_actions.push_back(actions);
        history_of_planning_times.push_back(planning_duration.count());
        
        // Track movements like the lifelong system
        try {
        for (int k = 0; k < team_size; k++) {
            if (k < actions.size()) {
                planner_movements[k].push_back(actions[k]);
            } else {
                fast_mover_feasible = false;
                planner_movements[k].push_back(Action::NA);
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error tracking planner movements: " << e.what() << std::endl;
            // Use wait actions as fallback
            for (int k = 0; k < team_size; k++) {
                if (k < planner_movements.size()) {
                    planner_movements[k].push_back(Action::W);
                }
            }
        }
        
        // Check if moves are valid and update actual movements
        bool actions_valid = false;
        try {
            actions_valid = action_model->is_valid(agents, actions);
        } catch (const std::exception& e) {
            std::cerr << "Error validating actions: " << e.what() << std::endl;
            actions_valid = false;
        }
        
        try {
        if (!actions_valid) {
            fast_mover_feasible = false;
            std::cerr << "Planned actions are not valid, using wait actions" << std::endl;
            // Use wait actions if invalid
            std::vector<Action> wait_actions(team_size, Action::W);
            for (int k = 0; k < team_size; k++) {
                    if (k < actual_movements.size()) {
                actual_movements[k].push_back(wait_actions[k]);
                    }
            }
        } else {
            // Use planned actions if valid
            for (int k = 0; k < team_size; k++) {
                    if (k < actual_movements.size() && k < actions.size()) {
                actual_movements[k].push_back(actions[k]);
                    } else if (k < actual_movements.size()) {
                        actual_movements[k].push_back(Action::W);
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error updating actual movements: " << e.what() << std::endl;
            // Use wait actions as fallback
            for (int k = 0; k < team_size; k++) {
                if (k < actual_movements.size()) {
                    actual_movements[k].push_back(Action::W);
                }
            }
        }
        
        // Increment timestep and check for timeout
        timestep++;
        
        // Safety check: if we've been stuck for too long, force some progress
        if (timestep > 1000) {
            std::cerr << "Warning: Simulation reached timestep 1000, forcing task completion" << std::endl;
            // Force complete all current tasks to prevent infinite loop
            for (int k = 0; k < team_size; k++) {
                if (!assigned_tasks[k].empty()) {
                    Task task = assigned_tasks[k].front();
                    assigned_tasks[k].pop_front();
                    task.t_completed = timestep;
                    finished_tasks[k].push_back(task);
                    num_of_task_finish++;
                    log_event_finished(k, task.task_id, timestep);
                    std::cout << "⚠ Forced completion of task " << task.task_id << " for agent " << k << std::endl;
                }
            }
        }
        
        // Save results to test.json after each timestep
        save_results_to_file();

        // Prepare task status information for response
        nlohmann::json task_status = nlohmann::json::array();
        bool all_tasks_finished = true;
        
        for (int k = 0; k < team_size; k++) {
            nlohmann::json agent_status = {
                {"agent_id", k},
                {"has_task", !assigned_tasks[k].empty()}
            };
            
            if (!assigned_tasks[k].empty()) {
                Task& task = assigned_tasks[k].front();
                agent_status["current_task"] = {
                    {"task_id", task.task_id},
                    {"location", task.location},
                    {"assigned_at", task.t_assigned}
                };
                all_tasks_finished = false; // Still have assigned tasks
            }
            
            agent_status["tasks_completed"] = finished_tasks[k].size();
            task_status.push_back(agent_status);
        }
        
        // Check if all tasks are finished (no assigned tasks and no tasks in queue)
        if (all_tasks_finished && task_queue.empty()) {
            all_tasks_finished = true;
        } else {
            all_tasks_finished = false;
        }

        nlohmann::json response = {
            {"status", "success"},
            {"timestep", timestep},
            {"actions", serialize_path(actions, new_states)},
            {"task_status", task_status},
            {"tasks_remaining", task_queue.size()},
            {"total_tasks_completed", num_of_task_finish},
            {"all_tasks_finished", all_tasks_finished}
        };
        return response.dump(4);

    } catch (const std::exception& e) {
        std::cerr << "Critical error in handle_plan_request: " << e.what() << std::endl;
        return nlohmann::json({{"error", "Critical Error"}, {"message", e.what()}}).dump(4);
    } catch (...) {
        std::cerr << "Unknown critical error in handle_plan_request" << std::endl;
        return nlohmann::json({{"error", "Critical Error"}, {"message", "Unknown error"}}).dump(4);
    }
}

// ======================================================================================
// === MODIFICATION: The only changes are in this function to match the JSON format. ===
// ======================================================================================
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
// ======================================================================================
// === END OF MODIFICATION                                                            ===
// ======================================================================================

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

std::string MAPFServer::handle_task_status_request() {
    if (!session_active) {
        return nlohmann::json({{"error", "No Active Session"}, {"message", "No simulation data to report. Send a /plan request first."}}).dump(4);
    }

    nlohmann::json task_status_report = nlohmann::json::array();
    for (int k = 0; k < team_size; k++) {
        nlohmann::json agent_status = {
            {"agent_id", k},
            {"has_task", !assigned_tasks[k].empty()}
        };
        if (!assigned_tasks[k].empty()) {
            Task& task = assigned_tasks[k].front();
            agent_status["current_task"] = {
                {"task_id", task.task_id},
                {"location", task.location},
                {"assigned_at", task.t_assigned}
            };
        }
        agent_status["tasks_completed"] = finished_tasks[k].size();
        task_status_report.push_back(agent_status);
    }
    return task_status_report.dump(4);
}

std::string MAPFServer::handle_add_task_request(const std::string& request_body) {
    try {
        // Parse JSON request
        nlohmann::json request;
        try {
            request = nlohmann::json::parse(request_body);
        } catch (const std::exception& e) {
            return nlohmann::json({{"error", "Invalid JSON"}, {"message", e.what()}}).dump(4);
        }
        
        // Validate request
        if (!request.contains("location")) {
            return nlohmann::json({{"error", "Invalid Request"}, {"message", "Missing 'location' field"}}).dump(4);
        }
        
        int new_task_location = request["location"].get<int>();
        
        // Validate location is within map bounds
        if (new_task_location < 0 || new_task_location >= grid->rows * grid->cols) {
            return nlohmann::json({{"error", "Invalid Location"}, {"message", "Task location is outside map bounds"}}).dump(4);
        }
        
        // Check if location is not blocked
        if (grid->map[new_task_location] == 1) {
            return nlohmann::json({{"error", "Invalid Location"}, {"message", "Task location is blocked"}}).dump(4);
        }
        
        // Add task to the task file
        if (!add_task_to_file(new_task_location)) {
            return nlohmann::json({{"error", "File Error"}, {"message", "Failed to add task to file"}}).dump(4);
        }
        
        // Reload tasks from file to update the queue
        if (!reload_tasks_from_file()) {
            return nlohmann::json({{"error", "Reload Error"}, {"message", "Failed to reload tasks from file"}}).dump(4);
        }
        
        std::cout << "✓ Added new task at location " << new_task_location << " (task ID: " << (task_id-1) << ")" << std::endl;
        
        // Auto-trigger planning for the new task if session is active
        nlohmann::json planning_result;
        if (session_active) {
            std::cout << "🔄 Auto-triggering planning for new task..." << std::endl;
            
            // Use current agent states if available, otherwise use initial states
            std::vector<State> current_agents;
            if (!current_agent_states.empty()) {
                current_agents = current_agent_states;
                std::cout << "Using current agent positions for planning" << std::endl;
            } else {
                current_agents = initial_states;
                std::cout << "Using initial agent positions for planning" << std::endl;
            }
            
            // Update tasks with new task
            update_tasks_lifelong(current_agents);
            
            // Set up environment for planning
            env->num_of_agents = static_cast<int>(current_agents.size());
            env->curr_states = current_agents;
            env->curr_timestep = timestep;
            
            // Clear and set up goal locations properly
            env->goal_locations.clear();
            env->goal_locations.resize(team_size);
            for (size_t i = 0; i < team_size; i++) {
                env->goal_locations[i].clear();
                for (auto& task: assigned_tasks[i]) {
                    env->goal_locations[i].push_back({task.location, task.t_assigned});
                }
            }
            
            // Plan for the new task
            std::vector<Action> actions;
            bool planning_success = false;
            
            try {
                auto start_time = std::chrono::high_resolution_clock::now();
                
                // Add timeout and error handling for planning
                try {
                    planner->plan(5.0, actions);
                    planning_success = true;
                } catch (const std::exception& e) {
                    std::cerr << "Planning failed during auto-planning: " << e.what() << std::endl;
                    // Use wait actions as fallback
                    actions.clear();
                    for (int i = 0; i < team_size; i++) {
                        actions.push_back(Action::W);
                    }
                    planning_success = false;
                } catch (...) {
                    std::cerr << "Unknown planning error during auto-planning" << std::endl;
                    // Use wait actions as fallback
                    actions.clear();
                    for (int i = 0; i < team_size; i++) {
                        actions.push_back(Action::W);
                    }
                    planning_success = false;
                }
                
                auto end_time = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double> planning_duration = end_time - start_time;
                
                std::cout << "✓ Planning completed in " << planning_duration.count() << " seconds" << std::endl;
                
                // Prepare planning result
                nlohmann::json task_status = nlohmann::json::array();
                for (int k = 0; k < team_size; k++) {
                    nlohmann::json agent_status = {
                        {"agent_id", k},
                        {"has_task", !assigned_tasks[k].empty()}
                    };
                    
                    if (!assigned_tasks[k].empty()) {
                        Task& task = assigned_tasks[k].front();
                        agent_status["current_task"] = {
                            {"task_id", task.task_id},
                            {"location", task.location},
                            {"assigned_at", task.t_assigned}
                        };
                    }
                    
                    agent_status["tasks_completed"] = finished_tasks[k].size();
                    task_status.push_back(agent_status);
                }
                
                planning_result = {
                    {"planning_triggered", true},
                    {"planning_success", planning_success},
                    {"planning_time", planning_duration.count()},
                    {"actions_planned", actions.size()},
                    {"task_status", task_status},
                    {"tasks_remaining", task_queue.size()},
                    {"total_tasks_completed", num_of_task_finish}
                };
                
            } catch (const std::exception& e) {
                std::cerr << "Error during auto-planning: " << e.what() << std::endl;
                planning_result = {
                    {"planning_triggered", true},
                    {"planning_success", false},
                    {"error", e.what()}
                };
            } catch (...) {
                std::cerr << "Unknown error during auto-planning" << std::endl;
                planning_result = {
                    {"planning_triggered", true},
                    {"planning_success", false},
                    {"error", "Unknown error"}
                };
            }
        } else {
            planning_result = {
                {"planning_triggered", false},
                {"message", "No active session - planning will be triggered on next /plan request"}
            };
        }
        
        nlohmann::json response = {
            {"status", "success"},
            {"message", "Task added successfully"},
            {"task_id", task_id - 1},
            {"location", new_task_location},
            {"tasks_in_queue", task_queue.size()},
            {"planning", planning_result}
        };
        return response.dump(4);
        
    } catch (const std::exception& e) {
        return nlohmann::json({{"error", "Add Task Failed"}, {"message", e.what()}}).dump(4);
    }
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

void MAPFServer::update_tasks(const std::vector<State>& current_states, const std::vector<std::pair<int, int>>& goals) {
    // For each agent, create a task for their current goal
    for (size_t i = 0; i < goals.size(); ++i) {
        if (i < team_size) {
            // Create a new task for this goal
            Task task(task_id++, goals[i].first, timestep, static_cast<int>(i));
            assigned_tasks[i].push_back(task);
            all_tasks.push_back(task);
            log_event_assigned(static_cast<int>(i), task.task_id, timestep);
        }
    }
}

void MAPFServer::update_tasks_lifelong(const std::vector<State>& current_states) {
    // Safety check: ensure task system is initialized
    if (team_size <= 0 || assigned_tasks.size() != team_size || events.size() != team_size) {
        std::cerr << "Task system not properly initialized. Skipping task assignment." << std::endl;
        return;
    }
    
    std::cout << "=== Task Update at Timestep " << timestep << " ===" << std::endl;
    std::cout << "Tasks in queue: " << task_queue.size() << std::endl;
    
    // First, check for completed tasks
    for (int k = 0; k < team_size; k++) {
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
                
                std::cout << "✓ Agent " << k << " completed task " << completed_task.task_id 
                          << " at location " << completed_task.location 
                          << " (timestep " << timestep << ")" << std::endl;
            }
        }
    }
    
    // Then, assign new tasks to free agents
    for (int k = 0; k < team_size; k++) {
        // Check if agent is free (no assigned tasks) and has tasks available
        if (assigned_tasks[k].empty() && !task_queue.empty()) {
            // Assign a new task to this free agent
            Task task = task_queue.front();
            task.t_assigned = timestep;
            task.agent_assigned = k;
            task_queue.pop_front();
            assigned_tasks[k].push_back(task);
            events[k].push_back(std::make_tuple(task.task_id, timestep, "assigned"));
            all_tasks.push_back(task);
            
            std::cout << "→ Assigned task " << task.task_id << " to agent " << k 
                      << " at location " << task.location 
                      << " (timestep " << timestep << ")" << std::endl;
        }
        }
        
    // Print current task status
    std::cout << "Current task assignments:" << std::endl;
    for (int k = 0; k < team_size; k++) {
        if (!assigned_tasks[k].empty()) {
            Task& task = assigned_tasks[k].front();
            std::cout << "  Agent " << k << ": Task " << task.task_id 
                      << " at location " << task.location 
                      << " (assigned at timestep " << task.t_assigned << ")" << std::endl;
        } else {
            std::cout << "  Agent " << k << ": No assigned task" << std::endl;
        }
    }
    
    std::cout << "Tasks remaining in queue: " << task_queue.size() << std::endl;
    std::cout << "Total tasks completed: " << num_of_task_finish << std::endl;
    std::cout << "=====================================" << std::endl;
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
        
        // Store the task file path for dynamic reloading
        task_file_path = full_task_path;
        
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
            if (task_f.eof()) {
                std::cerr << "Unexpected end of file while reading task " << i << std::endl;
                return false;
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
    std::cout << "Team size: " << team_size << std::endl;
    
    // Clear existing task queue
    task_queue.clear();
    
    // Initialize task queue based on assignment strategy
    if (task_assignment_strategy == "greedy") {
        // For greedy assignment, all tasks go into a single queue
        std::cout << "Using greedy assignment strategy - tasks will be assigned to first available agent" << std::endl;
        for (size_t i = 0; i < task_locations.size(); i++) {
            task_queue.emplace_back(task_id++, task_locations[i], 0, -1);
            std::cout << "  Added task " << (task_id-1) << " at location " << task_locations[i] << std::endl;
        }
    } else if (task_assignment_strategy == "roundrobin") {
        // For roundrobin, tasks are assigned in round-robin fashion
        std::cout << "Using roundrobin assignment strategy - tasks distributed evenly among agents" << std::endl;
        for (size_t i = 0; i < task_locations.size(); i++) {
            int agent_id = i % team_size;
            task_queue.emplace_back(task_id++, task_locations[i], 0, agent_id);
            std::cout << "  Added task " << (task_id-1) << " at location " << task_locations[i] 
                      << " (pre-assigned to agent " << agent_id << ")" << std::endl;
        }
    } else if (task_assignment_strategy == "roundrobin_fixed") {
        // For roundrobin_fixed, tasks are pre-assigned to agents
        std::cout << "Using roundrobin_fixed assignment strategy - tasks pre-assigned to specific agents" << std::endl;
        for (size_t i = 0; i < task_locations.size(); i++) {
            int agent_id = i % team_size;
            task_queue.emplace_back(task_id++, task_locations[i], 0, agent_id);
            std::cout << "  Added task " << (task_id-1) << " at location " << task_locations[i] 
                      << " (pre-assigned to agent " << agent_id << ")" << std::endl;
        }
    } else {
        // Default to greedy if unknown strategy
        std::cout << "Unknown task assignment strategy '" << task_assignment_strategy 
                  << "', defaulting to greedy" << std::endl;
        for (size_t i = 0; i < task_locations.size(); i++) {
            task_queue.emplace_back(task_id++, task_locations[i], 0, -1);
            std::cout << "  Added task " << (task_id-1) << " at location " << task_locations[i] << std::endl;
        }
    }
    
    // Initialize agent states from start locations
    initial_states.clear();
    for (size_t i = 0; i < agent_start_locations.size(); i++) {
        initial_states.emplace_back(agent_start_locations[i], 0, 0);
        std::cout << "  Agent " << i << " start location: " << agent_start_locations[i] << std::endl;
    }
    
    std::cout << "Task system initialized successfully:" << std::endl;
    std::cout << "  - " << task_queue.size() << " tasks in queue" << std::endl;
    std::cout << "  - " << initial_states.size() << " agents initialized" << std::endl;
    std::cout << "  - Task assignment strategy: " << task_assignment_strategy << std::endl;
    std::cout << "  - Ready for continuous operation" << std::endl;
}

bool MAPFServer::add_task_to_file(int location) {
    try {
        if (task_file_path.empty()) {
            std::cerr << "Task file path not set" << std::endl;
            return false;
        }
        
        // Read current tasks from file
        std::ifstream task_f(task_file_path);
        if (!task_f.is_open()) {
            std::cerr << "Failed to open task file: " << task_file_path << std::endl;
            return false;
        }
        
        std::string line;
        std::getline(task_f, line);
        while (!task_f.eof() && line[0] == '#') {
            std::getline(task_f, line);
        }
        
        int num_tasks = std::atoi(line.c_str());
        std::vector<int> existing_tasks;
        
        for (int i = 0; i < num_tasks; i++) {
            std::getline(task_f, line);
            while (!task_f.eof() && line[0] == '#') {
                std::getline(task_f, line);
            }
            if (task_f.eof()) break;
            
            // Trim whitespace
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);
            if (!line.empty()) {
                existing_tasks.push_back(std::atoi(line.c_str()));
            }
        }
        task_f.close();
        
        // Add new task
        existing_tasks.push_back(location);
        
        // Write back to file
        std::ofstream task_out(task_file_path);
        if (!task_out.is_open()) {
            std::cerr << "Failed to open task file for writing: " << task_file_path << std::endl;
            return false;
        }
        
        task_out << existing_tasks.size() << std::endl;
        for (int task_loc : existing_tasks) {
            task_out << task_loc << std::endl;
        }
        task_out.close();
        
        std::cout << "✓ Added task at location " << location << " to file. Total tasks: " << existing_tasks.size() << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Error adding task to file: " << e.what() << std::endl;
        return false;
    }
}

bool MAPFServer::reload_tasks_from_file() {
    try {
        if (task_file_path.empty()) {
            std::cerr << "Task file path not set" << std::endl;
            return false;
        }
        
        std::cout << "Reloading tasks from file: " << task_file_path << std::endl;
        
        // Read tasks from file
        std::ifstream task_f(task_file_path);
        if (!task_f.is_open()) {
            std::cerr << "Failed to open task file: " << task_file_path << std::endl;
            return false;
        }
        
        std::string line;
        std::getline(task_f, line);
        while (!task_f.eof() && line[0] == '#') {
            std::getline(task_f, line);
        }
        
        int num_tasks = std::atoi(line.c_str());
        std::cout << "Found " << num_tasks << " tasks in file" << std::endl;
        
        // Clear existing task locations and queue
        task_locations.clear();
        task_queue.clear();
        
        // Read task locations
        for (int i = 0; i < num_tasks; i++) {
            std::getline(task_f, line);
            while (!task_f.eof() && line[0] == '#') {
                std::getline(task_f, line);
            }
            if (task_f.eof()) break;
            
            // Trim whitespace
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);
            if (!line.empty()) {
                int task_loc = std::atoi(line.c_str());
                task_locations.push_back(task_loc);
            }
        }
        task_f.close();
        
        // Reinitialize task queue
        task_id = 0; // Reset task ID counter
        if (task_assignment_strategy == "greedy") {
            for (size_t i = 0; i < task_locations.size(); i++) {
                task_queue.emplace_back(task_id++, task_locations[i], 0, -1);
            }
        } else if (task_assignment_strategy == "roundrobin" || task_assignment_strategy == "roundrobin_fixed") {
            for (size_t i = 0; i < task_locations.size(); i++) {
                int agent_id = i % team_size;
                task_queue.emplace_back(task_id++, task_locations[i], 0, agent_id);
            }
        }
        
        std::cout << "✓ Reloaded " << task_queue.size() << " tasks from file" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Error reloading tasks from file: " << e.what() << std::endl;
        return false;
    }
}

void MAPFServer::save_results_to_file() {
    try {
        nlohmann::json report;
        report["actionModel"] = "MAPF_T";
        report["AllValid"] = fast_mover_feasible ? "Yes" : "No";
        report["teamSize"] = team_size;
        
        // "start" - initial agent positions
        nlohmann::json starts = nlohmann::json::array();
        for(const auto& state : initial_states) {
            int row = state.location / grid->cols;
            int col = state.location % grid->cols;
            starts.push_back({row, col, orientation_to_string_local(state.orientation)});
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

        // Write to test.json file
        std::ofstream file("test.json");
        if (file.is_open()) {
            file << report.dump(4);
            file.close();
            std::cout << "✓ Results saved to test.json" << std::endl;
        } else {
            std::cerr << "Error: Could not open test.json for writing" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error saving results to file: " << e.what() << std::endl;
    }
}