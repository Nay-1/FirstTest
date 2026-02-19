#include <iostream>
#include <future>
#include <thread>
#include "threadpool.h"

using namespace std;

int num1(int a, int b)
{
    std::this_thread::sleep_for(std::chrono::seconds(5));
    return a + b;
}
int num2(int a, int b)
{
    return a + b;
}
int main()
{
    ThreadPool pool;
    pool.start();
    future<int> result = pool.submitTask(num1, 1, 2);
    cout<<result.get()<<endl;
    // packaged_task(类似function函数对象) 有一个get_future()成员函数 可以获取返回值
    // 然后用future对象接收，调用get()获取返回值
    // 适合跟thread使用 获取线程函数的返回值
    // packaged_task<int(int, int)> task(num1);
    // future类 
    //类似之前写的Result
    // future<int> result = task.get_future();
    // thread t(std::move(task), 1, 2);
    // cout << result.get() << endl;
    return 0;
}
