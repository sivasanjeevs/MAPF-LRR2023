#pragma once

struct Task {
    int task_id;
    int start_location;
    int goal_location;
    int t_assigned;
    int t_completed;
    int agent_assigned;

    Task(int id, int start, int goal, int assigned, int agent) : 
        task_id(id), start_location(start), goal_location(goal), 
        t_assigned(assigned), t_completed(-1), agent_assigned(agent) {}
};