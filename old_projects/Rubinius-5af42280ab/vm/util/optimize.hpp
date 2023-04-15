// Some typical optimization tricks.

// This one provides the compiler about branch hints, so it
// keeps the normal case fast.
#ifdef __GNUC__

/**
 * __builtin_expect() 是个内置函数，它负责给编译器提供分支预测
 * __builtin_expect() 不是C++的标准函数，而是gcc的扩展功能
 * 以unlikely()为例，它就会提示编译器"传递给参数的条件结果几乎全为假"。这样就能令编译器最优化，使其在条件为"假"时高速运作
 */
#define likely(x)       __builtin_expect((long int)(x),1)
#define unlikely(x)     __builtin_expect((long int)(x),0)

#else

#define likely(x) x
#define unlikely(x) x

#endif

