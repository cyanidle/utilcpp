#ifndef UTILCPP_DEFER_HPP
#define UTILCPP_DEFER_HPP

namespace util {

template<typename Fn>
struct [[nodiscard]] defer {
    defer(Fn f) noexcept(std::is_nothrow_copy_constructible_v<Fn>) :
        fn(std::move(f)) {}
    Fn fn;
    ~defer() noexcept(std::is_nothrow_invocable_v<Fn>) {fn();}
};
template<typename Fn> defer(Fn) -> defer<Fn>;

} //util

#endif //UTILCPP_DEFER_HPP