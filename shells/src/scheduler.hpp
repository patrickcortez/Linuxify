// Funux Task Scheduler - Background job scheduling
// Usage: #include "scheduler.hpp"
#ifndef FUNUX_SCHEDULER_HPP
#define FUNUX_SCHEDULER_HPP

#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <mutex>

namespace FunuxSys {

enum class JobType {
    ONCE,
    RECURRING,
    INTERVAL
};

struct ScheduledJob {
    int id;
    std::string name;
    std::string command;
    JobType type;
    std::chrono::seconds interval;
    std::chrono::system_clock::time_point nextRun;
    std::chrono::system_clock::time_point created;
    int runCount;
    bool enabled;
    
    ScheduledJob() : id(0), type(JobType::ONCE), interval(0), runCount(0), enabled(true) {}
};

class Scheduler {
private:
    std::vector<ScheduledJob> jobs;
    std::mutex mtx;
    int nextId = 1;
    std::function<void(const std::string&)> executor;
    
    static Scheduler* instance;
    
    Scheduler() {}
    
public:
    static Scheduler& get() {
        if (!instance) {
            instance = new Scheduler();
        }
        return *instance;
    }
    
    void setExecutor(std::function<void(const std::string&)> exec) {
        executor = exec;
    }
    
    int addJob(const std::string& name, const std::string& command, JobType type, 
               std::chrono::seconds interval = std::chrono::seconds(0)) {
        std::lock_guard<std::mutex> lock(mtx);
        
        ScheduledJob job;
        job.id = nextId++;
        job.name = name;
        job.command = command;
        job.type = type;
        job.interval = interval;
        job.created = std::chrono::system_clock::now();
        job.runCount = 0;
        job.enabled = true;
        
        if (type == JobType::ONCE) {
            job.nextRun = std::chrono::system_clock::now();
        } else {
            job.nextRun = std::chrono::system_clock::now() + interval;
        }
        
        jobs.push_back(job);
        return job.id;
    }
    
    int scheduleOnce(const std::string& name, const std::string& command, 
                     std::chrono::seconds delay = std::chrono::seconds(0)) {
        std::lock_guard<std::mutex> lock(mtx);
        
        ScheduledJob job;
        job.id = nextId++;
        job.name = name;
        job.command = command;
        job.type = JobType::ONCE;
        job.interval = std::chrono::seconds(0);
        job.created = std::chrono::system_clock::now();
        job.nextRun = std::chrono::system_clock::now() + delay;
        job.runCount = 0;
        job.enabled = true;
        
        jobs.push_back(job);
        return job.id;
    }
    
    int scheduleRecurring(const std::string& name, const std::string& command, 
                          std::chrono::seconds interval) {
        return addJob(name, command, JobType::RECURRING, interval);
    }
    
    bool removeJob(int id) {
        std::lock_guard<std::mutex> lock(mtx);
        
        for (auto it = jobs.begin(); it != jobs.end(); ++it) {
            if (it->id == id) {
                jobs.erase(it);
                return true;
            }
        }
        return false;
    }
    
    bool enableJob(int id, bool enabled) {
        std::lock_guard<std::mutex> lock(mtx);
        
        for (auto& job : jobs) {
            if (job.id == id) {
                job.enabled = enabled;
                return true;
            }
        }
        return false;
    }
    
    void tick() {
        std::lock_guard<std::mutex> lock(mtx);
        
        auto now = std::chrono::system_clock::now();
        std::vector<int> toRemove;
        
        for (auto& job : jobs) {
            if (!job.enabled) continue;
            
            if (now >= job.nextRun) {
                if (executor) {
                    executor(job.command);
                }
                job.runCount++;
                
                if (job.type == JobType::ONCE) {
                    toRemove.push_back(job.id);
                } else if (job.type == JobType::RECURRING || job.type == JobType::INTERVAL) {
                    job.nextRun = now + job.interval;
                }
            }
        }
        
        for (int id : toRemove) {
            for (auto it = jobs.begin(); it != jobs.end(); ++it) {
                if (it->id == id) {
                    jobs.erase(it);
                    break;
                }
            }
        }
    }
    
    std::vector<ScheduledJob> listJobs() {
        std::lock_guard<std::mutex> lock(mtx);
        return jobs;
    }
    
    ScheduledJob* getJob(int id) {
        std::lock_guard<std::mutex> lock(mtx);
        for (auto& job : jobs) {
            if (job.id == id) return &job;
        }
        return nullptr;
    }
    
    size_t count() {
        std::lock_guard<std::mutex> lock(mtx);
        return jobs.size();
    }
    
    void clear() {
        std::lock_guard<std::mutex> lock(mtx);
        jobs.clear();
    }
};

Scheduler* Scheduler::instance = nullptr;

}

#endif
