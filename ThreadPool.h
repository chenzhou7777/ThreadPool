// ThreadPool.h
// 线程池实现：支持任务异步提交与多线程并发执行
// ThreadPool.h
// 线程池实现：支持任务异步提交与多线程并发执行
#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <condition_variable> // 条件变量用于线程同步
#include <functional>         // std::function 用于任务封装
#include <future>    // std::future/std::packaged_task 用于异步结果
#include <memory>    // 智能指针
#include <mutex>     // 互斥锁
#include <queue>     // 任务队列
#include <stdexcept> // 异常处理
#include <thread>    // 线程
#include <vector>    // 线程容器

// 线程池类：用于管理和调度多个工作线程执行任务
#include <condition_variable> // 条件变量用于线程同步
#include <functional>         // std::function 用于任务封装
#include <future>    // std::future/std::packaged_task 用于异步结果
#include <memory>    // 智能指针
#include <mutex>     // 互斥锁
#include <queue>     // 任务队列
#include <stdexcept> // 异常处理
#include <thread>    // 线程
#include <vector>    // 线程容器

// 线程池类：用于管理和调度多个工作线程执行任务
class ThreadPool {
public:
  /**
   * 构造函数
   * @param threads 工作线程数量，必须大于0
   * 创建指定数量的工作线程，等待任务到来
   */
  explicit ThreadPool(size_t threads);

  /**
   * 向线程池提交任务
   * @tparam F 可调用对象类型
   * @tparam Args 参数类型
   * @param f 任务函数
   * @param args 任务参数
   * @return std::future<返回值类型>，可用于获取任务结果
   * 任务会被封装并异步执行
   */
  template <class F, class... Args>
  auto enqueue(F &&f, Args &&...args)
      -> std::future<typename std::invoke_result_t<F, Args...>>;

  /**
   * 析构函数
   * 停止所有线程并回收资源
   */
  ~ThreadPool();

  /**
   * 构造函数
   * @param threads 工作线程数量，必须大于0
   * 创建指定数量的工作线程，等待任务到来
   */
  explicit ThreadPool(size_t threads);

  /**
   * 向线程池提交任务
   * @tparam F 可调用对象类型
   * @tparam Args 参数类型
   * @param f 任务函数
   * @param args 任务参数
   * @return std::future<返回值类型>，可用于获取任务结果
   * 任务会被封装并异步执行
   */
  template <class F, class... Args>
  auto enqueue(F &&f, Args &&...args)
      -> std::future<typename std::invoke_result_t<F, Args...>>;

  /**
   * 析构函数
   * 停止所有线程并回收资源
   */
  ~ThreadPool();

private:
  std::vector<std::thread> m_workers;        // 工作线程容器
  std::queue<std::function<void()>> m_tasks; // 任务队列，存储待执行任务
  std::mutex m_queue_mutex;                  // 任务队列互斥锁
  std::condition_variable m_condition;       // 条件变量用于任务通知
  bool m_stop = false;                       // 线程池停止标志
};

/**
 * 构造函数实现
 * 创建指定数量的工作线程，每个线程循环等待任务队列有新任务
 * 线程安全：通过互斥锁和条件变量同步
 */
inline ThreadPool::ThreadPool(size_t threads) {
  if (threads == 0) {
    throw std::invalid_argument("ThreadPool size must be greater than 0");
  }
  for (size_t i = 0; i < threads; ++i)
    m_workers.emplace_back([this] {
      for (;;) {
        std::function<void()> task;
        {
          // 加锁保护任务队列
          std::unique_lock<std::mutex> lock(this->m_queue_mutex);
          // 等待任务到来或线程池停止
          this->m_condition.wait(
              lock, [this] { return this->m_stop || !this->m_tasks.empty(); });
          // 如果线程池停止且队列为空，则退出线程
          if (this->m_stop && this->m_tasks.empty())
            return;
          // 取出一个任务
          task = std::move(this->m_tasks.front());
          this->m_tasks.pop();
        }
        // 执行任务
        task();
      }
    });
}

/**
 * 向线程池提交任务
 * 任务会被封装为 std::packaged_task，支持返回值和异常传递
 * 线程安全：加锁保护任务队列
 */
template <class F, class... Args>
auto ThreadPool::enqueue(F &&f, Args &&...args)
    -> std::future<typename std::invoke_result_t<F, Args...>> {
  using return_type = typename std::invoke_result_t<F, Args...>;
  // 将任务和参数绑定，封装为可异步执行的 packaged_task
  auto task = std::make_shared<std::packaged_task<return_type()>>(
      std::bind(std::forward<F>(f), std::forward<Args>(args)...));
  std::future<return_type> res = task->get_future(); // 获取任务结果 future
  {
    std::unique_lock<std::mutex> lock(m_queue_mutex); // 加锁保护队列
    if (m_stop)
      throw std::runtime_error(
          "enqueue on stopped ThreadPool"); // 停止后不允许提交
    // 将任务加入队列，等待线程执行
    m_tasks.emplace([task = std::move(task)]() { (*task)(); });
  }
  m_condition.notify_one(); // 通知一个工作线程
  return res;
}

/**
 * 析构函数实现
 * 停止所有线程，等待所有任务完成并回收资源
 * 线程安全：加锁设置停止标志，通知所有线程
 */
inline ThreadPool::~ThreadPool() {
  {
    std::unique_lock<std::mutex> lock(m_queue_mutex);
    m_stop = true; // 设置停止标志
  }
  m_condition.notify_all(); // 唤醒所有线程
  for (std::thread &worker : m_workers) {
    try {
      if (worker.joinable())
        worker.join(); // 等待线程结束
    } catch (...) {
      // 析构中不能抛异常，忽略异常
    }
  }
}

#endif
