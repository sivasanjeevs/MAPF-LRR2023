#include "MAPFServer.h"
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
    });
    
    signal(SIGABRT, [](int sig) {
        std::cerr << "Abort signal caught! Attempting graceful recovery..." << std::endl;
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
                agents = initial_states;
            } else {
                initial_states = agents;
            }
        }

        current_agent_states = agents;
        update_tasks_lifelong(agents);

        env->curr_states = agents;
        env->goal_locations.clear();
        env->goal_locations.resize(team_size);
        
        for (int i = 0; i < team_size; i++) {
            env->goal_locations[i].clear();
            for (auto& task: assigned_tasks[i]) {
                env->goal_locations[i].push_back({task.location, task.t_assigned});
            }
            if (env->goal_locations[i].empty()) {
                env->goal_locations[i].push_back({agents[i].location, timestep});
            }
        }
            
        std::vector<Action> actions;
        auto start_time = std::chrono::high_resolution_clock::now();
        
        try {
            planner->plan(5.0, actions);
        } catch (const std::exception& e) {
            std::cerr << "Planning failed with exception: " << e.what() << std::endl;
            actions.assign(team_size, Action::W);
        } catch (...) {
            std::cerr << "Unknown planning error" << std::endl;
            actions.assign(team_size, Action::W);
        }
            
        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> planning_duration = end_time - start_time;
        
        if (actions.size() != agents.size()) {
            std::cerr << "CRITICAL: Planner returned " << actions.size() 
                      << " actions for " << agents.size() << " agents. Using wait actions." << std::endl;
            actions.assign(team_size, Action::W);
        }

        for (int a = 0; a < team_size; a++) {
            if (!env->goal_locations[a].empty()) {
                solution_costs[a]++;
            }
        }
        
        std::vector<State> new_states;
        try {
            new_states = action_model->result_states(agents, actions);
        } catch (...) {
            std::cerr << "Result states computation failed, using wait actions" << std::endl;
            actions.assign(team_size, Action::W);
            new_states = action_model->result_states(agents, actions);
        }
        
        for (int k = 0; k < team_size; k++) {
            if (k < new_states.size()) {
                current_agent_states[k] = new_states[k];
                if (!assigned_tasks[k].empty() && new_states[k].location == assigned_tasks[k].front().location) {
                    Task task = assigned_tasks[k].front();
                    assigned_tasks[k].pop_front();
                    task.t_completed = timestep;
                    finished_tasks[k].push_back(task);
                    num_of_task_finish++;
                    log_event_finished(k, task.task_id, timestep);
                }
            }
        }

        history_of_actions.push_back(actions);
        history_of_planning_times.push_back(planning_duration.count());
        
        for (int k = 0; k < team_size; k++) {
            planner_movements[k].push_back(k < actions.size() ? actions[k] : Action::NA);
        }
        
        if (!action_model->is_valid(agents, actions)) {
            fast_mover_feasible = false;
            std::vector<Action> wait_actions(team_size, Action::W);
            for (int k = 0; k < team_size; k++) {
                actual_movements[k].push_back(wait_actions[k]);
            }
        } else {
            for (int k = 0; k < team_size; k++) {
                actual_movements[k].push_back(k < actions.size() ? actions[k] : Action::W);
            }
        }
        
        timestep++;
        
        save_results_to_file();

        nlohmann::json task_status = nlohmann::json::array();
        bool all_tasks_finished = task_queue.empty();
        
        for (int k = 0; k < team_size; k++) {
            nlohmann::json agent_status;
            agent_status["agent_id"] = k;
            agent_status["has_task"] = !assigned_tasks[k].empty();
            if(agent_status["has_task"]) {
                 all_tasks_finished = false;
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
    
    nlohmann::json starts = nlohmann::json::array();
    for(const auto& state : initial_states) {
        starts.push_back({state.location / grid->cols, state.location % grid->cols, orientation_to_string_local(state.orientation)});
    }
    report["start"] = starts;

    report["numTaskFinished"] = num_of_task_finish;
    int sum_of_cost = 0;
    int makespan = 0;
    if (team_size > 0) {
        sum_of_cost = std::accumulate(solution_costs.begin(), solution_costs.end(), 0);
        makespan = *std::max_element(solution_costs.begin(), solution_costs.end());
    }
    report["sumOfCost"] = sum_of_cost;
    report["makespan"] = makespan;

    std::vector<std::string> actual_paths(team_size, "");
    std::vector<std::string> planner_paths(team_size, "");
    
    for (int i = 0; i < team_size; i++) {
        for (const auto action : actual_movements[i]) actual_paths[i] += action_to_string_local(action) + ",";
        if(!actual_paths[i].empty()) actual_paths[i].pop_back();

        for (const auto action : planner_movements[i]) planner_paths[i] += action_to_string_local(action) + ",";
        if(!planner_paths[i].empty()) planner_paths[i].pop_back();
    }
    report["actualPaths"] = actual_paths;
    report["plannerPaths"] = planner_paths;

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

    nlohmann::json tasks = nlohmann::json::array();
    for (const auto& t: all_tasks) {
        tasks.push_back({t.task_id, t.location / grid->cols, t.location % grid->cols});
    }
    report["tasks"] = tasks;

    return report.dump(4);
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
        nlohmann::json request = nlohmann::json::parse(request_body);
        
        if (!request.contains("location")) {
            return nlohmann::json({{"error", "Invalid Request"}, {"message", "Missing 'location' field"}}).dump(4);
        }
        
        int new_task_location = request["location"].get<int>();
        
        if (new_task_location < 0 || new_task_location >= grid->rows * grid->cols || grid->map[new_task_location] == 1) {
            return nlohmann::json({{"error", "Invalid Location"}}).dump(4);
        }
        
        // Safely add task to in-memory queue and persist it
        task_queue.emplace_back(task_id, new_task_location, 0, -1);
        std::cout << "✓ Added new task at location " << new_task_location << " (task ID: " << task_id << ")" << std::endl;
        add_task_to_file(new_task_location); // Persist for restarts
        int new_task_id = task_id;
        task_id++;


        nlohmann::json planning_result;
        if (session_active) {
            std::cout << "🔄 Auto-triggering planning for new task..." << std::endl;
            
            // This will trigger the next planning cycle in the continuous client
            // No need to call plan() directly here, the client loop will handle it
            planning_result = {
                {"planning_triggered", true},
                {"message", "New task added to queue. Continuous client will initiate next plan."}
            };
            
        } else {
            planning_result = {
                {"planning_triggered", false},
                {"message", "No active session. Task queued. Start a session with /plan."}
            };
        }
        
        nlohmann::json response = {
            {"status", "success"},
            {"message", "Task added successfully"},
            {"task_id", new_task_id},
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
        states.emplace_back(agent["location"], agent.value("timestep", 0), agent.value("orientation", 0));
    }
    return states;
}

std::vector<std::pair<int, int>> MAPFServer::parse_goals(const nlohmann::json& goals) {
    std::vector<std::pair<int, int>> goal_pairs;
    for (const auto& goal : goals) {
        goal_pairs.emplace_back(goal["location"], goal.value("timestep", 0));
    }
    return goal_pairs;
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

void MAPFServer::update_tasks_lifelong(const std::vector<State>& current_states) {
    if (team_size <= 0 || assigned_tasks.size() != team_size || events.size() != team_size) {
        return;
    }
    
    // Assign new tasks to free agents
    for (int k = 0; k < team_size; k++) {
        if (assigned_tasks[k].empty() && !task_queue.empty()) {
            Task task = task_queue.front();
            task_queue.pop_front();
            task.t_assigned = timestep;
            task.agent_assigned = k;
            assigned_tasks[k].push_back(task);
            all_tasks.push_back(task);
            log_event_assigned(k, task.task_id, timestep);
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
        std::ifstream f(problem_file);
        if (!f.is_open()) return false;
        
        nlohmann::json data = nlohmann::json::parse(f);
        
        team_size = data["teamSize"];
        task_assignment_strategy = data.value("taskAssignmentStrategy", "greedy");
        
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
        for (int i = 0; i < num_tasks; i++) task_f >> task_locations[i];
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error loading problem configuration: " << e.what() << std::endl;
        return false;
    }
}

void MAPFServer::initialize_task_system() {
    task_queue.clear();
    task_id = 0;
    for (int loc : task_locations) {
        task_queue.emplace_back(task_id++, loc, 0, -1);
    }
    
    initial_states.clear();
    for (int loc : agent_start_locations) {
        initial_states.emplace_back(loc, 0, 0);
    }
}

bool MAPFServer::add_task_to_file(int location) {
    try {
        std::vector<int> tasks;
        std::ifstream infile(task_file_path);
        int count;
        infile >> count;
        tasks.resize(count);
        for(int i = 0; i < count; ++i) infile >> tasks[i];
        infile.close();

        tasks.push_back(location);

        std::ofstream outfile(task_file_path);
        outfile << tasks.size() << std::endl;
        for(int task_loc : tasks) outfile << task_loc << std::endl;
        outfile.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error adding task to file: " << e.what() << std::endl;
        return false;
    }
}

bool MAPFServer::reload_tasks_from_file() {
    // This function is problematic and no longer used for dynamic task adding.
    // It's kept for the initial setup.
    try {
        if (task_file_path.empty()) return false;
        std::ifstream task_f(task_file_path);
        if (!task_f.is_open()) return false;
        
        int num_tasks;
        task_f >> num_tasks;
        task_locations.assign(num_tasks, 0);
        for (int i = 0; i < num_tasks; i++) task_f >> task_locations[i];
        
        initialize_task_system();
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Error reloading tasks from file: " << e.what() << std::endl;
        return false;
    }
}

void MAPFServer::save_results_to_file() {
    try {
        std::ofstream file("test.json");
        file << handle_report_request();
    } catch (const std::exception& e) {
        std::cerr << "Error saving results to file: " << e.what() << std::endl;
    }
}