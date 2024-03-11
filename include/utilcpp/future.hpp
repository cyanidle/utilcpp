#ifndef UTILCPP_PROMISE_HPP
#define UTILCPP_PROMISE_HPP

#include <cassert>
#include <atomic>
#include "move_func.hpp"
#include "meta.hpp"
#include "defer.hpp"

#define MV(x) x=std::move(x)

namespace std {
template<typename T> class future;
template<typename T> class promise;
}

namespace util
{

template<typename T> struct Promise;
template<typename T> struct is_promise : std::false_type {};
template<typename T> struct is_promise<Promise<T>> : std::true_type {};
template<typename T> struct Future;
template<typename T> struct is_future : std::false_type {};
template<typename T> struct is_future<Future<T>> : std::true_type {};
template<typename T> struct FutureStateData;
template<typename T> struct FutureState;
template<typename T> struct FutureResult;

template<typename T> struct FutureResult
{
    explicit operator bool() const noexcept {
        return bool(res);
    }
    T* Result() const noexcept {
        return res;
    }
    [[noreturn]] void Rethrow() {
        assert(exc && "Always check for error before Rethrow()");
        std::rethrow_exception(std::move(exc));
    }
    std::exception_ptr Exception() const noexcept {
        return exc;
    }
    FutureResult(T* res) noexcept {
        this->res = res;
    }
    FutureResult(std::exception_ptr exc) noexcept :
        exc(std::move(exc)) {}
    FutureResult(FutureResult&&) noexcept = default;
    FutureResult(const FutureResult&) noexcept = default;
private:
    mutable std::exception* unpacked = {};
    std::exception_ptr exc = {};
    T* res = {};
};

template<typename T>
struct FutureState {
    FutureStateData<T>* data;
    FutureState(FutureStateData<T>* st = nullptr) : data(st) {
        if(data)
            data->AddRef();
    }
    operator bool() const noexcept {
        return bool(data);
    }
    FutureStateData<T>* operator->() const noexcept {
        return data;
    }
    FutureState(const FutureState& o) noexcept {
        data = o.data;
        if(data) data->AddRef();
    }
    FutureState(FutureState&& o) noexcept :
        data(std::exchange(o.data, nullptr))
    {}
    FutureState& operator=(const FutureState& o) noexcept {
        if (this != &o) {
            if(data) data->Unref();
            data = o.data;
            if(data) data->AddRef();
        }
        return *this;
    }
    FutureState& operator=(FutureState&& o) noexcept {
        std::swap(data, o.data);
        return *this;
    }
    ~FutureState() {
        if(data) data->Unref();
    }
};

template<typename T>
struct [[nodiscard]] Future
{
public:
    using type = T;
    Future() = default;
    Future(Future const&) = delete;
    Future(Future&& o) noexcept : state(std::exchange(o.state, nullptr)) {}
    Future& operator=(Future&& o) noexcept {
        std::swap(state, o.state);
        return *this;
    }
    bool IsValid() const noexcept {return state;}
    template<typename Cb>
    auto Then(Cb cb) noexcept;
    template<typename Guard, typename Cb>
    auto ThenIf(Guard g, Cb cb) noexcept {
        checkState();
        state.data->Guard = std::move(g);
        return this->Then(std::move(cb));
    }
    template<typename Cb>
    void Catch(Cb callback) noexcept {
        this->Then([MV(callback)](FutureResult<T> res){
            if (!res) {
                if constexpr(std::is_invocable_v<Cb, std::exception&>) {
                    try {res.Rethrow();} catch(std::exception& e) {callback(e);}
                } else {
                    callback(res.Exception());
                }
            }
        });
    }
    FutureState<T> TakeState() {
        return std::exchange(state, FutureState<T>{});
    }
    FutureStateData<T>* PeekState() {
        return state.data;
    }
    Future(FutureState<T> state) noexcept : state(state) {}
protected:
    void checkState() {
        assert(state && "invalid future resolved");
    }
    FutureState<T> state = {};
};

template<typename T>
struct PromiseBase {
    template<typename U>
    using if_exception = std::enable_if_t<std::is_base_of_v<std::exception, std::decay_t<U>>, int>;
    using type = T;
    using State = FutureState<T>;
    using Result = FutureResult<T>;
    PromiseBase() : PromiseBase(State{new FutureStateData<T>}) {}
    PromiseBase(State state_) noexcept : state(std::move(state_)) {}
    PromiseBase(PromiseBase &&o) noexcept :
        state{std::exchange(o.state, nullptr)}
    {}
    PromiseBase& operator=(PromiseBase &&o) noexcept {
        std::swap(this->state, o.state);
        return *this;
    }
    Future<T> GetFuture() {
        checkValid();
        state->StartGetFuture();
        return Future<T>(state);
    }
    FutureState<T> TakeState() {
        return std::exchange(state, FutureState<T>{});
    }
    FutureStateData<T>* PeekState() {
        return state.data;
    }
    void Resolve(std::exception_ptr exc) const noexcept {
        checkValid();
        state->StartExcept();
        if (state->Callback)
            state->DoCallback(Result{std::move(exc)});
        else
            state->SetError(std::move(exc));
    }
    template<typename U, if_exception<U> = 1>
    void Resolve(U&& exc) const noexcept {
        Resolve(std::make_exception_ptr(std::forward<U>(exc)));
    }
    bool IsValid() const noexcept {
        return state && !state.data->IsResolved();
    }
    ~PromiseBase() {
        if (util_Unlikely(IsValid() && state.data->IsFutureTaken())) {
            Resolve(TimeoutError{});
        }
    }
protected:
    void checkValid() const {
        if (util_Unlikely(!state)) {
            throw std::runtime_error("invalid Promise<T> accessed");
        }
    }
    State state;
};

template<typename T>
struct Promise : PromiseBase<T>
{
    using PromiseBase<T>::Resolve;
    using PromiseBase<T>::PromiseBase;
    using State = FutureState<T>;
    using Result = FutureResult<T>;
    void Resolve(T value) const noexcept {
        this->checkValid();
        this->state->StartResolve();
        if (this->state->Callback)
            this->state->DoCallback(Result{&value});
        else
            this->state->SetResult(&value);
    }
    void Resolve(Result res) const noexcept {
        if (auto r = res.Result()) {
            Resolve(std::move(*r));
        } else {
            Resolve(res.Exception());
        }
    }
    template<typename U>
    void operator()(U&& val) const noexcept {
        Resolve(std::forward<U>(val));
    }
};

struct FutureStateBase {
    FutureStateBase() noexcept = default;
    FutureStateBase(const FutureStateBase&) = delete;
    FutureStateBase(FutureStateBase&&) = delete;
    enum Flags {
        result_valid = 1,
        future_taken = 2,
        err_set = 4,
    };
    bool IsResolved() const noexcept {
        return Flags & (err_set | result_valid);
    }
    bool IsFutureTaken() const noexcept {
        return Flags & future_taken;
    }
    void StartGetFuture();
    void StartExcept();
    void StartResolve();

    bool IsError() const noexcept {
        return Flags & err_set;
    }
    void SetError(std::exception_ptr exc) noexcept {
        std::swap(error, exc);
    }
    std::exception_ptr GetError() noexcept {
        std::exception_ptr temp;
        std::swap(temp, error);
        return temp;
    }
    void AddRef() noexcept {
        refs.fetch_add(1, std::memory_order_relaxed);
    }

    int Flags = 0;
protected:
    std::exception_ptr error = {};
    std::atomic<int> refs = 0;
};

namespace det {
template<typename U> struct _is_small :
                   std::bool_constant<sizeof(U) <= sizeof(U*) && std::is_trivial_v<U>> {};
template<> struct _is_small<void> : std::true_type {};
inline constexpr std::true_type defGuard() noexcept {
    return {};
}
}

template<typename T>
struct FutureStateData : FutureStateBase {
    static constexpr bool is_small = det::_is_small<T>::value;
    FutureStateData() noexcept {}
    MoveFunc<bool()> Guard = {det::defGuard};
    MoveFunc<void(FutureResult<T>)> Callback {};
    void DoCallback(FutureResult<T> res) {
        if (Guard())
            Callback(std::move(res));
    }
    T* GetResult() noexcept {
        if constexpr (!is_small) return result;
        else return (Flags & result_valid) ? reinterpret_cast<T*>(&result) : nullptr;
    }
    void SetResult(T* res) {
        if constexpr (std::is_void_v<T>) {
            result = res;
        } else if constexpr (is_small) {
            new (&result) T{std::move(*res)};
        } else {
            result = new T{std::move(*res)};
        }
    }
    void Unref() noexcept {
        if (refs.fetch_sub(1, std::memory_order_release) == 1) {
            std::atomic_thread_fence(std::memory_order_acquire);
            delete this;
        }
    }
    ~FutureStateData() {
        if constexpr(!is_small && !std::is_void_v<T>)
            if (result)
                delete result;
    }
protected:
    T* result = {}; //maybe an erased small value => use Get/SetResult()
};

// callback for any number of args, which does nothing
constexpr inline auto IgnoreAll() noexcept {
    return [](auto&&...) noexcept {};
}

template<typename T>
std::future<T> ToStdFuture(Future<T>&& fut) {
    auto prom = std::promise<T>();
    auto f = prom.get_future();
    fut.Then([p=std::move(prom)](FutureResult<T> res) mutable {
        if (auto ok = res.Result()) {
            if constexpr(std::is_void_v<T>) p.set_value();
            else p.set_value(std::move(*ok));
            (void)ok;
        } else {
            p.set_exception(std::move(res.Exception()));
        }
    });
    return f;
}

template<typename T>
Future<T> FutureFromResult(T value) {
    auto state = new FutureStateData<T>();
    state->StartResolve();
    state->SetResult(&value);
    return Future<T>(state);
}

inline Future<void> FutureFromVoid() {
    auto state = new FutureStateData<void>();
    state->StartResolve();
    state->SetResult(reinterpret_cast<void*>(1));
    return Future<void>(state);
}

template<typename T>
Future<T> FutureFromException(std::exception_ptr exc) {
    auto state = new FutureStateData<T>();
    state->StartExcept();
    state->SetError(std::move(exc));
    return Future<T>(state);
}

template<typename T, typename Exc, typename...A>
Future<T> FutureFromException(A...a) {
    return FutureFromException<T>(std::make_exception_ptr(Exc(std::move(a)...)));
}

namespace det {
template<typename T> struct strip_fut {using type = T;};
template<typename T> struct strip_fut<Future<T>> {using type = T;};
}

template<typename T>
template<typename Cb>
auto Future<T>::Then(Cb cb) noexcept {
    checkState();
    defer finalize([this]{
        if (auto r = state.data->GetResult()) {
            state.data->DoCallback(FutureResult<T>{r});
        } else if (auto e = state.data->GetError()) {
            state.data->DoCallback(FutureResult<T>{std::move(e)});
        }
        state = {};
    });
    if constexpr (is_promise<Cb>::value) {
        using cb_type = typename Cb::type;
        using foreign_result = FutureResult<cb_type>;
        if constexpr (std::is_same_v<cb_type, T>) {
            this->state.data->Callback = std::move(cb);
        } else {
            auto pass = cb.TakeState();
            this->state.data->Callback =
                [MV(pass)](FutureResult<T> res) mutable noexcept
            {
                if (res) {
                    cb_type passed = std::move(*res.Result());
                    pass(foreign_result{&passed});
                } else {
                    pass(foreign_result{std::move(res.Exception())});
                }
            };
        }
    } else if constexpr (std::is_invocable_v<Cb, FutureResult<T>>) {
        using rawResT = std::invoke_result_t<Cb, FutureResult<T>>;
        using resT = typename det::strip_fut<rawResT>::type;
        if constexpr (std::is_void_v<rawResT>) {
            this->state.data->Callback = std::move(cb);
        } else {
            Promise<resT> chain;
            auto fut = chain.GetFuture();
            this->state.data->Callback =
                [MV(cb), MV(chain)](FutureResult<T> res) mutable noexcept
            {
                try {
                    if constexpr (is_future<rawResT>::value) {
                        cb(std::move(res)).Then(std::move(chain));
                    } else {
                        chain.Resolve(cb(std::move(res)));
                    }
                } catch (...) {chain.Resolve(std::current_exception());}
            };
            return fut;
        }
    } else if constexpr (!std::is_void_v<T> && std::is_invocable_v<Cb, T>) {
        using rawResT = std::invoke_result_t<Cb, T>;
        using resT = typename det::strip_fut<rawResT>::type;
        Promise<resT> chain;
        auto fut = chain.GetFuture();
        this->state.data->Callback =
            [MV(cb), MV(chain)](FutureResult<T> res) mutable noexcept
        {
            if (!res) {
                chain.Resolve(res.Exception());
            } else try {
                auto& pass = *res.Result();
                if constexpr (std::is_void_v<rawResT>) {
                    cb(std::move(pass));
                    chain.Resolve();
                } else if constexpr (is_future<rawResT>::value) {
                    cb(std::move(pass)).Then(std::move(chain));
                } else {
                    chain.Resolve(cb(std::move(pass)));
                }
            } catch (...) {chain.Resolve(std::current_exception());}
        };
        return fut;
    } else if constexpr (std::is_void_v<T> && std::is_invocable_v<Cb>) {
        using rawResT = std::invoke_result_t<Cb>;
        using resT = typename det::strip_fut<rawResT>::type;
        Promise<resT> chain;
        auto fut = chain.GetFuture();
        this->state.data->Callback =
            [MV(cb), MV(chain)](FutureResult<T> res) mutable noexcept
        {
            if (!res) {
                chain.Resolve(res.Exception());
            } else try {
                if constexpr (std::is_void_v<rawResT>) {
                    cb(); chain.Resolve();
                } else if constexpr (is_future<rawResT>::value) {
                    cb().Then(std::move(chain));
                } else {
                    chain.Resolve(cb());
                }
            } catch (...) {chain.Resolve(std::current_exception());}
        };
        return fut;
    } else {
        static_assert(always_false_v<Cb>, "Invalid callback => must accept Result<T> or T");
    }
}

template<>
struct Promise<void> : PromiseBase<void> {
    using PromiseBase<void>::Resolve;
    using PromiseBase<void>::PromiseBase;
    using PromiseBase<void>::operator=;
    void Resolve() const noexcept {
        auto res = reinterpret_cast<void*>(1);
        this->checkValid();
        this->state->StartResolve();
        if (this->state->Callback)
            this->state->DoCallback(Result{res});
        else
            this->state->SetResult(res);
    }
    void operator()() const noexcept {
        Resolve();
    }
    void Resolve(Result res) const noexcept {
        if (res.Result()) {
            Resolve();
        } else {
            Resolve(res.Exception());
        }
    }
    template<typename U>
    void operator()(U&& val) const noexcept {
        Resolve(std::forward<U>(val));
    }
};


inline void FutureStateBase::StartGetFuture() {
    if (util_Unlikely(Flags & future_taken)) {
        assert(false && "future taken");
        throw std::runtime_error("GetFuture() already called");
    }
    Flags |= future_taken;
}

inline void FutureStateBase::StartExcept() {
    if (util_Unlikely(IsResolved())) {
        assert(false && "double resolve");
        throw std::runtime_error("Promise already resolved (Attempt to resolve Error)");
    }
    Flags |= err_set;
}

inline void FutureStateBase::StartResolve() {
    if (util_Unlikely(IsResolved())) {
        assert(false && "double resolve");
        throw std::runtime_error("Promise already resolved (Attempt to resolve Result)");
    }
    Flags |= result_valid;
}

} //util

#endif // UTILCPP_PROMISE_HPP
