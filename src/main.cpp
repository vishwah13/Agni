#include <vk_engine.h>

int main(int argc, char* argv[])
{
	AgniEngine engine;

	engine.init();

	engine.run();

	engine.cleanup();

	return 0;
}
