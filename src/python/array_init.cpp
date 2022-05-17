/*
    init.cpp -- implementation of drjit.ArrayBase.__init__(), which provides
    flexible and generic way fill a Dr.Jit array with contents

    Dr.Jit: A Just-In-Time-Compiler for Differentiable Rendering
    Copyright 2022, Realistic Graphics Lab, EPFL.

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE.txt file.
*/

#include "python.h"
#include "../ext/nanobind/src/buffer.h"

NAMESPACE_BEGIN(drjit)
NAMESPACE_BEGIN(detail)

namespace nb = nanobind;

inline bool operator==(const meta &a, const meta &b) {
    return memcmp(&a, &b, sizeof(meta)) == 0;
}

bool array_resize(PyObject *self, const supp &s, Py_ssize_t len) {
    if (s.meta.shape[0] == DRJIT_DYNAMIC) {
        try {
            s.init(nb::inst_ptr<void>(self), (size_t) len);
        } catch (const std::exception &e) {
            PyErr_Format(PyExc_TypeError, "%s.__init__(): %s",
                         Py_TYPE(self)->tp_name, e.what());
            return false;
        }

        return true;
    } else {
        if (s.meta.shape[0] != len) {
            PyErr_Format(
                PyExc_TypeError,
                "%s.__init__(): input sequence has wrong size (expected %u, got %zd)!",
                Py_TYPE(self)->tp_name, (unsigned) s.meta.shape[0], len);
            return false;
        }
        return true;
    }
}

void array_init_from_tensor(nb::handle self, nb::handle arg) {
    const supp &s = nb::type_supplement<supp>(self.type());
    size_t shape[4];

    nb::detail::tensor_req tr;
    tr.ndim = s.meta.ndim;
    tr.shape = shape;
    tr.dtype = dlpack_dtype((VarType) s.meta.type);
    tr.req_order = 'C';
    tr.req_dtype = true;
    tr.req_shape = true;

    for (size_t i = 0; i < s.meta.ndim; ++i) {
        shape[i] = s.meta.shape[i];
        if (shape[i] == DRJIT_DYNAMIC)
            shape[i] = nb::any;
    }

    nb::detail::tensor_handle *th = nb::detail::tensor_import(
        arg.ptr(), &tr, (uint8_t) nb::detail::cast_flags::convert);

    nb::tensor<> tensor(th);
    if (!tensor.is_valid()) {
        nb::detail::Buffer buf(64);
        buf.fmt("%s.__init__(): unable to initialize from tensor of "
                "type '%s'. The input must have the following configuration "
                "for this to succeed: shape=(",
                Py_TYPE(self.ptr())->tp_name,
                ((PyTypeObject *) arg.type().ptr())->tp_name);
        for (int i = 0; i < s.meta.ndim; ++i) {
            if (s.meta.shape[i] == DRJIT_DYNAMIC)
                buf.put('*');
            else
                buf.put_uint32((uint32_t) s.meta.shape[i]);
            if (i + 1 < s.meta.ndim)
                buf.put(", ");
        }
        buf.put("), dtype=");
        switch ((nb::dlpack::dtype_code) tr.dtype.code) {
            case nb::dlpack::dtype_code::Int: buf.put("int"); break;
            case nb::dlpack::dtype_code::UInt: buf.put("uint"); break;
            case nb::dlpack::dtype_code::Float: buf.put("float"); break;
            case nb::dlpack::dtype_code::Bfloat: buf.put("bfloat"); break;
            case nb::dlpack::dtype_code::Complex: buf.put("complex"); break;
        }
        buf.put_uint32(tr.dtype.bits);
        buf.put(", order='C'.");

        throw nb::type_error(buf.get());
    }

    size_t size = 1;
    for (size_t i = 0; i < tensor.ndim(); ++i)
        size *= tensor.shape(i);

    meta temp_meta { };
    temp_meta.is_llvm = s.meta.is_llvm;
    temp_meta.is_cuda = s.meta.is_cuda;
    temp_meta.is_diff = s.meta.is_diff;
    temp_meta.type = s.meta.type;
    temp_meta.ndim = 1;
    temp_meta.shape[0] = DRJIT_DYNAMIC;
    nb::handle temp_t = drjit::detail::array_get(temp_meta);
    nb::object temp = temp_t();

    if (s.meta.is_cuda || s.meta.is_llvm) {
        JitBackend backend =
            s.meta.is_cuda ? JitBackend::CUDA : JitBackend::LLVM;
        int32_t device_type =
            s.meta.is_cuda ? nb::device::cuda::value : nb::device::cpu::value;

        uint32_t index;

        if (device_type == tensor.device_type()) {
            index = jit_var_mem_map(
                s.meta.is_cuda ? JitBackend::CUDA : JitBackend::LLVM,
                (VarType) s.meta.type, tensor.data(), size, 0);

            if (index) {
                nb::detail::tensor_inc_ref(th);
                jit_var_set_callback(index, [](uint32_t /* i */, int free, void *o) {
                    if (free)
                        nb::detail::tensor_dec_ref((nb::detail::tensor_handle *) o);
                }, th);
            }
        } else {
            AllocType at;
            switch (tensor.device_type()) {
                case nb::device::cuda::value: at = AllocType::Device; break;
                case nb::device::cpu::value:  at = AllocType::Host; break;
                default: throw std::runtime_error("Unsupported source device!");
            }

            index = jit_var_mem_copy(backend, at, (VarType) s.meta.type,
                                     tensor.data(), size);
        }

        nb::type_supplement<supp>(temp_t).op_set_index(nb::inst_ptr<void>(temp), index);
    } else {
        if (tensor.device_type() != nb::device::cpu::value)
            throw std::runtime_error("Unsupported source device!");

        nb::type_supplement<supp>(temp_t).init(nb::inst_ptr<void>(temp), size);
        size *= tensor.dtype().bits / 8;
        memcpy(s.ptr(nb::inst_ptr<void>(temp)), tensor.data(), size);
    }

    nb::object unraveled = unravel(
        nb::borrow<nb::type_object_t<dr::ArrayBase>>(self.type()),
        nb::handle_t<dr::ArrayBase>(temp), 'C');

    nb::inst_destruct(self);
    nb::inst_move(self, unraveled);
}

int array_init(PyObject *self, PyObject *args, PyObject *kwds) {
    PyTypeObject *self_tp = Py_TYPE(self);
    const supp &s = nb::type_supplement<supp>(self_tp);

    if (kwds) {
        PyErr_Format(
            PyExc_TypeError,
            "%s.__init__(): constructor does not take keyword arguments!",
            self_tp->tp_name);
        return -1;
    }

    auto assign_item = self_tp->tp_as_sequence->sq_ass_item;
    size_t argc = (size_t) PyTuple_GET_SIZE(args);

    if (argc == 0) {
        // Zero-initialize
        nb::detail::nb_inst_zero(self);
        return 0;
    } else if (argc == 1) {
        PyObject *arg = PyTuple_GET_ITEM(args, 0);
        PyTypeObject *arg_tp = Py_TYPE(arg);
        bool try_sequence_import = arg_tp != s.value;

        // Copy/conversion from a compatible Dr.Jit array
        if (is_drjit_type(arg_tp)) {
            if (arg_tp == self_tp) {
                nb::detail::nb_inst_copy(self, arg);
                return 0;
            }

            meta arg_meta    = nb::type_supplement<supp>(arg_tp).meta,
                 arg_meta_cp = arg_meta;
            arg_meta_cp.type = s.meta.type;

            if (arg_meta_cp == s.meta && s.op_cast) {
                try {
                    if (s.op_cast(nb::inst_ptr<void>(arg), (VarType) arg_meta.type,
                                  nb::inst_ptr<void>(self)) == 0) {
                        nb::inst_mark_ready(self);
                        return 0;
                    }
                } catch (const std::exception &e) {
                    PyErr_Format(PyExc_RuntimeError, "%s.__init__(): %s",
                                 Py_TYPE(self)->tp_name, e.what());
                    return -1;
                }
            }

            // Disallow inefficient element-by-element imports of JIT arrays
            if (arg_meta.ndim == 1 && arg_meta.shape[0] == DRJIT_DYNAMIC)
                try_sequence_import = false;
        }

        nb::detail::nb_inst_zero(self);

        // Fast path for tuples/list instances
        if (arg_tp == &PyTuple_Type) {
            Py_ssize_t len = PyTuple_GET_SIZE(arg);
            if (!array_resize(self, s, len))
                return -1;

            for (Py_ssize_t i = 0; i < len; ++i) {
                if (assign_item(self, i, PyTuple_GET_ITEM(arg, i)))
                    return -1;
            }

            return 0;
        } else if (arg_tp == &PyList_Type) {
            Py_ssize_t len = PyList_GET_SIZE(arg);
            if (!array_resize(self, s, len))
                return -1;

            for (Py_ssize_t i = 0; i < len; ++i) {
                if (assign_item(self, i, PyList_GET_ITEM(arg, i)))
                    return -1;
            }

            return 0;
        }

        bool is_dynamic = s.meta.is_tensor;
        for (int i = 0; i < s.meta.ndim; ++i)
            is_dynamic |= s.meta.shape[i] == DRJIT_DYNAMIC;

        if (is_dynamic) {
            const char *module_name =
                nb::borrow<nb::str>(nb::handle(arg_tp).attr("__module__")).c_str();

            bool is_numpy = strcmp(arg_tp->tp_name, "numpy.ndarray") == 0,
                 is_pytorch = strcmp(arg_tp->tp_name, "Tensor") == 0 &&
                              strcmp(module_name, "torch") == 0,
                 is_jax = strcmp(arg_tp->tp_name, "DeviceArray") == 0 &&
                          strncmp(module_name, "jaxlib", 6) == 0,
                 is_tf = strstr(arg_tp->tp_name, "Tensor") != nullptr &&
                         strncmp(module_name, "tensorflow", 10) == 0;

            if (is_numpy || is_pytorch || is_jax || is_tf) {
                try {
                    array_init_from_tensor(self, arg);
                    nb::inst_mark_ready(self);
                } catch (const std::exception &e) {
                    PyErr_Format(PyExc_TypeError, "%s.__init__(): %s",
                                 Py_TYPE(self)->tp_name, e.what());
                    return -1;
                }

                return 0;
            }
        }

        if (try_sequence_import && arg_tp->tp_as_sequence) {
            // General path for all sequence types
            auto arg_item = arg_tp->tp_as_sequence->sq_item;
            auto arg_length = arg_tp->tp_as_sequence->sq_length;

            if (arg_length && arg_item) {
                Py_ssize_t len = arg_length(arg);
                if (!array_resize(self, s, len))
                    return -1;

                for (Py_ssize_t i = 0; i < len; ++i) {
                    PyObject *o = arg_item(arg, i);
                    if (!o)
                        return -1;
                    if (assign_item(self, i, o)) {
                        Py_DECREF(o);
                        return -1;
                    }
                    Py_DECREF(o);
                }
                return 0;
            }
        }

        // Catch-all case for iterable types
        if (try_sequence_import && arg_tp->tp_iter) {
            PyObject *list = PySequence_List(arg);
            if (!list)
                return -1;
            PyObject *sub_args = PyTuple_New(1);
            PyTuple_SET_ITEM(sub_args, 0, list);
            int rv = array_init(self, sub_args, kwds);
            Py_DECREF(sub_args);
            return rv;
        }

        // No sequence/iterable type, broadcast to elements
        PyObject *result;
        if (arg_tp == s.value) {
            result = arg;
            Py_INCREF(result);
        } else {
            PyObject *args[2] = { nullptr, arg };
            result = NB_VECTORCALL((PyObject *) s.value, args + 1,
                                   1 | PY_VECTORCALL_ARGUMENTS_OFFSET, nullptr);
            if (!result) {
                PyErr_Clear();
                PyErr_Format(
                    PyExc_TypeError,
                    "%s.__init__(): initialization from type '%s' failed!",
                    Py_TYPE(self)->tp_name, arg_tp->tp_name);
                return -1;
            }
        }

        Py_ssize_t len = s.meta.shape[0];

        if (len == 0) {
            PyErr_Format(
                PyExc_TypeError,
                "%s.__init__(): too many arguments provided (expected 0, got 1)!",
                Py_TYPE(self)->tp_name);
            return -1;
        }

        if (len == DRJIT_DYNAMIC) {
            len = 1;

            if (s.op_full) {
                try {
                    s.op_full(result, len, nb::inst_ptr<void>(self));
                } catch (const std::exception &e) {
                    Py_DECREF(result);
                    PyErr_Format(PyExc_RuntimeError, "%s.__init__(): %s",
                                 Py_TYPE(self)->tp_name, e.what());
                    return -1;
                }
                Py_DECREF(result);
                return 0;
            } else if (!array_resize(self, s, len)) {
                Py_DECREF(result);
                return -1;
            }
        }

        for (Py_ssize_t i = 0; i < len; ++i) {
            if (assign_item(self, i, result)) {
                Py_DECREF(result);
                return -1;
            }

        }

        Py_DECREF(result);

        return 0;
    } else {
        nb::detail::nb_inst_zero(self);

        Py_ssize_t len = PyTuple_GET_SIZE(args);
        if (!array_resize(self, s, len))
            return -1;

        for (Py_ssize_t i = 0; i < len; ++i) {
            if (assign_item(self, i, PyTuple_GET_ITEM(args, i)))
                return -1;
        }

        return 0;
    }
}

int tensor_init(PyObject *self, PyObject *args, PyObject *kwds) {
    PyObject *array = nullptr, *shape = nullptr;
    const char *kwlist[3] = { "array", "shape", nullptr };
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OO!", (char **) kwlist,
                                     &array, &PyTuple_Type, &shape))
        return -1;

    PyTypeObject *self_tp = Py_TYPE(self);
    const supp &s = nb::type_supplement<supp>(self_tp);

    if (!shape && !array) {
        // Zero-initialize
        nb::detail::nb_inst_zero(self);
        s.op_tensor_shape(nb::inst_ptr<void>(self)).push_back(0);
        return 0;
    }

    if (!shape) {
        PyTypeObject *array_tp = Py_TYPE(array);

        // Same type -> copy constructor
        if (array_tp == self_tp) {
            nb::detail::nb_inst_copy(self, array);
            return 0;
        }

        /// XXX need dr.ravel(), and initialize shape here..

        nb::detail::nb_inst_zero(self);
        PyObject *value = s.op_tensor_array(self);
        if (array_init(value, args, kwds)) {
            Py_DECREF(value);
            return -1;
        }

        s.op_tensor_shape(nb::inst_ptr<void>(self)).push_back(len(value));
        Py_DECREF(value);
        return 0;
    }

    return -1;
}

NAMESPACE_END(detail)
NAMESPACE_END(drjit)
