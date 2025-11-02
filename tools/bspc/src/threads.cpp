#include "threads.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

#include "logging.hpp"

namespace bspc::threads
{
namespace
{
constexpr int kMaxWorkerCount = 64;

struct ThreadState
{
    CriticalSection dispatch_lock;
    std::atomic<bool> threading_active{false};
    int worker_count = 1;
    int dispatch = 0;
    int work_count = 0;
    int last_progress = -1;
    bool show_pacifier = false;
};

ThreadState &State()
{
    static ThreadState state;
    return state;
}

int ClampWorkerCount(int requested)
{
    if (requested <= 0)
    {
        unsigned int hardware = std::thread::hardware_concurrency();
        if (hardware == 0)
        {
            requested = 1;
        }
        else
        {
            requested = static_cast<int>(hardware);
        }
    }
    return std::clamp(requested, 1, kMaxWorkerCount);
}

int NextWorkIndex(ThreadState &state)
{
    CriticalSection::Guard guard(state.dispatch_lock);
    if (!state.threading_active.load(std::memory_order_acquire))
    {
        log::Fatal("ThreadLock invoked without active workers");
    }

    if (state.dispatch >= state.work_count)
    {
        return -1;
    }

    const int current = state.dispatch++;
    if (state.show_pacifier && state.work_count > 0)
    {
        const int progress = (10 * state.dispatch) / state.work_count;
        if (progress != state.last_progress)
        {
            state.last_progress = progress;
            log::Info("%d...", progress);
        }
    }

    return current;
}

} // namespace

CriticalSection::CriticalSection()
    : locked_(false),
      owner_()
{
}

CriticalSection::~CriticalSection()
{
    if (locked_)
    {
        log::Fatal("Destroying a locked critical section");
    }
}

void CriticalSection::Enter()
{
    mutex_.lock();
    if (locked_)
    {
        mutex_.unlock();
        log::Fatal("Recursive thread lock");
    }

    locked_ = true;
    owner_ = std::this_thread::get_id();
}

void CriticalSection::Leave()
{
    if (!locked_)
    {
        log::Fatal("Thread unlock without lock");
    }

    if (owner_ != std::this_thread::get_id())
    {
        log::Fatal("Thread unlock from non-owner thread");
    }

    locked_ = false;
    owner_ = std::thread::id{};
    mutex_.unlock();
}

CriticalSection::Guard::Guard(CriticalSection &section)
    : section_(&section),
      owns_lock_(true)
{
    section_->Enter();
}

CriticalSection::Guard::Guard(Guard &&other) noexcept
    : section_(other.section_),
      owns_lock_(other.owns_lock_)
{
    other.section_ = nullptr;
    other.owns_lock_ = false;
}

CriticalSection::Guard &CriticalSection::Guard::operator=(Guard &&other) noexcept
{
    if (this != &other)
    {
        if (owns_lock_ && section_ != nullptr)
        {
            section_->Leave();
        }
        section_ = other.section_;
        owns_lock_ = other.owns_lock_;
        other.section_ = nullptr;
        other.owns_lock_ = false;
    }
    return *this;
}

CriticalSection::Guard::~Guard()
{
    if (owns_lock_ && section_ != nullptr)
    {
        section_->Leave();
    }
}

void CriticalSection::Guard::Release()
{
    if (owns_lock_ && section_ != nullptr)
    {
        section_->Leave();
        owns_lock_ = false;
    }
}

void Configure(int requested_workers)
{
    ThreadState &state = State();
    state.worker_count = ClampWorkerCount(requested_workers);
}

int WorkerCount() noexcept
{
    return State().worker_count;
}

void RunWorkerRange(int work_count, bool show_progress, WorkerFunction worker)
{
    if (work_count <= 0 || !worker)
    {
        return;
    }

    ThreadState &state = State();
    state.dispatch = 0;
    state.work_count = work_count;
    state.last_progress = -1;
    state.show_pacifier = show_progress;
    state.threading_active.store(true, std::memory_order_release);

    auto worker_proc = [&state, &worker]() {
        while (true)
        {
            const int index = NextWorkIndex(state);
            if (index == -1)
            {
                break;
            }
            worker(index);
        }
    };

    const int workers = state.worker_count;
    if (workers <= 1)
    {
        worker_proc();
    }
    else
    {
        std::vector<std::thread> threads;
        threads.reserve(static_cast<std::size_t>(workers));
        for (int i = 0; i < workers; ++i)
        {
            threads.emplace_back(worker_proc);
        }
        for (auto &thread : threads)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }
    }

    state.threading_active.store(false, std::memory_order_release);
}

} // namespace bspc::threads

