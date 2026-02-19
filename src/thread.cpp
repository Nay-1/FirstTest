#include "thread.h"
#include <thread>

Thread::Thread(ThreadFunc func)
    : func_(func)
    , threadId_(generatreId_++)
{}
Thread::~Thread()
{}
int Thread::generatreId_ = 0;
// 启动线程
void Thread::start()
{
    std::thread t(func_, threadId_);
    t.detach(); // 分离线程
}

// 获取线程id
int Thread::getThreadId() const
{
    return threadId_;
}