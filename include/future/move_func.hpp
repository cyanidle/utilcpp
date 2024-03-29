#ifndef FUT_CALL_ONCE_HPP
#define FUT_CALL_ONCE_HPP

#include <cassert>
#include <stdexcept>
#include <utility>
#include <cstddef>
#include <type_traits>
#include "meta/meta.hpp"

namespace fut {

using namespace meta;

static constexpr auto DEFAULT_SOO = sizeof(void*) * 3;

struct InvalidMoveFuncCall : public std::exception {
    const char* what() const noexcept {
        return "Invalid MoveFunc Call";
    }
};

namespace call {

template<size_t SOO> union Storage {
    alignas(std::max_align_t) char _small[SOO];
    void* _big;
};

template<size_t SOO, typename Ret, typename...Args>
struct Manager {
    virtual void destroy(Storage<SOO>* stor) noexcept = 0;
    virtual void move(Storage<SOO>* from, Storage<SOO>* to) noexcept = 0;
    virtual Ret call(Storage<SOO>* stor, Args...a) = 0;
protected:
    ~Manager() = default;
};

template<typename Fn, size_t SOO, typename Ret, typename...Args>
struct ManagerImpl final : Manager<SOO, Ret, Args...> {
    meta_alwaysInline static Fn& get(Storage<SOO>* stor) noexcept {
        if constexpr (sizeof(Fn) > SOO)
            return *static_cast<Fn*>(stor->_big);
        else
            return *std::launder(reinterpret_cast<Fn*>(stor->_small));
    }
    void destroy(Storage<SOO>* stor) noexcept final {
        if constexpr (sizeof(Fn) > SOO)
            delete &get(stor);
        else
            get(stor).~Fn();
    }
    void move(Storage<SOO>* from, Storage<SOO>* to) noexcept final {
        if constexpr (sizeof(Fn) > SOO) {
            to->_big = std::exchange(from->_big, nullptr);
        } else {
            new (to->_small) Fn(std::move(get(from)));
            get(from).~Fn();
        }
    }
    Ret call(Storage<SOO>* stor, Args...a) final {
        return get(stor)(std::forward<Args>(a)...);
    }
};
} //call

template<typename Ret, typename...Args>
struct FuncSig {
    using R = Ret;
    using A = meta::TypeList<Args...>;
};

template<typename Sig, size_t SOO = DEFAULT_SOO> class MoveFunc;

template<typename Ret, typename...Args, size_t SOO>
class MoveFunc<Ret(Args...), SOO>
{
    call::Storage<SOO> stor = {};
    call::Manager<SOO, Ret, Args...>* manager = nullptr;
public:
    using Sig = FuncSig<Ret, Args...>;
    static constexpr auto SOO_size = SOO;
    MoveFunc() noexcept = default;
    template<typename Fn>
    MoveFunc(Fn f) {
        static_assert(std::is_move_constructible_v<Fn>);
        static_assert(std::is_invocable_v<Fn, Args&&...>);
        static_assert(std::is_convertible_v<std::invoke_result_t<Fn, Args&&...>, Ret>);
        static call::ManagerImpl<Fn, SOO, Ret, Args...> impl;
        manager = &impl;
        if constexpr (sizeof(Fn) > SOO) {
            stor._big = new Fn(std::move(f));
        } else {
            new (stor._small) Fn(std::move(f));
        }
    }
    explicit operator bool() const noexcept {
        return manager;
    }
    meta_alwaysInline Ret operator()(Args...a) {
        if (!manager) {
            throw InvalidMoveFuncCall();
        }
        return manager->call(&stor, std::forward<Args>(a)...);
    }
    MoveFunc(MoveFunc&& oth) noexcept {
        moveIn(oth);
    }
    MoveFunc& operator=(MoveFunc&& oth) noexcept {
        // cannot just swap => cannot swap small storage for
        // non-trivially movable types (pinned-like)
        if (this != &oth) {
            deref();
            moveIn(oth);
        }
        return *this;
    }
    ~MoveFunc() {
        deref();
    }
private:
    void deref() noexcept {
        if (manager) {
            manager->destroy(&stor);
        }
    }
    void moveIn(MoveFunc& oth) noexcept {
        manager = std::exchange(oth.manager, nullptr);
        if (manager) {
            manager->move(&oth.stor, &stor);
        }
    }
};

template<typename Ret, typename...Args>
MoveFunc(Ret(*)(Args...)) -> MoveFunc<Ret(Args...)>;

} //fut

#endif // FUT_CALL_ONCE_HPP
