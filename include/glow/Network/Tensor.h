#ifndef GLOW_NETWORK_TENSOR_H
#define GLOW_NETWORK_TENSOR_H

#include "Config.h"

#include "glow/Support/ADT.h"
#include "glow/Support/Compiler.h"
#include "glow/Support/Random.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <numeric>

namespace glow {

struct ShapeNHWC {
  size_t n;
  size_t h;
  size_t w;
  size_t c;
  ShapeNHWC(ArrayRef<size_t> shape) {
    assert(shape.size() == 4 && "Invalid shape");
    n = shape[0];
    h = shape[1];
    w = shape[2];
    c = shape[3];
  }
};

constexpr unsigned max_tensor_dimensions = 6;

/// This is the default floating point type used for training.
using FloatTy = TRAINING_TENSOR_ELEMENT_TYPE;

template <class ElemTy> static char valueToChar(ElemTy val) {
  char ch = ' ';
  if (val > 0.2) {
    ch = '.';
  }
  if (val > 0.4) {
    ch = ',';
  }
  if (val > 0.6) {
    ch = ':';
  }
  if (val > 0.8) {
    ch = 'o';
  }
  if (val > 1.0) {
    ch = 'O';
  }
  if (val > 1.5) {
    ch = '0';
  }
  if (val > 2.0) {
    ch = '@';
  }
  if (val < -0.1) {
    ch = '-';
  }
  if (val < -0.2) {
    ch = '~';
  }
  if (val < -0.4) {
    ch = '=';
  }
  if (val < -1.0) {
    ch = '#';
  }
  return ch;
}

enum class ElemKind : unsigned char {
  FloatTy,
  DoubleTy,
  Int8Ty,
  Int32Ty,
  IndexTy,
};

template <class ElemTy> class Handle;

/// A class that represents a contiguous n-dimensional array (a tensor).
class Tensor final {
  /// A pointer to the tensor data.
  char *data_{nullptr};

  /// Contains the dimentions (sizes) of the tensor. Ex: [sx, sy, sz, ...].
  size_t sizes_[max_tensor_dimensions] = {
      0,
  };

  /// Contains the number of dimensions used by the tensor.
  unsigned char numSizes_{0};

  /// Specifies the element type of the tensor.
  ElemKind elementType_;

  template <class ElemTy> friend class Handle;

public:
  /// \returns true if the templated parameter \p ElemTy matches the type that's
  /// specified by the parameter \p Ty.
  template <class ElemTy> static bool isType(ElemKind Ty) {
    switch (Ty) {
    case ElemKind::FloatTy:
      return std::is_same<ElemTy, float>::value;
    case ElemKind::DoubleTy:
      return std::is_same<ElemTy, double>::value;
    case ElemKind::Int8Ty:
      return std::is_same<ElemTy, int8_t>::value;
    case ElemKind::Int32Ty:
      return std::is_same<ElemTy, int32_t>::value;
    case ElemKind::IndexTy:
      return std::is_same<ElemTy, size_t>::value;
    }
    glow_unreachable();
  }

  /// \return the size of the element \p Ty.
  static unsigned getElementSize(ElemKind Ty) {
    switch (Ty) {
    case ElemKind::FloatTy:
      return sizeof(float);
    case ElemKind::DoubleTy:
      return sizeof(double);
    case ElemKind::Int8Ty:
      return sizeof(int8_t);
    case ElemKind::Int32Ty:
      return sizeof(int32_t);
    case ElemKind::IndexTy:
      return sizeof(size_t);
    }
    glow_unreachable();
  }

  /// \return the element type of the tensor.
  ElemKind getElementType() const { return elementType_; }

  /// \returns True if the coordinate is within the array.
  bool isInBounds(ArrayRef<size_t> indices) const {
    assert(numSizes_ == indices.size() && "Invalid number of indices");
    for (size_t i = 0u, e = indices.size(); i < e; i++) {
      if (indices[i] >= sizes_[i]) {
        return false;
      }
    }
    return true;
  }

  /// Set the content of the tensor to zero.
  void zero() {
    std::fill(&data_[0], &data_[0] + size() * getElementSize(elementType_), 0);
  }

  /// \returns the shape of the tensor.
  ArrayRef<size_t> dims() const { return {sizes_, numSizes_}; }

  /// \returns the number of elements in the tensor.
  size_t size() const {
    if (!numSizes_) {
      return 0;
    }

    size_t s = 1;
    for (unsigned i = 0; i < numSizes_; i++) {
      s *= size_t(sizes_[i]);
    }

    return s;
  }

  /// \returns a pointer to the raw data, of type \p ElemTy.
  template <class ElemTy> ElemTy *getRawDataPointer() {
    assert(isType<ElemTy>(elementType_) && "Asking for the wrong ptr type.");
    return reinterpret_cast<ElemTy *>(data_);
  }

  /// Initialize an empty tensor.
  Tensor() = default;

  /// Initialize from a list of float literals.
  Tensor(const std::initializer_list<double> &vec) {
    reset(ElemKind::FloatTy, {vec.size()});
    FloatTy *data = getRawDataPointer<FloatTy>();
    int i = 0;
    for (auto &f : vec) {
      data[i++] = f;
    }
  }

  /// Allocate and initialize a new tensor.
  Tensor(ElemKind elemTy, ArrayRef<size_t> dims)
      : data_(nullptr), elementType_(elemTy) {
    reset(elemTy, dims);
  }

  Tensor(const Tensor &other) = delete;
  Tensor &operator=(const Tensor &other) = delete;

  /// Reset the shape and type of this tensor to match the shape and type of
  /// \p other.
  void reset(const Tensor *other) {
    reset(other->getElementType(), other->dims());
  }

  /// Assigns a new shape to the tensor and allocates a new buffer.
  void reset(ElemKind elemTy, ArrayRef<size_t> shape) {
    // If the new size is identical to the allocated size then there is no need
    // to re-allocate the buffer.
    if (elemTy == elementType_ && shape == this->dims()) {
      zero();
      return;
    }

    // Delete the old buffer, update the shape, and allocate a new one.
    delete[] data_;
    elementType_ = elemTy;

    assert(shape.size() < max_tensor_dimensions && "Too many indices");
    for (size_t i = 0, e = shape.size(); i < e; i++) {
      sizes_[i] = shape[i];
    }
    numSizes_ = shape.size();

    if (size()) {
      data_ = new char[size() * getElementSize(elementType_)];
      zero();
    }
  }

  ~Tensor() { delete[] data_; }

  // Move ctor.
  Tensor(Tensor &&other) noexcept {
    std::swap(data_, other.data_);
    for (int i = 0; i < max_tensor_dimensions; i++) {
      std::swap(sizes_[i], other.sizes_[i]);
    }
    std::swap(numSizes_, other.numSizes_);
    std::swap(elementType_, other.elementType_);
  }

  /// Move assignment operator.
  Tensor &operator=(Tensor &&other) noexcept {
    std::swap(data_, other.data_);
    for (int i = 0; i < max_tensor_dimensions; i++) {
      std::swap(sizes_[i], other.sizes_[i]);
    }
    std::swap(numSizes_, other.numSizes_);
    std::swap(elementType_, other.elementType_);
    return *this;
  }

  /// Update the content of the tensor from the tensor \p t.
  void copyFrom(const Tensor *t) {
    assert(this != t && "Copying to self");
    reset(t);
    size_t bufferSize = size() * getElementSize(elementType_);
    std::copy(&t->data_[0], &t->data_[bufferSize], data_);
  }

  /// Update the content of the tensor with a slice from tensor \p t. A slice
  /// is one index from the first dimension of the tensor.
  void copySlice(const Tensor *t, size_t slice) {
    auto dim = t->dims().drop_front();
    (void)dim;
    assert(dim == dims() && "Invalid slice size");
    assert(getElementType() == t->getElementType() && "Invalid element type");

    size_t bufferSize = size() * getElementSize(elementType_);
    std::copy(&t->data_[bufferSize * slice],
              &t->data_[bufferSize * (slice + 1)], data_);
  }

  /// Update the content of the tensor with a sequence of slices from the
  /// tensor \p t. A slice is one index from the first dimension of the tensor.
  /// The copying operation may overlap the end of the tensor \p t one or more
  /// times. This means that the data in the input tensor may be duplicated.
  void copyConsecutiveSlices(const Tensor *t, size_t startSliceIdx) {
    auto onceSliceDim = t->dims().drop_front();
    (void)onceSliceDim;
    assert(onceSliceDim == dims().drop_front() && "Invalid slice size");
    assert(getElementType() == t->getElementType() && "Invalid element type");
    assert(dims().size() > 1 && "Tensor must contain at least two dimensions");

    size_t numSlicesInInput = t->dims()[0];
    size_t numElementsInSlice = size() / dims()[0];
    size_t bufferSize = numElementsInSlice * getElementSize(elementType_);

    // For each outer slice in the current tensor:
    for (size_t n = 0, e = dims()[0]; n < e; n++) {
      size_t startIdx = (startSliceIdx + n) % numSlicesInInput;
      std::copy(&t->data_[bufferSize * startIdx],
                &t->data_[bufferSize * (startIdx + 1)], &data_[bufferSize * n]);
    }
  }

  /// Create a new copy of the current tensor.
  Tensor clone() const {
    Tensor slice;
    slice.copyFrom(this);
    return slice;
  }

  /// \return a new handle that points and manages this tensor.
  template <class ElemTy> Handle<ElemTy> getHandle();
};

/// A class that provides indexed access to a tensor. This class has value
/// semantics and it's copied around. One of the reasons for making this class
/// value semantics is to allow efficient index calculation that the compiler
/// can optimize (because stack allocated structures don't alias).
template <class ElemTy> class Handle final {
  /// A pointer to the tensor that this handle wraps.
  Tensor *tensor_{nullptr};

  /// Contains the multiplication of the sizes from current position to end.
  /// For example, for index (w,z,y,z):  [x * y * z, y * z, z, 1]
  size_t sizeIntegral[max_tensor_dimensions] = {
      0,
  };

  size_t sizes_[max_tensor_dimensions] = {
      0,
  };

  /// Saves the number of dimensions used in the tensor.
  uint8_t numDims{0};

  /// Create a new invalid handle. Notice that this method is private and may
  /// only be used by the static factory method below.
  Handle() = default;

public:
  /// Allocate a new invalid handle.
  static Handle createInvalidHandle() { return Handle(); }

  /// \returns true if this Handle points to a valid tensor.
  bool isValid() { return tensor_; }

  /// Calculate the index for a specific element in the tensor. Notice that
  /// the list of indices may be incomplete.
  size_t getElementPtr(ArrayRef<size_t> indices) const {
    assert(indices.size() <= numDims && "Invalid number of indices");
    size_t index = 0;
    for (int i = 0, e = indices.size(); i < e; i++) {
      index += size_t(sizeIntegral[i]) * size_t(indices[i]);
    }

    return index;
  }

  /// \returns the value of the n'th dimension \p dim, for the raw index \p idx.
  size_t getDimForPtr(size_t dim, size_t idx) const {
    assert(dim < numDims && "Invalid dimension");
    auto R = idx / sizeIntegral[dim];
    return R % sizes_[dim];
  }

  ElemKind getElementType() const { return tensor_->getElementType(); }

  /// Construct a Tensor handle.
  explicit Handle(Tensor *tensor) : tensor_(tensor) {
    auto sizes = tensor->dims();
    numDims = sizes.size();

    /// We allow handles that wrap uninitialized tensors.
    if (!numDims) {
      return;
    }

    // Copy the sizes of the tensor.
    memcpy(sizes_, tensor_->sizes_, max_tensor_dimensions * sizeof(sizes_[0]));

    size_t pi = 1;
    for (int i = numDims - 1; i >= 0; i--) {
      sizeIntegral[i] = pi;
      assert(sizes_[i] > 0 && "invalid dim size");
      pi *= sizes_[i];
    }

    assert(numDims < max_tensor_dimensions);
  }

  ArrayRef<size_t> dims() const { return ArrayRef<size_t>(sizes_, numDims); }

  size_t size() const { return tensor_->size(); }

  bool isInBounds(ArrayRef<size_t> indices) const {
    return tensor_->isInBounds(indices);
  }

  void clear(ElemTy value = 0) {
    ElemTy *data = tensor_->getRawDataPointer<ElemTy>();
    std::fill(&data[0], &data[0] + size(), value);
  }

  ElemTy &at(ArrayRef<size_t> indices) {
    assert(tensor_->isInBounds(indices));
    size_t index = getElementPtr(indices);
    assert(index < size() && "Out of bounds");
    ElemTy *data = tensor_->getRawDataPointer<ElemTy>();
    return data[index];
  }

  const ElemTy &at(ArrayRef<size_t> indices) const {
    assert(tensor_->isInBounds(indices));
    size_t index = getElementPtr(indices);
    assert(index < size() && "Out of bounds");
    ElemTy *data = tensor_->getRawDataPointer<ElemTy>();
    return data[index];
  }

  /// \returns the element at offset \p idx without any size calculations.
  ElemTy &raw(size_t index) {
    assert(index < size() && "Out of bounds");
    ElemTy *data = tensor_->getRawDataPointer<ElemTy>();
    return data[index];
  }

  /// \returns the element at offset \p idx without any size calculations.
  const ElemTy &raw(size_t index) const {
    assert(index < size() && "Out of bounds");
    ElemTy *data = tensor_->getRawDataPointer<ElemTy>();
    return data[index];
  }

  /// Extract a smaller dimension tensor from a specific slice (that has to be
  /// the first dimention).
  Tensor extractSlice(size_t idx) const {
    auto sizes = tensor_->dims();
    assert(sizes.size() > 1 && "Tensor has only one dimension");
    assert(idx < sizes[0] && "Invalid first index");
    auto elemTy = tensor_->getElementType();
    Tensor slice(elemTy, sizes.drop_front());

    // Extract the whole slice.
    size_t startIdx = sizeIntegral[0] * idx;
    ElemTy *base = tensor_->getRawDataPointer<ElemTy>() + startIdx;
    ElemTy *dest = slice.getRawDataPointer<ElemTy>();
    std::copy(base, base + sizeIntegral[0], dest);

    return slice;
  }

  /// Create a new copy of the current tensor.
  Tensor clone() const { return tensor_->clone(); }

  /// Update the content of the tensor from a literal list:
  void operator=(const std::initializer_list<ElemTy> &vec) {
    assert(size() == vec.size() && "Invalid input size.");
    size_t i = 0;
    for (auto &e : vec) {
      raw(i++) = e;
    }
  }

  void dumpAscii(const std::string &prefix = "",
                 const std::string &suffix = "\n") const {
    auto d = tensor_->dims();
    std::cout << prefix << "\n";

    if (d.size() == 2) {
      for (size_t y = 0; y < d[1]; y++) {
        for (size_t x = 0; x < d[0]; x++) {
          auto val = at({x, y});
          std::cout << valueToChar(val);
        }
        std::cout << "\n";
      }
    } else if (d.size() == 3) {
      for (size_t z = 0; z < d[2]; z++) {
        std::cout << "\n";
        for (size_t y = 0; y < d[1]; y++) {
          for (size_t x = 0; x < d[0]; x++) {
            auto val = at({x, y, z});
            std::cout << valueToChar(val);
          }
          std::cout << "\n";
        }
      }
    } else {
      assert(false && "Invalid tensor size");
    }

    std::cout << suffix;
  }

  /// \returns the index of the highest value.
  size_t maxArg() {
    ElemTy max = at({0});
    size_t idx = 0;

    for (size_t i = 1, e = size(); i < e; i++) {
      ElemTy val = at({i});
      if (val > max) {
        max = val;
        idx = i;
      }
    }
    return idx;
  }

  void dump(const char *title = "", const char *suffix = "") const {
    ElemTy *data = tensor_->getRawDataPointer<ElemTy>();
    ElemTy mx = *std::max_element(&data[0], &data[size()]);
    ElemTy mn = *std::min_element(&data[0], &data[size()]);

    std::cout << title << "max=" << mx << " min=" << mn << " [";
    const unsigned maxNumElem = 100;

    for (size_t i = 0, e = std::min<size_t>(maxNumElem, size()); i < e; i++) {
      std::cout << raw(i) << " ";
    }
    if (size() > maxNumElem) {
      std::cout << "...";
    }
    std::cout << "]" << suffix;
  }

  /// Fill the array with random data that's close to zero using the
  /// Xavier method, based on the paper [Bengio and Glorot 2010].
  /// The parameter \p filterSize is the number of elements in the
  /// tensor (or the relevant slice).
  void randomize(size_t filterSize) {
    assert(filterSize > 0 && "invalid filter size");
    double scale = std::sqrt(3.0 / double(filterSize));
    for (size_t i = 0, e = size(); i < e; ++i) {
      raw(i) = (nextRand()) * scale;
    }
  }

  /// \returns the mean and variance of the tensor.
  std::pair<ElemTy, ElemTy> calculateMeanVariance() const {
    size_t n = size();
    assert(n > 1 && "Input must haev at least 2 elements.");

    // Calculate mean.
    ElemTy sum = 0;
    for (size_t i = 0; i < n; i++) {
      sum += raw({i});
    }

    ElemTy mean = sum / n;

    // Calculate variance.
    ElemTy sigma = 0;
    for (size_t i = 0; i < n; i++) {
      ElemTy t = (raw({i}) - mean);
      sigma += t * t;
    }

    ElemTy variance = sigma / (n - 1);
    return {mean, variance};
  }
};

template <class ElemTy> Handle<ElemTy> Tensor::getHandle() {
  assert(isType<ElemTy>(elementType_) && "Getting a handle to the wrong type.");
  return Handle<ElemTy>(this);
}

} // namespace glow

namespace {
using glow::ArrayRef;
using glow::Handle;
/// Concats or splits tensors.
/// This method concats or extracts a slice from a tensor.
/// \p sliceCoor and \p fusedCoor are temporary storage that the function uses
/// to construct the coordinates to access the tensor. They must be initialized
/// to be the size of the shape of the tensor.
/// \p slice and \p fused are the tensors to concat or extract.
/// \p offset is the offset of the slice to add or extract along the dimension
/// \p offsetDim. \p d is the recursion depth parameter that's following the
/// number of the axis.
/// if \p isInsert is set then data is copied from \p slice to \p fused.
/// Otherwise data is copied from \p fused to \p slice.
template <class ElemTy>
void insertTensorsImpl(std::vector<size_t> &sliceCoor,
                       std::vector<size_t> &fusedCoor, Handle<ElemTy> &slice,
                       Handle<ElemTy> &fused, bool isInsert,
                       ArrayRef<size_t> offset, unsigned d) {
  bool isDone = (d == slice.dims().size());

  if (isDone) {
    if (isInsert) {
      fused.at(fusedCoor) = slice.at(sliceCoor);
    } else {
      slice.at(sliceCoor) = fused.at(fusedCoor);
    }
    return;
  }

  for (size_t i = 0, e = slice.dims()[d]; i < e; i++) {
    // Construct the coordinates for the slice and for the joint shape.
    // Add the 'offset' to the dimension that we concat the shapes on.
    sliceCoor[d] = i;
    fusedCoor[d] = i + offset[d];
    insertTensorsImpl(sliceCoor, fusedCoor, slice, fused, isInsert, offset,
                      d + 1);
  }
}
} // namespace

namespace glow {
/// Insert the tensor \p slice into \p fused. Insert at location \p offset.
/// The tensors must be of the right dimensions.
template <class ElemTy>
void insertTensors(Handle<ElemTy> &slice, Handle<ElemTy> &fused,
                   ArrayRef<size_t> offset) {
  auto sliceCoor = slice.dims().vec();
  auto fusedCoor = fused.dims().vec();
  insertTensorsImpl(sliceCoor, fusedCoor, slice, fused, true, offset, 0);
}

/// Extract the tensor \p slice from \p fused. Extract at location \p offset.
/// The tensors must be of the right dimensions.
template <class ElemTy>
void extractTensors(Handle<ElemTy> &slice, Handle<ElemTy> &fused,
                    ArrayRef<size_t> offset) {
  auto sliceCoor = slice.dims().vec();
  auto fusedCoor = fused.dims().vec();
  insertTensorsImpl(sliceCoor, fusedCoor, slice, fused, false, offset, 0);
}

/// This is a slow generic transpose. This method performs a single for loop
/// over a single dimension, or if we've reached the last dimension perform a
/// single copy of a single element.
template <class ElemTy>
void transposeImpl(Handle<ElemTy> &src, Handle<ElemTy> &dest, size_t *srcCoor,
                   size_t *destCoor, ArrayRef<unsigned> shuffle,
                   unsigned depth) {
  if (depth == shuffle.size()) {
    auto srcIdx = ArrayRef<size_t>(srcCoor, depth);
    auto destIdx = ArrayRef<size_t>(destCoor, depth);
    dest.at(destIdx) = src.at(srcIdx);
    return;
  }

  // Iterate over one dimension and continue recursively to the next dim.
  for (size_t x = 0, e = dest.dims()[depth]; x < e; x++) {
    unsigned swizzledDepth = shuffle[depth];
    srcCoor[swizzledDepth] = x;
    destCoor[depth] = x;
    transposeImpl(src, dest, srcCoor, destCoor, shuffle, depth + 1);
  }
}

/// Transpose the tensor \p src into the empty tensor \p dest. Shuffle the
/// axis based on the list \p shuffle, where each element is the src index.
template <class ElemTy>
void transposeTensors(Tensor *dest, Tensor *src, ArrayRef<unsigned> shuffle) {
  auto SH = src->getHandle<ElemTy>();

  unsigned numDims = shuffle.size();
  assert(SH.dims().size() == shuffle.size() && "Invalid dimensions");

  size_t newSizes[max_tensor_dimensions];

  // Generate the swizzeled dimensions.
  auto origDims = SH.dims();
  for (unsigned i = 0; i < numDims; i++) {
    newSizes[i] = origDims[shuffle[i]];
  }

  // Resize the tensor to the transposed shape.
  dest->reset(SH.getElementType(), ArrayRef<size_t>(newSizes, numDims));

  size_t srcCoor[max_tensor_dimensions];
  size_t destCoor[max_tensor_dimensions];

  auto DH = dest->getHandle<ElemTy>();
  transposeImpl(SH, DH, srcCoor, destCoor, shuffle, 0);
}

} // namespace glow

#endif // GLOW_NETWORK_TENSOR_H
