#pragma once

#include "Grid.h"
#include "ActionModel.h"
#include "MAPFPlanner.h"
#include "Tasks.h"
#include <iostream>
#include <string>
#include <vector>
#include <deque>
#include <list>
#include <tuple>
#include <chrono>
#include <fstream>
#include <csignal>
#include <csetjmp>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <nlohmann/json.hpp>
#include <memory>

class MAPFServer {
public:
    // Constructor and Destructor
    MAPFServer(const std::string& map_file, const std::string& config_file, int port);
    ~MAPFServer();

    // Public methods to control the server
    bool initialize();
    void run();
    void stop();

private:
    // --- Private Methods ---

    void start_http_server();
    void handle_http_request(const std::string& method, const std::string& path,
                             const std::string& body, std::string& response);

    // Endpoint handlers
    std::string handle_plan_request(const std::string& request_body);
    std::string handle_report_request();
    std::string handle_reset_request();
    std::string handle_add_task_request(const std::string& request_body);
    // FIX: Re-add missing function declarations
    std::string handle_health_request();
    std::string handle_task_status_request();


    // Helper methods
    bool validate_planning_request(const nlohmann::json& request);
    std::vector<State> parse_agent_states(const nlohmann::json& agents);
    nlohmann::json serialize_path(const std::vector<Action>& actions, const std::vector<State>& states);

    // Task and event tracking
    void update_tasks_lifelong(const std::vector<State>& current_states);
    void log_event_assigned(int agent_id, int task_id, int timestep);
    void log_event_finished(int agent_id, int task_id, int timestep);
    
    // Problem loading and saving
    bool load_problem_configuration(const std::string& problem_file);
    void initialize_task_system();
    bool add_task_to_file(int start_location, int goal_location);
    void save_results_to_file();

    // --- Member Variables ---

    std::string map_file;
    std::string config_file;
    int port;
    bool running = false;

    std::unique_ptr<boost::asio::io_context> io_context;
    std::unique_ptr<Grid> grid;
    std::unique_ptr<ActionModelWithRotate> action_model;
    std::unique_ptr<SharedEnvironment> env;
    std::unique_ptr<MAPFPlanner> planner;

    bool session_active = false;
    int team_size = 0;
    int timestep = 0;
    std::vector<State> initial_states;
    std::vector<State> current_agent_states;
    std::vector<std::vector<Action>> history_of_actions;
    std::vector<double> history_of_planning_times;
    
    std::vector<std::list<Task>> finished_tasks;
    std::vector<std::deque<Task>> assigned_tasks;
    std::vector<bool> is_carrying_task;
    std::vector<std::list<std::tuple<int, int, std::string>>> events;
    std::list<Task> all_tasks;
    std::vector<int> solution_costs;
    int num_of_task_finish = 0;
    int task_id = 0;
    bool fast_mover_feasible = true;
    
    std::vector<std::list<Action>> actual_movements;
    std::vector<std::list<Action>> planner_movements;
    
    std::deque<Task> task_queue;
    std::vector<int> agent_start_locations;
    std::vector<std::pair<int, int>> task_locations;
    std::string task_file_path;
    std::string task_assignment_strategy = "greedy";
};