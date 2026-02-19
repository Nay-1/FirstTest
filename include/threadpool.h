#pragma once

#include <vector>
#include <thread>
#include <functional>
#include <atomic>
#include <condition_variable>
#include <queue>
#include <memory>
#include <unordered_map>
#include <iostream>
#include "thread.h"
#include <future>

const int TaskQueueMaxThresholdDefault = INT32_MAX; // 任务队列默认最大阈值
const int ThreadMaxThresholdDefault = 10; // 线程数量上限默认阈值
const int ThreadIdleTimeDefault = 60; // 线程空闲时间默认阈值 单位：秒

enum class ThreadPoolMode {
    Fixed, // 固定线程池
    Cached // 动态线程池
};

class ThreadPool {
public:
    ThreadPool()
        : mode_(ThreadPoolMode::Fixed)
        , initThreadSize_(4)
        , curThreadSize_(0)
        , idleThreadSize_(0)
        , taskSize_(0)
        , taskQueueMaxThreshold_(TaskQueueMaxThresholdDefault)
        , threadMaxThreshold_(ThreadMaxThresholdDefault)
        , isPoolRunning_(false)
    {}
    ~ThreadPool()
    {
        // 设置线程池运行状态为 false
        isPoolRunning_ = false;

        std::unique_lock<std::mutex> lock(taskQueueMutex_);
        // 唤醒所有线程
        taskQueueNotEmpty_.notify_all();
        // 等待所有线程退出
        exitCond_.wait(lock, [this]() -> bool { return threads_.size() == 0; });
    }
    // 设置线程池模式
    void setThreadPoolType(ThreadPoolMode mode)
    {
        // 检查线程池是否运行
        if(!checkRunningState())
        {
            mode_ = mode;
        }
    }
    //  设置task任务队列上限阈值
    void setTaskQueueMaxThreshold(int threshold)
    {
        // 检查线程池是否运行
        if(!checkRunningState())
        {
            taskQueueMaxThreshold_ = threshold;
        }   
    }
    // 设置线程数量上限阈值
    void setThreadMaxThreshold(int threshold)
    {
        // 检查线程池是否运行
        if(!checkRunningState())
        {
            threadMaxThreshold_ = threshold;
        }   
    }
    // 开启线程池   std::thread::hardware_concurrency() 获取当前系统cpu核数
    void start(int initThreadSize = std::thread::hardware_concurrency())
    {
        // 设置线程池运行状态为 true
        isPoolRunning_ = true;
        initThreadSize_ = initThreadSize;
        curThreadSize_ = initThreadSize_;
        // 创建线程对象，并放入线程池中
        for(int i = 0; i < initThreadSize_; ++i)
        {
            // 线程函数绑定到线程池的成员函数
            // 为什么要这样设计？因为启动线程需要一个函数,而这个函数需要访问线程池的成员变量，所以线程池提供一个成员函数作为线程函数，并绑定到线程对象上
            // make_unique 封装了 new 操作，创建一个 Thread 对象，并返回一个 unique_ptr 管理这个对象的生命周期，避免内存泄漏
            // threads_.emplace_back(std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this)));
            auto thread = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
            threads_.emplace(thread->getThreadId(), std::move(thread));
        }
        // 启动线程池中的线程
        for(int i = 0; i < initThreadSize_; ++i)
        {
            threads_[i]->start();
            ++idleThreadSize_;
        }
    }
    // 提交任务
    // 使用可变参模板编程 让submitTask函数可以接受任意数量的参数
    // pool.submitTask(sum, 1, 2);
    template<typename Func, typename... Args>
    auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))>
    {
        using Rtype = decltype(func(args...));
        auto task = std::make_shared<std::packaged_task<Rtype()>>(
            std::bind(std::forward<Func>(func), std::forward<Args>(args)...)
        );
        std::future<Rtype> result = task->get_future();

        // 获取任务队列锁
        std::unique_lock<std::mutex> lock(taskQueueMutex_);
        // 等待任务队列非满1秒钟，超时返回空
        if(!taskQueueNotFull_.wait_for(lock, std::chrono::seconds(1), [this]() -> bool { return taskQueue_.size() < taskQueueMaxThreshold_; }))
        {
            std::cout << "submitTask wait_for timeout, taskQueue is full, submit task failed!" << std::endl;
            std::promise<Rtype> prom;
            prom.set_value(Rtype());
            return prom.get_future();
        }
        // 将任务放入任务队列
        taskQueue_.emplace([task]() { (*task)(); });
        ++taskSize_;
        // 通知线程池中的线程有任务了 
        taskQueueNotEmpty_.notify_all();

        // cache模式，根据任务数量动态调整线程数量
        if(mode_ == ThreadPoolMode::Cached 
            && idleThreadSize_ == 0 
            && taskSize_ > idleThreadSize_ 
            && curThreadSize_ < threadMaxThreshold_)
        {
            // 创建新线程
            // threads_.emplace_back(std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this)));
            auto thread = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
            threads_.emplace(thread->getThreadId(), std::move(thread));
            // 启动新线程
            threads_[thread->getThreadId()]->start();
            ++curThreadSize_;
            ++idleThreadSize_;
        }

        // 返回任务的Result对象
        return result;
    }

    ThreadPool(const ThreadPool&) = delete; // 禁止拷贝构造
    ThreadPool& operator=(const ThreadPool&) = delete; // 禁止拷贝赋值

private:
    // 线程池线程工作函数
    void threadFunc(int threadId)
    {
        // 获取当前的时间
        auto lastTime = std::chrono::high_resolution_clock::now();
        for(;;)
        // while(isPoolRunning_) // 线程池退出 不需要继续完成提交的任务
        {
            Task task;
            {
                // 获取任务队列锁
                std::unique_lock<std::mutex> lock(taskQueueMutex_);
                std::cout << "tid:" << std::this_thread::get_id() << "获取任务中..." << std::endl;
                // 锁加双重判断
                // while(isPoolRunning_ && taskQueue_.size() == 0 )
                while(taskQueue_.size() == 0)
                {
                    if(!isPoolRunning_)
                    {
                        threads_.erase(threadId);
                        std::cout << "tid:" << std::this_thread::get_id() << "回收线程" << std::endl;
                        exitCond_.notify_all();
                        return;
                    }
                    // cache模式下 空闲时间超过60秒，且当前线程数量大于初始线程数量，就销毁当前线程
                    if(mode_ == ThreadPoolMode::Cached )
                    {  
                        // 每秒钟返回一次 看是超时返回还是有任务返回  超时返回
                        if(taskQueueNotEmpty_.wait_for(lock, std::chrono::seconds(1)) == std::cv_status::timeout)
                        {
                            // auto 推出来的类型是 std::chrono::time_point<std::chrono::high_resolution_clock>
                            auto now = std::chrono::high_resolution_clock::now();
                            // auto 推出来的类型是 std::chrono::duration
                            auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
                            
                            if(dur.count() >= ThreadIdleTimeDefault)
                            {
                                // 回收当前线程
                                // 记录线程数量相关变量的值修改
                                // 把线程对象从线程池中删除  没办法找到threadFunc对应的线程对象
                                threads_.erase(threadId);
                                --curThreadSize_;
                                --idleThreadSize_;
                                std::cout << "tid:" << std::this_thread::get_id() << "回收线程" << std::endl;
                                return;
                            }          
                        }
                    }
                    else
                    {
                        // 等待任务队列非空
                        taskQueueNotEmpty_.wait(lock);
                    }
                }
                // if(!isPoolRunning_)
                // {
                //     // 跳出while循环 退出线程
                //     break;
                // }
                
                --idleThreadSize_;
                // 从任务队列中取出一个任务
                task = taskQueue_.front();
                taskQueue_.pop();
                --taskSize_;
                std::cout << "tid:" << std::this_thread::get_id() << "获取任务成功" << std::endl;
                // 如果任务队列还有任务了，通知线程池中的线程有任务了
                if(!taskQueue_.empty())
                {
                    taskQueueNotEmpty_.notify_all();
                }
                // 通知线程池中的线程任务队列未满了
                taskQueueNotFull_.notify_all();
            }// 释放任务队列锁
            
            // 执行任务
            task();
            ++idleThreadSize_;
            // 任务执行完，更新线程空闲时间
            lastTime = std::chrono::high_resolution_clock::now();
        }
        // threads_.erase(threadId);
        // std::cout << "tid:" << std::this_thread::get_id() << "回收线程" << std::endl;
        // exitCond_.notify_all();
        // return;
    }

    // 检查线程池是否运行
    bool checkRunningState() const
    {
        return isPoolRunning_;
    }

private:
    ThreadPoolMode mode_; // 线程池模式
    
    std::unordered_map<int, std::unique_ptr<Thread>> threads_;
    int initThreadSize_; // 初始线程数量
    std::atomic_uint curThreadSize_; // 线程数量
    int threadMaxThreshold_; // 线程数量上限阈值
    std::atomic_bool isPoolRunning_; // 线程池是否运行
    std::atomic_uint idleThreadSize_; // 记录空闲线程数量

    using Task = std::function<void()>;
    std::queue<Task> taskQueue_; // 任务队列
    std::atomic_uint taskSize_; // 任务数量
    int taskQueueMaxThreshold_; // 任务队列最大阈值

    std::mutex taskQueueMutex_; // 任务队列互斥锁
    std::condition_variable taskQueueNotEmpty_; // 任务队列非空条件变量
    std::condition_variable taskQueueNotFull_; // 任务队列未满条件变量
    std::condition_variable exitCond_; // 线程池退出条件变量
}; 