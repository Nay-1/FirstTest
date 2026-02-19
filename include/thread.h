#pragma once

#include <functional>
class Thread{
public:
    using ThreadFunc = std::function<void(int)>;

    Thread(ThreadFunc func = nullptr);
    ~Thread();

    // 启动线程
    void start();

    // 获取线程id
    int getThreadId() const;
private:
    ThreadFunc func_; // 线程函数
    static int generatreId_;
    int threadId_; // 保存线程id
};