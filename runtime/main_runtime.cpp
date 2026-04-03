#include <Application.hpp>
#include <AgniEngine.hpp>

class RuntimeApp : public agni::Application
{
protected:
	void onPostInit() override
	{
		// TODO: Load game scene
		// SceneSerializer(getEngine()).loadScene("assets/scenes/main.json");

		// TODO: Register and start game systems
	}

	void onUpdate(float /*dt*/) override
	{
		// TODO: Game logic update
	}
};

int main([[maybe_unused]] int argc, [[maybe_unused]] char** argv)
{
	RuntimeApp app;
	return app.run(argc, argv);
}
