#ifndef UTILCPP_META_HPP
#define UTILCPP_META_HPP

#include <type_traits>
#include <map>
#include <vector>
#include <string_view>

namespace util
{

#ifdef __GNUC__
#define util_Likely(x)       __builtin_expect(!!(x), 1)
#define util_Unlikely(x)     __builtin_expect(!!(x), 0)
#else
#define util_Likely(x)       (x)
#define util_Unlikely(x)     (x)
#endif

#ifdef __GNUC__ // GCC, Clang, ICC
#define util_AlwaysInline [[gnu::always_inline]]
#define util_Unreachable() __builtin_unreachable();
#elif defined(_MSC_VER) // MSVC
#define util_AlwaysInline __forceinline
#define util_Unreachable() __assume(false);
#else
#define util_AlwaysInline
#define util_Unreachable()
#endif

template<typename...T> struct TypeList{};
template<typename T> constexpr bool always_false_v = false;

template<typename T, typename = void>
struct is_assoc_container : std::false_type {};
template<typename T>
struct is_assoc_container<T, std::void_t<
    typename T::mapped_type,
    typename T::key_type,
    decltype(std::declval<T>().insert(
        std::declval<typename T::key_type>(),
        std::declval<typename T::mapped_type>()
    ))
>>: std::true_type {};

template<typename T>
constexpr bool is_assoc_container_v = is_assoc_container<T>::value;

template<typename T, typename = void>
struct is_container : std::false_type {};
template<typename T>
struct is_container<T, std::void_t<
    typename T::value_type,
    decltype(std::declval<T>().push_back(
         std::declval<typename T::value_type>()
    ))
>>: std::true_type {};

template<typename T>
constexpr bool is_container_v = is_container<T>::value;

template<typename T>
constexpr bool is_string_like_v =
    std::is_base_of_v<std::string_view, T> || std::is_base_of_v<std::string, T>;

static_assert(is_container_v<std::vector<int>>);
static_assert(!is_assoc_container_v<std::vector<int>>);

static_assert(!is_container_v<std::map<int, int>>);
static_assert(is_assoc_container_v<std::map<int, int>>);

struct empty{};

template<typename T> using non_void_t = std::conditional_t<std::is_void_v<T>, empty, T>;

}

#endif //UTILCPP_META_HPP
