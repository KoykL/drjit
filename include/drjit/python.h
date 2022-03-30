#pragma once

#include <drjit/array.h>
#include <drjit/jit.h>
#include <drjit/dynamic.h>
#include <drjit/math.h>
#include <nanobind/nanobind.h>
#include <nanobind/stl/pair.h>

#if defined(DRJIT_PYTHON_BUILD)
#  define DRJIT_PYTHON_EXPORT DRJIT_EXPORT
#else
#  define DRJIT_PYTHON_EXPORT DRJIT_IMPORT
#endif

NAMESPACE_BEGIN(drjit)
NAMESPACE_BEGIN(detail)

using array_unop = void (*) (const void *, void *);
using array_unop_2 = void (*) (const void *, void *, void *);
using array_binop = void (*) (const void *, const void *, void *);
using array_ternop = void (*) (const void *, const void *, const void *, void *);
using array_richcmp = void (*) (const void *, const void *, int, void *);
using array_reduce_mask = void (*) (const void *, void *);
using array_id = uint32_t (*) (const void *);
using array_sized_init = void (*) (void *, size_t);

struct array_metadata {
    uint16_t is_vector     : 1;
    uint16_t is_complex    : 1;
    uint16_t is_quaternion : 1;
    uint16_t is_matrix     : 1;
    uint16_t is_tensor     : 1;
    uint16_t is_diff       : 1;
    uint16_t is_llvm       : 1;
    uint16_t is_cuda       : 1;
    uint16_t is_valid      : 1;
    uint16_t type          : 4;
    uint16_t ndim          : 3;
    uint8_t tsize_rel;  // type size as multiple of 'talign'
    uint8_t talign;     // type alignment
    uint8_t shape[4];
};

struct array_ops {
    size_t (*len)(const void *) noexcept;
    void (*init)(void *, size_t);

    array_sized_init op_zero, op_empty, op_arange;
    array_binop op_add;
    array_binop op_subtract;
    array_binop op_multiply;
    array_binop op_remainder;
    array_binop op_floor_divide;
    array_binop op_true_divide;
    array_binop op_and;
    array_binop op_or;
    array_binop op_xor;
    array_binop op_lshift;
    array_binop op_rshift;
    array_unop op_negative;
    array_unop op_invert;
    array_unop op_absolute;
    array_reduce_mask op_all;
    array_reduce_mask op_any;
    array_richcmp op_richcmp;
    array_ternop op_fma;
    array_ternop op_select;
    array_id op_index, op_index_ad;

    array_unop op_sqrt, op_cbrt;
    array_unop op_sin, op_cos, op_tan;
    array_unop op_sinh, op_cosh, op_tanh;
    array_unop op_asin, op_acos, op_atan;
    array_unop op_asinh, op_acosh, op_atanh;
    array_unop op_exp, op_exp2, op_log, op_log2;
    array_unop op_floor, op_ceil, op_round, op_trunc;
    array_unop op_rcp, op_rsqrt;
    array_binop op_min, op_max, op_atan2, op_ldexp;
    array_unop_2 op_sincos, op_sincosh, op_frexp;
};

struct array_supplement {
    array_metadata meta;
    PyTypeObject *value;
    PyTypeObject *mask;
    array_ops ops;
};

static_assert(sizeof(array_metadata) == 8);
// static_assert(sizeof(array_supplement) == 8 + sizeof(void *) * 4);

extern DRJIT_PYTHON_EXPORT const char *array_name(array_metadata meta);
extern DRJIT_PYTHON_EXPORT const nanobind::handle array_get(array_metadata meta);

extern DRJIT_PYTHON_EXPORT nanobind::handle
bind(const char *name, array_supplement &supp, const std::type_info *type,
     const std::type_info *value_type, void (*copy)(void *, const void *),
     void (*move)(void *, void *) noexcept, void (*destruct)(void *) noexcept,
     void (*type_callback)(PyTypeObject *) noexcept) noexcept;

extern DRJIT_PYTHON_EXPORT int array_init(PyObject *self, PyObject *args,
                                          PyObject *kwds);

template <typename T>
constexpr uint8_t size_or_zero_v = std::is_scalar_v<T> ? 0 : (uint8_t) array_size_v<T>;

template <typename T> void type_callback(PyTypeObject *tp) noexcept {
    namespace nb = nanobind;
    using Value = std::decay_t<decltype(std::declval<T>().entry(0))>;

    PySequenceMethods *sm = tp->tp_as_sequence;

    tp->tp_init = array_init;

    sm->sq_item = [](PyObject *o, Py_ssize_t i_) noexcept -> PyObject * {
        T *inst = nb::inst_ptr<T>(o);
        size_t i = (size_t) i_, size = inst->size();

        if (size == 1) // Broadcast
            i = 0;

        PyObject *result = nullptr;
        if (i < size) {
            nb::detail::cleanup_list cleanup(o);
            result = nb::detail::make_caster<Value>::from_cpp(
                         inst->entry(i),
                         nb::rv_policy::reference_internal,
                         &cleanup).ptr();
            cleanup.release();
        } else {
            PyErr_Format(
                PyExc_IndexError,
                "%s.__getitem__(): entry %zu is out of bounds (the array is of size %zu).",
                Py_TYPE(o)->tp_name, i, size);
        }
        return result;
    };

    sm->sq_ass_item = [](PyObject *o, Py_ssize_t i_, PyObject *value) noexcept -> int {
        T *inst = nb::inst_ptr<T>(o);
        size_t i = (size_t) i_, size = inst->size();
        if (i < size) {
            nb::detail::cleanup_list cleanup(o);
            nb::detail::make_caster<Value> in;
            bool success = in.from_python(
                value, (uint8_t) nb::detail::cast_flags::convert, &cleanup);
            if (success)
                inst->set_entry(i, in.operator Value & ());
            cleanup.release();

            if (success) {
                return 0;
            } else {
                PyErr_Format(
                    PyExc_TypeError,
                    "%s.__setitem__(): could not initialize element with a value of type '%s'.",
                    Py_TYPE(o)->tp_name, Py_TYPE(value)->tp_name);
                return -1;
            }
        } else {
            PyErr_Format(
                PyExc_IndexError,
                "%s.__setitem__(): entry %zu is out of bounds (the array is of size %zu).",
                Py_TYPE(o)->tp_name, i, size);
            return -1;
        }
    };
}

NAMESPACE_END(detail)


template <typename T> nanobind::class_<T> bind(const char *name = nullptr) {
    namespace nb = nanobind;
    using Mask = mask_t<T>;

    static_assert(
        std::is_copy_constructible_v<T> &&
        std::is_move_constructible_v<T> &&
        std::is_destructible_v<T>,
        "drjit::bind(): type must be copy/move constructible and destructible!"
    );

    constexpr uint8_t RelSize = (uint8_t) (sizeof(T) / alignof(T));

    static_assert(alignof(T) <= 0xFF && RelSize * alignof(T) == sizeof(T),
                  "drjit::bind(): type is too large!");

    detail::array_supplement s;

    s.meta.is_vector = T::IsVector;
    s.meta.is_complex = T::IsComplex;
    s.meta.is_quaternion = T::IsQuaternion;
    s.meta.is_matrix = T::IsMatrix;
    s.meta.is_tensor = T::IsTensor;
    s.meta.is_diff = T::IsDiff;
    s.meta.is_llvm = T::IsLLVM;
    s.meta.is_cuda = T::IsCUDA;
    s.meta.is_valid = 1;

    if (T::IsMask)
        s.meta.type = (uint16_t) VarType::Bool;
    else
        s.meta.type = (uint16_t) var_type_v<scalar_t<T>>;

    s.meta.ndim = (uint16_t) array_depth_v<T>;
    s.meta.tsize_rel = (uint8_t) RelSize;
    s.meta.talign = (uint8_t) alignof(T);
    s.meta.shape[0] = detail::size_or_zero_v<T>;
    s.meta.shape[1] = detail::size_or_zero_v<value_t<T>>;
    s.meta.shape[2] = detail::size_or_zero_v<value_t<value_t<T>>>;
    s.meta.shape[3] = detail::size_or_zero_v<value_t<value_t<value_t<T>>>>;

    memset(&s.ops, 0, sizeof(detail::array_ops));

    if constexpr (T::Size == Dynamic) {
        s.ops.len = [](const void *a) noexcept -> size_t {
            return ((const T *) a)->size();
        };

        s.ops.init = [](void *a, size_t size) {
            ((T *) a)->init_(size);
        };
    }

    if constexpr (T::Depth == 1 && T::Size == Dynamic) {
        s.ops.op_zero = [](void *a, size_t size) {
            new ((T *) a) T(zero<T>(size));
        };

        s.ops.op_empty = [](void *a, size_t size) {
            new ((T *) a) T(empty<T>(size));
        };

        s.ops.op_select = [](const void *a, const void *b, const void *c, void *d) {
            new ((T *) d) T(select(*(const mask_t<T> *) a, *(const T *) b, *(const T *) c));
        };

        if constexpr (T::IsArithmetic) {
            s.ops.op_add = [](const void *a, const void *b, void *c) {
                new ((T *) c) T(*(const T *) a + *(const T *) b);
            };

            s.ops.op_subtract = [](const void *a, const void *b, void *c) {
                new ((T *) c) T(*(const T *) a - *(const T *) b);
            };

            s.ops.op_multiply = [](const void *a, const void *b, void *c) {
                new ((T *) c) T(*(const T *) a * *(const T *) b);
            };

            s.ops.op_min = [](const void *a, const void *b, void *c) {
                new ((T *) c) T(drjit::min(*(const T *) a, *(const T *) b));
            };

            s.ops.op_max = [](const void *a, const void *b, void *c) {
                new ((T *) c) T(drjit::max(*(const T *) a, *(const T *) b));
            };

            s.ops.op_fma = [](const void *a, const void *b, const void *c, void *d) {
                new ((T *) d) T(fmadd(*(const T *) a, *(const T *) b, *(const T *) c));
            };

            if constexpr (std::is_signed_v<scalar_t<T>>) {
                s.ops.op_absolute = [](const void *a, void *b) {
                    new ((T *) b) T(((const T *) a)->abs_());
                };
                s.ops.op_negative = [](const void *a, void *b) {
                    new ((T *) b) T(-*(const T *) a);
                };
            }
        }

        if constexpr (T::IsIntegral) {
            s.ops.op_remainder = [](const void *a, const void *b, void *c) {
                new ((T *) c) T(*(const T *) a % *(const T *) b);
            };

            s.ops.op_floor_divide = [](const void *a, const void *b, void *c) {
                new ((T *) c) T(*(const T *) a / *(const T *) b);
            };

            s.ops.op_lshift = [](const void *a, const void *b, void *c) {
                new ((T *) c) T((*(const T *) a) << (*(const T *) b));
            };

            s.ops.op_rshift = [](const void *a, const void *b, void *c) {
                new ((T *) c) T((*(const T *) a) >> (*(const T *) b));
            };
        } else {
            s.ops.op_true_divide = [](const void *a, const void *b, void *c) {
                new ((T *) c) T(*(const T *) a / *(const T *) b);
            };
        }

        if constexpr (T::IsIntegral || T::IsMask) {
            s.ops.op_and = [](const void *a, const void *b, void *c) {
                new ((T *) c) T(*(const T *) a & *(const T *) b);
            };

            s.ops.op_or = [](const void *a, const void *b, void *c) {
                new ((T *) c) T(*(const T *) a | *(const T *) b);
            };

            s.ops.op_xor = [](const void *a, const void *b, void *c) {
                if constexpr (T::IsIntegral)
                    new ((T *) c) T(*(const T *) a ^ *(const T *) b);
                else
                    new ((T *) c) T(neq(*(const T *) a, *(const T *) b));
            };

            s.ops.op_invert = [](const void *a, void *b) {
                if constexpr (T::IsIntegral)
                    new ((T *) b) T(~*(const T *) a);
                else
                    new ((T *) b) T(!*(const T *) a);
            };
        }

        if constexpr (T::IsArithmetic || T::IsMask) {
            s.ops.op_richcmp = [](const void *a, const void *b, int op, void *c) {
                switch (op) {
                    case Py_LT:
                        new ((Mask *) c) Mask((*(const T *) a) < (*(const T *) b));
                        break;
                    case Py_LE:
                        new ((Mask *) c) Mask(*(const T *) a <= *(const T *) b);
                        break;
                    case Py_GT:
                        new ((Mask *) c) Mask(*(const T *) a > *(const T *) b);
                        break;
                    case Py_GE:
                        new ((Mask *) c) Mask(*(const T *) a >= *(const T *) b);
                        break;
                    case Py_EQ:
                        new ((Mask *) c) Mask(eq(*(const T *) a, *(const T *) b));
                        break;
                    case Py_NE:
                        new ((Mask *) c) Mask(neq(*(const T *) a, *(const T *) b));
                        break;
                }
            };
        }

        if constexpr (T::IsFloat) {
            s.ops.op_sqrt  = [](const void *a, void *b) { new ((T *) b) T(sqrt(*(const T *) a)); };
            s.ops.op_cbrt  = [](const void *a, void *b) { new ((T *) b) T(cbrt(*(const T *) a)); };
            s.ops.op_sin   = [](const void *a, void *b) { new ((T *) b) T(sin(*(const T *) a)); };
            s.ops.op_cos   = [](const void *a, void *b) { new ((T *) b) T(cos(*(const T *) a)); };
            s.ops.op_tan   = [](const void *a, void *b) { new ((T *) b) T(tan(*(const T *) a)); };
            s.ops.op_asin  = [](const void *a, void *b) { new ((T *) b) T(asin(*(const T *) a)); };
            s.ops.op_acos  = [](const void *a, void *b) { new ((T *) b) T(acos(*(const T *) a)); };
            s.ops.op_atan  = [](const void *a, void *b) { new ((T *) b) T(atan(*(const T *) a)); };
            s.ops.op_sinh  = [](const void *a, void *b) { new ((T *) b) T(sinh(*(const T *) a)); };
            s.ops.op_cosh  = [](const void *a, void *b) { new ((T *) b) T(cosh(*(const T *) a)); };
            s.ops.op_tanh  = [](const void *a, void *b) { new ((T *) b) T(tanh(*(const T *) a)); };
            s.ops.op_asinh = [](const void *a, void *b) { new ((T *) b) T(asinh(*(const T *) a)); };
            s.ops.op_acosh = [](const void *a, void *b) { new ((T *) b) T(acosh(*(const T *) a)); };
            s.ops.op_atanh = [](const void *a, void *b) { new ((T *) b) T(atanh(*(const T *) a)); };
            s.ops.op_exp   = [](const void *a, void *b) { new ((T *) b) T(exp(*(const T *) a)); };
            s.ops.op_exp2  = [](const void *a, void *b) { new ((T *) b) T(exp2(*(const T *) a)); };
            s.ops.op_log   = [](const void *a, void *b) { new ((T *) b) T(log(*(const T *) a)); };
            s.ops.op_log2  = [](const void *a, void *b) { new ((T *) b) T(log2(*(const T *) a)); };
            s.ops.op_floor = [](const void *a, void *b) { new ((T *) b) T(floor(*(const T *) a)); };
            s.ops.op_ceil  = [](const void *a, void *b) { new ((T *) b) T(ceil(*(const T *) a)); };
            s.ops.op_round = [](const void *a, void *b) { new ((T *) b) T(round(*(const T *) a)); };
            s.ops.op_trunc = [](const void *a, void *b) { new ((T *) b) T(trunc(*(const T *) a)); };
            s.ops.op_rcp   = [](const void *a, void *b) { new ((T *) b) T(rcp(*(const T *) a)); };
            s.ops.op_rsqrt = [](const void *a, void *b) { new ((T *) b) T(rsqrt(*(const T *) a)); };
            s.ops.op_ldexp = [](const void *a, const void *b, void *c) { new ((T *) c) T(drjit::ldexp(*(const T *) a, *(const T *) b)); };
            s.ops.op_atan2 = [](const void *a, const void *b, void *c) { new ((T *) c) T(drjit::atan2(*(const T *) a, *(const T *) b)); };
            s.ops.op_sincos = [](const void *a, void *b, void *c) {
                auto [b_, c_] = sincos(*(const T *) a);
                new ((T *) b) T(b_);
                new ((T *) c) T(c_);
            };
            s.ops.op_sincosh = [](const void *a, void *b, void *c) {
                auto [b_, c_] = sincosh(*(const T *) a);
                new ((T *) b) T(b_);
                new ((T *) c) T(c_);
            };
            s.ops.op_frexp = [](const void *a, void *b, void *c) {
                auto [b_, c_] = frexp(*(const T *) a);
                new ((T *) b) T(b_);
                new ((T *) c) T(c_);
            };
        }
    } else {
        // Default implementations of everything
        const detail::array_unop default_unop =
            (detail::array_unop) uintptr_t(1);
        const detail::array_unop_2 default_unop_2 =
            (detail::array_unop_2) uintptr_t(1);
        const detail::array_binop default_binop =
            (detail::array_binop) uintptr_t(1);
        const detail::array_ternop default_ternop =
            (detail::array_ternop) uintptr_t(1);
        (void) default_unop; (void) default_unop_2;
        (void) default_binop; (void) default_ternop;

        s.ops.op_select = default_ternop;

        if constexpr (T::IsArithmetic) {
            s.ops.op_add = default_binop;
            s.ops.op_subtract = default_binop;
            s.ops.op_multiply = default_binop;
            s.ops.op_min = default_binop;
            s.ops.op_max = default_binop;
            s.ops.op_fma = default_ternop;

            if constexpr (std::is_signed_v<scalar_t<T>>) {
                s.ops.op_absolute = default_unop;
                s.ops.op_negative = default_unop;
            }
        }

        if constexpr (T::IsIntegral) {
            s.ops.op_remainder = default_binop;
            s.ops.op_floor_divide = default_binop;
            s.ops.op_lshift = default_binop;
            s.ops.op_rshift = default_binop;
        } else {
            s.ops.op_true_divide = default_binop;
        }

        if constexpr (T::IsIntegral || T::IsMask) {
            s.ops.op_and = default_binop;
            s.ops.op_or = default_binop;
            s.ops.op_xor = default_binop;
            s.ops.op_invert = default_unop;
        }

        if constexpr (T::IsArithmetic || T::IsMask)
            s.ops.op_richcmp = (detail::array_richcmp) uintptr_t(1);

        if constexpr (T::IsFloat) {
            s.ops.op_sqrt  = default_unop;
            s.ops.op_cbrt  = default_unop;
            s.ops.op_sin   = default_unop;
            s.ops.op_cos   = default_unop;
            s.ops.op_tan   = default_unop;
            s.ops.op_asin  = default_unop;
            s.ops.op_acos  = default_unop;
            s.ops.op_atan  = default_unop;
            s.ops.op_sinh  = default_unop;
            s.ops.op_cosh  = default_unop;
            s.ops.op_tanh  = default_unop;
            s.ops.op_asinh = default_unop;
            s.ops.op_acosh = default_unop;
            s.ops.op_atanh = default_unop;
            s.ops.op_exp   = default_unop;
            s.ops.op_exp2  = default_unop;
            s.ops.op_log   = default_unop;
            s.ops.op_log2  = default_unop;
            s.ops.op_floor = default_unop;
            s.ops.op_ceil  = default_unop;
            s.ops.op_round = default_unop;
            s.ops.op_trunc = default_unop;
            s.ops.op_rcp   = default_unop;
            s.ops.op_rsqrt = default_unop;
            s.ops.op_ldexp = default_binop;
            s.ops.op_atan2 = default_binop;
            s.ops.op_sincos = default_unop_2;
            s.ops.op_sincosh = default_unop_2;
            s.ops.op_frexp = default_unop_2;
        }
    }

    if constexpr (T::Depth == 1 && T::IsDynamic && T::IsMask) {
        s.ops.op_all = [](const void *a, void *b) { new (b) T(((const T *) a)->all_()); };
        s.ops.op_any = [](const void *a, void *b) { new (b) T(((const T *) a)->any_()); };
    } else {
        const detail::array_reduce_mask default_reduce_mask =
            (detail::array_reduce_mask) uintptr_t(1);

        s.ops.op_all = default_reduce_mask;
        s.ops.op_any = default_reduce_mask;
    }

    if (T::IsMask && T::Depth == 1 && T::Size != Dynamic) {
        s.ops.op_invert = [](const void *a, void *b) {
            new ((T *) b) T(!*(const T *) a);
        };
    }

    if constexpr (T::IsJIT && T::Depth == 1)
        s.ops.op_index = [](const void *a) { return ((const T *) a)->index(); };

    if constexpr (T::IsDiff && T::Depth == 1 && T::IsFloat)
        s.ops.op_index_ad = [](const void *a) { return ((const T *) a)->index_ad(); };

    void (*copy)(void *, const void *) = nullptr;
    void (*move)(void *, void *) noexcept = nullptr;
    void (*destruct)(void *) noexcept = nullptr;

    if constexpr (!std::is_trivially_copy_constructible_v<T>)
        copy = nb::detail::wrap_copy<T>;

    if constexpr (!std::is_trivially_move_constructible_v<T>)
        move = nb::detail::wrap_move<T>;

    if constexpr (!std::is_trivially_destructible_v<T>)
        destruct = nb::detail::wrap_destruct<T>;

    using Value = typename T::Value;

    return nb::steal<nb::class_<T>>(detail::bind(
        name, s, &typeid(T), std::is_scalar_v<Value> ? nullptr : &typeid(Value),
        copy, move, destruct, detail::type_callback<T>));
}

// Run bind() for many types
template <typename T> void bind_1() {
    if constexpr (is_jit_array_v<T>)
        bind<bool_array_t<T>>();
    else
        bind<mask_t<T>>();

    bind<float32_array_t<T>>();
    bind<float64_array_t<T>>();
    bind<uint32_array_t<T>>();
    bind<int32_array_t<T>>();
    bind<uint64_array_t<T>>();
    bind<int64_array_t<T>>();
}

// .. and for many dimensions
template <typename T> void bind_2() {
    if constexpr (!std::is_scalar_v<T>)
        bind_1<T>();

    bind_1<Array<T, 0>>();
    bind_1<Array<T, 1>>();
    bind_1<Array<T, 2>>();
    bind_1<Array<T, 3>>();
    bind_1<Array<T, 4>>();
    bind_1<DynamicArray<T>>();
}

NAMESPACE_END(drjit)
