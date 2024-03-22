#ifndef FUT_GATHER_HPP
#define FUT_GATHER_HPP

#include "future.hpp"
#include <vector>
#include <memory>

namespace fut
{

template<typename T>
using Futures = std::vector<Future<T>>;

namespace detail
{


template<typename...Args>
struct GatherCtx {
    std::tuple<non_void_t<Args>...> results {};
    size_t doneCount = {};
    Promise<std::tuple<non_void_t<Args>...>> setter {};
};

template<typename...Args>
using SharedGatherCtx = std::shared_ptr<GatherCtx<Args...>>;

template<size_t idx, typename T, typename...Args>
void handleSingleProm(SharedGatherCtx<Args...> ctx, Future<T> prom)
{
    prom.Then([ctx](auto res){
        if(!ctx->setter.IsValid())
            return;
        if (auto&& err = res.Exception()) {
            ctx->setter.Resolve(std::move(err));
        } else {
            if constexpr (!std::is_void_v<T>)
                std::get<idx>(ctx->results) = std::move(*res.Result());
            if (++ctx->doneCount == sizeof...(Args)) {
                ctx->setter.Resolve(std::move(ctx->results));
            }
        }
    });
}

template<size_t...idx, typename...Args>
void callGatherHandlers(SharedGatherCtx<Args...> ctx,
                        std::index_sequence<idx...>,
                        Future<Args>...proms)
{
    (handleSingleProm<idx>(ctx, std::move(proms)), ...);
}

}

template<typename...Args>
Future<std::tuple<non_void_t<Args>...>> Gather(Future<Args>...futs)
{
    static_assert(sizeof...(Args), "Empty Promise List");
    using Ctx = detail::GatherCtx<Args...>;
    auto ctx = std::make_shared<Ctx>(Ctx{});
    auto gathered = ctx->setter.GetFuture();
    callGatherHandlers(std::move(ctx), std::index_sequence_for<Args...>{}, std::move(futs)...);
    return gathered;
}

template<typename T>
auto Gather(std::vector<Future<T>> futs)
{
    using promT = std::conditional_t<std::is_void_v<T>, void, std::vector<T>>;
    using resultsT = std::conditional_t<std::is_void_v<T>, empty, std::vector<T>>;
    if (futs.empty()) {
        if constexpr (!std::is_void_v<T>)
            return FutureFromResult(promT{});
        else
            return FutureFromVoid();
    }
    struct Base {
        resultsT results;
    };
    struct Ctx : Base {
        Promise<promT> prom;
        size_t left;
    };
    auto ctx = std::make_shared<Ctx>();
    ctx->left = futs.size();
    auto final = ctx->prom.GetFuture();
    for (auto& f: futs) {
        f.Then([ctx](auto res){
            if (!ctx->prom.IsValid())
                return;
            if (res) {
                if constexpr (!std::is_void_v<T>)
                    ctx->results.emplace_back(std::move(*res.Result()));
                if (!--ctx->left) {                    
                    if constexpr (!std::is_void_v<T>)
                        ctx->prom.Resolve(std::move(ctx->results));
                    else
                        ctx->prom.Resolve();
                }
            } else {
                ctx->prom.Resolve(res.Exception());
            }
        });
    }
    return final;
}

} //fut

#endif //FUT_GATHER_HPP
