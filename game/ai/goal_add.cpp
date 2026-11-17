// SPDX-License-Identifier: GPL-2.0-or-later
// AI goals (goals.h): the script bindings that add goals, AddTopGoal, AddSubGoal and
// AddSubGoal_Front.
//
// The AI scripts plan by adding goals: `ai:AddTopGoal(id, life, ...)` on the character's think
// adds one under its root goal, `goal:AddSubGoal(id, life, ...)` inside a goal's script adds
// one under that goal. AddSubGoal_Front puts the new goal at the front of the sub-goal queue, so
// it runs before the ones already queued. The script engine binds one function per parameter
// count (AddTopGoal takes up to 7 parameters, AddSubGoal up to 13, AddSubGoal_Front exactly 3);
// each passes the owner goal, the id, the life and its parameters to the game's goal-adding
// code with the remaining parameters zero, and returns what that returns: the new goal's script
// object, or null when the id is negative or the think's goal pool is full.
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "ai_think.h"
#include "goals.h"

namespace ai {
namespace {

// The goal scripts' context (GoalFunc::context) is the goal itself: the timers and numbers
// ai_think.h names there are the goal's own.
Goal *goal_of(GoalContext *context)
{
    return reinterpret_cast<Goal *>(context);
}

// The think's goal fields (goals.h, ThinkGoals).
ThinkGoals *goals_of(AiThink *think)
{
    return reinterpret_cast<ThinkGoals *>(think);
}

// A goal's parameters, all of them; the bindings leave the ones they do not take zero.
using GoalParameters = std::array<float, goal_parameter_count>;

// Calls a goal-adding function with every parameter, in order.
template <typename AddFunction, std::size_t... Index>
GoalScriptObject *call_with_parameters(AddFunction add, Goal *owner, int32_t goal_id, float life,
                                       const GoalParameters &parameters, std::index_sequence<Index...>)
{
    return add(owner, goal_id, life, parameters[Index]...);
}

// Adds a goal under `owner` with the given parameters and zero for the rest, at the back of
// its sub-goal queue (`front`: at the front).
template <typename... Params>
GoalScriptObject *add_goal(bool front, Goal *owner, int32_t goal_id, float life, Params... params)
{
    static_assert(sizeof...(Params) <= goal_parameter_count);
    const GoalParameters parameters = {params...};
    const auto every_parameter = std::make_index_sequence<goal_parameter_count>{};
    if (front)
        return call_with_parameters(goal_add_front, owner, goal_id, life, parameters, every_parameter);
    return call_with_parameters(goal_add, owner, goal_id, life, parameters, every_parameter);
}

template <typename... Params>
GoalScriptObject *add_top_goal(AiFunc *func, int32_t goal_id, float life, Params... params)
{
    return add_goal(false, goals_of(func->think)->root_goal, goal_id, life, params...);
}

template <typename... Params>
GoalScriptObject *add_sub_goal(GoalFunc *func, int32_t goal_id, float life, Params... params)
{
    return add_goal(false, goal_of(func->context), goal_id, life, params...);
}

}  // namespace
}  // namespace ai

using namespace ai;

// ai:AddTopGoal(id, life [, p1 .. p7])
extern "C" GoalScriptObject *bb_ai_func_add_top_goal_0(AiFunc *func, int32_t goal_id, float life)
{
    return add_top_goal(func, goal_id, life);
}

extern "C" GoalScriptObject *bb_ai_func_add_top_goal_1(AiFunc *func, int32_t goal_id, float life, float p1)
{
    return add_top_goal(func, goal_id, life, p1);
}

extern "C" GoalScriptObject *bb_ai_func_add_top_goal_2(AiFunc *func, int32_t goal_id, float life, float p1, float p2)
{
    return add_top_goal(func, goal_id, life, p1, p2);
}

extern "C" GoalScriptObject *bb_ai_func_add_top_goal_3(AiFunc *func, int32_t goal_id, float life, float p1, float p2,
                                                       float p3)
{
    return add_top_goal(func, goal_id, life, p1, p2, p3);
}

extern "C" GoalScriptObject *bb_ai_func_add_top_goal_4(AiFunc *func, int32_t goal_id, float life, float p1, float p2,
                                                       float p3, float p4)
{
    return add_top_goal(func, goal_id, life, p1, p2, p3, p4);
}

extern "C" GoalScriptObject *bb_ai_func_add_top_goal_5(AiFunc *func, int32_t goal_id, float life, float p1, float p2,
                                                       float p3, float p4, float p5)
{
    return add_top_goal(func, goal_id, life, p1, p2, p3, p4, p5);
}

extern "C" GoalScriptObject *bb_ai_func_add_top_goal_6(AiFunc *func, int32_t goal_id, float life, float p1, float p2,
                                                       float p3, float p4, float p5, float p6)
{
    return add_top_goal(func, goal_id, life, p1, p2, p3, p4, p5, p6);
}

extern "C" GoalScriptObject *bb_ai_func_add_top_goal_7(AiFunc *func, int32_t goal_id, float life, float p1, float p2,
                                                       float p3, float p4, float p5, float p6, float p7)
{
    return add_top_goal(func, goal_id, life, p1, p2, p3, p4, p5, p6, p7);
}

// goal:AddSubGoal(id, life [, p1 .. p13])
extern "C" GoalScriptObject *bb_ai_goal_func_add_sub_goal_0(GoalFunc *func, int32_t goal_id, float life)
{
    return add_sub_goal(func, goal_id, life);
}

extern "C" GoalScriptObject *bb_ai_goal_func_add_sub_goal_1(GoalFunc *func, int32_t goal_id, float life, float p1)
{
    return add_sub_goal(func, goal_id, life, p1);
}

extern "C" GoalScriptObject *bb_ai_goal_func_add_sub_goal_2(GoalFunc *func, int32_t goal_id, float life, float p1,
                                                            float p2)
{
    return add_sub_goal(func, goal_id, life, p1, p2);
}

extern "C" GoalScriptObject *bb_ai_goal_func_add_sub_goal_3(GoalFunc *func, int32_t goal_id, float life, float p1,
                                                            float p2, float p3)
{
    return add_sub_goal(func, goal_id, life, p1, p2, p3);
}

extern "C" GoalScriptObject *bb_ai_goal_func_add_sub_goal_4(GoalFunc *func, int32_t goal_id, float life, float p1,
                                                            float p2, float p3, float p4)
{
    return add_sub_goal(func, goal_id, life, p1, p2, p3, p4);
}

extern "C" GoalScriptObject *bb_ai_goal_func_add_sub_goal_5(GoalFunc *func, int32_t goal_id, float life, float p1,
                                                            float p2, float p3, float p4, float p5)
{
    return add_sub_goal(func, goal_id, life, p1, p2, p3, p4, p5);
}

extern "C" GoalScriptObject *bb_ai_goal_func_add_sub_goal_6(GoalFunc *func, int32_t goal_id, float life, float p1,
                                                            float p2, float p3, float p4, float p5, float p6)
{
    return add_sub_goal(func, goal_id, life, p1, p2, p3, p4, p5, p6);
}

extern "C" GoalScriptObject *bb_ai_goal_func_add_sub_goal_7(GoalFunc *func, int32_t goal_id, float life, float p1,
                                                            float p2, float p3, float p4, float p5, float p6,
                                                            float p7)
{
    return add_sub_goal(func, goal_id, life, p1, p2, p3, p4, p5, p6, p7);
}

extern "C" GoalScriptObject *bb_ai_goal_func_add_sub_goal_8(GoalFunc *func, int32_t goal_id, float life, float p1,
                                                            float p2, float p3, float p4, float p5, float p6,
                                                            float p7, float p8)
{
    return add_sub_goal(func, goal_id, life, p1, p2, p3, p4, p5, p6, p7, p8);
}

extern "C" GoalScriptObject *bb_ai_goal_func_add_sub_goal_9(GoalFunc *func, int32_t goal_id, float life, float p1,
                                                            float p2, float p3, float p4, float p5, float p6,
                                                            float p7, float p8, float p9)
{
    return add_sub_goal(func, goal_id, life, p1, p2, p3, p4, p5, p6, p7, p8, p9);
}

extern "C" GoalScriptObject *bb_ai_goal_func_add_sub_goal_10(GoalFunc *func, int32_t goal_id, float life, float p1,
                                                             float p2, float p3, float p4, float p5, float p6,
                                                             float p7, float p8, float p9, float p10)
{
    return add_sub_goal(func, goal_id, life, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10);
}

extern "C" GoalScriptObject *bb_ai_goal_func_add_sub_goal_11(GoalFunc *func, int32_t goal_id, float life, float p1,
                                                             float p2, float p3, float p4, float p5, float p6,
                                                             float p7, float p8, float p9, float p10, float p11)
{
    return add_sub_goal(func, goal_id, life, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11);
}

extern "C" GoalScriptObject *bb_ai_goal_func_add_sub_goal_12(GoalFunc *func, int32_t goal_id, float life, float p1,
                                                             float p2, float p3, float p4, float p5, float p6,
                                                             float p7, float p8, float p9, float p10, float p11,
                                                             float p12)
{
    return add_sub_goal(func, goal_id, life, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12);
}

extern "C" GoalScriptObject *bb_ai_goal_func_add_sub_goal_13(GoalFunc *func, int32_t goal_id, float life, float p1,
                                                             float p2, float p3, float p4, float p5, float p6,
                                                             float p7, float p8, float p9, float p10, float p11,
                                                             float p12, float p13)
{
    return add_sub_goal(func, goal_id, life, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13);
}

// goal:AddSubGoal_Front(id, life, p1, p2, p3)
extern "C" GoalScriptObject *bb_ai_goal_func_add_sub_goal_front(GoalFunc *func, int32_t goal_id, float life, float p1,
                                                                float p2, float p3)
{
    return add_goal(true, goal_of(func->context), goal_id, life, p1, p2, p3);
}
