#ifndef META_VISIT_HPP
#define META_VISIT_HPP

#include <variant>
#include "meta.hpp"

namespace meta {

template<typename Var, typename...Fs>
decltype(auto) Visit(Var&& v, Fs&&...fs) {
    return std::visit(overloaded{std::forward<Fs>(fs)...}, std::forward<Var>(v));
}

}

#endif //META_VISIT_HPP