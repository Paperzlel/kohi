#include "editor.h"

#include "assets/kasset_types.h"
#include "audio/audio_frontend.h"
#include "controls/checkbox_control.h"
#include "controls/image_box_control.h"
#include "controls/kui_scrollable.h"
#include "controls/kui_tree_item.h"
#include "core/event.h"
#include "core/keymap.h"
#include "core_resource_types.h"
#include "debug/kassert.h"
#include "defines.h"
#include "editor/editor_gizmo.h"
#include "input_types.h"
#include "kui_system.h"
#include "kui_types.h"
#include "math/geometry_2d.h"
#include "math/math_types.h"
#include "memory/kmemory.h"
#include "plugins/plugin_types.h"
#include "renderer/renderer_frontend.h"
#include "strings/kname.h"
#include "strings/kstring.h"
#include "systems/asset_system.h"
#include "systems/kcamera_system.h"
#include "systems/kshader_system.h"
#include "systems/plugin_system.h"
#include "systems/texture_system.h"
#include "utils/kcolour.h"
#include "utils_plugin_defines.h"
#include "world/heightfield_terrain.h"
#include "world/kscene.h"
#include "world/world_types.h"
#include "world/world_utils.h"

#include <containers/darray.h>
#include <controls/kui_button.h>
#include <controls/kui_label.h>
#include <controls/kui_panel.h>
#include <controls/kui_textbox.h>
#include <core/console.h>
#include <core/engine.h>
#include <kui_plugin_main.h>
#include <math/kmath.h>
#include <platform/platform.h>
#include <systems/ktimeline_system.h>
#include <utils/ksort.h>

#include <logger.h>

typedef struct editor_gizmo_global_ubo {
	mat4 projection;
	mat4 view;
} editor_gizmo_global_ubo;

typedef struct editor_gizmo_immediate_data {
	mat4 model;
} editor_gizmo_immediate_data;

static f32 get_engine_delta_time(void);
static f32 get_engine_total_time(void);

static void editor_on_yaw(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static void editor_on_pitch(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static void editor_on_set_render_mode_default(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static void editor_on_set_render_mode_lighting(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static void editor_on_set_render_mode_normals(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static void editor_on_set_render_mode_cascades(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static void editor_on_set_render_mode_wireframe(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static void editor_on_set_gizmo_mode(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static void editor_on_gizmo_orientation_set(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static void editor_on_move_forward(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static void editor_on_sprint_forward(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static void editor_on_move_backward(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static void editor_on_move_left(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static void editor_on_move_right(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static void editor_on_move_up(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static void editor_on_move_down(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static void editor_on_save_scene(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static void editor_on_zoom_extents(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data);
static b8 editor_on_mouse_move(u16 code, void* sender, void* listener_inst, event_context context);
static b8 editor_on_drag(u16 code, void* sender, void* listener_inst, event_context context);
static b8 editor_on_button(u16 code, void* sender, void* listener_inst, event_context context);

static void editor_command_execute(console_command_context context);

static void editor_register_events(struct editor_state* state);
static void editor_unregister_events(struct editor_state* state);
static void editor_register_commands(struct editor_state* state);
static void editor_unregister_commands(struct editor_state* state);

static b8 save_button_clicked(struct kui_state* state, kui_control self, struct kui_mouse_event event);
static b8 mode_scene_button_clicked(struct kui_state* state, kui_control self, struct kui_mouse_event event);
static b8 mode_entity_button_clicked(struct kui_state* state, kui_control self, struct kui_mouse_event event);
static b8 mode_tree_button_clicked(struct kui_state* state, kui_control self, struct kui_mouse_event event);
static b8 mode_hf_terrain_button_clicked(struct kui_state* state, kui_control self, struct kui_mouse_event event);

static void show_bvh_checkbox_check_changed(struct kui_state* state, kui_control self, struct kui_checkbox_event event);
static void show_grid_checkbox_check_changed(struct kui_state* state, kui_control self, struct kui_checkbox_event event);
static void show_debug_checkbox_check_changed(struct kui_state* state, kui_control self, struct kui_checkbox_event event);
static void scene_visibility_checkbox_check_changed(struct kui_state* state, kui_control self, struct kui_checkbox_event event);

static void scene_name_textbox_on_key(kui_state* state, kui_control self, kui_keyboard_event evt);
static void scene_fog_colour_textbox_on_key(kui_state* state, kui_control self, kui_keyboard_event evt);

static void entity_name_textbox_on_key(kui_state* state, kui_control self, kui_keyboard_event evt);
static void entity_position_x_textbox_on_key(kui_state* state, kui_control self, kui_keyboard_event evt);
static void entity_position_y_textbox_on_key(kui_state* state, kui_control self, kui_keyboard_event evt);
static void entity_position_z_textbox_on_key(kui_state* state, kui_control self, kui_keyboard_event evt);
static void entity_orientation_x_textbox_on_key(kui_state* state, kui_control self, kui_keyboard_event evt);
static void entity_orientation_y_textbox_on_key(kui_state* state, kui_control self, kui_keyboard_event evt);
static void entity_orientation_z_textbox_on_key(kui_state* state, kui_control self, kui_keyboard_event evt);
static void entity_orientation_w_textbox_on_key(kui_state* state, kui_control self, kui_keyboard_event evt);
static void entity_scale_x_textbox_on_key(kui_state* state, kui_control self, kui_keyboard_event evt);
static void entity_scale_y_textbox_on_key(kui_state* state, kui_control self, kui_keyboard_event evt);
static void entity_scale_z_textbox_on_key(kui_state* state, kui_control self, kui_keyboard_event evt);

static void tree_clear(editor_state* state);
static void tree_refresh(editor_state* state);
static b8 tree_item_clicked(struct kui_state* state, kui_control self, struct kui_mouse_event event);
static b8 tree_item_expanded(struct kui_state* state, kui_control self, struct kui_mouse_event event);
static b8 tree_item_collapsed(struct kui_state* state, kui_control self, struct kui_mouse_event event);

static void hft_paint_brush_diameter_textbox_on_key(kui_state* state, kui_control self, kui_keyboard_event evt);
static void hft_paint_brush_strength_textbox_on_key(kui_state* state, kui_control self, kui_keyboard_event evt);
static void hft_paint_material_index_textbox_on_key(kui_state* state, kui_control self, kui_keyboard_event evt);

static void hf_terrain_checkbox_check_changed(struct kui_state* state, kui_control self, struct kui_checkbox_event event);

b8 editor_initialize(u64* memory_requirement, struct editor_state* state) {
	*memory_requirement = sizeof(editor_state);
	if (!state) {
		return true;
	}

	// Setup gizmo.
	if (!editor_gizmo_create(&state->gizmo)) {
		KERROR("Failed to create editor gizmo!");
		return false;
	}
	if (!editor_gizmo_initialize(&state->gizmo)) {
		KERROR("Failed to initialize editor gizmo!");
		return false;
	}
	if (!editor_gizmo_load(&state->gizmo)) {
		KERROR("Failed to load editor gizmo!");
		return false;
	}

	state->renderer = engine_systems_get()->renderer_system;

	state->standard_vertex_buffer = renderer_renderbuffer_get(state->renderer, kname_create(KRENDERBUFFER_NAME_VERTEX_STANDARD));
	state->index_buffer = renderer_renderbuffer_get(state->renderer, kname_create(KRENDERBUFFER_NAME_INDEX_STANDARD));

	// Editor gizmo pass state
	{
		state->editor_gizmo_pass.gizmo_shader = kshader_system_get(kname_create(SHADER_NAME_PLUGIN_UTILS_EDITOR_GIZMO), kname_create(PACKAGE_NAME_PLUGIN_UTILS));
		KASSERT_DEBUG(state->editor_gizmo_pass.gizmo_shader != KSHADER_INVALID);

		state->editor_gizmo_pass.set0_instance_id = kshader_acquire_binding_set_instance(state->editor_gizmo_pass.gizmo_shader, 0);
	}

	// Editor camera. Use the same view properties of the world camera, but different starting position/rotation.
	vec3 editor_cam_pos = (vec3){-10.0f, 10.0f, -10.0f};
	vec3 editor_cam_rot_euler_radians = (vec3){deg_to_rad(-35.0f), deg_to_rad(225.0f), deg_to_rad(0.0f)};
	rect_2di world_vp_rect = {0, 0, 1280 - 40, 720 - 40};
	state->editor_camera = kcamera_create(KCAMERA_TYPE_3D, world_vp_rect, editor_cam_pos, editor_cam_rot_euler_radians, deg_to_rad(45.0f), 0.1f, 1000.0f);

	state->editor_camera_forward_move_speed = 5.0f * 5.0f;
	state->editor_camera_backward_move_speed = 2.5f * 5.0f;

	state->selection_list = darray_create(kentity);

	kruntime_plugin* kui_plugin = plugin_system_get(engine_systems_get()->plugin_system, "kohi.plugin.ui.kui");
	kui_state* kui_state = ((kui_plugin_state*)kui_plugin->plugin_state)->state;
	state->kui_state = kui_state;

	// UI elements. Create/load them all up here.
	state->font_name = kname_create("Noto Sans CJK JP, Bold");
	state->font_size = 32;
	state->textbox_font_name = kname_create("Noto Sans Mono CJK JP");
	state->textbox_font_size = 30;

	// Main root control for everything else to belong to.
	{
		state->editor_root = kui_base_control_create(kui_state, "editor_root", KUI_CONTROL_TYPE_BASE);
		KASSERT(kui_system_control_add_child(kui_state, INVALID_KUI_CONTROL, state->editor_root));

		kui_control_set_is_visible(kui_state, state->editor_root, false);
	}

	// Main window
	{
		// Main background panel.
		state->main_bg_panel = kui_panel_control_create(kui_state, "main_bg_panel", (vec2){250.0f, 600.0f}, (vec4){0.0f, 0.0f, 0.0f, 0.75f});
		KASSERT(kui_system_control_add_child(kui_state, state->editor_root, state->main_bg_panel));
		kui_control_position_set(kui_state, state->main_bg_panel, (vec3){10, 10, 0});

		// Save button.
		{
			state->save_button = kui_button_control_create_with_text(kui_state, "save_button", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Save");
			KASSERT(kui_system_control_add_child(kui_state, state->main_bg_panel, state->save_button));
			kui_button_control_width_set(kui_state, state->save_button, 240);
			kui_control_position_set(kui_state, state->save_button, (vec3){5, 50, 0});
			kui_control_set_on_click(kui_state, state->save_button, save_button_clicked);
		}

		// Scene mode button.
		{
			state->mode_scene_button = kui_button_control_create_with_text(kui_state, "mode_scene_button", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Scene");
			KASSERT(kui_system_control_add_child(kui_state, state->main_bg_panel, state->mode_scene_button));
			kui_button_control_width_set(kui_state, state->mode_scene_button, 115);
			kui_control_position_set(kui_state, state->mode_scene_button, (vec3){5, 100, 0});
			kui_control_set_user_data(kui_state, state->mode_scene_button, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_click(kui_state, state->mode_scene_button, mode_scene_button_clicked);
		}

		// Entity mode button.
		{
			state->mode_entity_button = kui_button_control_create_with_text(kui_state, "mode_entity_button", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Entity");
			KASSERT(kui_system_control_add_child(kui_state, state->main_bg_panel, state->mode_entity_button));
			kui_button_control_width_set(kui_state, state->mode_entity_button, 115);
			kui_control_position_set(kui_state, state->mode_entity_button, (vec3){125, 100, 0});
			kui_control_set_user_data(kui_state, state->mode_entity_button, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_click(kui_state, state->mode_entity_button, mode_entity_button_clicked);
		}

		// Tree mode button.
		{
			state->mode_tree_button = kui_button_control_create_with_text(kui_state, "mode_tree_button", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Tree");
			KASSERT(kui_system_control_add_child(kui_state, state->main_bg_panel, state->mode_tree_button));
			kui_button_control_width_set(kui_state, state->mode_tree_button, 115);
			kui_control_position_set(kui_state, state->mode_tree_button, (vec3){5, 150, 0});
			kui_control_set_user_data(kui_state, state->mode_tree_button, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_click(kui_state, state->mode_tree_button, mode_tree_button_clicked);
		}

		// HF Terrain mode button.
		{
			state->mode_hf_terrain_button = kui_button_control_create_with_text(kui_state, "mode_hf_terrain_button", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "HF Terr.");
			KASSERT(kui_system_control_add_child(kui_state, state->main_bg_panel, state->mode_hf_terrain_button));
			kui_button_control_width_set(kui_state, state->mode_hf_terrain_button, 115);
			kui_control_position_set(kui_state, state->mode_hf_terrain_button, (vec3){125, 150, 0});
			kui_control_set_user_data(kui_state, state->mode_hf_terrain_button, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_click(kui_state, state->mode_hf_terrain_button, mode_hf_terrain_button_clicked);
		}

		// Toggle options label
		state->view_label = kui_label_control_create(kui_state, "view_label", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Visibility:");
		KASSERT(kui_system_control_add_child(kui_state, state->main_bg_panel, state->view_label));
		kui_control_position_set(kui_state, state->view_label, (vec3){5, 270.0f, 0});

		// Debug toggles
		{
			// Toggle visiblity of general debug data. If this is disabled, so are the other debug options.
			{
				state->view_debug_checkbox = kui_checkbox_control_create(kui_state, "view_debug_checkbox", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Debug");
				KASSERT(kui_system_control_add_child(kui_state, state->main_bg_panel, state->view_debug_checkbox));
				kui_control_position_set(kui_state, state->view_debug_checkbox, (vec3){5, 300, 0});
				kui_control_set_user_data(kui_state, state->view_debug_checkbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
				kui_checkbox_set_on_checked(kui_state, state->view_debug_checkbox, show_debug_checkbox_check_changed);
			}

			// Toggle visiblity of BVH debug data.
			{
				state->view_bvh_checkbox = kui_checkbox_control_create(kui_state, "view_bvh_checkbox", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "BVH");
				KASSERT(kui_system_control_add_child(kui_state, state->main_bg_panel, state->view_bvh_checkbox));
				kui_control_position_set(kui_state, state->view_bvh_checkbox, (vec3){35, 330, 0});
				kui_control_set_user_data(kui_state, state->view_bvh_checkbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
				kui_checkbox_set_on_checked(kui_state, state->view_bvh_checkbox, show_bvh_checkbox_check_changed);
			}

			// Toggle visiblity of debug grid.
			{
				state->view_grid_checkbox = kui_checkbox_control_create(kui_state, "view_grid_checkbox", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Grid");
				KASSERT(kui_system_control_add_child(kui_state, state->main_bg_panel, state->view_grid_checkbox));
				kui_control_position_set(kui_state, state->view_grid_checkbox, (vec3){35, 360, 0});
				kui_control_set_user_data(kui_state, state->view_grid_checkbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
				kui_checkbox_set_on_checked(kui_state, state->view_grid_checkbox, show_grid_checkbox_check_changed);
			}
		}

		// General scene visiblity
		{
			// Toggle visiblity of the skybox.
			{
				state->view_skybox_checkbox = kui_checkbox_control_create(kui_state, "view_skybox_checkbox", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Skybox");
				KASSERT(kui_system_control_add_child(kui_state, state->main_bg_panel, state->view_skybox_checkbox));
				kui_control_position_set(kui_state, state->view_skybox_checkbox, (vec3){5, 390, 0});
				kui_control_set_user_data(kui_state, state->view_skybox_checkbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
				kui_checkbox_set_on_checked(kui_state, state->view_skybox_checkbox, scene_visibility_checkbox_check_changed);
			}

			// Toggle visiblity of fog.
			{
				state->view_fog_checkbox = kui_checkbox_control_create(kui_state, "view_fog_checkbox", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Fog");
				KASSERT(kui_system_control_add_child(kui_state, state->main_bg_panel, state->view_fog_checkbox));
				kui_control_position_set(kui_state, state->view_fog_checkbox, (vec3){5, 420, 0});
				kui_control_set_user_data(kui_state, state->view_fog_checkbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
				kui_checkbox_set_on_checked(kui_state, state->view_fog_checkbox, scene_visibility_checkbox_check_changed);
			}
		}
	}

	// Scene inspector window panel.
	{
		state->scene_inspector_width = 650.0f;
		state->scene_inspector_right_col_x = 130.0f;
		state->scene_inspector_bg_panel = kui_panel_control_create(kui_state, "scene_inspector_bg_panel", (vec2){state->scene_inspector_width, 400.0f}, (vec4){0.0f, 0.0f, 0.0f, 0.75f});
		KASSERT(kui_system_control_add_child(kui_state, state->editor_root, state->scene_inspector_bg_panel));
		kui_control_position_set(kui_state, state->scene_inspector_bg_panel, (vec3){1280 - (state->scene_inspector_width + 10)});
		kui_control_set_is_active(kui_state, state->scene_inspector_bg_panel, false);
		kui_control_set_is_visible(kui_state, state->scene_inspector_bg_panel, false);

		// Window Label
		state->scene_inspector_title = kui_label_control_create(kui_state, "scene_inspector_title", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Scene");
		KASSERT(kui_system_control_add_child(kui_state, state->scene_inspector_bg_panel, state->scene_inspector_title));
		kui_control_position_set(kui_state, state->scene_inspector_title, (vec3){10, -5.0f, 0});

		// scene name
		{
			// Name label.
			state->scene_name_label = kui_label_control_create(kui_state, "scene_name_label", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Name");
			KASSERT(kui_system_control_add_child(kui_state, state->scene_inspector_bg_panel, state->scene_name_label));
			kui_control_position_set(kui_state, state->scene_name_label, (vec3){10, 50 + -5.0f, 0});

			// Name textbox.
			state->scene_name_textbox = kui_textbox_control_create(kui_state, "scene_name_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_STRING);
			KASSERT(kui_system_control_add_child(kui_state, state->scene_inspector_bg_panel, state->scene_name_textbox));
			KASSERT(kui_textbox_control_width_set(kui_state, state->scene_name_textbox, 380));
			kui_control_position_set(kui_state, state->scene_name_textbox, (vec3){state->scene_inspector_right_col_x, 50, 0});
			kui_control_set_user_data(kui_state, state->scene_name_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->scene_name_textbox, scene_name_textbox_on_key);
		}

		// Fog colour
		{
			// Fog colour label
			state->scene_fog_colour_label = kui_label_control_create(kui_state, "scene_fog_colour_label", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Fog colour");
			KASSERT(kui_system_control_add_child(kui_state, state->scene_inspector_bg_panel, state->scene_fog_colour_label));
			kui_control_position_set(kui_state, state->scene_fog_colour_label, (vec3){10, 100 + -5.0f, 0});

			// Fog colour R textbox.
			state->scene_fog_colour_r_textbox = kui_textbox_control_create(kui_state, "scene_fog_colour_r_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_FLOAT);
			KASSERT(kui_system_control_add_child(kui_state, state->scene_inspector_bg_panel, state->scene_fog_colour_r_textbox));
			kui_control_position_set(kui_state, state->scene_fog_colour_r_textbox, (vec3){state->scene_inspector_right_col_x, 100, 0});
			KASSERT(kui_textbox_control_width_set(kui_state, state->scene_fog_colour_r_textbox, 120));
			kui_textbox_control_colour_set(kui_state, state->scene_fog_colour_r_textbox, EDITOR_AXIS_COLOUR_R);
			kui_control_set_user_data(kui_state, state->scene_fog_colour_r_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->scene_fog_colour_r_textbox, scene_fog_colour_textbox_on_key);

			// Fog colour g textbox.
			state->scene_fog_colour_g_textbox = kui_textbox_control_create(kui_state, "scene_fog_colour_g_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_FLOAT);
			KASSERT(kui_system_control_add_child(kui_state, state->scene_inspector_bg_panel, state->scene_fog_colour_g_textbox));
			kui_control_position_set(kui_state, state->scene_fog_colour_g_textbox, (vec3){state->scene_inspector_right_col_x + 130, 100, 0});
			KASSERT(kui_textbox_control_width_set(kui_state, state->scene_fog_colour_g_textbox, 120));
			kui_textbox_control_colour_set(kui_state, state->scene_fog_colour_g_textbox, EDITOR_AXIS_COLOUR_G);
			kui_control_set_user_data(kui_state, state->scene_fog_colour_g_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->scene_fog_colour_g_textbox, scene_fog_colour_textbox_on_key);

			// Fog colour b textbox.
			state->scene_fog_colour_b_textbox = kui_textbox_control_create(kui_state, "scene_fog_colour_b_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_FLOAT);
			KASSERT(kui_system_control_add_child(kui_state, state->scene_inspector_bg_panel, state->scene_fog_colour_b_textbox));
			kui_control_position_set(kui_state, state->scene_fog_colour_b_textbox, (vec3){state->scene_inspector_right_col_x + 260, 100, 0});
			KASSERT(kui_textbox_control_width_set(kui_state, state->scene_fog_colour_b_textbox, 120));
			kui_textbox_control_colour_set(kui_state, state->scene_fog_colour_b_textbox, EDITOR_AXIS_COLOUR_B);
			kui_control_set_user_data(kui_state, state->scene_fog_colour_b_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->scene_fog_colour_b_textbox, scene_fog_colour_textbox_on_key);

			// Fog colour a textbox.
			state->scene_fog_colour_a_textbox = kui_textbox_control_create(kui_state, "scene_fog_colour_a_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_FLOAT);
			KASSERT(kui_system_control_add_child(kui_state, state->scene_inspector_bg_panel, state->scene_fog_colour_a_textbox));
			kui_control_position_set(kui_state, state->scene_fog_colour_a_textbox, (vec3){state->scene_inspector_right_col_x + 390, 100, 0});
			KASSERT(kui_textbox_control_width_set(kui_state, state->scene_fog_colour_a_textbox, 120));
			/* kui_textbox_control_colour_set(kui_state, state->scene_fog_colour_a_textbox, EDITOR_AXIS_COLOUR_A); */
			kui_control_set_user_data(kui_state, state->scene_fog_colour_a_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->scene_fog_colour_a_textbox, scene_fog_colour_textbox_on_key);
		}

		// TODO: more controls
	}

	// Entity inspector window panel.
	{
		state->entity_inspector_width = 650.0f;
		state->entity_inspector_right_col_x = 130.0f;
		state->entity_inspector_bg_panel = kui_panel_control_create(kui_state, "entity_inspector_bg_panel", (vec2){state->entity_inspector_width, 400.0f}, (vec4){0.0f, 0.0f, 0.0f, 0.75f});
		KASSERT(kui_system_control_add_child(kui_state, state->editor_root, state->entity_inspector_bg_panel));
		kui_control_position_set(kui_state, state->entity_inspector_bg_panel, (vec3){1280 - (state->entity_inspector_width + 10)});
		kui_control_set_is_active(kui_state, state->entity_inspector_bg_panel, false);
		kui_control_set_is_visible(kui_state, state->entity_inspector_bg_panel, false);

		// Window Label
		state->entity_inspector_title = kui_label_control_create(kui_state, "entity_inspector_title", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Entity (no selection)");
		KASSERT(kui_system_control_add_child(kui_state, state->entity_inspector_bg_panel, state->entity_inspector_title));
		kui_control_position_set(kui_state, state->entity_inspector_title, (vec3){10, -5.0f, 0});

		// Entity name
		{
			// Name label.
			state->entity_name_label = kui_label_control_create(kui_state, "entity_name_label", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Name:");
			KASSERT(kui_system_control_add_child(kui_state, state->entity_inspector_bg_panel, state->entity_name_label));
			kui_control_position_set(kui_state, state->entity_name_label, (vec3){10, 50 + -5.0f, 0});

			// Name textbox.
			state->entity_name_textbox = kui_textbox_control_create(kui_state, "entity_name_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_STRING);
			KASSERT(kui_system_control_add_child(kui_state, state->entity_inspector_bg_panel, state->entity_name_textbox));
			KASSERT(kui_textbox_control_width_set(kui_state, state->entity_name_textbox, 380));
			kui_control_position_set(kui_state, state->entity_name_textbox, (vec3){state->entity_inspector_right_col_x, 50, 0});
			kui_control_set_user_data(kui_state, state->entity_name_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->entity_name_textbox, entity_name_textbox_on_key);
		}

		// Entity position
		{
			// Position label
			state->entity_position_label = kui_label_control_create(kui_state, "entity_position_label", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Position");
			KASSERT(kui_system_control_add_child(kui_state, state->entity_inspector_bg_panel, state->entity_position_label));
			kui_control_position_set(kui_state, state->entity_position_label, (vec3){10, 100 + -5.0f, 0});

			// Position x textbox.
			state->entity_position_x_textbox = kui_textbox_control_create(kui_state, "entity_position_x_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_FLOAT);
			KASSERT(kui_system_control_add_child(kui_state, state->entity_inspector_bg_panel, state->entity_position_x_textbox));
			kui_control_position_set(kui_state, state->entity_position_x_textbox, (vec3){state->entity_inspector_right_col_x, 100, 0});
			KASSERT(kui_textbox_control_width_set(kui_state, state->entity_position_x_textbox, 120));
			kui_textbox_control_colour_set(kui_state, state->entity_position_x_textbox, EDITOR_AXIS_COLOUR_R);
			kui_control_set_user_data(kui_state, state->entity_position_x_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->entity_position_x_textbox, entity_position_x_textbox_on_key);

			// Position y textbox.
			state->entity_position_y_textbox = kui_textbox_control_create(kui_state, "entity_position_y_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_FLOAT);
			KASSERT(kui_system_control_add_child(kui_state, state->entity_inspector_bg_panel, state->entity_position_y_textbox));
			kui_control_position_set(kui_state, state->entity_position_y_textbox, (vec3){state->entity_inspector_right_col_x + 130, 100, 0});
			KASSERT(kui_textbox_control_width_set(kui_state, state->entity_position_y_textbox, 120));
			kui_textbox_control_colour_set(kui_state, state->entity_position_y_textbox, EDITOR_AXIS_COLOUR_G);
			kui_control_set_user_data(kui_state, state->entity_position_y_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->entity_position_y_textbox, entity_position_y_textbox_on_key);

			// Position z textbox.
			state->entity_position_z_textbox = kui_textbox_control_create(kui_state, "entity_position_z_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_FLOAT);
			KASSERT(kui_system_control_add_child(kui_state, state->entity_inspector_bg_panel, state->entity_position_z_textbox));
			kui_control_position_set(kui_state, state->entity_position_z_textbox, (vec3){state->entity_inspector_right_col_x + 260, 100, 0});
			KASSERT(kui_textbox_control_width_set(kui_state, state->entity_position_z_textbox, 120));
			kui_textbox_control_colour_set(kui_state, state->entity_position_z_textbox, EDITOR_AXIS_COLOUR_B);
			kui_control_set_user_data(kui_state, state->entity_position_z_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->entity_position_z_textbox, entity_position_z_textbox_on_key);
		}

		// Entity rotation
		{
			// Position label
			state->entity_orientation_label = kui_label_control_create(kui_state, "entity_orientation_label", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Orientation");
			KASSERT(kui_system_control_add_child(kui_state, state->entity_inspector_bg_panel, state->entity_orientation_label));
			kui_control_position_set(kui_state, state->entity_orientation_label, (vec3){10, 150 + -5.0f, 0});

			// Orientatiohttps://music.youtube.com/playlist?list=OLAK5uy_lW21dMR_nuKQOOxBTKzKpvzJCjP3hqtzw&si=pgXjcRP9HzglQh4Cn x textbox.
			state->entity_orientation_x_textbox = kui_textbox_control_create(kui_state, "entity_orientation_x_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_FLOAT);
			KASSERT(kui_system_control_add_child(kui_state, state->entity_inspector_bg_panel, state->entity_orientation_x_textbox));
			kui_control_position_set(kui_state, state->entity_orientation_x_textbox, (vec3){state->entity_inspector_right_col_x, 150, 0});
			KASSERT(kui_textbox_control_width_set(kui_state, state->entity_orientation_x_textbox, 120));
			kui_control_set_user_data(kui_state, state->entity_orientation_x_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->entity_orientation_x_textbox, entity_orientation_x_textbox_on_key);

			// Orientation y textbox.
			state->entity_orientation_y_textbox = kui_textbox_control_create(kui_state, "entity_orientation_y_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_FLOAT);
			KASSERT(kui_system_control_add_child(kui_state, state->entity_inspector_bg_panel, state->entity_orientation_y_textbox));
			kui_control_position_set(kui_state, state->entity_orientation_y_textbox, (vec3){state->entity_inspector_right_col_x + 130, 150, 0});
			KASSERT(kui_textbox_control_width_set(kui_state, state->entity_orientation_y_textbox, 120));
			kui_control_set_user_data(kui_state, state->entity_orientation_y_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->entity_orientation_y_textbox, entity_orientation_y_textbox_on_key);

			// Orientation z textbox.
			state->entity_orientation_z_textbox = kui_textbox_control_create(kui_state, "entity_orientation_z_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_FLOAT);
			KASSERT(kui_system_control_add_child(kui_state, state->entity_inspector_bg_panel, state->entity_orientation_z_textbox));
			kui_control_position_set(kui_state, state->entity_orientation_z_textbox, (vec3){state->entity_inspector_right_col_x + 260, 150, 0});
			KASSERT(kui_textbox_control_width_set(kui_state, state->entity_orientation_z_textbox, 120));
			kui_control_set_user_data(kui_state, state->entity_orientation_z_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->entity_orientation_z_textbox, entity_orientation_z_textbox_on_key);

			// Orientation z textbox.
			state->entity_orientation_w_textbox = kui_textbox_control_create(kui_state, "entity_orientation_w_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_FLOAT);
			KASSERT(kui_system_control_add_child(kui_state, state->entity_inspector_bg_panel, state->entity_orientation_w_textbox));
			kui_control_position_set(kui_state, state->entity_orientation_w_textbox, (vec3){state->entity_inspector_right_col_x + 390, 150, 0});
			KASSERT(kui_textbox_control_width_set(kui_state, state->entity_orientation_w_textbox, 120));
			kui_control_set_user_data(kui_state, state->entity_orientation_w_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->entity_orientation_w_textbox, entity_orientation_w_textbox_on_key);
		}

		// Entity scale
		{
			// Scale label
			state->entity_scale_label = kui_label_control_create(kui_state, "entity_scale_label", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Scale");
			KASSERT(kui_system_control_add_child(kui_state, state->entity_inspector_bg_panel, state->entity_scale_label));
			kui_control_position_set(kui_state, state->entity_scale_label, (vec3){10, 200 + -5.0f, 0});

			// Scale x textbox.
			state->entity_scale_x_textbox = kui_textbox_control_create(kui_state, "entity_scale_x_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_FLOAT);
			KASSERT(kui_system_control_add_child(kui_state, state->entity_inspector_bg_panel, state->entity_scale_x_textbox));
			kui_control_position_set(kui_state, state->entity_scale_x_textbox, (vec3){state->entity_inspector_right_col_x, 200, 0});
			KASSERT(kui_textbox_control_width_set(kui_state, state->entity_scale_x_textbox, 120));
			kui_textbox_control_colour_set(kui_state, state->entity_scale_x_textbox, EDITOR_AXIS_COLOUR_R);
			kui_control_set_user_data(kui_state, state->entity_scale_x_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->entity_scale_x_textbox, entity_scale_x_textbox_on_key);

			// Scale y textbox.
			state->entity_scale_y_textbox = kui_textbox_control_create(kui_state, "entity_scale_y_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_FLOAT);
			KASSERT(kui_system_control_add_child(kui_state, state->entity_inspector_bg_panel, state->entity_scale_y_textbox));
			kui_control_position_set(kui_state, state->entity_scale_y_textbox, (vec3){state->entity_inspector_right_col_x + 130, 200, 0});
			KASSERT(kui_textbox_control_width_set(kui_state, state->entity_scale_y_textbox, 120));
			kui_textbox_control_colour_set(kui_state, state->entity_scale_y_textbox, EDITOR_AXIS_COLOUR_G);
			kui_control_set_user_data(kui_state, state->entity_scale_y_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->entity_scale_y_textbox, entity_scale_y_textbox_on_key);

			// Scale z textbox.
			state->entity_scale_z_textbox = kui_textbox_control_create(kui_state, "entity_scale_z_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "", KUI_TEXTBOX_TYPE_FLOAT);
			KASSERT(kui_system_control_add_child(kui_state, state->entity_inspector_bg_panel, state->entity_scale_z_textbox));
			kui_control_position_set(kui_state, state->entity_scale_z_textbox, (vec3){state->entity_inspector_right_col_x + 260, 200, 0});
			KASSERT(kui_textbox_control_width_set(kui_state, state->entity_scale_z_textbox, 120));
			kui_textbox_control_colour_set(kui_state, state->entity_scale_z_textbox, EDITOR_AXIS_COLOUR_B);
			kui_control_set_user_data(kui_state, state->entity_scale_z_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->entity_scale_z_textbox, entity_scale_z_textbox_on_key);
		}
	}

	// Tree window panel.
	{
		state->tree_inspector_width = 500.0f;
		state->tree_inspector_right_col_x = 150.0f;
		state->tree_inspector_bg_panel = kui_panel_control_create(kui_state, "tree_inspector_bg_panel", (vec2){state->tree_inspector_width, 600.0f}, (vec4){0.0f, 0.0f, 0.0f, 0.75f});
		KASSERT(kui_system_control_add_child(kui_state, state->editor_root, state->tree_inspector_bg_panel));
		kui_control_position_set(kui_state, state->tree_inspector_bg_panel, (vec3){1280 - (state->tree_inspector_width + 10)});
		kui_control_set_is_active(kui_state, state->tree_inspector_bg_panel, false);
		kui_control_set_is_visible(kui_state, state->tree_inspector_bg_panel, false);

		// Window Label
		state->tree_inspector_title = kui_label_control_create(kui_state, "tree_inspector_title", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Tree");
		KASSERT(kui_system_control_add_child(kui_state, state->tree_inspector_bg_panel, state->tree_inspector_title));
		kui_control_position_set(kui_state, state->tree_inspector_title, (vec3){10, -5.0f, 0});

		// Base tree control.
		state->tree_scrollable_control = kui_scrollable_control_create(kui_state, "tree_base_control", (vec2){state->tree_inspector_width, 200}, true, true);
		KASSERT(kui_system_control_add_child(kui_state, state->tree_inspector_bg_panel, state->tree_scrollable_control));
		kui_control_position_set(kui_state, state->tree_scrollable_control, (vec3){0, 50, 0});

		state->tree_content_container = kui_scrollable_control_get_content_container(state->kui_state, state->tree_scrollable_control);

		// TODO: more controls
	}

	// HF Terrain window panel.
	{
		state->hf_terrain_window_width = 540.0f;
		state->hf_terrain_right_col_x = 130.0f;
		state->hf_terrain_bg_panel = kui_panel_control_create(kui_state, "hf_terrain_bg_panel", (vec2){state->hf_terrain_window_width, 400.0f}, (vec4){0.0f, 0.0f, 0.0f, 0.75f});
		KASSERT(kui_system_control_add_child(kui_state, state->editor_root, state->hf_terrain_bg_panel));
		kui_control_position_set(kui_state, state->hf_terrain_bg_panel, (vec3){1280 - (state->hf_terrain_window_width + 10)});
		kui_control_set_is_active(kui_state, state->hf_terrain_bg_panel, false);
		kui_control_set_is_visible(kui_state, state->hf_terrain_bg_panel, false);

		// Window Label
		state->hf_terrain_title = kui_label_control_create(kui_state, "hf_terrain_title", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Heightfield Terrain Editor");
		KASSERT(kui_system_control_add_child(kui_state, state->hf_terrain_bg_panel, state->hf_terrain_title));
		kui_control_position_set(kui_state, state->hf_terrain_title, (vec3){10, -5.0f, 0});

		// Paint sub-mode
		{

			// Some reasonable defaults.
			state->hft_paint_brush_diameter = 20;
			state->hft_paint_brush_strength = 8;
			state->hft_paint_material_index = 0;

			state->hft_mode_paint_checkbox = kui_checkbox_control_create(kui_state, "hft_mode_paint_checkbox", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Paint");
			KASSERT(kui_system_control_add_child(kui_state, state->hf_terrain_bg_panel, state->hft_mode_paint_checkbox));
			kui_control_position_set(kui_state, state->hft_mode_paint_checkbox, (vec3){5, 50, 0});
			kui_control_set_user_data(kui_state, state->hft_mode_paint_checkbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_checkbox_set_on_checked(kui_state, state->hft_mode_paint_checkbox, hf_terrain_checkbox_check_changed);

			// Content pane
			state->hft_mode_paint_content = kui_base_control_create(kui_state, "hft_mode_paint_content", KUI_CONTROL_TYPE_BASE);
			KASSERT(kui_system_control_add_child(kui_state, state->hf_terrain_bg_panel, state->hft_mode_paint_content));
			kui_control_position_set(kui_state, state->hft_mode_paint_content, (vec3){5, 90, 0});
			kui_control_set_is_active(kui_state, state->hft_mode_paint_content, false);
			kui_control_set_is_visible(kui_state, state->hft_mode_paint_content, false);

			state->hft_paint_brush_diameter_label = kui_label_control_create(kui_state, "hft_paint_brush_diameter_label", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Diameter");
			KASSERT(kui_system_control_add_child(kui_state, state->hft_mode_paint_content, state->hft_paint_brush_diameter_label));
			kui_control_position_set(kui_state, state->hft_paint_brush_diameter_label, (vec3){5, 0, 0});

			state->hft_paint_brush_strength_label = kui_label_control_create(kui_state, "hft_paint_brush_strength_label", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Strength");
			KASSERT(kui_system_control_add_child(kui_state, state->hft_mode_paint_content, state->hft_paint_brush_strength_label));
			kui_control_position_set(kui_state, state->hft_paint_brush_strength_label, (vec3){5, 50, 0});

			state->hft_paint_material_index_label = kui_label_control_create(kui_state, "hft_paint_brush_material_index_label", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Mat. Index");
			KASSERT(kui_system_control_add_child(kui_state, state->hft_mode_paint_content, state->hft_paint_material_index_label));
			kui_control_position_set(kui_state, state->hft_paint_material_index_label, (vec3){5, 100, 0});

			// Paint brush diameter textbox.
			state->hft_paint_brush_diameter_textbox = kui_textbox_control_create(kui_state, "hft_paint_brush_diameter_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "20", KUI_TEXTBOX_TYPE_INT);
			KASSERT(kui_system_control_add_child(kui_state, state->hft_mode_paint_content, state->hft_paint_brush_diameter_textbox));
			kui_control_position_set(kui_state, state->hft_paint_brush_diameter_textbox, (vec3){state->hf_terrain_right_col_x, 0, 0});
			KASSERT(kui_textbox_control_width_set(kui_state, state->hft_paint_brush_diameter_textbox, 120));
			kui_control_set_user_data(kui_state, state->hft_paint_brush_diameter_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->hft_paint_brush_diameter_textbox, hft_paint_brush_diameter_textbox_on_key);

			// Strength
			state->hft_paint_brush_strength_textbox = kui_textbox_control_create(kui_state, "hft_paint_brush_strength_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "8", KUI_TEXTBOX_TYPE_INT);
			KASSERT(kui_system_control_add_child(kui_state, state->hft_mode_paint_content, state->hft_paint_brush_strength_textbox));
			kui_control_position_set(kui_state, state->hft_paint_brush_strength_textbox, (vec3){state->hf_terrain_right_col_x, 50, 0});
			KASSERT(kui_textbox_control_width_set(kui_state, state->hft_paint_brush_strength_textbox, 120));
			kui_control_set_user_data(kui_state, state->hft_paint_brush_strength_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->hft_paint_brush_strength_textbox, hft_paint_brush_strength_textbox_on_key);

			// Material index.
			state->hft_paint_brush_material_index_textbox = kui_textbox_control_create(kui_state, "hft_paint_brush_material_index_textbox", FONT_TYPE_SYSTEM, state->textbox_font_name, state->textbox_font_size, "0", KUI_TEXTBOX_TYPE_INT);
			KASSERT(kui_system_control_add_child(kui_state, state->hft_mode_paint_content, state->hft_paint_brush_material_index_textbox));
			kui_control_position_set(kui_state, state->hft_paint_brush_material_index_textbox, (vec3){state->hf_terrain_right_col_x, 100, 0});
			KASSERT(kui_textbox_control_width_set(kui_state, state->hft_paint_brush_material_index_textbox, 120));
			kui_control_set_user_data(kui_state, state->hft_paint_brush_material_index_textbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_control_set_on_key(kui_state, state->hft_paint_brush_material_index_textbox, hft_paint_material_index_textbox_on_key);
		}

		// Elevation sub-mode
		{
			state->hft_mode_elevation_checkbox = kui_checkbox_control_create(kui_state, "hft_mode_elevation_checkbox", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Elevation");
			KASSERT(kui_system_control_add_child(kui_state, state->hf_terrain_bg_panel, state->hft_mode_elevation_checkbox));
			kui_control_position_set(kui_state, state->hft_mode_elevation_checkbox, (vec3){105, 50, 0});
			kui_control_set_user_data(kui_state, state->hft_mode_elevation_checkbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_checkbox_set_on_checked(kui_state, state->hft_mode_elevation_checkbox, hf_terrain_checkbox_check_changed);

			state->hft_mode_elevation_content = kui_base_control_create(kui_state, "hft_mode_elevation_content", KUI_CONTROL_TYPE_BASE);
			KASSERT(kui_system_control_add_child(kui_state, state->hf_terrain_bg_panel, state->hft_mode_elevation_content));
			kui_control_position_set(kui_state, state->hft_mode_elevation_content, (vec3){5, 90, 0});
			kui_control_set_is_active(kui_state, state->hft_mode_elevation_content, false);
			kui_control_set_is_visible(kui_state, state->hft_mode_elevation_content, false);
		}

		// Chunk sub-mode
		{
			state->hft_mode_chunk_checkbox = kui_checkbox_control_create(kui_state, "hft_mode_chunk_checkbox", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Chunk");
			KASSERT(kui_system_control_add_child(kui_state, state->hf_terrain_bg_panel, state->hft_mode_chunk_checkbox));
			kui_control_position_set(kui_state, state->hft_mode_chunk_checkbox, (vec3){250, 50, 0});
			kui_control_set_user_data(kui_state, state->hft_mode_chunk_checkbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_checkbox_set_on_checked(kui_state, state->hft_mode_chunk_checkbox, hf_terrain_checkbox_check_changed);

			state->hft_mode_chunk_content = kui_base_control_create(kui_state, "hft_mode_chunk_content", KUI_CONTROL_TYPE_BASE);
			KASSERT(kui_system_control_add_child(kui_state, state->hf_terrain_bg_panel, state->hft_mode_chunk_content));
			kui_control_position_set(kui_state, state->hft_mode_chunk_content, (vec3){5, 90, 0});
			kui_control_set_is_active(kui_state, state->hft_mode_chunk_content, false);
			kui_control_set_is_visible(kui_state, state->hft_mode_chunk_content, false);
		}

		// Remove sub-mode
		{
			state->hft_mode_remove_checkbox = kui_checkbox_control_create(kui_state, "hft_mode_remove_checkbox", FONT_TYPE_SYSTEM, state->font_name, state->font_size, "Remove");
			KASSERT(kui_system_control_add_child(kui_state, state->hf_terrain_bg_panel, state->hft_mode_remove_checkbox));
			kui_control_position_set(kui_state, state->hft_mode_remove_checkbox, (vec3){365, 50, 0});
			kui_control_set_user_data(kui_state, state->hft_mode_remove_checkbox, sizeof(*state), state, false, MEMORY_TAG_EDITOR);
			kui_checkbox_set_on_checked(kui_state, state->hft_mode_remove_checkbox, hf_terrain_checkbox_check_changed);

			state->hft_mode_remove_content = kui_base_control_create(kui_state, "hft_mode_remove_content", KUI_CONTROL_TYPE_BASE);
			KASSERT(kui_system_control_add_child(kui_state, state->hf_terrain_bg_panel, state->hft_mode_remove_content));
			kui_control_position_set(kui_state, state->hft_mode_remove_content, (vec3){5, 90, 0});
			kui_control_set_is_active(kui_state, state->hft_mode_remove_content, false);
			kui_control_set_is_visible(kui_state, state->hft_mode_remove_content, false);
		}
	}

	state->is_running = true;

	return true;
}
void editor_shutdown(struct editor_state* state) {

	editor_gizmo_destroy(&state->gizmo);

	editor_destroy_keymaps(state);

	// TODO: dirty check. If dirty, return false here. May need some sort of callback to
	// allow a "this is saved, now we can close" function.

	KTRACE("Shutting down editor.");

	darray_destroy(state->selection_list);
	state->selection_list = KNULL;

	tree_clear(state);

	if (state->edit_scene) {
		kscene_destroy(state->edit_scene);
		state->edit_scene = KNULL;
	}
}

b8 editor_open(struct editor_state* state, kname scene_name, kname scene_package_name) {
	kasset_text* scene_asset = asset_system_request_text_from_package_sync(
		engine_systems_get()->asset_state,
		kname_string_get(scene_package_name),
		kname_string_get(scene_name));
	if (!scene_asset) {
		KERROR("%s - Failed to request scene asset. See logs for details.", __FUNCTION__);
		return false;
	}

	KINFO("Opening editor scene...");

	// Creates scene and triggers load.
	state->edit_scene = kscene_create(scene_asset->content, 0, 0);
	state->scene_asset_name = scene_name;
	state->scene_package_name = scene_package_name;

	asset_system_release_text(engine_systems_get()->asset_state, scene_asset);
	if (!state->edit_scene) {
		KERROR("%s - Failed to create and load scene. See logs for details.", __FUNCTION__);
		return false;
	}

	// Reset checkboxes
	b8 debug_enabled = kscene_get_flag(state->edit_scene, KSCENE_FLAG_DEBUG_ENABLED_BIT);
	kui_checkbox_set_checked(state->kui_state, state->view_debug_checkbox, debug_enabled);
	kui_control_set_is_active(state->kui_state, state->view_bvh_checkbox, debug_enabled);
	kui_control_set_is_active(state->kui_state, state->view_grid_checkbox, debug_enabled);
	kui_checkbox_set_checked(state->kui_state, state->view_bvh_checkbox, kscene_get_flag(state->edit_scene, KSCENE_FLAG_DEBUG_BVH_ENABLED_BIT));
	kui_checkbox_set_checked(state->kui_state, state->view_grid_checkbox, kscene_get_flag(state->edit_scene, KSCENE_FLAG_DEBUG_GRID_ENABLED_BIT));

	// General scene visiblity
	kui_checkbox_set_checked(state->kui_state, state->view_skybox_checkbox, kscene_get_flag(state->edit_scene, KSCENE_FLAG_RENDER_SKYBOX_BIT));
	kui_checkbox_set_checked(state->kui_state, state->view_fog_checkbox, kscene_get_flag(state->edit_scene, KSCENE_FLAG_RENDER_FOG_BIT));

	const char* scene_name_str = kscene_get_name(state->edit_scene);
	kui_textbox_text_set(state->kui_state, state->scene_name_textbox, scene_name_str ? scene_name_str : "");
	colour4 fog_colour = kscene_get_fog_colour(state->edit_scene);
	kui_textbox_f32_set(state->kui_state, state->scene_fog_colour_r_textbox, fog_colour.r);
	kui_textbox_f32_set(state->kui_state, state->scene_fog_colour_g_textbox, fog_colour.g);
	kui_textbox_f32_set(state->kui_state, state->scene_fog_colour_b_textbox, fog_colour.b);
	kui_textbox_f32_set(state->kui_state, state->scene_fog_colour_a_textbox, fog_colour.a);

	// If opened successfully, change keymaps.
	if (!input_keymap_pop()) {
		KERROR("No keymap was popped during world->editor");
	}
	input_keymap_push(&state->editor_keymap);

	state->is_running = true;

	// Events and console commands for the editor should only be available when it is running.
	editor_register_events(state);
	editor_register_commands(state);

	// Enable UI elements.
	kui_control_set_is_visible(state->kui_state, state->editor_root, true);

	// Set the default mode.
	editor_set_mode(state, EDITOR_MODE_SCENE);

	return true;
}

b8 editor_close(struct editor_state* state) {
	// TODO: dirty check. If dirty, return false here. May need some sort of callback to
	// allow a "this is saved, now we can close" function.

	KTRACE("Destroying editor scene...");
	// Unload the current zone's scene from the world.
	kscene_destroy(state->edit_scene);
	state->edit_scene = KNULL;

	state->scene_asset_name = INVALID_KNAME;
	state->scene_package_name = INVALID_KNAME;

	KTRACE("Editor scene destroyed.");

	// Events and console commands for the editor should only be available when it is running.
	editor_unregister_events(state);
	editor_unregister_commands(state);

	state->is_running = false;

	// Disable UI elements.
	kui_control_set_is_visible(state->kui_state, state->editor_root, false);

	return true;
}

kui_control get_inspector_base_for_mode(struct editor_state* state, editor_mode mode) {
	switch (mode) {
	case EDITOR_MODE_SCENE:
		return state->scene_inspector_bg_panel;
	case EDITOR_MODE_ENTITY:
		return state->entity_inspector_bg_panel;
	case EDITOR_MODE_TREE:
		return state->tree_inspector_bg_panel;
	case EDITOR_MODE_HF_TERRAIN:
		return state->hf_terrain_bg_panel;
	case EDITOR_MODE_ASSETS:
		// TODO: other types
		return INVALID_KUI_CONTROL;
	}
}

void editor_set_mode(struct editor_state* state, editor_mode mode) {
	// Disable current window
	kui_control window = get_inspector_base_for_mode(state, state->mode);
	kui_control_set_is_visible(state->kui_state, window, false);
	kui_control_set_is_active(state->kui_state, window, false);

	// Set mode an enable the new.
	state->mode = mode;
	window = get_inspector_base_for_mode(state, state->mode);
	kui_control_set_is_visible(state->kui_state, window, true);
	kui_control_set_is_active(state->kui_state, window, true);
}

void editor_clear_selected_entities(struct editor_state* state) {
	darray_clear(state->selection_list);
	state->gizmo.selected_transform = KTRANSFORM_INVALID;
	KTRACE("Selection cleared.");

	// No selection, turn stuff off.
	kui_label_text_set(state->kui_state, state->entity_inspector_title, "Entity (no selection)");
	kui_textbox_text_set(state->kui_state, state->entity_name_textbox, "");

	// Update inspector position controls.
	kui_textbox_text_set(state->kui_state, state->entity_position_x_textbox, "");
	kui_textbox_text_set(state->kui_state, state->entity_position_y_textbox, "");
	kui_textbox_text_set(state->kui_state, state->entity_position_z_textbox, "");

	// Update inspector orientation controls.
	kui_textbox_text_set(state->kui_state, state->entity_orientation_x_textbox, "");
	kui_textbox_text_set(state->kui_state, state->entity_orientation_y_textbox, "");
	kui_textbox_text_set(state->kui_state, state->entity_orientation_z_textbox, "");
	kui_textbox_text_set(state->kui_state, state->entity_orientation_w_textbox, "");

	// Update inspector scale controls.
	kui_textbox_text_set(state->kui_state, state->entity_scale_x_textbox, "");
	kui_textbox_text_set(state->kui_state, state->entity_scale_y_textbox, "");
	kui_textbox_text_set(state->kui_state, state->entity_scale_z_textbox, "");
}

void editor_select_entities(struct editor_state* state, u32 count, kentity* entities) {
	editor_clear_selected_entities(state);

	editor_add_to_selected_entities(state, count, entities);
}

void editor_add_to_selected_entities(struct editor_state* state, u32 count, kentity* entities) {

	for (u32 s = 0; s < count; ++s) {
		kentity entity = entities[s];
		kname name = kscene_get_entity_name(state->edit_scene, entity);
		KINFO("Selection [%u]: '%s'", s, kname_string_get(name));
		if (!editor_selection_contains(state, entity)) {
			darray_push(state->selection_list, entity);
		}
	}

	// Set the gizmo to the selection.
	// HACK: force single-select for now.
	editor_gizmo_selected_transform_set(
		&state->gizmo,
		kscene_get_entity_transform(state->edit_scene, state->selection_list[0]));
	// TODO: Set the gizmo to an average position of all selected entity transforms,
	// and apply the modifications to transforms individually, but together.

	// Update inspector controls.
	const char* type_str = kentity_type_to_string(kentity_unpack_type(state->selection_list[0]));
	char* title_str = string_format("Entity (%s)", type_str);
	kui_label_text_set(state->kui_state, state->entity_inspector_title, title_str);
	string_free(title_str);

	kname name = kscene_get_entity_name(state->edit_scene, state->selection_list[0]);
	const char* name_str = kname_string_get(name);
	kui_textbox_text_set(state->kui_state, state->entity_name_textbox, name_str ? name_str : "");

	// Update inspector position controls.
	{
		vec3 position = kscene_get_entity_position(state->edit_scene, state->selection_list[0]);
		const char* x = f32_to_string(position.x);
		kui_textbox_text_set(state->kui_state, state->entity_position_x_textbox, x);
		string_free(x);
		const char* y = f32_to_string(position.y);
		kui_textbox_text_set(state->kui_state, state->entity_position_y_textbox, y);
		string_free(y);
		const char* z = f32_to_string(position.z);
		kui_textbox_text_set(state->kui_state, state->entity_position_z_textbox, z);
		string_free(z);
	}
	// Update inspector orientation controls.
	{
		quat rotation = kscene_get_entity_rotation(state->edit_scene, state->selection_list[0]);
		const char* x = f32_to_string(rotation.x);
		kui_textbox_text_set(state->kui_state, state->entity_orientation_x_textbox, x);
		string_free(x);
		const char* y = f32_to_string(rotation.y);
		kui_textbox_text_set(state->kui_state, state->entity_orientation_y_textbox, y);
		string_free(y);
		const char* z = f32_to_string(rotation.z);
		kui_textbox_text_set(state->kui_state, state->entity_orientation_z_textbox, z);
		string_free(z);
		const char* w = f32_to_string(rotation.w);
		kui_textbox_text_set(state->kui_state, state->entity_orientation_w_textbox, w);
		string_free(w);
	}
	// Update inspector scale controls.
	{
		vec3 scale = kscene_get_entity_scale(state->edit_scene, state->selection_list[0]);
		const char* x = f32_to_string(scale.x);
		kui_textbox_text_set(state->kui_state, state->entity_scale_x_textbox, x);
		string_free(x);
		const char* y = f32_to_string(scale.y);
		kui_textbox_text_set(state->kui_state, state->entity_scale_y_textbox, y);
		string_free(y);
		const char* z = f32_to_string(scale.z);
		kui_textbox_text_set(state->kui_state, state->entity_scale_z_textbox, z);
		string_free(z);
	}
}

void editor_select_parent(struct editor_state* state) {
	u32 count = darray_length(state->selection_list);
	if (count != 1) {
		KWARN("%s - cannot select parent unless exactly one entity is selected.", __FUNCTION__);
		return;
	}

	kentity parent = kscene_get_entity_parent(state->edit_scene, state->selection_list[0]);
	if (parent == KENTITY_INVALID) {
		KINFO("Selected object has no parent.");
		return;
	}

	state->selection_list[0] = parent;

	editor_gizmo_selected_transform_set(
		&state->gizmo,
		kscene_get_entity_transform(state->edit_scene, state->selection_list[0]));
}

b8 editor_selection_contains(struct editor_state* state, kentity entity) {
	u32 selection_count = darray_length(state->selection_list);
	for (u32 s = 0; s < selection_count; ++s) {
		if (state->selection_list[s] == entity) {
			return true;
		}
	}

	return false;
}

void editor_update(struct editor_state* state, frame_data* p_frame_data) {
	editor_gizmo_update(&state->gizmo, state->editor_camera);

	// Update the listener orientation. In editor mode, the sound follows the camera.
	vec3 cam_pos = kcamera_get_position(state->editor_camera);
	vec3 cam_forward = kcamera_forward(state->editor_camera);
	vec3 cam_up = kcamera_up(state->editor_camera);
	kaudio_system_listener_orientation_set(engine_systems_get()->audio_system, cam_pos, cam_forward, cam_up);

	if (!kscene_update(state->edit_scene, p_frame_data)) {
		KWARN("Failed to update editor scene.");
	}

	if (state->trigger_tree_refresh) {
		tree_refresh(state);
		state->trigger_tree_refresh = false;
	}
}

void editor_frame_prepare(struct editor_state* state, frame_data* p_frame_data, b8 draw_gizmo, keditor_gizmo_pass_render_data* gizmo_pass_render_data) {
	// Setup data required for the editor gizmo pass

	editor_gizmo_render_frame_prepare(&state->gizmo, p_frame_data);
	b8 has_selection = state->selection_list && darray_length(state->selection_list);

	gizmo_pass_render_data->do_pass = state->mode == EDITOR_MODE_ENTITY && has_selection && draw_gizmo;
	if (gizmo_pass_render_data->do_pass) {

		gizmo_pass_render_data->projection = state->gizmo.render_projection;
		gizmo_pass_render_data->view = kcamera_get_view(state->editor_camera);
		gizmo_pass_render_data->visible = has_selection;
		gizmo_pass_render_data->gizmo_transform = state->gizmo.render_model;

		kgeometry g = state->gizmo.mode_data[state->gizmo.mode].geo;
		kdebug_geometry_render_data* geo_rd = &gizmo_pass_render_data->geometry;
		geo_rd->geo.index_count = g.index_count;
		geo_rd->geo.index_offset = g.index_buffer_offset;
		geo_rd->geo.vertex_count = g.vertex_count;
		geo_rd->geo.vertex_offset = g.vertex_buffer_offset;
		geo_rd->geo.transform = KTRANSFORM_INVALID; // NOTE: transform isn't directly used here. app->state->gizmo.ktransform_handle;

		// Inverted winding not supported for debug geometries.
		geo_rd->geo.flags = FLAG_SET(geo_rd->geo.flags, KGEOMETRY_RENDER_DATA_FLAG_WINDING_INVERTED_BIT, false);
	}
}

static void set_render_state_defaults(rect_2di vp_rect) {
	renderer_begin_debug_label("frame defaults", vec3_zero());

	renderer_set_depth_test_enabled(false);
	renderer_set_depth_write_enabled(false);
	renderer_set_stencil_test_enabled(false);
	renderer_set_stencil_compare_mask(0);
	renderer_set_depth_bias_enabled(false);
	renderer_set_depth_bias(0.0f, 0.0f, 0.0f);

	renderer_cull_mode_set(RENDERER_CULL_MODE_BACK);
	// Default winding is counter clockwise
	renderer_winding_set(RENDERER_WINDING_COUNTER_CLOCKWISE);

	rect_2di viewport_rect = {vp_rect.x, vp_rect.y + vp_rect.height, vp_rect.width, -(f32)vp_rect.height};
	renderer_viewport_set(viewport_rect);

	rect_2di scissor_rect = {vp_rect.x, vp_rect.y, vp_rect.width, vp_rect.height};
	renderer_scissor_set(scissor_rect);

	renderer_end_debug_label();
}

b8 editor_render(struct editor_state* state, frame_data* p_frame_data, ktexture colour_buffer_target, b8 draw_gizmo, keditor_gizmo_pass_render_data* render_data) {

#if KOHI_DEBUG
	// NOTE: Editor gizmo only included in non-release builds
	if (render_data->do_pass) {

		if (render_data->visible) {

			renderer_begin_debug_label("editor gizmo", (vec3){0.5f, 1.0f, 0.5});

			rect_2di vp_rect = {0};
			if (!texture_dimensions_get(colour_buffer_target, (u32*)&vp_rect.width, (u32*)&vp_rect.height)) {
				return false;
			}
			// Editor gizmo begin render
			renderer_begin_rendering(state->renderer, p_frame_data, vp_rect, 1, &colour_buffer_target, INVALID_KTEXTURE, 0);
			set_render_state_defaults(vp_rect);

			// Disable depth test/write so the gizmo is always on top.
			renderer_set_depth_test_enabled(false);
			renderer_set_depth_write_enabled(false);
			renderer_set_stencil_test_enabled(false);

			kshader_system_use_with_topology(state->editor_gizmo_pass.gizmo_shader, PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_LIST_BIT, 0);
			renderer_cull_mode_set(RENDERER_CULL_MODE_NONE);

			// Global UBO data
			editor_gizmo_global_ubo global_ubo_data = {
				.view = render_data->view,
				.projection = render_data->projection};
			kshader_set_binding_data(state->editor_gizmo_pass.gizmo_shader, 0, state->editor_gizmo_pass.set0_instance_id, 0, 0, &global_ubo_data, sizeof(editor_gizmo_global_ubo));
			kshader_apply_binding_set(state->editor_gizmo_pass.gizmo_shader, 0, state->editor_gizmo_pass.set0_instance_id);

			kdebug_geometry_render_data* g = &render_data->geometry;

			editor_gizmo_immediate_data immediate_data = {.model = render_data->gizmo_transform};
			kshader_set_immediate_data(state->editor_gizmo_pass.gizmo_shader, &immediate_data, sizeof(editor_gizmo_immediate_data));

			// Draw it.
			b8 includes_index_data = g->geo.index_count > 0;

			if (!renderer_renderbuffer_draw(state->renderer, state->standard_vertex_buffer, g->geo.vertex_offset, g->geo.vertex_count, 0, includes_index_data)) {
				KERROR("renderer_renderbuffer_draw failed to draw vertex buffer;");
				return false;
			}
			if (includes_index_data) {
				if (!renderer_renderbuffer_draw(state->renderer, state->index_buffer, g->geo.index_offset, g->geo.index_count, 0, !includes_index_data)) {
					KERROR("renderer_renderbuffer_draw failed to draw index buffer;");
					return false;
				}
			}

			// Editor gizmo end render
			renderer_end_rendering(state->renderer, p_frame_data);
			renderer_end_debug_label();
		}
	}
#endif
	return true;
}

void editor_on_window_resize(struct editor_state* state, const struct kwindow* window) {
	if (!window->width || !window->height) {
		return;
	}

	// Resize cameras.
	rect_2di world_vp_rect = {0, 0, window->width, window->height};

	kcamera_set_vp_rect(state->editor_camera, world_vp_rect);

	// Send the resize off to the scene, if it exists.
	kscene_on_window_resize(state->edit_scene, window);

	// UI elements
	kui_control_position_set(state->kui_state, state->scene_inspector_bg_panel, (vec3){window->width - (state->scene_inspector_width + 10), 10});
	kui_control_position_set(state->kui_state, state->entity_inspector_bg_panel, (vec3){window->width - (state->entity_inspector_width + 10), 10});

	kui_control_position_set(state->kui_state, state->tree_inspector_bg_panel, (vec3){window->width - (state->tree_inspector_width + 10), 10});
	kui_control_position_set(state->kui_state, state->hf_terrain_bg_panel, (vec3){window->width - (state->hf_terrain_window_width + 10), 10});

	// HACK: hardcoded offset.
	f32 tree_bottom_offset = 420.0f;
	kui_panel_set_height(state->kui_state, state->tree_inspector_bg_panel, window->height - tree_bottom_offset);

	kui_scrollable_control_resize(state->kui_state, state->tree_scrollable_control, (vec2){state->tree_inspector_width, window->height - tree_bottom_offset - 50.0f});
}

void editor_setup_keymaps(struct editor_state* state) {
	state->editor_keymap = keymap_create();
	/* state->editor_keymap.overrides_all = true; */

	keymap_binding_add(&state->editor_keymap, KEY_A, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_yaw);
	keymap_binding_add(&state->editor_keymap, KEY_LEFT, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_yaw);

	keymap_binding_add(&state->editor_keymap, KEY_D, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_yaw);
	keymap_binding_add(&state->editor_keymap, KEY_RIGHT, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_yaw);

	keymap_binding_add(&state->editor_keymap, KEY_UP, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_pitch);
	keymap_binding_add(&state->editor_keymap, KEY_DOWN, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_pitch);

	keymap_binding_add(&state->editor_keymap, KEY_W, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_move_forward);
	keymap_binding_add(&state->editor_keymap, KEY_W, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_SHIFT_BIT, state, editor_on_sprint_forward);
	keymap_binding_add(&state->editor_keymap, KEY_S, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_move_backward);
	keymap_binding_add(&state->editor_keymap, KEY_Q, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_move_left);
	keymap_binding_add(&state->editor_keymap, KEY_E, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_move_right);
	keymap_binding_add(&state->editor_keymap, KEY_SPACE, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_move_up);
	keymap_binding_add(&state->editor_keymap, KEY_X, KEYMAP_BIND_TYPE_HOLD, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_move_down);

	keymap_binding_add(&state->editor_keymap, KEY_0, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_CONTROL_BIT, state, editor_on_set_render_mode_default);
	keymap_binding_add(&state->editor_keymap, KEY_1, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_CONTROL_BIT, state, editor_on_set_render_mode_lighting);
	keymap_binding_add(&state->editor_keymap, KEY_2, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_CONTROL_BIT, state, editor_on_set_render_mode_normals);
	keymap_binding_add(&state->editor_keymap, KEY_3, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_CONTROL_BIT, state, editor_on_set_render_mode_cascades);
	keymap_binding_add(&state->editor_keymap, KEY_4, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_CONTROL_BIT, state, editor_on_set_render_mode_wireframe);

	keymap_binding_add(&state->editor_keymap, KEY_1, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_set_gizmo_mode);
	keymap_binding_add(&state->editor_keymap, KEY_2, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_set_gizmo_mode);
	keymap_binding_add(&state->editor_keymap, KEY_3, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_set_gizmo_mode);
	keymap_binding_add(&state->editor_keymap, KEY_4, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_set_gizmo_mode);
	keymap_binding_add(&state->editor_keymap, KEY_G, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_gizmo_orientation_set);

	// ctrl s
	keymap_binding_add(&state->editor_keymap, KEY_S, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_CONTROL_BIT, state, editor_on_save_scene);

	keymap_binding_add(&state->editor_keymap, KEY_Z, KEYMAP_BIND_TYPE_PRESS, KEYMAP_MODIFIER_NONE_BIT, state, editor_on_zoom_extents);
}

void editor_destroy_keymaps(struct editor_state* state) {
	keymap_clear(&state->editor_keymap);
}

static f32 get_engine_delta_time(void) {
	ktimeline engine = ktimeline_system_get_engine();
	return ktimeline_system_delta_get(engine);
}

static f32 get_engine_total_time(void) {
	ktimeline engine = ktimeline_system_get_engine();
	return ktimeline_system_total_get(engine);
}

static b8 editor_has_focused_control(editor_state* editor) {
	return editor->kui_state->focused.val != INVALID_KUI_CONTROL.val;
}

static void editor_on_yaw(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	editor_state* state = (editor_state*)user_data;
	if (editor_has_focused_control(state)) {
		return;
	}

	f32 f = 0.0f;
	if (key == KEY_LEFT || key == KEY_A) {
		f = 1.0f;
	} else if (key == KEY_RIGHT || key == KEY_D) {
		f = -1.0f;
	}
	kcamera_yaw(state->editor_camera, f * get_engine_delta_time());
}

static void editor_on_pitch(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	editor_state* state = (editor_state*)user_data;
	if (editor_has_focused_control(state)) {
		return;
	}

	f32 f = 0.0f;
	if (key == KEY_UP) {
		f = 1.0f;
	} else if (key == KEY_DOWN) {
		f = -1.0f;
	}

	kcamera_pitch(state->editor_camera, f * get_engine_delta_time());
}

static void editor_on_set_render_mode_default(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	editor_state* state = (editor_state*)user_data;
	if (editor_has_focused_control(state)) {
		return;
	}
	console_command_execute("render_mode_set 0");
}

static void editor_on_set_render_mode_lighting(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	editor_state* state = (editor_state*)user_data;
	if (editor_has_focused_control(state)) {
		return;
	}
	console_command_execute("render_mode_set 1");
}

static void editor_on_set_render_mode_normals(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	editor_state* state = (editor_state*)user_data;
	if (editor_has_focused_control(state)) {
		return;
	}
	console_command_execute("render_mode_set 2");
}

static void editor_on_set_render_mode_cascades(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	editor_state* state = (editor_state*)user_data;
	if (editor_has_focused_control(state)) {
		return;
	}
	console_command_execute("render_mode_set 3");
}

static void editor_on_set_render_mode_wireframe(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	editor_state* state = (editor_state*)user_data;
	if (editor_has_focused_control(state)) {
		return;
	}
	console_command_execute("render_mode_set 4");
}

static void editor_on_set_gizmo_mode(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	editor_state* state = (editor_state*)user_data;
	if (editor_has_focused_control(state)) {
		return;
	}

	editor_gizmo_mode mode;
	switch (key) {
	case KEY_1:
	default:
		mode = EDITOR_GIZMO_MODE_NONE;
		break;
	case KEY_2:
		mode = EDITOR_GIZMO_MODE_MOVE;
		break;
	case KEY_3:
		mode = EDITOR_GIZMO_MODE_ROTATE;
		break;
	case KEY_4:
		mode = EDITOR_GIZMO_MODE_SCALE;
		break;
	}
	editor_gizmo_mode_set(&state->gizmo, mode);
}

static void editor_on_gizmo_orientation_set(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	editor_state* state = (editor_state*)user_data;
	if (editor_has_focused_control(state)) {
		return;
	}

	editor_gizmo_orientation orientation = editor_gizmo_orientation_get(&state->gizmo);
	orientation++;
	if (orientation > EDITOR_GIZMO_ORIENTATION_MAX) {
		orientation = 0;
	}
	editor_gizmo_orientation_set(&state->gizmo, orientation);
}

static void editor_on_move_forward(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	editor_state* state = (editor_state*)user_data;
	if (editor_has_focused_control(state)) {
		return;
	}

	f32 delta = get_engine_delta_time();

	kcamera_move_forward(state->editor_camera, state->editor_camera_forward_move_speed * delta);
}

static void editor_on_sprint_forward(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	editor_state* state = (editor_state*)user_data;
	if (editor_has_focused_control(state)) {
		return;
	}

	f32 delta = get_engine_delta_time();

	kcamera_move_forward(state->editor_camera, state->editor_camera_forward_move_speed * 2 * delta);
}

static void editor_on_move_backward(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	editor_state* state = (editor_state*)user_data;
	if (editor_has_focused_control(state)) {
		return;
	}

	f32 delta = get_engine_delta_time();

	kcamera_move_backward(state->editor_camera, state->editor_camera_backward_move_speed * delta);
}

static void editor_on_move_left(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	editor_state* state = (editor_state*)user_data;
	if (editor_has_focused_control(state)) {
		return;
	}

	f32 delta = get_engine_delta_time();

	kcamera_move_left(state->editor_camera, state->editor_camera_forward_move_speed * delta);
}

static void editor_on_move_right(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	editor_state* state = (editor_state*)user_data;
	if (editor_has_focused_control(state)) {
		return;
	}

	f32 delta = get_engine_delta_time();

	kcamera_move_right(state->editor_camera, state->editor_camera_forward_move_speed * delta);
}

static void editor_on_move_up(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	editor_state* state = (editor_state*)user_data;
	if (editor_has_focused_control(state)) {
		return;
	}

	kcamera_move_up(state->editor_camera, state->editor_camera_forward_move_speed * get_engine_delta_time());
}

static void editor_on_move_down(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	editor_state* state = (editor_state*)user_data;
	if (editor_has_focused_control(state)) {
		return;
	}

	kcamera_move_down(state->editor_camera, state->editor_camera_forward_move_speed * get_engine_delta_time());
}

static void save_scene(const struct kscene* scene, kname package_name, kname asset_name) {
	if (scene) {
		kscene_state scene_state = kscene_state_get(scene);
		if (scene_state == KSCENE_STATE_LOADED) {
			KDEBUG("Saving current scene...");
			const char* serialized = kscene_serialize(scene);
			if (!serialized) {
				KERROR("Scene serialization failed! Scene save thus fails. Check logs.");
				return;
			}

			// Write the text asset to disk
			if (!asset_system_write_text(engine_systems_get()->asset_state, package_name, asset_name, serialized)) {
				KERROR("Failed to save scene asset.");
			}
		} else {
			KERROR("Current scene is not in a loaded state, and cannot be saved.");
		}
	} else {
		KERROR("No scene is open to be saved.");
	}
}

static void editor_on_save_scene(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	editor_state* state = (editor_state*)user_data;
	if (editor_has_focused_control(state)) {
		return;
	}
	save_scene(state->edit_scene, state->scene_package_name, state->scene_asset_name);
}

static void editor_on_zoom_extents(keys key, keymap_entry_bind_type type, keymap_modifier modifiers, void* user_data) {
	KTRACE("Zoom extents");

	editor_state* state = (editor_state*)user_data;

	if (darray_length(state->selection_list)) {

		/* ktransform t = kscene_get_entity_transform(state->edit_scene, state->selection_list[0]);
		vec3 center = ktransform_world_position_get(t); */

		mat4 view = kcamera_get_view(state->editor_camera);

		f32 fov = kcamera_get_fov(state->editor_camera);
		rect_2di vp_rect = kcamera_get_vp_rect(state->editor_camera);
		f32 aspect = (f32)vp_rect.width / vp_rect.height;
		f32 tan_half_fov_y = ktan(fov * 0.5f);
		f32 tan_half_fov_x = tan_half_fov_y * aspect;

		f32 required_distance = 0.0f;

		aabb box = kscene_get_aabb(state->edit_scene, state->selection_list[0]);
		vec3 min = box.min;
		vec3 max = box.max;

		vec3 corners[8] = {
			{min.x, min.y, min.z},
			{max.x, min.y, min.z},
			{min.x, max.y, min.z},
			{max.x, max.y, min.z},
			{min.x, min.y, max.z},
			{max.x, min.y, max.z},
			{min.x, max.y, max.z},
			{max.x, max.y, max.z},
		};

		vec3 center = vec3_zero();
		for (u32 i = 0; i < 8; ++i) {
			// Center is the average of all points.
			center = vec3_add(center, corners[i]);

			// Move the corner to camera space.
			corners[i] = vec3_transform(corners[i], 1.0f, view);

			f32 x = kabs(corners[i].x);
			f32 y = kabs(corners[i].y);
			f32 z = -corners[i].z; // camera looks -z

			// Ignore corners behind camera .
			if (z <= 0.0f) {
				continue;
			}

			f32 d_x = x / tan_half_fov_x;
			f32 d_y = y / tan_half_fov_y;

			f32 d = KMAX(d_x, d_y);
			if (d > required_distance) {
				required_distance = d;
			}
		}
		center = vec3_div_scalar(center, 8.0f);

		// Pad it a bit.
		required_distance *= 1.05f;

		vec3 position = vec3_mul_scalar(kcamera_forward(state->editor_camera), required_distance);
		position = vec3_sub(center, position);

		kcamera_set_position(state->editor_camera, position);
	}
}

static void editor_command_execute(console_command_context context) {
	editor_state* state = (editor_state*)context.listener;
	if (strings_equal(context.command_name, "editor_save_scene")) {
		save_scene(state->edit_scene, state->scene_package_name, state->scene_asset_name);
	} else if (strings_equal(context.command_name, "editor_select_parent")) {
		editor_select_parent(state);
	} else if (strings_equal(context.command_name, "editor_dump_hierarchy")) {
		kscene_dump_hierarchy(state->edit_scene);
	} else if (strings_equal(context.command_name, "editor_set_selected_rotation")) {
		if (context.argument_count != 4) {
			KERROR("editor_set_selected_rotation requires 4 arguments (quaternion x, y, z, w)");
			return;
		} else {
			quat q = {0};
			for (u8 i = 0; i < 4; ++i) {
				string_to_f32(context.arguments[i].value, &q.elements[i]);
			}

			u32 len = darray_length(state->selection_list);
			if (len != 1) {
				KERROR("editor_set_selected_rotation requires exactly one entity be selected.");
				return;
			}

			kscene_set_entity_rotation(state->edit_scene, state->selection_list[0], q);
			editor_gizmo_refresh(&state->gizmo);
		}
	} else if (strings_equal(context.command_name, "editor_set_selected_position")) {
		if (context.argument_count != 3) {
			KERROR("editor_set_selected_position requires 3 arguments (position x, y, z)");
			return;
		} else {
			vec3 p = {0};
			for (u8 i = 0; i < 3; ++i) {
				string_to_f32(context.arguments[i].value, &p.elements[i]);
			}

			u32 len = darray_length(state->selection_list);
			if (len != 1) {
				KERROR("editor_set_selected_position requires exactly one entity be selected.");
				return;
			}

			kscene_set_entity_position(state->edit_scene, state->selection_list[0], p);
			editor_gizmo_refresh(&state->gizmo);
		}
	} else if (strings_equal(context.command_name, "editor_set_selected_scale")) {
		if (context.argument_count != 3) {
			KERROR("editor_set_selected_scale requires 3 arguments (scale x, y, z)");
			return;
		} else {
			vec3 scale = {0};
			for (u8 i = 0; i < 3; ++i) {
				string_to_f32(context.arguments[i].value, &scale.elements[i]);
			}

			u32 len = darray_length(state->selection_list);
			if (len != 1) {
				KERROR("editor_set_selected_scale requires exactly one entity be selected.");
				return;
			}

			kscene_set_entity_scale(state->edit_scene, state->selection_list[0], scale);
			editor_gizmo_refresh(&state->gizmo);
		}
	} else if (strings_equal(context.command_name, "editor_add_model")) {
		// editor_add_model "name with spaces" "asset name with spaces" "package name with spaces"
		// editor_add_model "barrels entity" "barrels model" Testbed
		kname name = kname_create(context.arguments[0].value);
		kname asset_name = kname_create(context.arguments[1].value);
		// Third property is optional and defaults to application package name.
		kname package_name = context.argument_count == 3 ? kname_create(context.arguments[2].value) : INVALID_KNAME;
		// Assign as a child of the first currently selected entity, if it exists.
		kentity parent = darray_length(state->selection_list) ? state->selection_list[0] : KENTITY_INVALID;

		kentity new_entity = kscene_add_model(state->edit_scene, name, KTRANSFORM_INVALID, parent, asset_name, package_name, 0, 0);

		// Select it
		editor_select_entities(state, 1, &new_entity);
	}
}

static void editor_register_events(struct editor_state* state) {
	KASSERT(event_register(EVENT_CODE_BUTTON_RELEASED, state, editor_on_button));
	KASSERT(event_register(EVENT_CODE_MOUSE_MOVED, state, editor_on_mouse_move));
	KASSERT(event_register(EVENT_CODE_MOUSE_DRAG_BEGIN, state, editor_on_drag));
	KASSERT(event_register(EVENT_CODE_MOUSE_DRAG_END, state, editor_on_drag));
	KASSERT(event_register(EVENT_CODE_MOUSE_DRAGGED, state, editor_on_drag));
}

static void editor_unregister_events(struct editor_state* state) {
	event_unregister(EVENT_CODE_BUTTON_RELEASED, state, editor_on_button);
	event_unregister(EVENT_CODE_MOUSE_MOVED, state, editor_on_mouse_move);
	event_unregister(EVENT_CODE_MOUSE_DRAG_BEGIN, state, editor_on_drag);
	event_unregister(EVENT_CODE_MOUSE_DRAG_END, state, editor_on_drag);
	event_unregister(EVENT_CODE_MOUSE_DRAGGED, state, editor_on_drag);
}

static void editor_register_commands(struct editor_state* state) {
	KASSERT(console_command_register("editor_save_scene", 0, 0, state, editor_command_execute));
	KASSERT(console_command_register("editor_select_parent", 0, 0, state, editor_command_execute));
	KASSERT(console_command_register("editor_dump_hierarchy", 0, 0, state, editor_command_execute));
	KASSERT(console_command_register("editor_set_selected_position", 3, 3, state, editor_command_execute));
	KASSERT(console_command_register("editor_set_selected_rotation", 4, 4, state, editor_command_execute));
	KASSERT(console_command_register("editor_set_selected_scale", 3, 3, state, editor_command_execute));
	KASSERT(console_command_register("editor_add_model", 2, 3, state, editor_command_execute));
}

static void editor_unregister_commands(struct editor_state* state) {
	console_command_unregister("editor_save_scene");
	console_command_unregister("editor_select_parent");
	console_command_unregister("editor_dump_hierarchy");
	console_command_unregister("editor_set_selected_position");
	console_command_unregister("editor_set_selected_rotation");
	console_command_unregister("editor_set_selected_scale");
	console_command_unregister("editor_add_model");
}

void editor_on_lib_load(struct editor_state* state) {
	if (state->is_running) {
		editor_register_events(state);
		editor_register_commands(state);
	}
}
void editor_on_lib_unload(struct editor_state* state) {
	editor_unregister_events(state);
	editor_unregister_commands(state);
}

static b8 save_button_clicked(struct kui_state* state, kui_control self, struct kui_mouse_event event) {
	KTRACE("Save button clicked.");

	console_command_execute("editor_save_scene");

	// Don't allow the event to popagate.
	return false;
}
static b8 mode_scene_button_clicked(struct kui_state* state, kui_control self, struct kui_mouse_event event) {
	KTRACE("Scene mode button clicked.");
	kui_base_control* base = kui_system_get_base(state, self);
	editor_set_mode(base->user_data, EDITOR_MODE_SCENE);
	// Don't allow the event to popagate.
	return false;
}
static b8 mode_entity_button_clicked(struct kui_state* state, kui_control self, struct kui_mouse_event event) {
	KTRACE("Entity mode button clicked.");
	kui_base_control* base = kui_system_get_base(state, self);
	editor_set_mode(base->user_data, EDITOR_MODE_ENTITY);
	// Don't allow the event to popagate.
	return false;
}
static b8 mode_tree_button_clicked(struct kui_state* state, kui_control self, struct kui_mouse_event event) {
	KTRACE("Tree mode button clicked.");
	kui_base_control* base = kui_system_get_base(state, self);
	editor_state* edit_state = base->user_data;

	if (edit_state->mode != EDITOR_MODE_TREE) {
		editor_set_mode(edit_state, EDITOR_MODE_TREE);

		edit_state->trigger_tree_refresh = true;
	}
	// Don't allow the event to popagate.
	return false;
}

static b8 mode_hf_terrain_button_clicked(struct kui_state* state, kui_control self, struct kui_mouse_event event) {
	KTRACE("HF Terrain mode button clicked.");
	kui_base_control* base = kui_system_get_base(state, self);
	editor_state* edit_state = base->user_data;

	if (edit_state->mode != EDITOR_MODE_HF_TERRAIN) {
		editor_set_mode(edit_state, EDITOR_MODE_HF_TERRAIN);
	}
	// Don't allow the event to popagate.
	return false;
}

static void show_bvh_checkbox_check_changed(struct kui_state* state, kui_control self, struct kui_checkbox_event event) {
	kui_base_control* base = kui_system_get_base(state, self);
	editor_state* edit_state = base->user_data;

	kscene_set_flag(edit_state->edit_scene, KSCENE_FLAG_DEBUG_BVH_ENABLED_BIT, event.checked);
}

static void show_grid_checkbox_check_changed(struct kui_state* state, kui_control self, struct kui_checkbox_event event) {
	kui_base_control* base = kui_system_get_base(state, self);
	editor_state* edit_state = base->user_data;

	kscene_set_flag(edit_state->edit_scene, KSCENE_FLAG_DEBUG_GRID_ENABLED_BIT, event.checked);
}

static void show_debug_checkbox_check_changed(struct kui_state* state, kui_control self, struct kui_checkbox_event event) {
	kui_base_control* base = kui_system_get_base(state, self);
	editor_state* edit_state = base->user_data;

	kscene_set_flag(edit_state->edit_scene, KSCENE_FLAG_DEBUG_ENABLED_BIT, event.checked);
	kui_control_set_is_active(edit_state->kui_state, edit_state->view_bvh_checkbox, event.checked);
	kui_control_set_is_active(edit_state->kui_state, edit_state->view_grid_checkbox, event.checked);
}

static void scene_visibility_checkbox_check_changed(struct kui_state* state, kui_control self, struct kui_checkbox_event event) {
	kui_base_control* base = kui_system_get_base(state, self);
	editor_state* edit_state = base->user_data;

	if (self.val == edit_state->view_skybox_checkbox.val) {
		kscene_set_flag(edit_state->edit_scene, KSCENE_FLAG_RENDER_SKYBOX_BIT, event.checked);
	} else if (self.val == edit_state->view_fog_checkbox.val) {
		kscene_set_flag(edit_state->edit_scene, KSCENE_FLAG_RENDER_FOG_BIT, event.checked);
	} else {
		KFATAL("Something is hooked to this that shouldn't be.");
	}
}

static b8 editor_on_mouse_move(u16 code, void* sender, void* listener_inst, event_context context) {
	editor_state* state = (editor_state*)listener_inst;

	if (!state->is_running) {
		// Do nothing, but allow other handlers to process the event.
		return false;
	}

	if (state->mode == EDITOR_MODE_ENTITY) {
		if (code == EVENT_CODE_MOUSE_MOVED && !input_is_button_dragging(MOUSE_BUTTON_LEFT)) {
			b8 has_selection = state->selection_list && darray_length(state->selection_list);
			if (has_selection) {
				i16 x = context.data.i16[0];
				i16 y = context.data.i16[1];

				mat4 view = kcamera_get_view(state->editor_camera);
				vec3 origin = kcamera_get_position(state->editor_camera);
				rect_2di vp_rect = kcamera_get_vp_rect(state->editor_camera);
				mat4 projection = kcamera_get_projection(state->editor_camera);

				ray r = ray_from_screen((vec2i){x, y}, vp_rect, origin, view, projection);

				editor_gizmo_handle_interaction(&state->gizmo, state->editor_camera, &r, EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_HOVER);
			}
		}
	} else if (state->mode == EDITOR_MODE_HF_TERRAIN) {
		i16 x = context.data.i16[0];
		i16 y = context.data.i16[1];

		mat4 view = kcamera_get_view(state->editor_camera);
		vec3 origin = kcamera_get_position(state->editor_camera);
		rect_2di vp_rect = kcamera_get_vp_rect(state->editor_camera);
		mat4 projection = kcamera_get_projection(state->editor_camera);

		ray r = ray_from_screen((vec2i){x, y}, vp_rect, origin, view, projection);

		hf_block block;
		hf_chunk chunk;
		vec3 pos;
		vec3 normal;
		if (kscene_hf_terrain_raycast(state->edit_scene, &r, &block, &chunk, &pos, &normal)) {
			// TODO: move some sort of indicator over terrain to show what will be interacted with on mouse down/drag.

			// Vary action based on selected hf terrain sub-editor mode (i.e. paint vs elevation change).
			switch (state->hft_edit_mode) {
			case HF_TERRAIN_EDIT_MODE_PAINT:
				break;
			case HF_TERRAIN_EDIT_MODE_ELEVATION:
				break;
			case HF_TERRAIN_EDIT_MODE_CHUNK:
				break;
			case HF_TERRAIN_EDIT_MODE_REMOVE:
				break;
			default:
				KFATAL("Hwhat");
			}
		}
	}

	return false; // Allow other event handlers to process this event.
}

static void hf_terrain_paint(editor_state* state, vec3 pos, vec3 normal, const hf_block* block, const hf_chunk* chunk) {
	f32 local_x = pos.x - block->aabb.min.x;
	f32 local_z = pos.z - block->aabb.min.z;
	f32 u = local_x / HF_BLOCK_SIZE_WORLD;
	f32 v = local_z / HF_BLOCK_SIZE_WORLD;

	u *= 4; // HF_TERRAIN_SPLATMAP_RESOLUTION;
	v *= 4; // HF_TERRAIN_SPLATMAP_RESOLUTION;

	if (u >= HF_TERRAIN_SPLATMAP_RESOLUTION || v >= HF_TERRAIN_SPLATMAP_RESOLUTION) {
		return;
	}

	/* i32 brush_diameter = 20;
	u8 brush_strength = 8; // 0-255, but 32 is really strong.;
	// NOTE: base material doesn't get painted.
	u8 material_index = 0; */

	u32 brush_diameter = state->hft_paint_brush_diameter;
	u8 brush_strength = state->hft_paint_brush_strength;
	u8 material_index = state->hft_paint_material_index;

	i32 radius_texels = brush_diameter * 0.5f;

	i32 min_x = KMAX(kfloor(u - radius_texels), 0);
	i32 min_z = KMAX(kfloor(v - radius_texels), 0);
	i32 max_x = KMIN(kceil(u + radius_texels), HF_TERRAIN_SPLATMAP_RESOLUTION - 1);
	i32 max_z = KMIN(kceil(v + radius_texels), HF_TERRAIN_SPLATMAP_RESOLUTION - 1);

	// Check if there is overflow into any of the surrounding 8 chunks.
	// If there is, these unclipped coords will need to be carried over to them as well,
	// translated to match thier splatmap's coords.

	min_x = KMAX(min_x, 0);
	min_z = KMAX(min_z, 0);
	max_x = KMIN(max_x, HF_TERRAIN_SPLATMAP_RESOLUTION - 1);
	max_z = KMIN(max_z, HF_TERRAIN_SPLATMAP_RESOLUTION - 1);

	u32 width = KMAX(max_x - min_x, 1);
	u32 height = KMAX(max_z - min_z, 1);

	vec2 brush_center = vec2_create(min_x + brush_diameter * 0.5f, min_z + brush_diameter * 0.5f);

	// FIXME: Maybe have some sort of buffer that sticks around for this...
	u32 total_px = width * height;
	u8* new_colour = KALLOC_TYPE_CARRAY(u8, total_px * 4);
	for (u32 z = 0; z < height; ++z) {
		for (u32 x = 0; x < width; ++x) {
			vec2 pos = vec2_create(min_x + x, min_z + z);
			f32 distance = vec2_distance(pos, brush_center);
			f32 weight = kfalloff_smooth(distance / brush_strength);
			u32 new_colour_base = ((z * width + x) * 4);
			u32 new_colour_index = new_colour_base + material_index;
			u32 cur_colour_base = (((min_z + z) * HF_TERRAIN_SPLATMAP_RESOLUTION + (min_x + x)) * 4);
			u32 cur_colour_index = cur_colour_base + material_index;

			// Get current pixel content
			u8 px_channel = block->splatmap_pixels[cur_colour_index];
			u16 temp = px_channel;
			temp += (u16)(weight * brush_strength);
			px_channel = (u8)KCLAMP(temp, 0, 255);

			// Write back to the splatmap px as well as the new array to write to the image.
			block->splatmap_pixels[cur_colour_index] = px_channel;
			new_colour[new_colour_index] = px_channel;

			// Calculate overflow between all channels and reduce the others if needed.
			f32 sum = 0.0f;
			for (u8 c = 0; c < 4; ++c) {
				sum += block->splatmap_pixels[cur_colour_base + c] / 255.0f;
			}

			f32 overflow = sum - 1.0f;
			if (overflow >= 0.0f) {
				f32 reducible = 0.0f;

				for (u8 c = 0; c < 4; ++c) {
					if (c == material_index) {
						continue;
					}
					reducible += block->splatmap_pixels[cur_colour_base + c] / 255.0f;
				}

				if (reducible <= 0.0f) {
					// Nothing to reduce. Clamp?
				} else {
					f32 scale = overflow / reducible;
					for (u8 c = 0; c < 4; ++c) {
						if (c == material_index) {
							continue;
						}

						f32 weight = block->splatmap_pixels[cur_colour_base + c] / 255.0f;
						f32 reduction = weight * scale * (1.0f - weight);
						block->splatmap_pixels[cur_colour_index] -= (u8)KCLAMP(reduction * 255, 0, 255);
						new_colour[new_colour_index] = block->splatmap_pixels[cur_colour_index];
					}
				}
			}
		}
	}

	/* u32 px_index = v * HF_TERRAIN_SPLATMAP_RESOLUTION + u; */
	/* u8 new_colour[4] = {255, 0, 0, 0}; */
	texture_write_data(block->splatmap, 32, min_x, min_z, 0, width, height, 1, new_colour);

	KFREE_TYPE_CARRAY(new_colour, u8, total_px * 4);
}

static b8 editor_on_drag(u16 code, void* sender, void* listener_inst, event_context context) {
	editor_state* state = (editor_state*)listener_inst;

	if (!state->is_running) {
		// Do nothing, but allow other handlers to process the event.
		return false;
	}

	i16 x = context.data.i16[0];
	i16 y = context.data.i16[1];
	u16 drag_button = context.data.u16[4];

	// Only care about left button drags.
	if (drag_button == MOUSE_BUTTON_LEFT) {
		mat4 view = kcamera_get_view(state->editor_camera);
		vec3 origin = kcamera_get_position(state->editor_camera);
		rect_2di vp_rect = kcamera_get_vp_rect(state->editor_camera);
		mat4 projection = kcamera_get_projection(state->editor_camera);

		ray r = ray_from_screen((vec2i){x, y}, vp_rect, origin, view, projection);

		if (state->mode == EDITOR_MODE_ENTITY) {
			if (code == EVENT_CODE_MOUSE_DRAG_BEGIN) {
				state->using_gizmo = true;
				// Drag start -- change the interaction mode to "dragging".
				editor_gizmo_interaction_begin(&state->gizmo, state->editor_camera, &r, EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_DRAG);
			} else if (code == EVENT_CODE_MOUSE_DRAGGED) {
				editor_gizmo_handle_interaction(&state->gizmo, state->editor_camera, &r, EDITOR_GIZMO_INTERACTION_TYPE_MOUSE_DRAG);
			} else if (code == EVENT_CODE_MOUSE_DRAG_END) {
				editor_gizmo_interaction_end(&state->gizmo);
				state->using_gizmo = false;
			}

			// TODO: update function
			// Update inspector position controls.
			{
				vec3 position = kscene_get_entity_position(state->edit_scene, state->selection_list[0]);
				const char* xt = f32_to_string(position.x);
				kui_textbox_text_set(state->kui_state, state->entity_position_x_textbox, xt);
				string_free(xt);
				const char* yt = f32_to_string(position.y);
				kui_textbox_text_set(state->kui_state, state->entity_position_y_textbox, yt);
				string_free(yt);
				const char* zt = f32_to_string(position.z);
				kui_textbox_text_set(state->kui_state, state->entity_position_z_textbox, zt);
				string_free(zt);
			}

			// Update inspector orientation controls.
			{
				quat rotation = kscene_get_entity_rotation(state->edit_scene, state->selection_list[0]);
				const char* x = f32_to_string(rotation.x);
				kui_textbox_text_set(state->kui_state, state->entity_orientation_x_textbox, x);
				string_free(x);
				const char* y = f32_to_string(rotation.y);
				kui_textbox_text_set(state->kui_state, state->entity_orientation_y_textbox, y);
				string_free(y);
				const char* z = f32_to_string(rotation.z);
				kui_textbox_text_set(state->kui_state, state->entity_orientation_z_textbox, z);
				string_free(z);
				const char* w = f32_to_string(rotation.w);
				kui_textbox_text_set(state->kui_state, state->entity_orientation_w_textbox, w);
				string_free(w);
			}

			// Update inspector scale controls.
			{
				vec3 scale = kscene_get_entity_scale(state->edit_scene, state->selection_list[0]);
				const char* xt = f32_to_string(scale.x);
				kui_textbox_text_set(state->kui_state, state->entity_scale_x_textbox, xt);
				string_free(xt);
				const char* yt = f32_to_string(scale.y);
				kui_textbox_text_set(state->kui_state, state->entity_scale_y_textbox, yt);
				string_free(yt);
				const char* zt = f32_to_string(scale.z);
				kui_textbox_text_set(state->kui_state, state->entity_scale_z_textbox, zt);
				string_free(zt);
			}
		} else if (state->mode == EDITOR_MODE_HF_TERRAIN) {
			// TODO: allow painting, move up/down while dragging

			// TODO: move some sort of indicator over terrain to show what will be interacted with on mouse down/drag.
			i16 x = context.data.i16[0];
			i16 y = context.data.i16[1];

			mat4 view = kcamera_get_view(state->editor_camera);
			vec3 origin = kcamera_get_position(state->editor_camera);
			rect_2di vp_rect = kcamera_get_vp_rect(state->editor_camera);
			mat4 projection = kcamera_get_projection(state->editor_camera);

			ray r = ray_from_screen((vec2i){x, y}, vp_rect, origin, view, projection);

			hf_block block;
			hf_chunk chunk;
			vec3 pos;
			vec3 normal;
			if (kscene_hf_terrain_raycast(state->edit_scene, &r, &block, &chunk, &pos, &normal)) {

				// Vary action based on selected hf terrain sub-editor mode (i.e. paint vs elevation change).
				switch (state->hft_edit_mode) {
				case HF_TERRAIN_EDIT_MODE_PAINT:
					hf_terrain_paint(state, pos, normal, &block, &chunk);
					break;
				case HF_TERRAIN_EDIT_MODE_ELEVATION:
					break;
				case HF_TERRAIN_EDIT_MODE_CHUNK:
					break;
				case HF_TERRAIN_EDIT_MODE_REMOVE:
					break;
				default:
					KFATAL("Hwhat");
				}
			}
		}
	}

	return false; // Let other handlers handle.
}

i32 raycast_hit_kquicksort_compare(void* a, void* b) {
	raycast_hit* a_typed = a;
	raycast_hit* b_typed = b;
	if (a_typed->distance > b_typed->distance) {
		return -1;
	} else if (a_typed->distance < b_typed->distance) {
		return 1;
	}
	return 0;
}
i32 raycast_hit_kquicksort_compare_desc(void* a, void* b) {
	raycast_hit* a_typed = a;
	raycast_hit* b_typed = b;
	if (a_typed->distance > b_typed->distance) {
		return 1;
	} else if (a_typed->distance < b_typed->distance) {
		return -1;
	}
	return 0;
}

/* KAPI void kquick_sort(u64 type_size, void* data, i32 low_index, i32 high_index, PFN_kquicksort_compare compare_pfn); */

static b8 editor_on_button(u16 code, void* sender, void* listener_inst, event_context context) {
	if (code == EVENT_CODE_BUTTON_PRESSED) {
		//
	} else if (code == EVENT_CODE_BUTTON_RELEASED) {
		u16 button = context.data.u16[4];
		switch (button) {
		case MOUSE_BUTTON_LEFT: {
			i16 x = context.data.i16[0];
			i16 y = context.data.i16[1];
			editor_state* state = (editor_state*)listener_inst;

			if (state->edit_scene) {

				if (state->using_gizmo) {
					return false;
				}
				kscene_state scene_state = kscene_state_get(state->edit_scene);
				if (scene_state == KSCENE_STATE_LOADED) {
					mat4 view = kcamera_get_view(state->editor_camera);
					mat4 projection = kcamera_get_projection(state->editor_camera);
					vec3 origin = kcamera_get_position(state->editor_camera);
					rect_2di current_vp_rect = kcamera_get_vp_rect(state->editor_camera);

					// Multi-select
					b8 multiselect = (input_is_key_down(KEY_LCONTROL) || input_is_key_down(KEY_RCONTROL));

					struct kscene* current_scene = state->edit_scene;
					// Cast a ray into the scene and see if anything can be selected.
					if (point_in_rect_2di((vec2i){x, y}, current_vp_rect)) {
						ray r = ray_from_screen((vec2i){x, y}, current_vp_rect, origin, view, projection);
						r.max_distance = 2000.0f;
						// Ignore collisions occurring where the ray's origin is inside a BVH node.
						FLAG_SET(r.flags, RAY_FLAG_IGNORE_IF_INSIDE_BIT, true);
						raycast_result result = {0};

						if (state->mode == EDITOR_MODE_ENTITY) {
							// Only allow selection in entity mode.
							if (kscene_raycast(current_scene, &r, &result)) {

								u32 hit_count = result.hits ? darray_length(result.hits) : 0;
								if (!hit_count) {
									KINFO("Nothing hit from raycast.");
									editor_clear_selected_entities(state);
								} else {
									if (!multiselect) {
										KINFO("Not multiselecting, clearing selection...");
										editor_clear_selected_entities(state);
									}

									// Sort hits by distance.
									kquick_sort(sizeof(raycast_hit), result.hits, 0, hit_count - 1, raycast_hit_kquicksort_compare);

									for (u32 i = 0; i < hit_count; ++i) {
										// Each thing. Use this to make selections, etc.
										raycast_hit* hit = &result.hits[i];

										kentity entity = (kentity)hit->user;

										// Skip BVH-only hits.
										if (hit->type == RAYCAST_HIT_TYPE_BVH_AABB) {
											KTRACE("Skipping BVH AABB hit (name='%k')", kscene_get_entity_name(state->edit_scene, entity));
											continue;
										}

										// Add to selection.
										editor_add_to_selected_entities(state, 1, &entity);
										// NOTE: only taking the first thing from the list.
										break;
									}
								}
							}
						} else if (state->mode == EDITOR_MODE_HF_TERRAIN) {
							// TODO: raycast to terrain, return point and normal. Perhaps block/chunk data too?
						}
					}
				}
			}
		} break;
		}
	}

	// Allow other handlers to process the event.
	return false;
}

static void scene_name_textbox_on_key(kui_state* state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state* editor = kui_control_get_user_data(state, self);

		if (key_code == KEY_ENTER) {
			const char* entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				kscene_set_name(editor->edit_scene, entry_control_text);
			}
		}
	}
}

static void scene_fog_colour_textbox_on_key(kui_state* state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state* editor = kui_control_get_user_data(state, self);

		kui_control next_control = INVALID_KUI_CONTROL;
		u8 element_index = 0;
		if (self.val == editor->scene_fog_colour_r_textbox.val) {
			element_index = 0;
			next_control = editor->scene_fog_colour_g_textbox;
		} else if (self.val == editor->scene_fog_colour_g_textbox.val) {
			element_index = 1;
			next_control = editor->scene_fog_colour_b_textbox;
		} else if (self.val == editor->scene_fog_colour_b_textbox.val) {
			element_index = 2;
			next_control = editor->scene_fog_colour_a_textbox;
		} else if (self.val == editor->scene_fog_colour_a_textbox.val) {
			element_index = 3;
			next_control = editor->scene_fog_colour_r_textbox;
		} else {
			KFATAL("Invalid control passed");
		}

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char* entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				colour4 fog_colour = kscene_get_fog_colour(editor->edit_scene);
				const char* val = kui_textbox_text_get(state, self);
				f32 parsed;
				if (string_to_f32(val, &parsed)) {
					fog_colour.elements[element_index] = parsed;
					kscene_set_fog_colour(editor->edit_scene, fog_colour);
				}
			}
		}
		if (key_code == KEY_TAB) {
			kui_system_focus_control(state, next_control);
		}
	}
}

static void entity_name_textbox_on_key(kui_state* state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state* editor = kui_control_get_user_data(state, self);

		if (key_code == KEY_ENTER) {
			const char* entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				kscene_set_entity_name(editor->edit_scene, editor->selection_list[0], kname_create(entry_control_text));
			}
		}
	}
}

static void entity_position_x_textbox_on_key(kui_state* state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state* editor = kui_control_get_user_data(state, self);

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char* entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				vec3 position = kscene_get_entity_position(editor->edit_scene, editor->selection_list[0]);
				const char* val = kui_textbox_text_get(state, self);
				f32 x;
				if (string_to_f32(val, &x)) {
					position.x = x;
					kscene_set_entity_position(editor->edit_scene, editor->selection_list[0], position);
				}
			}
		}
		if (key_code == KEY_TAB) {
			kui_system_focus_control(state, editor->entity_position_y_textbox);
		}
	}
}
static void entity_position_y_textbox_on_key(kui_state* state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state* editor = kui_control_get_user_data(state, self);

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char* entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				vec3 position = kscene_get_entity_position(editor->edit_scene, editor->selection_list[0]);
				const char* val = kui_textbox_text_get(state, self);
				f32 y;
				if (string_to_f32(val, &y)) {
					position.y = y;
					kscene_set_entity_position(editor->edit_scene, editor->selection_list[0], position);
				}
			}
		}
		if (key_code == KEY_TAB) {
			kui_system_focus_control(state, editor->entity_position_z_textbox);
		}
	}
}
static void entity_position_z_textbox_on_key(kui_state* state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state* editor = kui_control_get_user_data(state, self);

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char* entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				vec3 position = kscene_get_entity_position(editor->edit_scene, editor->selection_list[0]);
				const char* val = kui_textbox_text_get(state, self);
				f32 z;
				if (string_to_f32(val, &z)) {
					position.z = z;
					kscene_set_entity_position(editor->edit_scene, editor->selection_list[0], position);
				}
			}
		}
		if (key_code == KEY_TAB) {
			kui_system_focus_control(state, editor->entity_position_x_textbox);
		}
	}
}

static void entity_orientation_x_textbox_on_key(kui_state* state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state* editor = kui_control_get_user_data(state, self);

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char* entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				quat rotation = kscene_get_entity_rotation(editor->edit_scene, editor->selection_list[0]);
				const char* val = kui_textbox_text_get(state, self);
				f32 x;
				if (string_to_f32(val, &x)) {
					rotation.x = x;
					kscene_set_entity_rotation(editor->edit_scene, editor->selection_list[0], rotation);
				}
			}
		}
		if (key_code == KEY_TAB) {
			kui_system_focus_control(state, editor->entity_orientation_y_textbox);
		}
	}
}
static void entity_orientation_y_textbox_on_key(kui_state* state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state* editor = kui_control_get_user_data(state, self);

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char* entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				quat rotation = kscene_get_entity_rotation(editor->edit_scene, editor->selection_list[0]);
				const char* val = kui_textbox_text_get(state, self);
				f32 y;
				if (string_to_f32(val, &y)) {
					rotation.y = y;
					kscene_set_entity_rotation(editor->edit_scene, editor->selection_list[0], rotation);
				}
			}
		}
		if (key_code == KEY_TAB) {
			kui_system_focus_control(state, editor->entity_orientation_z_textbox);
		}
	}
}

static void entity_orientation_z_textbox_on_key(kui_state* state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state* editor = kui_control_get_user_data(state, self);

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char* entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				quat rotation = kscene_get_entity_rotation(editor->edit_scene, editor->selection_list[0]);
				const char* val = kui_textbox_text_get(state, self);
				f32 z;
				if (string_to_f32(val, &z)) {
					rotation.z = z;
					kscene_set_entity_rotation(editor->edit_scene, editor->selection_list[0], rotation);
				}
			}
		}
		if (key_code == KEY_TAB) {
			kui_system_focus_control(state, editor->entity_orientation_w_textbox);
		}
	}
}

static void entity_orientation_w_textbox_on_key(kui_state* state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state* editor = kui_control_get_user_data(state, self);

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char* entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				quat rotation = kscene_get_entity_rotation(editor->edit_scene, editor->selection_list[0]);
				const char* val = kui_textbox_text_get(state, self);
				f32 w;
				if (string_to_f32(val, &w)) {
					rotation.w = w;
					kscene_set_entity_rotation(editor->edit_scene, editor->selection_list[0], rotation);
				}
			}
		}
		if (key_code == KEY_TAB) {
			kui_system_focus_control(state, editor->entity_orientation_x_textbox);
		}
	}
}

static void entity_scale_x_textbox_on_key(kui_state* state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state* editor = kui_control_get_user_data(state, self);

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char* entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				vec3 scale = kscene_get_entity_scale(editor->edit_scene, editor->selection_list[0]);
				const char* val = kui_textbox_text_get(state, self);
				f32 x;
				if (string_to_f32(val, &x)) {
					scale.x = x;
					kscene_set_entity_scale(editor->edit_scene, editor->selection_list[0], scale);
				}
			}
		}
		if (key_code == KEY_TAB) {
			kui_system_focus_control(state, editor->entity_scale_y_textbox);
		}
	}
}
static void entity_scale_y_textbox_on_key(kui_state* state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state* editor = kui_control_get_user_data(state, self);

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char* entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				vec3 scale = kscene_get_entity_scale(editor->edit_scene, editor->selection_list[0]);
				const char* val = kui_textbox_text_get(state, self);
				f32 y;
				if (string_to_f32(val, &y)) {
					scale.y = y;
					kscene_set_entity_scale(editor->edit_scene, editor->selection_list[0], scale);
				}
			}
		}
		if (key_code == KEY_TAB) {
			kui_system_focus_control(state, editor->entity_scale_z_textbox);
		}
	}
}
static void entity_scale_z_textbox_on_key(kui_state* state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state* editor = kui_control_get_user_data(state, self);

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char* entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				vec3 scale = kscene_get_entity_scale(editor->edit_scene, editor->selection_list[0]);
				const char* val = kui_textbox_text_get(state, self);
				f32 z;
				if (string_to_f32(val, &z)) {
					scale.z = z;
					kscene_set_entity_scale(editor->edit_scene, editor->selection_list[0], scale);
				}
			}
		}
		if (key_code == KEY_TAB) {
			kui_system_focus_control(state, editor->entity_scale_x_textbox);
		}
	}
}

// TODO: all this stuff should exist in a kui_tree_control.
struct tree_hierarchy;
// An individual node within the hierarchy tree.
typedef struct tree_hierarchy_node {

	b8 expanded;

	// Pointer back to the tree.
	struct tree_hierarchy* tree;

	// user context for the node.
	u32 user_data_size;
	void* user_data;

	// A handle to the control associated with this item.
	kui_control tree_item;

	// Pointer to the parent.
	struct tree_hierarchy_node* parent;

	// Child nodes.
	u32 child_count;
	struct tree_hierarchy_node* children;
} tree_hierarchy_node;

// Top-level representation of the tree hierarchy.
typedef struct tree_hierarchy {
	// user context for the entire tree.
	u32 user_data_size;
	void* user_data;

	u32 root_count;
	struct tree_hierarchy_node* root_nodes;
} tree_hierarchy;

typedef struct hierarchy_node_context {
	editor_state* editor;
	kentity entity;
	tree_hierarchy_node* hierarchy_node;
} hierarchy_node_context;

static tree_hierarchy tree;

static void tree_node_cleanup_r(tree_hierarchy_node* node) {
	for (u32 i = 0; i < node->child_count; ++i) {
		tree_node_cleanup_r(&node->children[i]);
	}

	if (node->child_count && node->children) {
		KFREE_TYPE_CARRAY(node->children, tree_hierarchy_node, node->child_count);
	}
}

static void tree_setup_node_r(editor_state* state, kscene_hierarchy_node* scene_node, tree_hierarchy_node* tree_node, tree_hierarchy_node* parent_node, u32 index, f32* y_offset) {
	kui_state* kui_state = state->kui_state;

	kname name = kscene_get_entity_name(state->edit_scene, scene_node->entity);

	tree_node->child_count = scene_node->child_count;
	if (tree_node->child_count) {
		tree_node->children = KALLOC_TYPE_CARRAY(tree_hierarchy_node, tree_node->child_count);
	}

	const char* tree_item_name = string_format("tree_item_%i", index);

	tree_node->tree_item = kui_tree_item_control_create(
		kui_state,
		tree_item_name,
		state->tree_inspector_width - 10,
		FONT_TYPE_SYSTEM,
		state->font_name,
		state->font_size,
		kname_string_get(name),
		tree_node->child_count > 0);

	string_free(tree_item_name);

	if (parent_node) {
		/* kui_tree_item_control_add_child_tree_item(kui_state, parent_node->tree_item, tree_node); */
		kui_base_control* parent_base = kui_system_get_base(state->kui_state, parent_node->tree_item);
		kui_tree_item_control* typed_parent_control = (kui_tree_item_control*)parent_base;
		KASSERT(kui_system_control_add_child(kui_state, typed_parent_control->child_container, tree_node->tree_item));
	} else {
		// Add to the content container of the scrollable control.
		KASSERT(kui_system_control_add_child(kui_state, state->tree_content_container, tree_node->tree_item));
	}

	*y_offset += KUI_TREE_ITEM_HEIGHT;

	hierarchy_node_context* context = KALLOC_TYPE(hierarchy_node_context, MEMORY_TAG_EDITOR);
	context->editor = state;
	context->entity = scene_node->entity;
	context->hierarchy_node = tree_node;

	kui_control_set_user_data(kui_state, tree_node->tree_item, sizeof(hierarchy_node_context), context, true, MEMORY_TAG_EDITOR);
	kui_control_set_on_click(kui_state, tree_node->tree_item, tree_item_clicked);
	kui_tree_item_set_on_expanded(kui_state, tree_node->tree_item, tree_item_expanded);
	kui_tree_item_set_on_collapsed(kui_state, tree_node->tree_item, tree_item_collapsed);

	// Recurse children.
	for (u32 i = 0; i < tree_node->child_count; ++i) {
		tree_setup_node_r(state, &scene_node->children[i], &tree_node->children[i], tree_node, index + 1, y_offset);
	}
}

static f32 refresh_tree_item_expansion_r(editor_state* state, tree_hierarchy_node* node, f32 y_offset) {

	f32 accumulated_y_offset = 0.0f;
	kui_control_position_set(state->kui_state, node->tree_item, (vec3){44, y_offset, 0});

	accumulated_y_offset += KUI_TREE_ITEM_HEIGHT;

	if (node->expanded && node->child_count && node->children) {
		for (u32 i = 0; i < node->child_count; ++i) {
			accumulated_y_offset += refresh_tree_item_expansion_r(state, &node->children[i], i * KUI_TREE_ITEM_HEIGHT);
		}
	}

	return accumulated_y_offset;
}

static void refresh_tree_expansion(editor_state* state, tree_hierarchy* tree) {
	f32 accumulated_height = 0.0f;
	for (u32 i = 0; i < tree->root_count; ++i) {
		accumulated_height += refresh_tree_item_expansion_r(state, &tree->root_nodes[i], accumulated_height);
	}

	kui_scrollable_set_content_size(state->kui_state, state->tree_scrollable_control, state->tree_inspector_width, accumulated_height);
}

static void tree_clear(editor_state* state) {
	// Destroy current tree.
	if (tree.root_count && tree.root_nodes) {
		// First, cleanup the nodes recursively.
		for (u32 i = 0; i < tree.root_count; ++i) {
			tree_hierarchy_node* node = &tree.root_nodes[i];
			tree_node_cleanup_r(node);
		}

		KFREE_TYPE_CARRAY(tree.root_nodes, tree_hierarchy_node, tree.root_count);
		tree.root_count = 0;
		tree.root_nodes = KNULL;

		kui_control_destroy_all_children(state->kui_state, state->tree_content_container);
	}
}

static void tree_refresh(editor_state* state) {
	KTRACE("Tree refresh starting.");
	if (state->edit_scene) {
		tree_clear(state);

		// Refresh the data.
		u32 node_count = 0;
		kscene_hierarchy_node* scene_nodes = kscene_get_hierarchy(state->edit_scene, &node_count);
		if (node_count && scene_nodes) {

			tree.root_count = node_count;
			tree.root_nodes = KALLOC_TYPE_CARRAY(tree_hierarchy_node, tree.root_count);

			// Create all the new tree items.
			f32 y_offset = 0.0f;
			for (u32 i = 0; i < node_count; ++i) {
				kscene_hierarchy_node* scene_node = &scene_nodes[i];

				tree_setup_node_r(state, scene_node, &tree.root_nodes[i], KNULL, i, &y_offset);
			}

			// Cleanup once done building
			kscene_cleanup_hierarchy(scene_nodes, node_count);
			node_count = 0;
			scene_nodes = KNULL;
		}

		refresh_tree_expansion(state, &tree);
	}

	KTRACE("Tree refresh complete.");
}

static b8 tree_item_clicked(struct kui_state* state, kui_control self, struct kui_mouse_event event) {
	hierarchy_node_context* context = (hierarchy_node_context*)kui_control_get_user_data(state, self);

	editor_clear_selected_entities(context->editor);
	editor_add_to_selected_entities(context->editor, 1, &context->entity);

	return true;
}

static b8 tree_item_expanded(struct kui_state* state, kui_control self, struct kui_mouse_event event) {
	hierarchy_node_context* context = (hierarchy_node_context*)kui_control_get_user_data(state, self);

	context->hierarchy_node->expanded = true;

	refresh_tree_expansion(context->editor, &tree);

	return true;
}

static b8 tree_item_collapsed(struct kui_state* state, kui_control self, struct kui_mouse_event event) {
	hierarchy_node_context* context = (hierarchy_node_context*)kui_control_get_user_data(state, self);

	context->hierarchy_node->expanded = false;

	refresh_tree_expansion(context->editor, &tree);

	return true;
}

static void hf_terrain_checkbox_check_changed(struct kui_state* state, kui_control self, struct kui_checkbox_event event) {
	kui_base_control* base = kui_system_get_base(state, self);
	editor_state* edit_state = base->user_data;

	static kui_control checkboxes[HF_TERRAIN_EDIT_MODE_COUNT] = {0};
	checkboxes[HF_TERRAIN_EDIT_MODE_PAINT] = edit_state->hft_mode_paint_checkbox;
	checkboxes[HF_TERRAIN_EDIT_MODE_ELEVATION] = edit_state->hft_mode_elevation_checkbox;
	checkboxes[HF_TERRAIN_EDIT_MODE_CHUNK] = edit_state->hft_mode_chunk_checkbox;
	checkboxes[HF_TERRAIN_EDIT_MODE_REMOVE] = edit_state->hft_mode_remove_checkbox;

	static kui_control content_controls[HF_TERRAIN_EDIT_MODE_COUNT] = {0};
	content_controls[HF_TERRAIN_EDIT_MODE_PAINT] = edit_state->hft_mode_paint_content;
	content_controls[HF_TERRAIN_EDIT_MODE_ELEVATION] = edit_state->hft_mode_elevation_content;
	content_controls[HF_TERRAIN_EDIT_MODE_CHUNK] = edit_state->hft_mode_chunk_content;
	content_controls[HF_TERRAIN_EDIT_MODE_REMOVE] = edit_state->hft_mode_remove_content;

	for (u8 i = 0; i < HF_TERRAIN_EDIT_MODE_COUNT; ++i) {
		if (self.val == checkboxes[i].val) {
			edit_state->hft_edit_mode = (hf_terrain_edit_mode)i;
			kui_control_set_is_active(state, content_controls[i], true);
			kui_control_set_is_visible(state, content_controls[i], true);
		} else {
			kui_checkbox_set_checked(state, checkboxes[i], false);
			kui_control_set_is_active(state, content_controls[i], false);
			kui_control_set_is_visible(state, content_controls[i], false);
		}
	}

	switch (edit_state->hft_edit_mode) {

	case HF_TERRAIN_EDIT_MODE_PAINT:
		KTRACE("HF Terrain edit mode set to paint.");
		break;
	case HF_TERRAIN_EDIT_MODE_ELEVATION:
		KTRACE("HF Terrain edit mode set to elevation.");
		break;
	case HF_TERRAIN_EDIT_MODE_CHUNK:
		KTRACE("HF Terrain edit mode set to chunk.");
		break;
	case HF_TERRAIN_EDIT_MODE_REMOVE:
		KTRACE("HF Terrain edit mode set to remove.");
		break;
	case HF_TERRAIN_EDIT_MODE_COUNT:
		KERROR("Invalid HF terrain edit mode!");
		break;
	}
}

static void hft_paint_brush_diameter_textbox_on_key(kui_state* state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state* editor = kui_control_get_user_data(state, self);

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char* entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				const char* val = kui_textbox_text_get(state, self);
				i64 x;
				if (string_to_i64(val, &x)) {
					editor->hft_paint_brush_diameter = KCLAMP((u32)x, 1, 64);
				}
			}
		}
		if (key_code == KEY_TAB) {
			kui_system_focus_control(state, editor->hft_paint_brush_strength_textbox);
		}
	}
}
static void hft_paint_brush_strength_textbox_on_key(kui_state* state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state* editor = kui_control_get_user_data(state, self);

		if (key_code == KEY_ENTER || key_code == KEY_TAB) {
			const char* entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				const char* val = kui_textbox_text_get(state, self);
				i64 x;
				if (string_to_i64(val, &x)) {
					editor->hft_paint_brush_strength = (u8)KCLAMP((u8)x, 1, 255);
				}
			}
		}
		if (key_code == KEY_TAB) {
			kui_system_focus_control(state, editor->hft_paint_brush_diameter_textbox);
		}
	}
}
static void hft_paint_material_index_textbox_on_key(kui_state* state, kui_control self, kui_keyboard_event evt) {
	if (evt.type == KUI_KEYBOARD_EVENT_TYPE_PRESS) {
		u16 key_code = evt.key;

		editor_state* editor = kui_control_get_user_data(state, self);

		if (key_code == KEY_ENTER) {
			const char* entry_control_text = kui_textbox_text_get(state, self);
			u32 len = string_length(entry_control_text);
			if (len > 0) {
				const char* val = kui_textbox_text_get(state, self);
				i64 x;
				if (string_to_i64(val, &x)) {
					editor->hft_paint_material_index = (u8)KCLAMP((u8)x, 0, HF_TERRAIN_CHUNK_MAX_MATERIALS - 2);
				}
			}
		}
	}
}
