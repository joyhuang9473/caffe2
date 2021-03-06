#ifndef CAFFE2_CORE_CONTEXT_GPU_H_
#define CAFFE2_CORE_CONTEXT_GPU_H_

#include <ctime>

#include "caffe2/core/common_gpu.h"
#include "caffe2/core/context.h"
#include "caffe2/core/cuda_memorypool_gpu.h"
#include "caffe2/core/tensor.h"
#include "caffe2/core/types.h"
#include "caffe2/proto/caffe2.pb.h"
#include "caffe2/core/logging.h"

namespace caffe2 {

class CUDAContext {
 public:
  // The default cuda context constructor.
  explicit CUDAContext(const int gpu_id = -1)
      : cuda_gpu_id_(gpu_id), cuda_stream_(nullptr),
        cublas_handle_(nullptr), random_seed_(1701),
        curand_generator_(nullptr) {
    if (gpu_id == -1) {
      cuda_gpu_id_ = GetDefaultGPUID();
    }
    CUDA_CHECK(cudaSetDevice(cuda_gpu_id_));
    CUDA_CHECK(cudaStreamCreate(&cuda_stream_));
  }

  explicit CUDAContext(const DeviceOption& option)
      : cuda_stream_(nullptr), cublas_handle_(nullptr),
        random_seed_(
            option.has_random_seed() ? option.random_seed() : time(NULL)),
        curand_generator_(nullptr) {
    CAFFE_DCHECK_EQ(option.device_type(), CUDA);
    cuda_gpu_id_ = option.has_cuda_gpu_id() ?
                   option.cuda_gpu_id() : GetDefaultGPUID();
    CUDA_CHECK(cudaSetDevice(cuda_gpu_id_));
    CUDA_CHECK(cudaStreamCreate(&cuda_stream_));
  }

  virtual ~CUDAContext() {
    if (curand_generator_) {
      CURAND_CHECK(curandDestroyGenerator(curand_generator_));
    }
    if (cublas_handle_) {
      CUBLAS_CHECK(cublasDestroy(cublas_handle_));
    }
    if (cuda_stream_) {
      CUDA_CHECK(cudaStreamDestroy(cuda_stream_));
    }
  }

  inline void SwitchToDevice() {
    CUDA_CHECK(cudaSetDevice(cuda_gpu_id_));
  }

  inline bool FinishDeviceComputation() {
    cudaStreamSynchronize(cuda_stream_);
    cudaError_t error = cudaGetLastError();
    if (error == cudaSuccess) {
      return true;
    } else {
      CAFFE_LOG_ERROR << "Encountered CUDA error: "
                      << cudaGetErrorString(error);
      return false;
    }
  }

  int cuda_gpu_id() { return cuda_gpu_id_; }

  inline cudaStream_t& cuda_stream() { return cuda_stream_; }

  cublasHandle_t& cublas_handle() {
    if (cublas_handle_) {
      return cublas_handle_;
    } else {
      CUBLAS_CHECK(cublasCreate(&cublas_handle_));
      // The default is CUBLAS_POINTER_MODE_HOST. You can override
      // it after obtaining the cublas handle, but do that with
      // caution.
      CUBLAS_CHECK(cublasSetPointerMode(
          cublas_handle_, CUBLAS_POINTER_MODE_HOST));
      CUBLAS_CHECK(cublasSetStream(cublas_handle_, cuda_stream_));
      return cublas_handle_;
    }
  }

  curandGenerator_t& curand_generator() {
    if (curand_generator_) {
      return curand_generator_;
    } else {
      CURAND_CHECK(curandCreateGenerator(
          &curand_generator_, CURAND_RNG_PSEUDO_DEFAULT));
      CURAND_CHECK(curandSetPseudoRandomGeneratorSeed(
          curand_generator_, random_seed_));
      CURAND_CHECK(curandSetStream(curand_generator_, cuda_stream_));
      return curand_generator_;
    }

  }

  static inline void* New(size_t nbytes) {
    return CudaMemoryPool::New(nbytes);
  }

  static inline void Delete(void* data) {
    CudaMemoryPool::Delete(data);
  }

  template <class SrcContext, class DstContext>
  inline void Memcpy(size_t nbytes, const void* src, void* dst) {
    CUDA_CHECK(cudaMemcpyAsync(
        dst, src, nbytes, cudaMemcpyDefault, cuda_stream_));
  }

  template <typename T, class SrcContext, class DstContext>
  inline void Copy(int n, const T* src, T* dst) {
    Memcpy<SrcContext, DstContext>(n * sizeof(T),
                                 static_cast<const void*>(src),
                                 static_cast<void*>(dst));
  }

 protected:
  int cuda_gpu_id_;
  cudaStream_t cuda_stream_;
  cublasHandle_t cublas_handle_;
  int random_seed_;
  curandGenerator_t curand_generator_;
};

// For the CPU context, we also allow a (probably expensive) function
// to copy the data from a cuda context.
template<>
inline void CPUContext::Memcpy<CUDAContext, CPUContext>(
    size_t nbytes, const void* src, void* dst) {
  CUDAContext context;
  context.Memcpy<CUDAContext, CPUContext>(nbytes, src, dst);
}

// For simplicity, we will typedef Tensor<CPUContext> to TensorCPU.
typedef Tensor<CUDAContext> TensorCUDA;

}  // namespace caffe2

#endif  // CAFFE2_CORE_CONTEXT_GPU_H_
