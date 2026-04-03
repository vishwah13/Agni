#pragma once

namespace agni
{
namespace editor
{

// Evaluate a simple arithmetic expression string.
// Supports: +, -, *, /, (), unary minus, decimal numbers.
// Returns true if the expression was valid, result in outValue.
// Returns false on parse error (outValue unchanged).
bool evaluateExpression(const char* expr, float& outValue);

} // namespace editor
} // namespace agni
