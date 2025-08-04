#pragma once

#include "Grid.h"
#include "ActionModel.h"
#include "MAPFPlanner.h"
#include "Tasks.h"
#include <boost/asio.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <memory>
#include <vector>
#include <chrono>
#include <list>
#include <deque>
#include <tuple>

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

    // Core server loop and request router
    void start_http_server();
    void handle_http_request(const std::string& method, const std::string& path,
                             const std::string& body, std::string& response);

    // Endpoint handlers
    std::string handle_plan_request(const nlohmann::json& request);
    std::string handle_status_request();
    std::string handle_health_request();
    std::string handle_report_request(); // Handler for generating the JSON report
    std::string handle_reset_request();  // Handler to reset the simulation session

    // Helper methods for parsing and serialization
    bool validate_planning_request(const nlohmann::json& request);
    std::vector<State> parse_agent_states(const nlohmann::json& agents);
    std::vector<std::pair<int, int>> parse_goals(const nlohmann::json& goals);
    nlohmann::json serialize_path(const std::vector<Action>& actions, const std::vector<State>& states);

    // Task and event tracking methods
    void update_tasks(const std::vector<State>& current_states, const std::vector<std::pair<int, int>>& goals);
    void log_event_assigned(int agent_id, int task_id, int timestep);
    void log_event_finished(int agent_id, int task_id, int timestep);

    // --- Member Variables ---

    // Configuration
    std::string map_file;
    std::string config_file;
    int port;
    bool running;

    // Core MAPF components
    std::unique_ptr<boost::asio::io_context> io_context;
    std::unique_ptr<Grid> grid;
    std::unique_ptr<ActionModelWithRotate> action_model;
    std::unique_ptr<SharedEnvironment> env;
    std::unique_ptr<MAPFPlanner> planner;

    // Session logging for report generation
    bool session_active = false;
    int team_size = 0;
    int timestep = 0;
    std::vector<State> initial_states;
    std::vector<std::vector<Action>> history_of_actions;
    std::vector<double> history_of_planning_times;
    
    // Task and event tracking (similar to BaseSystem)
    std::vector<std::list<Task>> finished_tasks;
    std::vector<std::deque<Task>> assigned_tasks;
    std::vector<std::list<std::tuple<int, int, std::string>>> events;
    std::list<Task> all_tasks;
    std::vector<int> solution_costs;
    int num_of_task_finish = 0;
    int task_id = 0;
    bool fast_mover_feasible = true;
};