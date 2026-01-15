#include <AgniEngine.hpp>

int main()
{
	AgniEngine engine;

	engine.init();

	engine.run();

	engine.cleanup();

	return 0;
}
