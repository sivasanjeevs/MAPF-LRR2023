#include <cmath>
#include "CompetitionSystem.h"
#include <boost/tokenizer.hpp>
#include "nlohmann/json.hpp"
#include <fstream>
#include <functional>
#include <Logger.h>
#include "Utils.h" // Include the new utility file

using json = nlohmann::ordered_json;


std::list<Task> BaseSystem::move(vector<Action>& actions) {
    std::list<Task> finished_tasks_this_timestep;
    if (actions.size() != num_of_agents)
        return finished_tasks_this_timestep;

    curr_states = model->result_states(curr_states, actions);

    for (int k = 0; k < num_of_agents; k++) {
        if (!assigned_tasks[k].empty() && curr_states[k].location == assigned_tasks[k].front().goal_location) {
            Task finished_task = assigned_tasks[k].front();
            assigned_tasks[k].pop_front();
            finished_task.t_completed = timestep;
            finished_tasks[k].push_back(finished_task);
            finished_tasks_this_timestep.push_back(finished_task);
            log_event_finished(k, finished_task.task_id, timestep);
            num_of_task_finish++;
        }
    }
    return finished_tasks_this_timestep;
}


bool BaseSystem::valid_moves(vector<State>& prev, vector<Action>& action)
{
    return model->is_valid(prev, action);
}


void BaseSystem::sync_shared_env() {
    if (!started){
        env->goal_locations.resize(num_of_agents);
        for (size_t i = 0; i < num_of_agents; i++)
        {
            env->goal_locations[i].clear();
            for (auto& task: assigned_tasks[i])
            {
                env->goal_locations[i].push_back({task.goal_location, task.t_assigned });
            }
        }
        env->curr_states = curr_states;
    }
    env->curr_timestep = timestep;
}


vector<Action> BaseSystem::plan_wrapper()
{
    vector<Action> actions;
    planner->plan(plan_time_limit, actions);
    return actions;
}


vector<Action> BaseSystem::plan()
{
    using namespace std::placeholders;
    if (started && future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
    {
        if(logger)
        {
            logger->log_info("planner cannot run because the previous run is still running", timestep);
        }

        if (future.wait_for(std::chrono::seconds(plan_time_limit)) == std::future_status::ready)
        {
            task_td.join();
            started = false;
            return future.get();
        }
        logger->log_info("planner timeout", timestep);
        return {};
    }

    std::packaged_task<std::vector<Action>()> task(std::bind(&BaseSystem::plan_wrapper, this));
    future = task.get_future();
    if (task_td.joinable())
    {
        task_td.join();
    }
    task_td = std::thread(std::move(task));
    started = true;
    if (future.wait_for(std::chrono::seconds(plan_time_limit)) == std::future_status::ready)
    {
        task_td.join();
        started = false;
        return future.get();
    }
    logger->log_info("planner timeout", timestep);
    return {};
}


bool BaseSystem::planner_initialize()
{
    using namespace std::placeholders;
    std::packaged_task<void(int)> init_task(std::bind(&MAPFPlanner::initialize, planner, std::placeholders::_1));
    auto init_future = init_task.get_future();
    
    auto init_td = std::thread(std::move(init_task), preprocess_time_limit);
    if (init_future.wait_for(std::chrono::seconds(preprocess_time_limit)) == std::future_status::ready)
    {
        init_td.join();
        return true;
    }

    init_td.detach();
    return false;
}


void BaseSystem::log_preprocessing(bool succ)
{
    if (logger == nullptr)
        return;
    if (succ)
    {
        logger->log_info("Preprocessing success", timestep);
    } 
    else
    {
        logger->log_fatal("Preprocessing timeout", timestep);
    }
    logger->flush();
}


void BaseSystem::log_event_assigned(int agent_id, int task_id, int timestep)
{
    logger->log_info("Task " + std::to_string(task_id) + " is assigned to agent " + std::to_string(agent_id), timestep);
}


void BaseSystem::log_event_finished(int agent_id, int task_id, int timestep) 
{
    logger->log_info("Agent " + std::to_string(agent_id) + " finishes task " + std::to_string(task_id), timestep);
}


void BaseSystem::simulate(int simulation_time)
{
    initialize();
    int num_of_tasks = 0;

    for (; timestep < simulation_time; )
    {
        sync_shared_env();
        auto start = std::chrono::steady_clock::now();
        vector<Action> actions = plan();
        auto end = std::chrono::steady_clock::now();

        timestep += 1;
        for (int a = 0; a < num_of_agents; a++)
        {
            if (!env->goal_locations[a].empty())
                solution_costs[a]++;
        }

        list<Task> new_finished_tasks = move(actions);
        if (!planner_movements[0].empty() && planner_movements[0].back() == Action::NA)
        {
            planner_times.back()+=plan_time_limit;
        }
        else
        {
            auto diff = end-start;
            planner_times.push_back(std::chrono::duration<double>(diff).count());
        }

        for (auto task : new_finished_tasks)
        {
            finished_tasks[task.agent_assigned].emplace_back(task);
            num_of_tasks++;
            num_of_task_finish++;
        }

        update_tasks();

        bool complete_all = true;
        for (auto & t: assigned_tasks)
        {
            if(!t.empty()) 
            {
                complete_all = false;
                break;
            }
        }
        if (complete_all)
        {
            break;
        }
    }
}


void BaseSystem::initialize()
{
    paths.resize(num_of_agents);
    events.resize(num_of_agents);
    env->num_of_agents = num_of_agents;
    env->rows = map.rows;
    env->cols = map.cols;
    env->map = map.map;
    finished_tasks.resize(num_of_agents);
    timestep = 0;
    curr_states = starts;
    assigned_tasks.resize(num_of_agents);

    bool planner_initialize_success= planner_initialize();
    log_preprocessing(planner_initialize_success);
    if (!planner_initialize_success)
        _exit(124);

    update_tasks();
    sync_shared_env();

    actual_movements.resize(num_of_agents);
    planner_movements.resize(num_of_agents);
    solution_costs.resize(num_of_agents, 0);
}

void BaseSystem::savePaths(const string &fileName, int option) const
{
    std::ofstream output(fileName, std::ios::out);
    for (int i = 0; i < num_of_agents; i++)
    {
        output << "Agent " << i << ": ";
        const auto& moves = (option == 0) ? actual_movements[i] : planner_movements[i];
        bool first = true;
        for (const auto t : moves)
        {
            if (!first) {
                output << ",";
            }
            first = false;
            output << t;
        }
        output << std::endl;
    }
}


void BaseSystem::saveResults(const string &fileName, int screen) const
{
    json js;
    js["actionModel"] = "MAPF_T";
    js["AllValid"] = fast_mover_feasible ? "Yes" : "No";
    js["teamSize"] = num_of_agents;

    if (screen <= 2)
    {
        json start = json::array();
        for (int i = 0; i < num_of_agents; i++)
        {
            json s;
            s.push_back(starts[i].location/map.cols);
            s.push_back(starts[i].location%map.cols);
            s.push_back(orientation_to_string_local(starts[i].orientation));
            start.push_back(s);
        }
        js["start"] = start;
    }

    js["numTaskFinished"] = num_of_task_finish;
    int sum_of_cost = std::accumulate(solution_costs.begin(), solution_costs.end(), 0);
    int makespan = num_of_agents > 0 ? *std::max_element(solution_costs.begin(), solution_costs.end()) : 0;
    js["sumOfCost"] = sum_of_cost;
    js["makespan"] = makespan;
    
    if (screen <= 2)
    {
        json apaths = json::array();
        for (int i = 0; i < num_of_agents; i++)
        {
            std::string path;
            bool first = true;
            for (const auto action : actual_movements[i])
            {
                if (!first) path+= ",";
                first = false;
                path += action_to_string_local(action);
            }
            apaths.push_back(path);
        }
        js["actualPaths"] = apaths;
    }

    if (screen <=1)
    {
        json ppaths = json::array();
        for (int i = 0; i < num_of_agents; i++)
        {
            std::string path;
            bool first = true;
            for (const auto action : planner_movements[i])
            {
                if (!first) path+= ",";
                first = false;
                path += action_to_string_local(action);
            }  
            ppaths.push_back(path);
        }
        js["plannerPaths"] = ppaths;

        js["plannerTimes"] = planner_times;

        json errors = json::array();
        for (auto error: model->errors)
        {
            errors.push_back({std::get<1>(error), std::get<2>(error), std::get<3>(error), std::get<0>(error)});
        }
        js["errors"] = errors;

        json events_json = json::array();
        for (int i = 0; i < num_of_agents; i++)
        {
            json event = json::array();
            for(auto e: events[i])
            {
                event.push_back({std::get<0>(e), std::get<1>(e), std::get<2>(e)});
            }
            events_json.push_back(event);
        }
        js["events"] = events_json;

        json tasks_json = json::array();
        for (auto t: all_tasks)
        {
            json task_item;
            task_item["task_id"] = t.task_id;
            task_item["start_location"] = {t.start_location / map.cols, t.start_location % map.cols};
            task_item["goal_location"] = {t.goal_location / map.cols, t.goal_location % map.cols};
            tasks_json.push_back(task_item);
        }
        js["tasks"] = tasks_json;
    }

    std::ofstream f(fileName,std::ios_base::trunc |std::ios_base::out);
    f << std::setw(4) << js;
}

bool FixedAssignSystem::load_agent_tasks(string fname)
{
    string line;
    std::ifstream myfile(fname.c_str());
    if (!myfile.is_open()) return false;

    getline(myfile, line);
    while (!myfile.eof() && line[0] == '#') {
        getline(myfile, line);
    }

    boost::char_separator<char> sep(",");
    boost::tokenizer<boost::char_separator<char>> tok(line, sep);
    boost::tokenizer<boost::char_separator<char>>::iterator beg = tok.begin();

    num_of_agents = atoi((*beg).c_str());
    int task_id_counter = 0;

    if (num_of_agents == 0) {
        std::cerr << "The number of agents should be larger than 0" << endl;
        exit(-1);
    }
    starts.resize(num_of_agents);
    task_queue.resize(num_of_agents);
  
    for (int i = 0; i < num_of_agents; i++)
    {
        getline(myfile, line);
        while (!myfile.eof() && line[0] == '#')
            getline(myfile, line);

        boost::tokenizer<boost::char_separator<char>> tok_line(line, sep);
        auto it = tok_line.begin();
        int num_landmarks = atoi((*it).c_str());
        it++;
        auto loc = atoi((*it).c_str());
        starts[i] = State(loc, 0, 0);
        it++;
        for (int j = 0; j < num_landmarks; j++, it++)
        {
            auto task_loc = atoi((*it).c_str());
            task_queue[i].emplace_back(task_id_counter++, task_loc, task_loc, 0, i);
        }
    }
    myfile.close();

    return true;
}


void FixedAssignSystem::update_tasks()
{
    for (int k = 0; k < num_of_agents; k++)
    {
        while (assigned_tasks[k].size() < num_tasks_reveal && !task_queue[k].empty())
        {
            Task task = task_queue[k].front();
            task_queue[k].pop_front();
            assigned_tasks[k].push_back(task);
            events[k].push_back(make_tuple(task.task_id,timestep,"assigned"));
            if (std::find_if(all_tasks.begin(), all_tasks.end(), [&](const Task& t){ return t.task_id == task.task_id; }) == all_tasks.end()) {
                all_tasks.push_back(task);
            }
        }
    }
}


void TaskAssignSystem::update_tasks()
{
    for (int k = 0; k < num_of_agents; k++)
    {
        while (assigned_tasks[k].size() < num_tasks_reveal && !task_queue.empty())
        {
            Task task = task_queue.front();
            task.t_assigned = timestep;
            task.agent_assigned = k;
            task_queue.pop_front();
            assigned_tasks[k].push_back(task);
            events[k].push_back(make_tuple(task.task_id,timestep,"assigned"));
            if (std::find_if(all_tasks.begin(), all_tasks.end(), [&](const Task& t){ return t.task_id == task.task_id; }) == all_tasks.end()) {
                all_tasks.push_back(task);
            }
        }
    }
}


void InfAssignSystem::update_tasks(){
    for (int k = 0; k < num_of_agents; k++)
    {
        while (assigned_tasks[k].size() < num_tasks_reveal) 
        {
            int i = task_counter[k] * num_of_agents + k;
            int loc = tasks[i%tasks_size];
            Task task(task_id, loc, loc, timestep, k);
            assigned_tasks[k].push_back(task);
            events[k].push_back(make_tuple(task.task_id,timestep,"assigned"));
            all_tasks.push_back(task);
            task_id++;
            task_counter[k]++;
        }
    }
}