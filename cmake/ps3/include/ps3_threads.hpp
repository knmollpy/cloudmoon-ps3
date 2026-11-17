#pragma once
/*
 * PSL1GHT PPU threading compatibility for libdatachannel.
 * Uses real pthread primitives; no fake/no-op synchronization.
 */
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <sched.h>
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

template<class M> class lock_guard {
public:
    explicit lock_guard(M &m) : m_(m) { m_.lock(); }
    ~lock_guard() { m_.unlock(); }
    lock_guard(const lock_guard &) = delete;
    lock_guard &operator=(const lock_guard &) = delete;
private:
    M &m_;
};

template<class M> class unique_lock {
public:
    unique_lock() noexcept : m_(nullptr), owns_(false) {}
    explicit unique_lock(M &m) : m_(&m), owns_(false) { lock(); }
    unique_lock(M &m, defer_lock_t) noexcept : m_(&m), owns_(false) {}
    unique_lock(M &m, adopt_lock_t) noexcept : m_(&m), owns_(true) {}
    ~unique_lock() { if (owns_) unlock(); }
    unique_lock(const unique_lock &) = delete;
    unique_lock &operator=(const unique_lock &) = delete;
    unique_lock(unique_lock &&o) noexcept : m_(o.m_), owns_(o.owns_) { o.m_=nullptr; o.owns_=false; }
    unique_lock &operator=(unique_lock &&o) noexcept {
        if (this != &o) { if (owns_) unlock(); m_=o.m_; owns_=o.owns_; o.m_=nullptr; o.owns_=false; }
        return *this;
    }
    void lock() { if (!m_ || owns_) throw system_error(); m_->lock(); owns_=true; }
    bool try_lock() { if (!m_ || owns_) throw system_error(); owns_=m_->try_lock(); return owns_; }
    void unlock() { if (!m_ || !owns_) throw system_error(); m_->unlock(); owns_=false; }
    bool owns_lock() const noexcept { return owns_; }
    explicit operator bool() const noexcept { return owns_; }
    M *mutex() const noexcept { return m_; }
private:
    struct system_error : std::logic_error { system_error() : logic_error("invalid unique_lock operation") {} };
    M *m_;
    bool owns_;
};

template<class M> class shared_lock {
public:
    explicit shared_lock(M &m) : m_(&m), owns_(true) { m_->lock_shared(); }
    ~shared_lock() { if (owns_) m_->unlock_shared(); }
    shared_lock(const shared_lock &) = delete;
    shared_lock &operator=(const shared_lock &) = delete;
private:
    M *m_;
    bool owns_;
};

template<class M> class scoped_lock {
public:
    explicit scoped_lock(M &m) : a_(&m), b_(nullptr) { a_->lock(); }
    scoped_lock(M &a, M &b) : a_(&a), b_(&b) {
        for (;;) {
            a_->lock();
            if (b_->try_lock()) break;
            a_->unlock();
            sched_yield();
        }
    }
    ~scoped_lock() { if (b_) b_->unlock(); if (a_) a_->unlock(); }
    scoped_lock(const scoped_lock &) = delete;
    scoped_lock &operator=(const scoped_lock &) = delete;
private:
    M *a_;
    M *b_;
};

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
        if (!l.owns_lock()) throw std::logic_error("condition_variable requires locked mutex");
        pthread_cond_wait(&c_, l.mutex()->native_handle());
    }
    template<class P> void wait(unique_lock<mutex> &l, P pred) { while (!pred()) wait(l); }
    template<class Clock, class Duration>
    cv_status wait_until(unique_lock<mutex> &l, const chrono::time_point<Clock,Duration> &tp) {
        auto delta = tp - Clock::now();
        if (delta <= Duration::zero()) return cv_status::timeout;
        auto ns = chrono::duration_cast<chrono::nanoseconds>(delta).count();
        timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += ns / 1000000000LL;
        ts.tv_nsec += ns % 1000000000LL;
        if (ts.tv_nsec >= 1000000000L) { ++ts.tv_sec; ts.tv_nsec -= 1000000000L; }
        int rc = pthread_cond_timedwait(&c_, l.mutex()->native_handle(), &ts);
        return rc == ETIMEDOUT ? cv_status::timeout : cv_status::no_timeout;
    }
private:
    pthread_cond_t c_;
};

template<class T> struct ps3_future_state {
    mutex m;
    condition_variable cv;
    optional<T> value;
    exception_ptr error;
    bool ready = false;
};
template<> struct ps3_future_state<void> {
    mutex m;
    condition_variable cv;
    exception_ptr error;
    bool ready = false;
};

template<class T> class shared_future;
template<class T> class future;

template<class T> class promise {
public:
    promise() : s_(make_shared<ps3_future_state<T>>()) {}
    promise(promise &&) noexcept = default;
    promise &operator=(promise &&) noexcept = default;
    promise(const promise &) = delete;
    future<T> get_future();
    void set_value(T v) { lock_guard<mutex> g(s_->m); s_->value.emplace(move(v)); s_->ready=true; s_->cv.notify_all(); }
    void set_exception(exception_ptr e) { lock_guard<mutex> g(s_->m); s_->error=e; s_->ready=true; s_->cv.notify_all(); }
private:
    shared_ptr<ps3_future_state<T>> s_;
    friend class future<T>;
    friend class shared_future<T>;
};
template<> class promise<void> {
public:
    promise() : s_(make_shared<ps3_future_state<void>>()) {}
    promise(promise &&) noexcept = default;
    promise &operator=(promise &&) noexcept = default;
    promise(const promise &) = delete;
    future<void> get_future();
    void set_value() { lock_guard<mutex> g(s_->m); s_->ready=true; s_->cv.notify_all(); }
    void set_exception(exception_ptr e) { lock_guard<mutex> g(s_->m); s_->error=e; s_->ready=true; s_->cv.notify_all(); }
private:
    shared_ptr<ps3_future_state<void>> s_;
    friend class future<void>;
    friend class shared_future<void>;
};

template<class T> class future {
public:
    future() = default;
    future(future &&) noexcept = default;
    future &operator=(future &&) noexcept = default;
    future(const future &) = delete;
    future &operator=(const future &) = delete;
    explicit future(shared_ptr<ps3_future_state<T>> s) : s_(move(s)) {}
    void wait() const { unique_lock<mutex> l(s_->m); s_->cv.wait(l,[&]{return s_->ready;}); }
    T get() { wait(); if(s_->error) rethrow_exception(s_->error); return move(*s_->value); }
    shared_future<T> share() { return shared_future<T>(s_); }
private:
    shared_ptr<ps3_future_state<T>> s_;
    friend class shared_future<T>;
};
template<> class future<void> {
public:
    future() = default;
    future(future &&) noexcept = default;
    future &operator=(future &&) noexcept = default;
    future(const future &) = delete;
    future &operator=(const future &) = delete;
    explicit future(shared_ptr<ps3_future_state<void>> s) : s_(move(s)) {}
    void wait() const { unique_lock<mutex> l(s_->m); s_->cv.wait(l,[&]{return s_->ready;}); }
    void get() { wait(); if(s_->error) rethrow_exception(s_->error); }
    shared_future<void> share() { return shared_future<void>(s_); }
private:
    shared_ptr<ps3_future_state<void>> s_;
    friend class shared_future<void>;
};
template<class T> future<T> promise<T>::get_future() { return future<T>(s_); }
inline future<void> promise<void>::get_future() { return future<void>(s_); }

template<class T> class shared_future {
public:
    shared_future() = default;
    explicit shared_future(shared_ptr<ps3_future_state<T>> s) : s_(move(s)) {}
    shared_future(future<T> &&f) noexcept : s_(move(f.s_)) {}
    void wait() const { unique_lock<mutex> l(s_->m); s_->cv.wait(l,[&]{return s_->ready;}); }
    const T &get() const { wait(); if(s_->error) rethrow_exception(s_->error); return *s_->value; }
private:
    shared_ptr<ps3_future_state<T>> s_;
};
template<> class shared_future<void> {
public:
    shared_future() = default;
    explicit shared_future(shared_ptr<ps3_future_state<void>> s) : s_(move(s)) {}
    shared_future(future<void> &&f) noexcept : s_(move(f.s_)) {}
    void wait() const { unique_lock<mutex> l(s_->m); s_->cv.wait(l,[&]{return s_->ready;}); }
    void get() const { wait(); if(s_->error) rethrow_exception(s_->error); }
private:
    shared_ptr<ps3_future_state<void>> s_;
};

template<class> class packaged_task;
template<class R> class packaged_task<R()> {
public:
    template<class F> explicit packaged_task(F &&f) : fn_(forward<F>(f)), p_(make_shared<promise<R>>()) {}
    future<R> get_future() { return p_->get_future(); }
    void operator()() { try { if constexpr(is_void_v<R>) { fn_(); p_->set_value(); } else p_->set_value(fn_()); } catch(...) { p_->set_exception(current_exception()); } }
private:
    function<R()> fn_;
    shared_ptr<promise<R>> p_;
};

class thread {
    struct task_base { virtual ~task_base(){}; virtual void run()=0; };
    template<class F,class... A> struct task final : task_base {
        F f; tuple<A...> args;
        template<class FF,class... AA> task(FF&& fn,AA&&... a):f(forward<FF>(fn)),args(forward<AA>(a)...){}
        void run() override { apply(move(f),move(args)); }
    };
public:
    thread() noexcept : joinable_(false) {}
    template<class F,class... A> explicit thread(F&& f,A&&... a):joinable_(false) {
        using T=task<decay_t<F>,decay_t<A>...>;
        auto *p=new T(forward<F>(f),forward<A>(a)...);
        if(pthread_create(&t_,nullptr,&entry,p)!=0){delete p;throw runtime_error("pthread_create failed");}
        joinable_=true;
    }
    ~thread(){ if(joinable_) std::terminate(); }
    thread(const thread&)=delete;
    thread& operator=(const thread&)=delete;
    thread(thread&& o) noexcept:t_(o.t_),joinable_(o.joinable_){o.joinable_=false;}
    thread& operator=(thread&& o) noexcept { if(this!=&o){if(joinable_)std::terminate();t_=o.t_;joinable_=o.joinable_;o.joinable_=false;}return *this; }
    bool joinable() const noexcept{return joinable_;}
    void join(){if(!joinable_)throw system_error();pthread_join(t_,nullptr);joinable_=false;}
    void detach(){if(!joinable_)throw system_error();pthread_detach(t_);joinable_=false;}
    static unsigned hardware_concurrency() noexcept{return 2;}
private:
    struct system_error:logic_error{system_error():logic_error("invalid thread operation"){}};
    static void *entry(void *v){unique_ptr<task_base> p(static_cast<task_base*>(v));p->run();return nullptr;}
    pthread_t t_;
    bool joinable_;
};

} // namespace std
