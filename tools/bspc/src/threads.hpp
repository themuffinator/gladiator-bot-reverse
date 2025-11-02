#pragma once

#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace bspc::threads
{

class CriticalSection
{
public:
    CriticalSection();
    ~CriticalSection();

    CriticalSection(const CriticalSection &) = delete;
    CriticalSection &operator=(const CriticalSection &) = delete;

    void Enter();
    void Leave();

    class Guard
    {
    public:
        explicit Guard(CriticalSection &section);
        Guard(const Guard &) = delete;
        Guard &operator=(const Guard &) = delete;
        Guard(Guard &&other) noexcept;
        Guard &operator=(Guard &&other) noexcept;
        ~Guard();

        void Release();

    private:
        CriticalSection *section_;
        bool owns_lock_;
    };

private:
    std::mutex mutex_;
    bool locked_;
    std::thread::id owner_;
};

using WorkerFunction = std::function<void(int)>;

void Configure(int requested_workers);

int WorkerCount() noexcept;

void RunWorkerRange(int work_count, bool show_progress, WorkerFunction worker);

template <typename Func>
void RunWorkerRange(int work_count, bool show_progress, Func &&func)
{
    RunWorkerRange(work_count, show_progress, WorkerFunction(std::forward<Func>(func)));
}

} // namespace bspc::threads

