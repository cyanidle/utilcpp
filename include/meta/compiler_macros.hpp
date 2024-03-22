#ifdef __GNUC__
#define meta_Likely(x)       __builtin_expect(!!(x), 1)
#define meta_Unlikely(x)     __builtin_expect(!!(x), 0)
#else
#define meta_Likely(x)       (x)
#define meta_Unlikely(x)     (x)
#endif

#ifdef __GNUC__ // GCC, Clang, ICC
#define meta_alwaysInline [[gnu::always_inline]]
#define meta_Unreachable() __builtin_unreachable();
#elif defined(_MSC_VER) // MSVC
#define meta_alwaysInline __forceinline
#define meta_Unreachable() __assume(false);
#else
#define meta_Unreachable()
#endif

// disable min() max() on windows
#define meta_NO_MACRO
