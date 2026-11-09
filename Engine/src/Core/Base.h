#pragma once
#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace Stardust {

using u8 = uint8_t;   using u16 = uint16_t; using u32 = uint32_t; using u64 = uint64_t;
using i8 = int8_t;    using i16 = int16_t;  using i32 = int32_t;  using i64 = int64_t;
using f32 = float;    using f64 = double;

template<typename T> using Ref = std::shared_ptr<T>;
template<typename T> using Scope = std::unique_ptr<T>;
template<typename T, typename... A> Ref<T> MakeRef(A&&... a) { return std::make_shared<T>(std::forward<A>(a)...); }
template<typename T, typename... A> Scope<T> MakeScope(A&&... a) { return std::make_unique<T>(std::forward<A>(a)...); }

#define SD_BIT(x) (1u << (x))
#define SD_ENUM_FLAGS(E) \
    inline constexpr E operator|(E a, E b) { return E(u32(a) | u32(b)); } \
    inline constexpr E operator&(E a, E b) { return E(u32(a) & u32(b)); } \
    inline constexpr bool Any(E a) { return u32(a) != 0; }

} // namespace Stardust
