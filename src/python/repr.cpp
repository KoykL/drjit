#include "python.h"
#include "../ext/nanobind/src/buffer.h"
#include <nanobind/stl/vector.h>

namespace nb = nanobind;
namespace dr = drjit;

static nanobind::detail::Buffer buffer;

void tp_repr_impl(PyObject *self,
                  const std::vector<size_t> &shape,
                  std::vector<size_t> &index,
                  size_t depth) {
    size_t i = index.size() - 1 - depth,
           size = shape.empty() ? 0 : shape[i];

    buffer.put('[');
    for (size_t j = 0; j < size; ++j) {
        index[i] = j;

        if (i > 0) {
            tp_repr_impl(self, shape, index, depth + 1);
        } else {
            nb::object o = nb::borrow(self);

            for (size_t k = 0; k < index.size(); ++k)
                o = nb::steal(Py_TYPE(o.ptr())->tp_as_sequence->sq_item(
                    o.ptr(), index[k]));

            if (o.type() == nb::handle(&PyFloat_Type)) {
                double d = nb::cast<double>(o);
                buffer.fmt("%g", d);
            } else {
                buffer.put_dstr(nb::str(o).c_str());
            }
        }

        if (j + 1 < size) {
            if (i == 0) {
                buffer.put(", ");
            } else {
                buffer.put(",\n");
                buffer.put(' ', i);
            }
        }
    }
    buffer.put(']');
}

PyObject *tp_repr(PyObject *self) {
    (void) self;
    buffer.clear();

    nb::object shape_obj = shape(self);
    if (shape_obj.is_none()) {
        buffer.put("[ragged array[");
    } else {
        std::vector<size_t> shape = nb::cast<std::vector<size_t>>(shape_obj),
                            index(shape.size(), 0);
        tp_repr_impl(self, shape, index, 0);
    }

    return PyUnicode_FromString(buffer.get());
}

