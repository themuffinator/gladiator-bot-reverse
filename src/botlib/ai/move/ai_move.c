#include "bot_move.h"

#include "../../common/l_log.h"

void AI_MoveFrame(bot_moveresult_t *result,
                  int movestate,
                  const bot_goal_t *goal,
                  int travelflags)
{
    if (result == NULL)
    {
        return;
    }

    BotClearMoveResult(result);

    bot_movestate_t *ms = BotMoveStateFromHandle(movestate);
    if (ms == NULL)
    {
        result->failure = 1;
        BotLib_Print(PRT_WARNING,
                      "AI_MoveFrame: invalid movestate handle %d\n",
                      movestate);
        return;
    }

    BotMoveClassifyEnvironment(ms);

    if (goal == NULL)
    {
        result->failure = 1;
        result->type = RESULTTYPE_INSOLIDAREA;
        BotLib_Print(PRT_WARNING, "AI_MoveFrame: no goal for movestate %d\n", movestate);
        return;
    }

    BotMoveToGoal(result, movestate, goal, travelflags);
}

