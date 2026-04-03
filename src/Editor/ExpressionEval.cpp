#include <Editor/ExpressionEval.hpp>

#include <cctype>
#include <cstdlib>

namespace agni
{
namespace editor
{

namespace
{

struct Parser
{
	const char* pos;
	bool        error;

	void skipWhitespace()
	{
		while (*pos && (*pos == ' ' || *pos == '\t'))
			++pos;
	}

	float parseNumber()
	{
		skipWhitespace();
		const char* start = pos;
		char*       end   = nullptr;
		float       val   = std::strtof(start, &end);
		if (end == start)
		{
			error = true;
			return 0.0f;
		}
		pos = end;
		return val;
	}

	float parseFactor()
	{
		skipWhitespace();
		if (error) return 0.0f;

		if (*pos == '(')
		{
			++pos;
			float val = parseExpr();
			skipWhitespace();
			if (*pos == ')')
				++pos;
			else
				error = true;
			return val;
		}

		if (*pos == '-')
		{
			++pos;
			return -parseFactor();
		}

		if (*pos == '+')
		{
			++pos;
			return parseFactor();
		}

		return parseNumber();
	}

	float parseTerm()
	{
		float left = parseFactor();
		while (!error)
		{
			skipWhitespace();
			if (*pos == '*')
			{
				++pos;
				left *= parseFactor();
			}
			else if (*pos == '/')
			{
				++pos;
				float divisor = parseFactor();
				if (divisor == 0.0f)
				{
					error = true;
					return 0.0f;
				}
				left /= divisor;
			}
			else
			{
				break;
			}
		}
		return left;
	}

	float parseExpr()
	{
		float left = parseTerm();
		while (!error)
		{
			skipWhitespace();
			if (*pos == '+')
			{
				++pos;
				left += parseTerm();
			}
			else if (*pos == '-')
			{
				++pos;
				left -= parseTerm();
			}
			else
			{
				break;
			}
		}
		return left;
	}
};

} // anonymous namespace

bool evaluateExpression(const char* expr, float& outValue)
{
	if (!expr || !*expr)
		return false;

	Parser parser {expr, false};
	float  result = parser.parseExpr();

	parser.skipWhitespace();
	if (parser.error || *parser.pos != '\0')
		return false;

	outValue = result;
	return true;
}

} // namespace editor
} // namespace agni
