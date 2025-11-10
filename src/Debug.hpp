#pragma once
#include <fmt/core.h>
#include <cstdlib>

struct AllocationMetrics
{
	uint32_t totalAllocated;
	uint32_t totalFreed;
	uint32_t CurrentUsage()
	{
		return totalAllocated - totalFreed;
	}
};
static AllocationMetrics _allocationMetrics = {0, 0};
void*                    operator new(size_t size)
{
	_allocationMetrics.totalAllocated += size;
	return malloc(size);
}
void operator delete(void* memory, size_t size)
{
	_allocationMetrics.totalFreed += size;
	free(memory);
}
static void PrintAllocationMetrics()
{
	fmt::print("Total allocated: {} bytes\n",
	           _allocationMetrics.totalAllocated);
	fmt::print("Total freed: {} bytes\n", _allocationMetrics.totalFreed);
	fmt::print("Current usage: {} bytes\n", _allocationMetrics.CurrentUsage());
}