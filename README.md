# DeferredTasksExecutor

## Background
Modern processors have multiple cores. It is not efficient way to run heavy calculations on a single thread – most of the cores will be idle and underutilized. Running calculations on several threads is better, but not elegant from architecture point of view – there will be too much threads if every calculation module will run its own set of threads.
Instead, we are using deferred tasks approach. There is a single component (DeferredTasksExecutor) that owns a thread pool. Calculation algorithm is split into set of tasks which are submitted to DeferredTasksExecutor. Task is processed on first available idle thread within executor’s thread pool.

## Exercise
Develop DeferredTasksExecutor class that
* Owns a thread pool
* Accepts DeferredTasks
* Manages FIFO tasks queue
* Processes task on first available idle thread

Client code should use a simple interface for submitting and waiting for deferred tasks.
Example tasks may be implemented as usleep(random(1000000)) simulating real heavy calculations

## Advanced requirements
* Code is covered with unit tests (gtest, cppunit, qt test or similar)
* Number of threads in the pool is reasonable for current machine
* Clients have an interface to get the task progress (in queue, processing, done).
* Task has a start precondition - task does not start until precondition is met. This allows sequential task processing - one task waits for other task completion
* There is a way to cancel a task
* Each task has execution priority

## Coding guidelines
* Using of STL, boost or Qt frameworks is a plus
* However, do not use boost::future, QtConcurrent, QThreadPool or QRunnable – we are interested in your own DeferredTasksExecutor implementation. You may use these frameworks as a reference, though.

## Implementation notes
* STL(including c++11 features) and google test(as git submodule) libs used
* std::condition_variable used to block\resume threads
* std::deque<std::pair<int /\* priority \*/, DeferredTask\*>> used as FIFO queue container in DeferredTasksExecutor class to be able to find and cancel task(otherwise std::priority_queue could be used). This container kept asc ordered by priority - we insert new pair finding correct place with std::lower_bound() with logarithmic complexity for random-access containers. std::deque used because it's fast for insert at the beginning and pop from the end(common case when all tasks have same priority) and it's random-access container(this is initial decision and performance should be compared for all use-case scenarios). std::list and std::vector could be used instead however performance was bad for DeferredTasksExecutorTest_WithSingleBlockedThread.ExecutesAllTasksWithSamePriorityInSubmitOrder test(submit 5000 tasks).
* Use default constructor of DeferredTasksExecutor class to get count of worker threads equal to CPU cores, use overloaded constructor to set custom worker threads count
* Clients can get task state using DeferredTask::getState(), to check if task in queue use DeferredTasksExecutor::inQueue()
* Task start precondition(for sequential task processing) feature don't require any extra functionality - client code can just submit task for executor in correct time with correct priority resulting in sequental task processing, see ProofOfConcept.RunTaskSequentally test for an example
* Task can be cancelled from executor if it's not executing yet with DeferredTasksExecutor::cancel
* Taks priority is just an int value so client app can design priorities by itself(probably with enums)
* Implementation of DeferredTask or DeferredTasksExecutor don't use std::future, however tests code does(just to minimize amount of code - it's easiest way to block and release thread in STL)
* "test" project contains tests, "example" project contains demo console app shows realtime dummy tasks processing

## Build and run
Make sure you initialized submodules:
```
git submodule update --init
```

### Windows
Build using Visual Studio(there is project file for 2015 in msvc folder)
