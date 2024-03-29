#ifndef FUT_PROMISE_HPP
#define FUT_PROMISE_HPP

#include <cassert>
#include <atomic>
#include "move_func.hpp"

#define MV(x) x=std::move(x)

namespace std {
template<typename T> class future;
template<typename T> class promise;
}

namespace fut
{

struct TimeoutError : public std::exception {
    const char* what() const noexcept override {
        return "Timeout Error";
    }
};

template<typename T> struct Promise;
template<typename T> struct is_promise : std::false_type {};
template<typename T> struct is_promise<Promise<T>> : std::true_type {};
template<typename T> struct Future;
template<typename T> struct is_future : std::false_type {};
template<typename T> struct is_future<Future<T>> : std::true_type {};
template<typename T> struct FutureStateData;
template<typename T> struct FutureState;
template<typename T> struct FutureResult;

namespace det {
template<typename U>
using if_exception = std::enable_if_t<std::is_base_of_v<std::exception, std::decay_t<U>>, int>;
template<typename U> struct _is_small :
                   std::bool_constant<sizeof(U) <= sizeof(U*) && std::is_trivial_v<U>> {};
template<> struct _is_small<void> : std::true_type {};
inline constexpr std::true_type defGuard() noexcept {return {};}
}

template<typename T> struct FutureResult
{
    explicit operator bool() const noexcept {
        return bool(res);
    }
    T* Result() const noexcept {
        return res;
    }
    auto MoveResult() noexcept {
        if (!res) {
            assert(!"invalid result moved");
            std::abort();
        }
        return std::move(*res);
    }
    [[noreturn]] void Rethrow() {
        assert(exc && "Always check for error before Rethrow()");
        std::rethrow_exception(std::move(exc));
    }
    std::exception_ptr Exception() const noexcept {
        return exc;
    }
    std::exception_ptr MoveException() noexcept {
        return std::move(exc);
    }
    FutureResult(T* result) noexcept : res(result) {}
    FutureResult(std::exception_ptr exc) noexcept : exc(std::move(exc)) {}
    FutureResult(FutureResult&&) noexcept = default;
    FutureResult(const FutureResult&) noexcept = default;
private:
    std::exception_ptr exc = {};
    T* res = {};
};

template<typename T> struct FutureStateData {
    static constexpr bool is_small = det::_is_small<T>::value;
    FutureStateData() noexcept {}
    FutureStateData(const FutureStateData&) = delete;
    enum StateFlags {
        resolved = 1,
        future_taken = 2
    };
    MoveFunc<bool()> Guard = {det::defGuard};
    void SetCallback(MoveFunc<void(FutureResult<T>)> cb) noexcept {
        if (Flags & resolved) {
            if (!Guard()) return;
            if (error) cb({std::move(error)});
            else if constexpr (is_small) cb({reinterpret_cast<T*>(&result)});
            else cb({result});
        } else {
            callback = std::move(cb);
        }
    }
    void AddOnce(StateFlags flag) {
        if (Flags & flag) {
            assert(!bool("invalid promise (double resolve or GetFuture())"));
            std::abort();
        }
        Flags |= flag;
    }
    void Resolve(FutureResult<T> res) noexcept {
        AddOnce(resolved);
        Flags |= resolved;
        if (callback) {
            if (Guard()) callback(std::move(res));
        } else if (auto r = res.Result()) {
            if constexpr (std::is_void_v<T>)
                result = r;
            else if constexpr (is_small)
                new (&result) T{std::move(*r)};
            else
                result = new T{std::move(*r)};
        } else {
            error = res.MoveException();
        }
    }
    void Unref() noexcept {
        if (refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete this;
        }
    }
    void AddRef() noexcept {
        refs.fetch_add(1, std::memory_order_acq_rel);
    }
    ~FutureStateData() {
        if constexpr(!is_small) {
            if (result) {delete result;}
        }
    }
    std::underlying_type_t<StateFlags> Flags = {};
protected:
    std::exception_ptr error = {};
    std::atomic<int> refs = 0;
    MoveFunc<void(FutureResult<T>)> callback {};
    T* result = {}; //maybe an erased small value => use Get/SetResult()
};

template<typename T>
struct FutureState {
    FutureStateData<T>* data;
    FutureState(FutureStateData<T>* st = nullptr) : data(st) {
        if(data) data->AddRef();
    }
    operator bool() const noexcept {
        return bool(data);
    }
    FutureStateData<T>* operator->() const noexcept {
        return data;
    }
    FutureState(const FutureState& o) noexcept : data(o.data) {
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
    using value_type = T;
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
    void Catch(Cb cb) noexcept {
        Then([MV(cb)](FutureResult<T> res) noexcept {
            if (res) return;
            if constexpr(std::is_invocable_v<Cb, std::exception&>) {
                try {res.Rethrow();} catch(std::exception& e) {cb(e);}
            } else {
                cb(res.MoveException());
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
        if (meta_Unlikely(!state)) {
            assert("invalid future resolved");
            throw std::runtime_error("invalid future resolved");
        }
    }
    FutureState<T> state = {};
};

template<typename Der, typename T> struct PromiseBase {
    void Resolve(T value) const noexcept {
        static_cast<const Der&>(*this).Resolve({&value});
    }
};

template<typename Der> struct PromiseBase<Der, void> {
    void Resolve() const noexcept {
        static_cast<const Der&>(*this).Resolve({reinterpret_cast<void*>(1)});
    }
};

template<typename T>
struct Promise : PromiseBase<Promise<T>, T> {
    using value_type = T;
    using PromiseBase<Promise<T>, T>::Resolve;
    Promise() : Promise(FutureState<T>{new FutureStateData<T>}) {}
    Promise(FutureState<T> state_) noexcept : state(std::move(state_)) {}
    Promise(Promise &&o) noexcept :
        state{std::exchange(o.state, nullptr)}
    {}
    Promise& operator=(Promise &&o) noexcept {
        std::swap(this->state, o.state);
        return *this;
    }
    Future<T> GetFuture() {
        checkValid();
        state->AddOnce(FutureStateData<T>::future_taken);
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
        state->Resolve({std::move(exc)});
    }
    void Resolve(FutureResult<T> res) const noexcept {
        checkValid();
        state->Resolve(std::move(res));
    }
    template<typename U, det::if_exception<U> = 1>
    void Resolve(U&& exc) const noexcept {
        Resolve(std::make_exception_ptr(std::forward<U>(exc)));
    }
    bool IsValid() const noexcept {
        return state && !(state->Flags & FutureStateData<T>::resolved);
    }
    template<typename U>
    void operator()(U&& v) const noexcept {
        this->Resolve(std::forward<U>(v));
    }
    ~Promise() {
        auto hasFut = IsValid() && (state->Flags & FutureStateData<T>::future_taken);
        if (meta_Unlikely(hasFut)) {
            Resolve(TimeoutError{});
        }
    }
protected:
    void checkValid() const {
        if (meta_Unlikely(!state)) {
            assert(false && "invalid promise");
            std::abort();
        }
    }
    FutureState<T> state;
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
        if ([[maybe_unused]] auto ok = res.Result()) {
            if constexpr(std::is_void_v<T>) p.set_value();
            else p.set_value(std::move(*ok));
        } else {
            p.set_exception(res.MoveException());
        }
    });
    return f;
}

template<typename T>
Future<T> FutureFromResult(T value) {
    auto state = new FutureStateData<T>();
    state->Resolve(&value);
    return Future<T>(state);
}

inline Future<void> FutureFromVoid() {
    auto state = new FutureStateData<void>();
    state->Resolve(reinterpret_cast<void*>(1));
    return Future<void>(state);
}

template<typename T>
Future<T> FutureFromException(std::exception_ptr exc) {
    auto state = new FutureStateData<T>();
    state->Resolve(std::move(exc));
    return Future<T>(state);
}

template<typename T, typename Exc>
Future<T> FutureFromException(Exc&& exc) {
    return FutureFromException<T>(std::make_exception_ptr(std::forward<Exc>(exc)));
}

namespace det {
template<typename T> struct strip_fut {using type = T;};
template<typename T> struct strip_fut<Future<T>> {using type = T;};
}

template<typename T>
template<typename Cb>
auto Future<T>::Then(Cb cb) noexcept {
    checkState();
    // todo: maybe more optimal forward for promises
    if constexpr (std::is_invocable_v<Cb, FutureResult<T>>) {
        using rawResT = std::invoke_result_t<Cb, FutureResult<T>>;
        using resT = typename det::strip_fut<rawResT>::type;
        if constexpr (std::is_void_v<rawResT>) {
            this->state->SetCallback(std::move(cb));
            state = {};
        } else {
            Promise<resT> chain;
            auto fut = chain.GetFuture();
            this->state->SetCallback([MV(cb), MV(chain)](FutureResult<T> res) mutable noexcept
            {
                try {
                    if constexpr (is_future<rawResT>::value) {
                        cb(std::move(res)).Then(std::move(chain));
                    } else {
                        chain.Resolve(cb(std::move(res)));
                    }
                } catch (...) {chain.Resolve(std::current_exception());}
            });
            state = {};
            return fut;
        }
    } else if constexpr (!std::is_void_v<T> && std::is_invocable_v<Cb, T>) {
        using rawResT = std::invoke_result_t<Cb, T>;
        using resT = typename det::strip_fut<rawResT>::type;
        Promise<resT> chain;
        auto fut = chain.GetFuture();
        this->state->SetCallback([MV(cb), MV(chain)](FutureResult<T> res) mutable noexcept
        {
            if (!res) {
                chain.Resolve(res.MoveException());
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
        });
        state = {};
        return fut;
    } else if constexpr (std::is_void_v<T> && std::is_invocable_v<Cb>) {
        using rawResT = std::invoke_result_t<Cb>;
        using resT = typename det::strip_fut<rawResT>::type;
        Promise<resT> chain;
        auto fut = chain.GetFuture();
        this->state->SetCallback([MV(cb), MV(chain)](FutureResult<T> res) mutable noexcept
        {
            if (!res) {
                chain.Resolve(res.MoveException());
            } else try {
                    if constexpr (std::is_void_v<rawResT>) {
                        cb(); chain.Resolve();
                    } else if constexpr (is_future<rawResT>::value) {
                        cb().Then(std::move(chain));
                    } else {
                        chain.Resolve(cb());
                    }
                } catch (...) {chain.Resolve(std::current_exception());}
        });
        state = {};
        return fut;
    } else {
        static_assert(always_false<Cb>, "Invalid callback => must accept Result<T> or T");
    }
}

} //fut

#endif // FUT_PROMISE_HPP
