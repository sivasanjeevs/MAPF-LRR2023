#pragma once
#include "SharedEnv.h"
#include "Grid.h"
#include "Tasks.h"
#include "ActionModel.h"
#include "MAPFPlanner.h"
#include "Logger.h"
#include <pthread.h>
#include <future>
#include <queue>
#include <mutex>

class BaseSystem
{
public:
    int num_tasks_reveal = 1;
    Logger* logger = nullptr;

    struct NewTask {
        int agent_id;
        int start_loc;
        int goal_loc;
    };

    BaseSystem(Grid &grid, MAPFPlanner* planner, ActionModelWithRotate* model):
        map(grid), planner(planner), env(planner->env), model(model)
    {}

    virtual ~BaseSystem()
    {
        if (started)
        {
            task_td.join();
        }
        if (planner != nullptr)
        {
            delete planner;
        }
    };

    void set_num_tasks_reveal(int num){num_tasks_reveal = num;};
    void set_plan_time_limit(int limit){plan_time_limit = limit;};
    void set_preprocess_time_limit(int limit){preprocess_time_limit = limit;};
    void set_logger(Logger* logger){this->logger = logger;}

    void simulate(int simulation_time);
    vector<Action> plan();
    vector<Action> plan_wrapper();

    void savePaths(const string &fileName, int option) const;
    void saveResults(const string &fileName, int screen) const;

    std::queue<NewTask> new_tasks_queue;
    std::mutex new_tasks_mutex;

protected:
    Grid map;
    std::future<std::vector<Action>> future;
    std::thread task_td;
    bool started = false;
    MAPFPlanner* planner;
    SharedEnvironment* env;
    ActionModelWithRotate* model;
    int timestep;
    int preprocess_time_limit=10;
    int plan_time_limit = 3;
    std::vector<Path> paths;
    std::vector<std::list<Task>> finished_tasks;
    vector<State> starts;
    int num_of_agents;
    vector<State> curr_states;
    vector<list<Action>> actual_movements;
    vector<list<Action>> planner_movements;
    vector< deque<Task>> assigned_tasks;
    vector<list<std::tuple<int,int,std::string>>> events;
    list<Task> all_tasks;
    vector<int> solution_costs;
    int num_of_task_finish = 0;
    list<double> planner_times;
    bool fast_mover_feasible = true;
    int task_id = 0;

    void initialize();
    bool planner_initialize();
    virtual void update_tasks() = 0;

    void sync_shared_env();
    list<Task> move(vector<Action>& actions);
    bool valid_moves(vector<State>& prev, vector<Action>& next);

    void log_preprocessing(bool succ);
    void log_event_assigned(int agent_id, int task_id, int timestep);
    void log_event_finished(int agent_id, int task_id, int timestep);
};

class FixedAssignSystem : public BaseSystem
{
public:
    FixedAssignSystem(Grid &grid, string agent_task_filename, MAPFPlanner* planner, ActionModelWithRotate *model):
        BaseSystem(grid, planner, model)
    {
        load_agent_tasks(agent_task_filename);
    };

    FixedAssignSystem(Grid &grid, MAPFPlanner* planner, std::vector<int>& start_locs, std::vector<vector<int>>& tasks, ActionModelWithRotate* model):
        BaseSystem(grid, planner, model)
    {
        if (start_locs.size() != tasks.size())
        {
            std::cerr << "agent num does not match the task assignment" << std::endl;
            exit(1);
        }

        num_of_agents = static_cast<int>(start_locs.size());
        starts.resize(num_of_agents);
        task_queue.resize(num_of_agents);
        for (size_t i = 0; i < start_locs.size(); i++)
        {
            starts[i] = State(start_locs[i], 0, 0);
            for (auto& task_location: tasks[i])
            {
                // FIX: Use new Task constructor, assuming start and goal are the same
                all_tasks.emplace_back(task_id++, task_location, task_location, 0, static_cast<int>(i));
                task_queue[i].push_back(all_tasks.back());
            }
        }
    };

    ~FixedAssignSystem(){};

    bool load_agent_tasks(string fname);

private:
    vector<deque<Task>> task_queue;
    void update_tasks();
};

class TaskAssignSystem : public BaseSystem
{
public:
    TaskAssignSystem(Grid &grid, MAPFPlanner* planner, std::vector<int>& start_locs, std::vector<int>& tasks, ActionModelWithRotate* model):
        BaseSystem(grid, planner, model)
    {
        for (auto& task_location: tasks)
        {
            // FIX: Use new Task constructor, assuming start and goal are the same
            all_tasks.emplace_back(task_id++, task_location, task_location, 0, -1);
            task_queue.push_back(all_tasks.back());
        }
        num_of_agents = static_cast<int>(start_locs.size());
        starts.resize(num_of_agents);
        for (size_t i = 0; i < start_locs.size(); i++)
        {
            starts[i] = State(start_locs[i], 0, 0);
        }
    };

    ~TaskAssignSystem(){};

private:
    deque<Task> task_queue;
    void update_tasks();
};

class InfAssignSystem : public BaseSystem
{
public:
    InfAssignSystem(Grid &grid, MAPFPlanner* planner, std::vector<int>& start_locs, std::vector<int>& tasks, ActionModelWithRotate* model):
        tasks(tasks), BaseSystem(grid, planner, model)
    {
        num_of_agents = static_cast<int>(start_locs.size());
        starts.resize(num_of_agents);
        task_counter.resize(num_of_agents,0);
        tasks_size = static_cast<int>(tasks.size());

        for (size_t i = 0; i < start_locs.size(); i++)
        {
            if (grid.map[start_locs[i]] == 1)
            {
                cout<<"error: agent "<<i<<"'s start location is an obstacle("<<start_locs[i]<<")"<<endl;
                exit(0);
            }
            starts[i] = State(start_locs[i], 0, 0);
        }
    };

    ~InfAssignSystem(){};

private:
    std::vector<int>& tasks;
    std::vector<int> task_counter;
    int tasks_size;
    void update_tasks();
};