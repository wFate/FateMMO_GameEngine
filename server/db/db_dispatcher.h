#pragma once
#include <functional>
#include <memory>
#include <mutex>
#include <vector>
#include "server/db/db_pool.h"
#include "engine/job/job_system.h"

namespace fate {

// ============================================================================
// DbDispatcher — async DB operations via the fiber job system
//
// Dispatches DB queries to worker threads using the existing fiber job
// system and connection pool. Results are queued for the game thread
// to consume next tick. The game loop never blocks on DB I/O.
//
// Usage:
//   // From game loop (main thread):
//   dispatcher.dispatch<CharacterRecord>(
//       [charId](pqxx::connection& conn) -> CharacterRecord {
//           // Runs on worker fiber — safe to do slow DB queries here
//           pqxx::work txn(conn);
//           auto r = txn.exec_params("SELECT ...", charId);
//           txn.commit();
//           return rowToRecord(r[0]);
//       },
//       [this, clientId](CharacterRecord rec) {
//           // Runs on game thread next tick — safe to touch ECS/components
//           applyCharacterData(clientId, rec);
//       }
//   );
//
//   // In tick():
//   dispatcher.drainCompletions();
// ============================================================================

class DbDispatcher {
public:
    void init(DbPool* pool) { pool_ = pool; }

    /// Dispatch a DB operation to a worker fiber.
    /// workFn: runs on a worker thread with a pooled connection.
    /// completionFn: runs on the game thread via drainCompletions().
    template<typename Result>
    void dispatch(std::function<Result(pqxx::connection&)> workFn,
                  std::function<void(Result)> completionFn)
    {
        // Heap-allocate the task so it survives until the fiber picks it up
        auto* task = new TypedTask<Result>{
            std::move(workFn),
            std::move(completionFn),
            this
        };

        Job job;
        job.function = &TypedTask<Result>::execute;
        job.param = task;

        JobSystem::instance().submit(&job, 1);
    }

    /// Dispatch a fire-and-forget DB operation (no result needed).
    void dispatchVoid(std::function<void(pqxx::connection&)> workFn)
    {
        auto* task = new VoidTask{
            std::move(workFn),
            this
        };

        Job job;
        job.function = &VoidTask::execute;
        job.param = task;

        JobSystem::instance().submit(&job, 1);
    }

    /// Dispatch a fire-and-forget with a completion callback.
    void dispatchVoid(std::function<void(pqxx::connection&)> workFn,
                      std::function<void()> completionFn)
    {
        auto* task = new VoidCallbackTask{
            std::move(workFn),
            std::move(completionFn),
            this
        };

        Job job;
        job.function = &VoidCallbackTask::execute;
        job.param = task;

        JobSystem::instance().submit(&job, 1);
    }

    /// Call once per game tick on the main thread.
    /// Executes all completion callbacks queued by worker fibers.
    void drainCompletions() {
        std::vector<std::function<void()>> batch;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            batch.swap(completions_);
        }
        for (auto& fn : batch) {
            fn();
        }
    }

    /// Number of pending completions waiting to be drained.
    int pendingCompletions() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(completions_.size());
    }

private:
    void enqueueCompletion(std::function<void()> fn) {
        std::lock_guard<std::mutex> lock(mutex_);
        completions_.push_back(std::move(fn));
    }

    DbPool* pool_ = nullptr;
    mutable std::mutex mutex_;
    std::vector<std::function<void()>> completions_;

    // ---- Internal task types (allocated on heap, deleted after execution) ----

    template<typename Result>
    struct TypedTask {
        std::function<Result(pqxx::connection&)> work;
        std::function<void(Result)> completion;
        DbDispatcher* dispatcher;

        static void __stdcall execute(void* param) {
            auto* self = static_cast<TypedTask*>(param);
            try {
                auto guard = self->dispatcher->pool_->acquire_guard();
                Result result = self->work(guard.connection());

                if (self->completion) {
                    auto cb = std::move(self->completion);
                    auto r = std::move(result);
                    self->dispatcher->enqueueCompletion(
                        [cb = std::move(cb), r = std::move(r)]() mutable { cb(std::move(r)); });
                }
            } catch (const std::exception& e) {
                LOG_ERROR("DbDispatch", "DB job failed: %s", e.what());
            }
            delete self;
        }
    };

    struct VoidTask {
        std::function<void(pqxx::connection&)> work;
        DbDispatcher* dispatcher;

        static void __stdcall execute(void* param) {
            auto* self = static_cast<VoidTask*>(param);
            try {
                auto guard = self->dispatcher->pool_->acquire_guard();
                self->work(guard.connection());
            } catch (const std::exception& e) {
                LOG_ERROR("DbDispatch", "DB void job failed: %s", e.what());
            }
            delete self;
        }
    };

    struct VoidCallbackTask {
        std::function<void(pqxx::connection&)> work;
        std::function<void()> completion;
        DbDispatcher* dispatcher;

        static void __stdcall execute(void* param) {
            auto* self = static_cast<VoidCallbackTask*>(param);
            try {
                auto guard = self->dispatcher->pool_->acquire_guard();
                self->work(guard.connection());

                if (self->completion) {
                    auto cb = std::move(self->completion);
                    self->dispatcher->enqueueCompletion(std::move(cb));
                }
            } catch (const std::exception& e) {
                LOG_ERROR("DbDispatch", "DB void+cb job failed: %s", e.what());
            }
            delete self;
        }
    };
};

} // namespace fate
