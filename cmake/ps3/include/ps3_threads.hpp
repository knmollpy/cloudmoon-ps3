#pragma once
/*
 * PSL1GHT's libstdc++ is built without gthreads, so std::mutex,
 * std::recursive_mutex, std::shared_mutex and std::condition_variable
 * are not defined. lock_guard, unique_lock, scoped_lock and std::thread
 * ARE defined (bits/std_mutex.h, bits/unique_lock.h, bits/std_thread.h).
 * Do not include <condition_variable>, <future> or <thread> here and do
 * not redefine those types: this header is injected with -include.
 */
#include <pthread.h>
#include <time.h>
extern "C" int nanosleep(const struct timespec *, struct timespec *);
#include <errno.h>
#include <stdint.h>
#include <chrono>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

namespace std {

class mutex {
public:
    mutex() { pthread_mutex_init(&m_, nullptr); }
    ~mutex() { pthread_mutex_destroy(&m_); }
    mutex(const mutex &) = delete;
    mutex &operator=(const mutex &) = delete;
    void lock() { pthread_mutex_lock(&m_); }
    bool try_lock() { return pthread_mutex_trylock(&m_) == 0; }
    void unlock() { pthread_mutex_unlock(&m_); }
    pthread_mutex_t *native_handle() { return &m_; }
private:
    pthread_mutex_t m_;
};

class recursive_mutex {
public:
    recursive_mutex() {
        pthread_mutexattr_t a;
        pthread_mutexattr_init(&a);
        pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&m_, &a);
        pthread_mutexattr_destroy(&a);
    }
    ~recursive_mutex() { pthread_mutex_destroy(&m_); }
    recursive_mutex(const recursive_mutex &) = delete;
    recursive_mutex &operator=(const recursive_mutex &) = delete;
    void lock() { pthread_mutex_lock(&m_); }
    bool try_lock() { return pthread_mutex_trylock(&m_) == 0; }
    void unlock() { pthread_mutex_unlock(&m_); }
    pthread_mutex_t *native_handle() { return &m_; }
private:
    pthread_mutex_t m_;
};

class shared_mutex {
public:
    shared_mutex() : m_() {}
    void lock() { m_.lock(); }
    bool try_lock() { return m_.try_lock(); }
    void unlock() { m_.unlock(); }
    void lock_shared() { lock(); }
    bool try_lock_shared() { return try_lock(); }
    void unlock_shared() { unlock(); }
private:
    mutex m_;
};

}  // namespace std

#include <mutex>

namespace std {

enum class cv_status { no_timeout, timeout };

class condition_variable {
public:
    condition_variable() { pthread_cond_init(&c_, nullptr); }
    ~condition_variable() { pthread_cond_destroy(&c_); }
    condition_variable(const condition_variable &) = delete;
    condition_variable &operator=(const condition_variable &) = delete;
    void notify_one() noexcept { pthread_cond_signal(&c_); }
    void notify_all() noexcept { pthread_cond_broadcast(&c_); }
    void wait(unique_lock<mutex> &l) {
        if (!l.owns_lock() || l.mutex() == nullptr)
            throw std::logic_error("condition_variable requires a locked mutex");
        pthread_cond_wait(&c_, l.mutex()->native_handle());
    }
    template <class P>
    void wait(unique_lock<mutex> &l, P pred) {
        while (!pred())
            wait(l);
    }
    template <class Clock, class Duration>
    cv_status wait_until(unique_lock<mutex> &l, const chrono::time_point<Clock, Duration> &tp) {
        auto delta = tp - Clock::now();
        if (delta <= Duration::zero())
            return cv_status::timeout;
        auto ns = chrono::duration_cast<chrono::nanoseconds>(delta).count();
        timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += static_cast<time_t>(ns / 1000000000LL);
        ts.tv_nsec += static_cast<long>(ns % 1000000000LL);
        if (ts.tv_nsec >= 1000000000L) {
            ++ts.tv_sec;
            ts.tv_nsec -= 1000000000L;
        }
        int rc = pthread_cond_timedwait(&c_, l.mutex()->native_handle(), &ts);
        return rc == ETIMEDOUT ? cv_status::timeout : cv_status::no_timeout;
    }
private:
    pthread_cond_t c_;
};


} // namespace std
#include <future> // supplies std::future_status; gthreads-dependent classes stay disabled
namespace std {

template <class T>
struct ps3_future_state {
    mutex m;
    condition_variable cv;
    optional<T> value;
    exception_ptr error;
    bool ready = false;
};
template <>
struct ps3_future_state<void> {
    mutex m;
    condition_variable cv;
    exception_ptr error;
    bool ready = false;
};

template <class T>
class shared_future;
template <class T>
class future;

template <class T>
class promise {
public:
    promise() : s_(make_shared<ps3_future_state<T>>()) {}
    promise(promise &&) noexcept = default;
    promise &operator=(promise &&) noexcept = default;
    promise(const promise &) = delete;
    future<T> get_future();
    void set_value(T v) {
        lock_guard<mutex> g(s_->m);
        s_->value.emplace(move(v));
        s_->ready = true;
        s_->cv.notify_all();
    }
    void set_exception(exception_ptr e) {
        lock_guard<mutex> g(s_->m);
        s_->error = e;
        s_->ready = true;
        s_->cv.notify_all();
    }
private:
    shared_ptr<ps3_future_state<T>> s_;
    friend class future<T>;
    friend class shared_future<T>;
};

template <>
class promise<void> {
public:
    promise() : s_(make_shared<ps3_future_state<void>>()) {}
    promise(promise &&) noexcept = default;
    promise &operator=(promise &&) noexcept = default;
    promise(const promise &) = delete;
    future<void> get_future();
    void set_value() {
        lock_guard<mutex> g(s_->m);
        s_->ready = true;
        s_->cv.notify_all();
    }
    void set_exception(exception_ptr e) {
        lock_guard<mutex> g(s_->m);
        s_->error = e;
        s_->ready = true;
        s_->cv.notify_all();
    }
private:
    shared_ptr<ps3_future_state<void>> s_;
    friend class future<void>;
    friend class shared_future<void>;
};

template <class T>
class shared_future {
public:
    shared_future() = default;
    shared_future(future<T> &&other);
    explicit shared_future(shared_ptr<ps3_future_state<T>> s) : s_(move(s)) {}
    void wait() const {
        unique_lock<mutex> l(s_->m);
        s_->cv.wait(l, [&] { return s_->ready; });
    }
    template <class Rep, class Period>
    future_status wait_for(const chrono::duration<Rep, Period> &duration) const {
        auto deadline = chrono::steady_clock::now() + duration;
        unique_lock<mutex> l(s_->m);
        while (!s_->ready) {
            if (s_->cv.wait_until(l, deadline) == cv_status::timeout && !s_->ready)
                return future_status::timeout;
        }
        return future_status::ready;
    }
    const T &get() const {
        wait();
        if (s_->error)
            rethrow_exception(s_->error);
        return *s_->value;
    }
private:
    shared_ptr<ps3_future_state<T>> s_;
    friend class future<T>;
};

template <>
class shared_future<void> {
public:
    shared_future() = default;
    shared_future(future<void> &&other);
    explicit shared_future(shared_ptr<ps3_future_state<void>> s) : s_(move(s)) {}
    void wait() const {
        unique_lock<mutex> l(s_->m);
        s_->cv.wait(l, [&] { return s_->ready; });
    }
    template <class Rep, class Period>
    future_status wait_for(const chrono::duration<Rep, Period> &duration) const {
        auto deadline = chrono::steady_clock::now() + duration;
        unique_lock<mutex> l(s_->m);
        while (!s_->ready) {
            if (s_->cv.wait_until(l, deadline) == cv_status::timeout && !s_->ready)
                return future_status::timeout;
        }
        return future_status::ready;
    }
    void get() const {
        wait();
        if (s_->error)
            rethrow_exception(s_->error);
    }
private:
    shared_ptr<ps3_future_state<void>> s_;
    friend class future<void>;
};

template <class T>
class future {
public:
    future() = default;
    future(future &&) noexcept = default;
    future &operator=(future &&) noexcept = default;
    future(const future &) = delete;
    future &operator=(const future &) = delete;
    explicit future(shared_ptr<ps3_future_state<T>> s) : s_(move(s)) {}
    void wait() const {
        unique_lock<mutex> l(s_->m);
        s_->cv.wait(l, [&] { return s_->ready; });
    }
    template <class Rep, class Period>
    future_status wait_for(const chrono::duration<Rep, Period> &duration) const {
        auto deadline = chrono::steady_clock::now() + duration;
        unique_lock<mutex> l(s_->m);
        while (!s_->ready) {
            if (s_->cv.wait_until(l, deadline) == cv_status::timeout && !s_->ready)
                return future_status::timeout;
        }
        return future_status::ready;
    }
    T get() {
        wait();
        if (s_->error)
            rethrow_exception(s_->error);
        return move(*s_->value);
    }
    shared_future<T> share() { return shared_future<T>(s_); }
private:
    shared_ptr<ps3_future_state<T>> s_;
    friend class shared_future<T>;
};

template <>
class future<void> {
public:
    future() = default;
    future(future &&) noexcept = default;
    future &operator=(future &&) noexcept = default;
    future(const future &) = delete;
    future &operator=(const future &) = delete;
    explicit future(shared_ptr<ps3_future_state<void>> s) : s_(move(s)) {}
    void wait() const {
        unique_lock<mutex> l(s_->m);
        s_->cv.wait(l, [&] { return s_->ready; });
    }
    template <class Rep, class Period>
    future_status wait_for(const chrono::duration<Rep, Period> &duration) const {
        auto deadline = chrono::steady_clock::now() + duration;
        unique_lock<mutex> l(s_->m);
        while (!s_->ready) {
            if (s_->cv.wait_until(l, deadline) == cv_status::timeout && !s_->ready)
                return future_status::timeout;
        }
        return future_status::ready;
    }
    void get() {
        wait();
        if (s_->error)
            rethrow_exception(s_->error);
    }
    shared_future<void> share() { return shared_future<void>(s_); }
private:
    shared_ptr<ps3_future_state<void>> s_;
    friend class shared_future<void>;
};

template <class T>
shared_future<T>::shared_future(future<T> &&other) : s_(move(other.s_)) {}

inline shared_future<void>::shared_future(future<void> &&other) : s_(move(other.s_)) {}

template <class T>
future<T> promise<T>::get_future() {
    return future<T>(s_);
}
inline future<void> promise<void>::get_future() {
    return future<void>(s_);
}

template <class>
class packaged_task;
template <class R>
class packaged_task<R()> {
public:
    template <class F>
    explicit packaged_task(F &&f) : fn_(forward<F>(f)), p_(make_shared<promise<R>>()) {}
    future<R> get_future() { return p_->get_future(); }
    void operator()() {
        try {
            if constexpr (is_void_v<R>) {
                fn_();
                p_->set_value();
            } else {
                p_->set_value(fn_());
            }
        } catch (...) {
            p_->set_exception(current_exception());
        }
    }
private:
    function<R()> fn_;
    shared_ptr<promise<R>> p_;
};

}  // namespace std

/*
 * libstdc++ in the PSL1GHT SDK is built without gthreads. Its std::thread
 * declaration therefore has no callable constructor. libdatachannel needs
 * real joinable/detached workers, so use a separate pthread-backed type
 * rather than redefining std::thread.
 */
namespace rtc::impl {

class Ps3Thread {
private:
    struct TaskBase { virtual ~TaskBase() {} virtual void run() = 0; };
    template <class F> struct TaskModel final : TaskBase {
        explicit TaskModel(F &&f) : fn(std::move(f)) {}
        void run() override { fn(); }
        F fn;
    };
    static void *trampoline(void *arg) {
        std::unique_ptr<TaskBase> task(static_cast<TaskBase *>(arg));
        try { task->run(); } catch (...) { std::terminate(); }
        return nullptr;
    }
public:
    Ps3Thread() noexcept : joinable_(false) {}
    Ps3Thread(const Ps3Thread &) = delete;
    Ps3Thread &operator=(const Ps3Thread &) = delete;

    Ps3Thread(Ps3Thread &&other) noexcept : thread_(other.thread_), joinable_(other.joinable_) {
        other.joinable_ = false;
    }

    Ps3Thread &operator=(Ps3Thread &&other) noexcept {
        if (this != &other) {
            if (joinable_)
                pthread_detach(thread_);
            thread_ = other.thread_;
            joinable_ = other.joinable_;
            other.joinable_ = false;
        }
        return *this;
    }

    template <class F, class... Args>
    explicit Ps3Thread(F &&f, Args &&...args) : joinable_(false) {
        using Bound = decltype(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        TaskBase *task = new TaskModel<Bound>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        int rc = pthread_create(&thread_, nullptr, &Ps3Thread::trampoline, task);
        if (rc != 0) {
            delete task;
            throw std::runtime_error("PS3 pthread_create failed");
        }
        joinable_ = true;
    }

    ~Ps3Thread() {
        if (joinable_)
            pthread_detach(thread_);
    }

    bool joinable() const noexcept { return joinable_; }

    void join() {
        if (!joinable_)
            throw std::runtime_error("PS3 thread is not joinable");
        int rc = pthread_join(thread_, nullptr);
        if (rc != 0)
            throw std::runtime_error("PS3 pthread_join failed");
        joinable_ = false;
    }

    void detach() {
        if (!joinable_)
            throw std::runtime_error("PS3 thread is not joinable");
        int rc = pthread_detach(thread_);
        if (rc != 0)
            throw std::runtime_error("PS3 pthread_detach failed");
        joinable_ = false;
    }

    static unsigned int hardware_concurrency() noexcept { return 2; }

private:
    pthread_t thread_{};
    bool joinable_;
};

template <class Rep, class Period>
inline void ps3_sleep_for(const std::chrono::duration<Rep, Period> &duration) {
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    if (ns <= 0)
        return;
    timespec request;
    request.tv_sec = static_cast<time_t>(ns / 1000000000LL);
    request.tv_nsec = static_cast<long>(ns % 1000000000LL);
    while (nanosleep(&request, &request) != 0 && errno == EINTR) {
    }
}

} // namespace rtc::impl
