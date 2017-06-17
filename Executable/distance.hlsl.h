#include "common.hlsl.h"

float similarity(float c1, float c2, float var2) {
	float dr = c1.x - c2.x;

	float d2 = dr*dr;
	return exp(-d2 * (1.0f / var2));
}

float similarity2(float c1, float c2) {
	return similarity(c1, c2, 0.09f);
}

float weight_func(float2 p1, float2 p2, float c1, float c2) {
	return similarity2(c1, c2);
}

float density_func(float2 p) {
	return 1.0f; //uniform sampling or unknown density
}

float2 diff_integrate(float2 p1, float2 p2, float scalar)
{
// Approximate the particle distance on the Riemannian manifold
// with the mid-point density by assuming the density function 
// changes smoothly between neighboring particles.
	return (p1 - p2) * sqrt(density_func((p1 + p2) * 0.5f)); 
}
#ifdef FUZZY_BLUE_NOISE
float distance_func(float2 diff, float c1, float c2) {
	return length(diff) / similarity2(c1, c2);
}

float2 grad_func(float2 diff, float c1, float c2) {
	return diff * similarity2(c1, c2);
}

float2 diff_func(float2 diff, float c1, float c2) {
	return diff / similarity2(c1, c2);
}
#else
float distance_func(float2 diff, float c1, float c2) {
	return length(diff);
}

float2 grad_func(float2 diff, float c1, float c2) {
	return diff;
}

float2 diff_func(float2 diff, float c1, float c2) {
	return diff;
}
#endif