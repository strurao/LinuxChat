#include <iostream>
#include <vector>
#include <thread>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <stdexcept>
#include <future>

// 스레드 풀 클래스 정의
class ThreadPool {
public:
    // 생성자: 스레드 풀을 초기화하고 지정된 수의 스레드를 생성
    ThreadPool(size_t threads) : stop(false) {
        for(size_t i = 0; i < threads; ++i)
            workers.emplace_back(
                [this] {
                    for(;;) {
                        std::function<void()> task;

                        {
                            std::unique_lock<std::mutex> lock(this->queue_mutex);
                            // 작업이 있을 때까지 또는 스레드 풀이 중지될 때까지 대기
                            this->condition.wait(lock,
                                [this] { return this->stop || !this->tasks.empty(); });
                            // 스레드 풀이 중지되고 작업이 없으면 루프를 종료
                            if(this->stop && this->tasks.empty())
                                return;
                            // 대기 중인 작업을 가져오기
                            task = std::move(this->tasks.front());
                            this->tasks.pop();
                        }

                        // 작업 실행
                        task();
                    }
                }
            );
    }

    // 작업을 스레드 풀의 작업 큐에 추가. 작업은 나중에 스레드 중 하나에서 실행
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type> {
        using return_type = typename std::result_of<F(Args...)>::type;

        // 패키지된 태스크를 생성하여 작업과 결과를 저장
        auto task = std::make_shared< std::packaged_task<return_type()> >(
                std::bind(std::forward<F>(f), std::forward<Args>(args)...)
            );

        // 작업의 결과를 가져올 수 있는 future 객체를 얻기
        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex);

            // 스레드 풀이 이미 중지된 경우 예외
            if(stop)
                throw std::runtime_error("enqueue on stopped ThreadPool");

            // 작업을 큐에 추가합니다.
            tasks.emplace([task](){ (*task)(); });
        }
        condition.notify_one(); // 대기 중인 스레드 중 하나를 깨우기
        return res;
    }

    // 소멸자: 모든 스레드를 정상적으로 종료하고 리소스를 정리
    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true; // 스레드 풀 작업 중지 신호를 보냅니다.
        }
        condition.notify_all(); // 모든 대기 중인 스레드를 깨우기
        for(std::thread &worker: workers) // 모든 스레드가 종료될 때까지 대기
            worker.join();
    }

private:
    // 스레드 풀의 모든 스레드를 저장하는 벡터
    std::vector< std::thread > workers;
    // 대기 중인 작업을 저장하는 큐
    std::queue< std::function<void()> > tasks;
    
    // 큐에 대한 동시 접근을 제어하기 위한 뮤텍스
    std::mutex queue_mutex;
    // 작업이 추가되었을 때 스레드를 깨우기 위한 조건 변수
    std::condition_variable condition;
    // 스레드 풀 중지 여부를 나타내는 플래그
    std::atomic<bool> stop;
};
