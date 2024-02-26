#ifndef UTILCPP_META_HPP
#define UTILCPP_META_HPP

#include <type_traits>
#include <map>
#include <vector>
#include <string_view>

namespace util
{

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

}

#endif //UTILCPP_META_HPP
