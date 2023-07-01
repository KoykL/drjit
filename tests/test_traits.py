import drjit as dr
import pytest

@pytest.test_arrays("-tensor")
def test01_traits(t):
    from drjit import Dynamic
    v = t()
    tn = t.__name__
    tm = t.__module__

    assert not dr.is_array_v(()) and not dr.is_array_v(1.0)
    assert dr.is_array_v(t) and dr.is_array_v(v)
    assert dr.size_v(1) == 1
    assert dr.size_v("test") == 1

    size = dr.Dynamic
    for c in tn:
        if c.isdigit():
            size = int(c)
            break

    is_jit = "llvm" in tm or "cuda" in tm
    if is_jit and "Int" in tn or "Float" in tn or "X" in tn:
        size = dr.Dynamic

    depth = int("Array" in tn or "Complex" in tn or "Quaternion" in tn)
    depth += int("Matrix" in tn)*2
    depth += int("Array11" in tn or "Array22" in tn or "Array33" in tn or "Array44" in tn)
    depth += int(is_jit)

    assert dr.is_jit_v(t) == is_jit and dr.is_jit_v(v) == is_jit

    if tn.endswith('f') or tn.endswith('f16') or tn.endswith('f64') or 'Float' in tn:
        scalar_type = float
    elif tn[-1] == 'b' or 'Bool' in tn:
        scalar_type = bool
    else:
        scalar_type = int

    assert dr.size_v(t) == size

    assert dr.depth_v(1) == 0
    assert dr.depth_v("test") == 0
    assert dr.depth_v(t) == depth and dr.depth_v(v) == depth

    assert dr.scalar_t(True) is bool
    assert dr.scalar_t(1) is int
    assert dr.scalar_t(1.0) is float
    assert dr.scalar_t("test") is str

    assert dr.scalar_t(t) is scalar_type and dr.scalar_t(v) is scalar_type
    m = dr.mask_t(t)
    mv = m()
    mn = m.__name__
    assert dr.scalar_t(m) is bool and dr.scalar_t(mv) is bool

    assert dr.value_t(True) is bool
    assert dr.value_t(1) is int
    assert dr.value_t(1.0) is float
    assert dr.value_t("test") is str

    if depth == 1:
        assert dr.value_t(t) is dr.scalar_t(t)
    else:
        assert dr.is_array_v(dr.value_t(t))

    assert dr.mask_t(True) is bool
    assert dr.mask_t(1) is bool
    assert dr.mask_t(1.0) is bool
    assert dr.mask_t("test") is bool

    if tn == 'Matrix3f':
        assert mn == 'Array33b'
        assert dr.array_t(t).__name__ == "Array33f"
        assert dr.array_t(m).__name__ == "Array33b"

    if tn == 'Float':
        assert mn == 'Bool'
        assert dr.array_t(t).__name__ == "Float"
        assert dr.array_t(m).__name__ == "Bool"

    if scalar_type is int:
        assert dr.is_integral_v(t) and dr.is_integral_v(v)
        assert dr.is_arithmetic_v(t) and dr.is_arithmetic_v(v)
        assert not dr.is_float_v(t) and not dr.is_float_v(v)
        assert not dr.is_mask_v(t) and not dr.is_mask_v(v)
        assert dr.is_integral_v(scalar_type)
        assert dr.is_arithmetic_v(scalar_type)
        assert not dr.is_float_v(scalar_type)
        assert not dr.is_mask_v(scalar_type)
    elif scalar_type is float:
        assert not dr.is_integral_v(t) and not dr.is_integral_v(v)
        assert dr.is_arithmetic_v(t) and dr.is_arithmetic_v(v)
        assert dr.is_float_v(t) and dr.is_float_v(v)
        assert not dr.is_mask_v(t) and not dr.is_mask_v(v)
        assert not dr.is_integral_v(scalar_type)
        assert dr.is_arithmetic_v(scalar_type)
        assert dr.is_float_v(scalar_type)
        assert not dr.is_mask_v(scalar_type)
    else:
        assert not dr.is_integral_v(t) and not dr.is_integral_v(v)
        assert not dr.is_arithmetic_v(t) and not dr.is_arithmetic_v(v)
        assert not dr.is_float_v(t) and not dr.is_float_v(v)
        assert not dr.is_integral_v(scalar_type)
        assert dr.is_mask_v(t) and dr.is_mask_v(v)
        assert not dr.is_arithmetic_v(scalar_type)
        assert not dr.is_float_v(scalar_type)
        assert dr.is_mask_v(scalar_type)

    assert not dr.is_integral_v("str") and not dr.is_arithmetic_v(str)
    assert not dr.is_arithmetic_v("str") and not dr.is_arithmetic_v(str)
    assert not dr.is_float_v("str") and not dr.is_arithmetic_v(str)
    assert not dr.is_mask_v("str") and not dr.is_arithmetic_v(str)

    assert dr.is_complex_v(t) == ("Complex" in tn)
    assert dr.is_quaternion_v(t) == ("Quaternion" in tn)
    assert dr.is_matrix_v(t) == ("Matrix" in tn)
    assert dr.is_vector_v(t) == ("Array" in tn)
    assert dr.is_tensor_v(t) == False

    if scalar_type is float:
        assert dr.is_signed_v(t) and dr.is_signed_v(v)
        assert not dr.is_unsigned_v(t) and not dr.is_unsigned_v(v)
    elif scalar_type is bool:
        assert not dr.is_signed_v(t) and not dr.is_signed_v(v)
        assert dr.is_unsigned_v(t) and dr.is_unsigned_v(v)
    else:
        unsigned = 'UInt' in tn or 'u' in tn
        assert dr.is_signed_v(t) is not unsigned and dr.is_signed_v(v) is not unsigned
        assert dr.is_unsigned_v(t) is unsigned and dr.is_unsigned_v(v) is unsigned

    #  assert dr.uint32_array_t(float) is int
    #  assert dr.bool_array_t(float) is bool
    #  assert dr.float32_array_t(int) is float
    #
    #  assert dr.bool_array_t(dr.scalar.Array3f) is dr.scalar.Array3b
    #  assert dr.int32_array_t(dr.scalar.Array3f) is dr.scalar.Array3i
    #  assert dr.uint32_array_t(dr.scalar.Array3f64) is dr.scalar.Array3u
    #  assert dr.int64_array_t(dr.scalar.Array3f) is dr.scalar.Array3i64
    #  assert dr.uint64_array_t(dr.scalar.Array3f) is dr.scalar.Array3u64
    #  assert dr.uint_array_t(dr.scalar.Array3f) is dr.scalar.Array3u
    #  assert dr.int_array_t(dr.scalar.Array3f) is dr.scalar.Array3i
    #  assert dr.uint_array_t(dr.scalar.Array3f64) is dr.scalar.Array3u64
    #  assert dr.int_array_t(dr.scalar.Array3f64) is dr.scalar.Array3i64
    #  assert dr.float_array_t(dr.scalar.Array3u) is dr.scalar.Array3f
    #  assert dr.float32_array_t(dr.scalar.Array3u) is dr.scalar.Array3f
    #  assert dr.float_array_t(dr.scalar.Array3u64) is dr.scalar.Array3f64
    #  assert dr.float32_array_t(dr.scalar.Array3u64) is dr.scalar.Array3f
    #  assert dr.float_array_t(dr.scalar.TensorXu64) is dr.scalar.TensorXf64


