#pragma once
#include <fmt/core.h>

// Debug print macro — lightweight, no Vulkan dependencies.
// Include this instead of Debug.hpp when you only need AGNI_PRINT.
#ifndef NDEBUG
#define AGNI_PRINT(...) fmt::print(__VA_ARGS__)
#else
#define AGNI_PRINT(...) ((void)0)
#endif
