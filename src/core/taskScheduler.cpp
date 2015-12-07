#include "taskScheduler.h"
#include "utils/emath.h"
#include "utils/timer.h"
#include "task.h"

TaskScheduler::TaskScheduler(Engine& engine) : engine(engine) {
}

std::chrono::milliseconds TaskScheduler::update(std::chrono::milliseconds elapsedTime) {
    std::chrono::milliseconds nextTaskUpdate{std::chrono::milliseconds::max()};
    Timer timeAlreadyElapsed;

    for (auto& task : tasks) {
        if (task == nullptr) {
            continue;
        }

        task->accumulatedTime = clamp(task->accumulatedTime + elapsedTime, std::chrono::milliseconds(0),
                                      std::chrono::milliseconds(1000));

        while (task->accumulatedTime >= task->frequency) {
            task->update();
            task->accumulatedTime -= task->frequency;
        }

        if (nextTaskUpdate - timeAlreadyElapsed.elapsed() > task->frequency - task->accumulatedTime) {
            nextTaskUpdate = task->frequency - task->accumulatedTime;
            timeAlreadyElapsed.reset();
        }
    }

    return nextTaskUpdate - timeAlreadyElapsed.elapsed();
}
