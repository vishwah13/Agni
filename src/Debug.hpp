#pragma once
#include <fmt/core.h>
#include <cstdlib>

struct AllocationMetrics
{
	uint32_t m_totalAllocated;
	uint32_t m_totalFreed;
	uint32_t CurrentUsage()
	{
		return m_totalAllocated - m_totalFreed;
	}
};
static AllocationMetrics _allocationMetrics = {0, 0};
void*                    operator new(size_t size)
{
	_allocationMetrics.m_totalAllocated += size;
	return malloc(size);
}
void operator delete(void* memory, size_t size)
{
	_allocationMetrics.m_totalFreed += size;
	free(memory);
}
static void PrintAllocationMetrics()
{
	fmt::print("Total allocated: {} bytes\n",
	           _allocationMetrics.m_totalAllocated);
	fmt::print("Total freed: {} bytes\n", _allocationMetrics.m_totalFreed);
	fmt::print("Current usage: {} bytes\n", _allocationMetrics.CurrentUsage());
}