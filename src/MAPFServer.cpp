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
        grid = std::make_unique<Grid>(map_file);
        action_model = std::make_unique<ActionModelWithRotate>(*grid);
        env = std::make_unique<SharedEnvironment>();
        env->rows = grid->rows;
        env->cols = grid->cols;
        env->map = grid->map;
        env->map_name = grid->map_name;
        env->num_of_agents = 2; // Default, will be overridden
        planner = std::make_unique<MAPFPlanner>(env.get());
        setenv("CONFIG_PATH", config_file.c_str(), 1);
        planner->initialize(30);
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
            response = handle_plan_request(nlohmann::json::parse(body));
        } else if (method == "GET" && path == "/status") {
            response = handle_status_request();
        } else if (method == "GET" && path == "/health") {
            response = handle_health_request();
        } else if (method == "GET" && path == "/report") {
            response = handle_report_request();
        } else if (method == "POST" && path == "/reset") {
            response = handle_reset_request();
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
    num_of_task_finish = 0;
    task_id = 0;
    fast_mover_feasible = true;
    
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
        auto goals = parse_goals(request["goals"]);

        if (agents.size() != goals.size()) {
            return nlohmann::json({{"error", "Invalid Request"}, {"message", "The number of agents must match the number of goals."}}).dump(4);
        }

        if (!session_active) {
            session_active = true;
            initial_states = agents;
            team_size = agents.size();
            history_of_actions.clear();
            history_of_planning_times.clear();
            
            // Initialize task tracking structures
            finished_tasks.resize(team_size);
            assigned_tasks.resize(team_size);
            events.resize(team_size);
            solution_costs.resize(team_size, 0);
            timestep = 0;
            num_of_task_finish = 0;
            task_id = 0;
            fast_mover_feasible = true;
        }

        // Update tasks for this timestep
        update_tasks(agents, goals);

        env->num_of_agents = static_cast<int>(agents.size());
        env->curr_states = agents;
        env->goal_locations.assign(agents.size(), {});
        for (size_t i = 0; i < goals.size(); ++i) {
            env->goal_locations[i].push_back(goals[i]);
        }

        auto start_time = std::chrono::high_resolution_clock::now();
        std::vector<Action> actions;
        planner->plan(5.0, actions);
        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> planning_duration = end_time - start_time;
        
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

        // Check for finished tasks
        std::vector<State> new_states = action_model->result_states(agents, actions);
        for (int k = 0; k < team_size; k++) {
            if (!assigned_tasks[k].empty() && new_states[k].location == assigned_tasks[k].front().location) {
                Task task = assigned_tasks[k].front();
                assigned_tasks[k].pop_front();
                task.t_completed = timestep;
                finished_tasks[k].push_back(task);
                num_of_task_finish++;
                log_event_finished(k, task.task_id, timestep);
            }
        }

        history_of_actions.push_back(actions);
        history_of_planning_times.push_back(planning_duration.count());
        timestep++;

        nlohmann::json response = {
            {"status", "success"},
            {"actions", serialize_path(actions, agents)},
            {"num_agents", agents.size()},
            {"planning_time", planning_duration.count()}
        };
        return response.dump(4);

    } catch (const std::exception& e) {
        return nlohmann::json({{"error", "Planning Failed"}, {"message", e.what()}}).dump(4);
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
    
    for(size_t t = 0; t < history_of_actions.size(); ++t) {
        for(size_t i = 0; i < history_of_actions[t].size() && i < team_size; ++i) {
            if (t > 0) {
                actual_paths[i] += ",";
                planner_paths[i] += ",";
            }
            actual_paths[i] += action_to_string_local(history_of_actions[t][i]);
            planner_paths[i] += action_to_string_local(history_of_actions[t][i]);
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

bool MAPFServer::validate_planning_request(const nlohmann::json& request) {
    return request.contains("agents") && request.contains("goals") &&
           request["agents"].is_array() && request["goals"].is_array();
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