#pragma once
#include <vector>
#include <SFML/System.hpp>

class Engine;
class Task;
/** \brief Manages all Tasks in the system
*
*  It is more flexible version of traditional game loop.
*  It uses fixed timestep approach.
*  Any Task can have different frequency - so, for example, physics can be 100Hz, rendering 30Hz, and ai 2Hz.
*/
class TaskManager {
public:
	TaskManager(Engine& engine);
	~TaskManager();

	/** \brief creates new task and adds it to system
	*
	* \param args arguments to be passed to task constructor
	*
	* \returns pointer to created Task.
	*/
	template<typename TaskClass, typename ...Args>
	Task* addTask(Args&& ... args) {
		tasks.push_back(new TaskClass(engine, std::forward<Args>(args)...));
		return tasks.back();
	}

	/** deletes Task from the system
	*
	*   \param task pointer to task that will be deleted. nullptr allowed.
	*/
	void deleteTask(Task* task);

	/** \brief call to all Tasks that waits for it
	*
	*   \param elapsedTime time that has passed since last call of this method
	*
	*   \returns amount of time when it doesn't need to be called.
	*/
	sf::Time update(sf::Time elapsedTime);

private:
	std::vector<Task*> tasks;
	Engine& engine;
};
