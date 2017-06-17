#include <thrust/device_vector.h>
#include <thrust/sort.h>

template <class T>
void thrust_stable_sort(T* ptr, size_t num) {
	thrust::device_ptr<T> thrust_ptr(ptr);
	thrust::stable_sort(thrust_ptr, thrust_ptr + num);
}

extern void thrust_stable_sort(unsigned __int64* ptr, size_t num) {
	thrust_stable_sort<unsigned __int64>(ptr, num);
}