#include <AgniEngine.hpp>
#include <Camera.hpp>
#include <Debug.hpp>
#include <ECS/EntityFactory.hpp>
#include <Editor/AssetBrowser.hpp>
#include <Editor/ContextMenus.hpp>
#include <Editor/ECSInspector.hpp>
#include <Editor/EditorIcons.hpp>
#include <Editor/EditorManager.hpp>
#include <Editor/EditorTheme.hpp>
#include <Editor/EditorWidgets.hpp>
#include <Reflection/ComponentRegistry.hpp>

#ifdef AGNI_HAS_JOLT
#include <Physics/JoltPhysicsManager.hpp>
#endif

#include <ImGuizmo.h>
#include <fmt/core.h>
#include <imgui.h>

#include <algorithm>
#include <filesystem>

#include <glm/gtc/type_ptr.hpp>

namespace agni
{
	namespace editor
	{

		ECSInspector::ECSInspector(
		EditorManager&                     editorManager,
		agni::ecs::World&                  world,
		agni::ecs::EntityFactory&          entityFactory,
		agni::physics::JoltPhysicsManager* physicsManager) :
		    m_editorManager(editorManager),
		    m_world(world),
		    m_entityFactory(entityFactory),
		    m_physicsManager(physicsManager)
		{
		}

		void ECSInspector::render(bool& showHierarchy, bool& showInspector)
		{
			// ========================================================================
			// Hierarchy Window
			// ========================================================================
			if (showHierarchy)
			{
				if (ImGui::Begin("Hierarchy", &showHierarchy))
				{
					// Gizmo controls toolbar
					ImGui::PushStyleColor(ImGuiCol_Text, colors::TextDim);
					ImGui::Text("Gizmo:");
					ImGui::PopStyleColor();
					ImGui::SameLine();

					ImGui::PushID("GizmoOp");
					if (widgets::ButtonToggle(
					    "T##translate", m_gizmoOperation == 0, ImVec2(24, 0)))
						m_gizmoOperation = 0;
					widgets::TooltipOnHover("Translate (W)");

					ImGui::SameLine();
					if (widgets::ButtonToggle(
					    "R##rotate", m_gizmoOperation == 1, ImVec2(24, 0)))
						m_gizmoOperation = 1;
					widgets::TooltipOnHover("Rotate (E)");

					ImGui::SameLine();
					if (widgets::ButtonToggle(
					    "S##scale", m_gizmoOperation == 2, ImVec2(24, 0)))
						m_gizmoOperation = 2;
					widgets::TooltipOnHover("Scale (R)");
					ImGui::PopID();

					ImGui::SameLine();
					ImGui::PushStyleColor(ImGuiCol_Text, colors::TextDim);
					ImGui::Text("|");
					ImGui::PopStyleColor();
					ImGui::SameLine();

					ImGui::PushID("GizmoMode");
					if (widgets::ButtonToggle(
					    "L##local", m_gizmoMode == 0, ImVec2(24, 0)))
						m_gizmoMode = 0;
					widgets::TooltipOnHover("Local Space");

					ImGui::SameLine();
					if (widgets::ButtonToggle(
					    "W##world", m_gizmoMode == 1, ImVec2(24, 0)))
						m_gizmoMode = 1;
					widgets::TooltipOnHover("World Space");
					ImGui::PopID();

					ImGui::SameLine();
					ImGui::Checkbox("Snap", &m_useSnap);

					if (m_useSnap)
					{
						ImGui::SameLine();
						ImGui::SetNextItemWidth(80);
						if (m_gizmoOperation == 0)
							ImGui::DragFloat("##snap",
							                 &m_snapValues[0],
							                 0.1f,
							                 0.1f,
							                 10.0f,
							                 "%.1f");
						else if (m_gizmoOperation == 1)
							ImGui::DragFloat("##snap",
							                 &m_snapValues[1],
							                 1.0f,
							                 1.0f,
							                 90.0f,
							                 "%.0f");
						else
							ImGui::DragFloat("##snap",
							                 &m_snapValues[2],
							                 0.05f,
							                 0.05f,
							                 2.0f,
							                 "%.2f");
					}

					ImGui::Separator();

					// Entity list
					renderEntityList();
				}
				ImGui::End();
			}

			// ========================================================================
			// Inspector Window
			// ========================================================================
			if (showInspector)
			{
				if (ImGui::Begin("Inspector", &showInspector))
				{
					renderComponentInspector();
				}
				ImGui::End();
			}

			// ========================================================================
			// Create entity popup
			// ========================================================================
			if (m_showCreateEntityPopup)
			{
				renderCreateEntityPopup();
			}
		}

		void ECSInspector::renderEntityList()
		{
			// Count total entities
			int totalCount = 0;
			m_world.get().each([&totalCount]([[maybe_unused]] flecs::entity e)
			                   { totalCount++; });

			ImGui::Text("Entities (%d)", totalCount);
			ImGui::Separator();

			// Filter input
			ImGui::InputText("Filter", m_entityFilter, sizeof(m_entityFilter));

			ImGui::Separator();

			// Scrollable entity list
			ImGui::BeginChild("EntityList", ImVec2(0, -FLT_MIN), true);

			// Iterate through all entities with TransformComponent (filters out
			// internal Flecs entities)
			m_world.get().query<const TransformComponent>().each(
			[this](flecs::entity e, const TransformComponent&)
			{
				// Get display name from EntityInfoComponent (Unity-style, can
				// duplicate)
				std::string                displayName;
				const EntityInfoComponent* info =
				e.try_get<EntityInfoComponent>();
				if (info && !info->displayName.empty())
				{
					displayName = info->displayName;
				}
				else
				{
					// Fallback to Flecs name or generate from ID
					const char* namePtr = e.name();
					if (namePtr && strlen(namePtr) > 0)
					{
						displayName = namePtr;
					}
					else
					{
						displayName = "Entity_" + std::to_string(e.id());
					}
				}

				// Apply filter
				if (!matchesFilter(displayName.c_str()))
					return;

				// Check if this entity is selected
				bool isSelected = (e.id() == m_editorManager.getSelectedEntity());

				// Create selectable item with entity info
				char label[256];
				snprintf(
				label, sizeof(label), "%s##%lu", displayName.c_str(), static_cast<unsigned long>(e.id()));

				if (ImGui::Selectable(label, isSelected))
				{
					m_editorManager.setSelectedEntity(e.id());
				}

				// Right-click context menu
				if (ImGui::BeginPopupContextItem())
				{
					m_editorManager.setSelectedEntity(e.id());

					if (ImGui::MenuItem("Save as Prefab"))
					{
						m_editorManager.savePrefab(e.id());
					}
					if (ImGui::MenuItem("Duplicate"))
					{
						m_editorManager.duplicateSelectedEntity();
					}
					ImGui::Separator();
					if (ImGui::MenuItem("Delete"))
					{
						m_editorManager.deleteSelectedEntity();
					}

					ImGui::EndPopup();
				}

				// Show entity ID on hover
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Entity ID: %lu", static_cast<unsigned long>(e.id()));
				}
			});

			// Right-click on empty space (not on any entity) — show Create menu
			if (m_contextMenus)
			{
				if (ImGui::BeginPopupContextWindow("HierarchyEmptyContext",
				    ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
				{
					m_contextMenus->renderHierarchyMenuItems(NULL_ENTITY);
					ImGui::EndPopup();
				}
			}

			// Drag-drop target for spawning assets from Asset Browser
			if (ImGui::BeginDragDropTarget())
			{
				// Handle new ASSET_FILE payload (full path from file browser)
				if (const ImGuiPayload* payload =
				    ImGui::AcceptDragDropPayload("ASSET_FILE"))
				{
					const char* filePath = (const char*) payload->Data;
					std::filesystem::path path(filePath);

					// Check file extension
					std::string ext = path.extension().string();
					std::transform(
					ext.begin(), ext.end(), ext.begin(), ::tolower);

					if (ext == ".glb" || ext == ".gltf")
					{
						// Load if not already loaded
						std::string key = path.string();
						if (!m_editorManager.isAssetLoaded(key))
						{
							m_editorManager.loadAssetSync(path);
						}

						// Spawn the asset
						const auto& loadedAssets =
						m_editorManager.getLoadedAssets();
						auto it = loadedAssets.find(key);

						if (it != loadedAssets.end())
						{
							glm::vec3 spawnPos = glm::vec3(0.0f, 2.0f, 0.0f);
							m_entityFactory.createEntitiesFromGLTF(
							it->second,
							glm::translate(glm::mat4(1.0f), spawnPos));

							AGNI_PRINT("[ECSInspector] Spawned asset: {}\n",
							           path.filename().string());
						}
					}
					else if (ext == ".prefab")
					{
						glm::vec3 spawnPos = glm::vec3(0.0f, 2.0f, 0.0f);
						m_editorManager.instantiatePrefab(path.string(), spawnPos);
						AGNI_PRINT("[ECSInspector] Spawned prefab: {}\n",
						           path.filename().string());
					}

					// Clear drag state
					if (auto* assetBrowser = m_editorManager.getAssetBrowser())
					{
						assetBrowser->clearDrag();
					}
				}

				// Keep old ASSET_GLTF handler for backward compatibility
				if (const ImGuiPayload* payload =
				    ImGui::AcceptDragDropPayload("ASSET_GLTF"))
				{
					const char* assetName = (const char*) payload->Data;

					// Get loaded assets from editor manager
					const auto& loadedAssets =
					m_editorManager.getLoadedAssets();
					auto it = loadedAssets.find(assetName);

					if (it != loadedAssets.end())
					{
						glm::vec3 spawnPos = glm::vec3(0.0f, 2.0f, 0.0f);

						// Spawn the asset as ECS entities
						m_entityFactory.createEntitiesFromGLTF(
						it->second, glm::translate(glm::mat4(1.0f), spawnPos));

						AGNI_PRINT(
						"[ECSInspector] Spawned asset in hierarchy: {}\n",
						assetName);
					}

					// Clear drag state in asset browser
					if (auto* assetBrowser = m_editorManager.getAssetBrowser())
					{
						assetBrowser->clearDrag();
					}
				}
				ImGui::EndDragDropTarget();
			}

			ImGui::EndChild();
		}

		void ECSInspector::renderComponentInspector()
		{
			widgets::SectionHeader("Components");

			if (m_editorManager.getSelectedEntity() == NULL_ENTITY)
			{
				ImGui::TextDisabled("No entity selected");
				return;
			}

			auto entity = m_world.get().entity(m_editorManager.getSelectedEntity());
			if (!entity.is_valid())
			{
				ImGui::TextDisabled("Invalid entity");
				m_editorManager.setSelectedEntity(NULL_ENTITY);
				return;
			}

			// Show display name from EntityInfoComponent (Unity-style)
			const EntityInfoComponent* info =
			entity.try_get<EntityInfoComponent>();
			const char* displayName = (info && !info->displayName.empty())
			                          ? info->displayName.c_str()
			                          : entity.name().c_str();
			widgets::InfoRow("Entity", "%s", displayName);
			widgets::InfoRow("ID", "%llu", m_editorManager.getSelectedEntity());
			ImGui::Separator();

			// Scrollable component list
			ImGui::BeginChild("ComponentList");

			// Transform Component (special: uses gizmo, not generic fields)
			if (TransformComponent* transform =
			    m_world.getComponent<TransformComponent>(m_editorManager.getSelectedEntity()))
			{
				if (widgets::CollapsibleSection("Transform", icons::Transform))
				{
					editTransformComponent(*transform);
				}
			}

			// RenderMesh Component (special: has asset browser integration)
			if (agni::ecs::RenderMeshComponent* mesh =
			    m_world.getComponent<agni::ecs::RenderMeshComponent>(
			    m_editorManager.getSelectedEntity()))
			{
				if (widgets::CollapsibleSection(
				    "Render Mesh", icons::Mesh, ImGuiTreeNodeFlags_None))
				{
					editRenderMeshComponent(*mesh);
				}
			}

			// SceneNode Component (special: hierarchy info, not reflectable)
			if (const agni::ecs::SceneNodeComponent* node =
			    m_world.getComponent<agni::ecs::SceneNodeComponent>(
			    m_editorManager.getSelectedEntity()))
			{
				if (widgets::CollapsibleSection(
				    "Scene Node", icons::SceneNode, ImGuiTreeNodeFlags_None))
				{
					widgets::InfoRow("Depth", "%u", node->depth);
					widgets::InfoRow("Parent ID", "%llu", node->parent);
					widgets::InfoRow("Children", "%zu", node->children.size());
					widgets::PropertyCheckbox(
					"Dirty", const_cast<bool*>(&node->dirtyWorld));
				}
			}

			// === Reflection-based components (auto-generated UI) ===
			// Iterates all registered components. Skips TransformComponent
			// (handled above with gizmo).
			for (const auto* desc : agni::ComponentRegistry::Instance().GetAll())
			{
				// Skip Transform — already rendered above with custom gizmo editor
				if (std::strcmp(desc->name, "TransformComponent") == 0)
					continue;

				if (!desc->has(entity))
					continue;

				void* data = desc->getMut(entity);
				if (!data) continue;

				if (ImGui::CollapsingHeader(desc->name, ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::PushID(desc->name);

					for (const auto& prop : desc->properties)
					{
						if (prop.hidden) continue;

						void* fieldPtr = static_cast<char*>(data) + prop.offset;

						if (prop.readOnly) ImGui::BeginDisabled();

						switch (prop.type)
						{
						case agni::PropertyType::Float:
							widgets::PropertyFloat(prop.displayName, static_cast<float*>(fieldPtr),
								prop.hasRange ? prop.rangeMin : 0.0f,
								prop.hasRange ? prop.rangeMax : 0.0f);
							if (prop.unit) { ImGui::SameLine(); ImGui::TextDisabled("%s", prop.unit); }
							break;

						case agni::PropertyType::Int:
						{
							int intMin = prop.hasRange ? static_cast<int>(prop.rangeMin) : 0;
							int intMax = prop.hasRange ? static_cast<int>(prop.rangeMax) : 0;
							ImGuiSliderFlags flags = (intMin != 0 || intMax != 0) ? ImGuiSliderFlags_AlwaysClamp : 0;
							ImGui::DragInt(prop.displayName, static_cast<int*>(fieldPtr), 1.0f, intMin, intMax, "%d", flags);
							if (prop.unit) { ImGui::SameLine(); ImGui::TextDisabled("%s", prop.unit); }
							break;
						}

						case agni::PropertyType::UInt32:
						{
							int val = static_cast<int>(*static_cast<uint32_t*>(fieldPtr));
							int uintMax = prop.hasRange ? static_cast<int>(prop.rangeMax) : INT_MAX;
							if (ImGui::DragInt(prop.displayName, &val, 1.0f, 0, uintMax, "%d", ImGuiSliderFlags_AlwaysClamp))
								*static_cast<uint32_t*>(fieldPtr) = static_cast<uint32_t>(val);
							if (prop.unit) { ImGui::SameLine(); ImGui::TextDisabled("%s", prop.unit); }
							break;
						}

						case agni::PropertyType::Bool:
							widgets::PropertyCheckbox(prop.displayName, static_cast<bool*>(fieldPtr));
							break;

						case agni::PropertyType::String:
						{
							auto* str = static_cast<std::string*>(fieldPtr);
							char buf[256] = {};
							// Safe copy: destination is zero-initialized, copy up to size-1
							const size_t len = std::min(str->size(), sizeof(buf) - 1);
							std::memcpy(buf, str->c_str(), len);
							if (ImGui::InputText(prop.displayName, buf, sizeof(buf)))
								*str = buf;
							break;
						}

						case agni::PropertyType::Vec3:
							widgets::PropertyVec3(prop.displayName, static_cast<float*>(fieldPtr));
							break;

						case agni::PropertyType::Color3:
							ImGui::ColorEdit3(prop.displayName, static_cast<float*>(fieldPtr));
							break;

						case agni::PropertyType::Color4:
							ImGui::ColorEdit4(prop.displayName, static_cast<float*>(fieldPtr));
							break;

						case agni::PropertyType::Vec4:
							ImGui::DragFloat4(prop.displayName, static_cast<float*>(fieldPtr), 0.1f);
							break;

						case agni::PropertyType::Enum:
						{
							if (prop.enumDesc)
							{
								int currentVal = 0;
								std::memcpy(&currentVal, fieldPtr, std::min(prop.size, sizeof(int)));

								const char* currentName = prop.enumDesc->nameFromValue(currentVal);
								if (!currentName) currentName = "Unknown";

								if (ImGui::BeginCombo(prop.displayName, currentName))
								{
									for (const auto& c : prop.enumDesc->constants)
									{
										bool selected = (c.value == currentVal);
										if (ImGui::Selectable(c.name, selected))
										{
											int newVal = static_cast<int>(c.value);
											std::memcpy(fieldPtr, &newVal, std::min(prop.size, sizeof(int)));
										}
										if (selected) ImGui::SetItemDefaultFocus();
									}
									ImGui::EndCombo();
								}
							}
							break;
						}

						case agni::PropertyType::EntityID:
							widgets::InfoRow(prop.displayName, "%llu", *static_cast<uint64_t*>(fieldPtr));
							break;

						default:
							ImGui::TextDisabled("%s (unsupported type)", prop.displayName);
							break;
						}

						// Tooltip
						if (prop.tooltip && prop.tooltip[0] != '\0')
							if (ImGui::IsItemHovered())
								ImGui::SetTooltip("%s", prop.tooltip);

						if (prop.readOnly) ImGui::EndDisabled();
					}

					ImGui::PopID();
				}
			}

			// === "Add Component" button ===
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
			if (ImGui::Button("Add Component", ImVec2(-1, 0)))
				ImGui::OpenPopup("AddComponentPopup");

			if (ImGui::BeginPopup("AddComponentPopup"))
			{
				for (const auto* desc : agni::ComponentRegistry::Instance().GetAll())
				{
					if (desc->has(entity)) continue; // already has it

					if (ImGui::MenuItem(desc->name))
					{
						// Construct default component and set on entity
						alignas(16) uint8_t buffer[512];
						assert(desc->typeSize <= sizeof(buffer));
						desc->construct(buffer);
						desc->set(entity, buffer);
						desc->destruct(buffer);
					}
				}
				ImGui::EndPopup();
			}

			ImGui::EndChild();
		}

		void ECSInspector::editTransformComponent(TransformComponent& transform)
		{
			ImGui::PushID("Transform");

			// Extract position from local transform
			glm::vec3 position = glm::vec3(transform.localTransform[3]);
			if (widgets::PropertyVec3(
			    "Position", glm::value_ptr(position), 0.0f, 0.1f))
			{
				m_world.setPosition(m_editorManager.getSelectedEntity(), position);
			}

			// Show world position (read-only)
			glm::vec3 worldPos = glm::vec3(transform.worldTransform[3]);
			ImGui::PushStyleColor(ImGuiCol_Text, colors::TextDim);
			ImGui::Text(
			"World: (%.2f, %.2f, %.2f)", worldPos.x, worldPos.y, worldPos.z);
			ImGui::PopStyleColor();

			ImGui::PopID();
		}

		void ECSInspector::editRigidBodyComponent(RigidBodyComponent& rigidbody)
		{
			ImGui::PushID("RigidBody");

			// Body type
			const char* bodyTypes[] = {"Static", "Dynamic", "Kinematic"};
			int         currentType = static_cast<int>(rigidbody.type);
			if (widgets::PropertyCombo("Type", &currentType, bodyTypes, 3))
			{
				rigidbody.type = static_cast<RigidBodyType>(currentType);
			}

			// Physics properties
			widgets::PropertyFloat(
			"Mass", &rigidbody.mass, 0.1f, 100.0f, "%.1f");
			widgets::PropertyFloat(
			"Friction", &rigidbody.friction, 0.0f, 1.0f, "%.2f");
			widgets::PropertyFloat(
			"Restitution", &rigidbody.restitution, 0.0f, 1.0f, "%.2f");
			widgets::PropertyCheckbox("Use Gravity", &rigidbody.useGravity);

			// Velocity (read-only display)
			widgets::SeparatorText("Velocity");
			widgets::InfoRow("Linear",
			                 "(%.2f, %.2f, %.2f)",
			                 rigidbody.linearVelocity.x,
			                 rigidbody.linearVelocity.y,
			                 rigidbody.linearVelocity.z);

			widgets::InfoRow("Angular",
			                 "(%.2f, %.2f, %.2f)",
			                 rigidbody.angularVelocity.x,
			                 rigidbody.angularVelocity.y,
			                 rigidbody.angularVelocity.z);

			// Body ID
			widgets::InfoRow("Body ID", "%u", rigidbody.joltBodyID);

			ImGui::PopID();
		}

		void ECSInspector::editColliderComponent(ColliderComponent& collider)
		{
			ImGui::PushID("Collider");

			// Collider type
			const char* colliderTypes[] = {"Box", "Sphere", "Capsule"};
			int         currentType     = static_cast<int>(collider.type);
			if (widgets::PropertyCombo("Type", &currentType, colliderTypes, 3))
			{
				collider.type = static_cast<ColliderType>(currentType);
			}

			// Shape parameters based on type
			switch (collider.type)
			{
				case ColliderType::Box:
					widgets::PropertyVec3(
					"Half Extents",
					glm::value_ptr(collider.boxHalfExtents),
					0.5f,
					0.05f);
					break;
				case ColliderType::Sphere:
					widgets::PropertyFloat(
					"Radius", &collider.sphereRadius, 0.01f, 10.0f, "%.2f");
					break;
				case ColliderType::Capsule:
					widgets::PropertyFloat(
					"Radius", &collider.capsuleRadius, 0.01f, 10.0f, "%.2f");
					widgets::PropertyFloat("Half Height",
					                       &collider.capsuleHalfHeight,
					                       0.01f,
					                       10.0f,
					                       "%.2f");
					break;
			}

			// Center offset
			widgets::PropertyVec3(
			"Center", glm::value_ptr(collider.center), 0.0f, 0.05f);

			// Flags
			widgets::PropertyCheckbox("Is Trigger", &collider.isTrigger);

			ImGui::PopID();
		}

		void ECSInspector::editLightComponent(LightComponent& light)
		{
			ImGui::PushID("Light");

			// Light type
			const char* lightTypes[] = {"Point", "Directional", "Spot"};
			int         currentType  = static_cast<int>(light.type);
			if (widgets::PropertyCombo("Type", &currentType, lightTypes, 3))
			{
				light.type = static_cast<LightType>(currentType);
			}

			// Color
			widgets::PropertyColor3("Color", glm::value_ptr(light.color));

			// Intensity
			widgets::PropertyFloat(
			"Intensity", &light.intensity, 0.0f, 100.0f, "%.1f");

			// Type-specific properties
			if (light.type == LightType::Point || light.type == LightType::Spot)
			{
				widgets::PropertyFloat(
				"Radius", &light.radius, 0.1f, 100.0f, "%.1f");
			}

			if (light.type == LightType::Directional ||
			    light.type == LightType::Spot)
			{
				widgets::PropertyVec3(
				"Direction", glm::value_ptr(light.direction), 0.0f, 0.01f);
			}

			if (light.type == LightType::Spot)
			{
				widgets::PropertyFloat(
				"Inner Cone", &light.innerConeAngle, 0.0f, 90.0f, "%.1f deg");
				widgets::PropertyFloat(
				"Outer Cone", &light.outerConeAngle, 0.0f, 90.0f, "%.1f deg");
			}

			ImGui::PopID();
		}

		void ECSInspector::editRenderMeshComponent(
		agni::ecs::RenderMeshComponent& mesh)
		{
			ImGui::PushID("RenderMesh");

			ImGui::Checkbox("Visible", &mesh.visible);

			if (mesh.meshAsset)
			{
				ImGui::Text("Mesh: %s", mesh.meshAsset->m_name.c_str());
				ImGui::Text("Surfaces: %zu", mesh.meshAsset->m_surfaces.size());
			}
			else
			{
				ImGui::TextDisabled("No mesh asset");
			}

			ImGui::PopID();
		}

		void ECSInspector::renderCreateEntityPopup()
		{
			ImGui::OpenPopup("Create Entity");

			if (ImGui::BeginPopupModal("Create Entity",
			                           &m_showCreateEntityPopup,
			                           ImGuiWindowFlags_AlwaysAutoResize))
			{
				ImGui::InputText(
				"Name", m_newEntityName, sizeof(m_newEntityName));

				ImGui::Combo("Type", &m_newEntityType, "Empty\0Mesh\0Light\0");

				ImGui::DragFloat3(
				"Position", glm::value_ptr(m_spawnPosition), 0.1f);

				// Mesh selection if creating mesh entity
				if (m_newEntityType == 1 && m_meshResources)
				{
					ImGui::Text("Select Mesh:");
					int idx = 0;
					for (auto& [name, mesh] : m_meshResources->meshes)
					{
						if (ImGui::Selectable(name.c_str(),
						                      m_selectedMeshIndex == idx))
						{
							m_selectedMeshIndex = idx;
						}
						idx++;
					}
				}

				ImGui::Separator();

				if (ImGui::Button("Create"))
				{
					glm::mat4 transform =
					glm::translate(glm::mat4(1.0f), m_spawnPosition);

					if (m_newEntityType == 0) // Empty
					{
						m_world.createEntity(strlen(m_newEntityName) > 0
						                     ? m_newEntityName
						                     : nullptr);
					}
					else if (m_newEntityType == 1 && m_meshResources) // Mesh
					{
						// Get selected mesh
						int                        idx = 0;
						std::shared_ptr<MeshAsset> selectedMesh;
						for (auto& [name, mesh] : m_meshResources->meshes)
						{
							if (idx == m_selectedMeshIndex)
							{
								selectedMesh = mesh;
								break;
							}
							idx++;
						}

						if (selectedMesh)
						{
							auto entity = m_entityFactory.createMeshEntity(
							selectedMesh,
							transform,
							strlen(m_newEntityName) > 0 ? m_newEntityName
							                            : nullptr);

							// Add physics by default
							m_world.addComponent(
							entity.id(),
							RigidBodyComponent {.type = RigidBodyType::Dynamic,
							                    .mass = 1.0f});
							m_world.addComponent(
							entity.id(),
							ColliderComponent {.type = ColliderType::Box,
							                   .boxHalfExtents =
							                   glm::vec3(0.5f)});

							AGNI_PRINT("[ECSInspector] Created mesh entity "
							           "'{}' at ({}, {}, {})\n",
							           m_newEntityName,
							           m_spawnPosition.x,
							           m_spawnPosition.y,
							           m_spawnPosition.z);
						}
					}
					else if (m_newEntityType == 2) // Light
					{
						[[maybe_unused]] auto entity = m_entityFactory.createLightEntity(
						LightComponent {},
						transform,
						strlen(m_newEntityName) > 0 ? m_newEntityName
						                            : nullptr);
					}

					m_showCreateEntityPopup = false;
					memset(m_newEntityName, 0, sizeof(m_newEntityName));
				}

				ImGui::SameLine();

				if (ImGui::Button("Cancel"))
				{
					m_showCreateEntityPopup = false;
				}

				ImGui::EndPopup();
			}
		}

		bool ECSInspector::matchesFilter(const char* entityName) const
		{
			if (strlen(m_entityFilter) == 0)
				return true;

			return strstr(entityName, m_entityFilter) != nullptr;
		}

		void ECSInspector::renderGizmo(Camera* camera, VkExtent2D windowExtent)
		{
			if (!camera || m_editorManager.getSelectedEntity() == NULL_ENTITY)
				return;

			auto entity = m_world.get().entity(m_editorManager.getSelectedEntity());
			if (!entity.is_valid())
				return;

			TransformComponent* transform =
			m_world.getComponent<TransformComponent>(m_editorManager.getSelectedEntity());
			if (!transform)
				return;

			// Setup ImGuizmo
			ImGuizmo::SetOrthographic(false);
			ImGuizmo::BeginFrame();

			// Get camera matrices
			glm::mat4 view = camera->getViewMatrix();
			// ImGuizmo expects OpenGL-style projection (no Y-flip, standard
			// near/far order)
			glm::mat4 projection = glm::perspective(glm::radians(70.f),
			                                        (float) windowExtent.width /
			                                        (float) windowExtent.height,
			                                        0.1f,
			                                        10000.f);

			// Set ImGuizmo rect to cover full viewport
			ImGuizmo::SetRect(
			0, 0, (float) windowExtent.width, (float) windowExtent.height);

			// Get gizmo operation
			ImGuizmo::OPERATION operation;
			switch (m_gizmoOperation)
			{
				case 0:
					operation = ImGuizmo::TRANSLATE;
					break;
				case 1:
					operation = ImGuizmo::ROTATE;
					break;
				case 2:
					operation = ImGuizmo::SCALE;
					break;
				default:
					operation = ImGuizmo::TRANSLATE;
			}

			// Get gizmo mode
			ImGuizmo::MODE mode =
			(m_gizmoMode == 0) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

			// Prepare snap values
			float* snap = m_useSnap ? &m_snapValues[m_gizmoOperation] : nullptr;

			// Get the SceneNode to check hierarchy
			const agni::ecs::SceneNodeComponent* node =
			m_world.getComponent<agni::ecs::SceneNodeComponent>(
			m_editorManager.getSelectedEntity());

			// Use local transform for root entities, world transform for
			// children This ensures the gizmo shows the correct position
			glm::mat4 matrix;
			if (node && node->parent == NULL_ENTITY)
			{
				// Root entity: use local transform (which equals world
				// transform)
				matrix = transform->localTransform;
			}
			else
			{
				// Child entity: use world transform
				matrix = transform->worldTransform;
			}

			// Manipulate
			bool manipulated = ImGuizmo::Manipulate(glm::value_ptr(view),
			                                        glm::value_ptr(projection),
			                                        operation,
			                                        mode,
			                                        glm::value_ptr(matrix),
			                                        nullptr,
			                                        snap);

			// Update transform if manipulated
			if (manipulated)
			{
				if (node && node->parent == NULL_ENTITY)
				{
					// No parent: directly set local transform
					m_world.setLocalTransform(m_editorManager.getSelectedEntity(), matrix);
				}
				else if (node && node->parent != NULL_ENTITY)
				{
					// Has parent: calculate local from world
					// localTransform = inverse(parentWorld) * worldTransform
					auto parent = m_world.get().entity(node->parent);
					if (parent.is_valid())
					{
						const TransformComponent* parentTransform =
						m_world.getComponent<TransformComponent>(node->parent);
						if (parentTransform)
						{
							glm::mat4 localTransform =
							glm::inverse(parentTransform->worldTransform) *
							matrix;
							m_world.setLocalTransform(m_editorManager.getSelectedEntity(),
							                          localTransform);
						}
					}
				}

#ifdef AGNI_HAS_JOLT
				// Also update physics body if this entity has one
				// This prevents physics from overwriting the gizmo changes
				if (m_physicsManager)
				{
					const RigidBodyComponent* rigidbody =
					m_world.getComponent<RigidBodyComponent>(m_editorManager.getSelectedEntity());
					if (rigidbody && rigidbody->joltBodyID != 0)
					{
						// Update the physics body transform to match the new
						// world transform
						m_physicsManager->setBodyTransform(
						rigidbody->joltBodyID, matrix);
					}
				}
#endif
			}
		}

	} // namespace editor
} // namespace agni
