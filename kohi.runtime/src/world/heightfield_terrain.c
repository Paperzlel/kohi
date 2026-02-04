#include "heightfield_terrain.h"

#include "core/engine.h"
#include "core/frame_data.h"
#include "core_render_types.h"
#include "math/geometry.h"
#include "math/kmath.h"
#include "math/math_types.h"
#include "renderer/renderer_frontend.h"
#include "renderer/renderer_types.h"
#include "runtime_defines.h"
#include "strings/kname.h"
#include "strings/kstring.h"
#include "systems/kshader_system.h"
#include "systems/texture_system.h"
#include <debug/kassert.h>
#include <memory/kmemory.h>

static void generate_normals(u32 vertex_count, hf_vertex_3d* vertices, u32 index_count, u32* indices) {
	for (u32 i = 0; i < index_count; i += 3) {
		u32 i0 = indices[i + 0];
		u32 i1 = indices[i + 1];
		u32 i2 = indices[i + 2];

		vec3 edge1 = vec3_from_vec4(vec4_sub(vertices[i1].position, vertices[i0].position));
		vec3 edge2 = vec3_from_vec4(vec4_sub(vertices[i2].position, vertices[i0].position));

		vec3 normal = vec3_normalized(vec3_cross(edge1, edge2));

		// NOTE: This just generates a face normal. Smoothing out should be done in
		// a separate pass if desired.
		vertices[i0].normal = vec4_from_vec3(normal, vertices[i0].normal.w);
		vertices[i1].normal = vec4_from_vec3(normal, vertices[i1].normal.w);
		vertices[i2].normal = vec4_from_vec3(normal, vertices[i2].normal.w);
	}
}

static void generate_tangents(u32 vertex_count, hf_vertex_3d* vertices, u32 index_count, u32* indices) {
	for (u32 i = 0; i < index_count; i += 3) {
		u32 i0 = indices[i + 0];
		u32 i1 = indices[i + 1];
		u32 i2 = indices[i + 2];

		vec3 edge1 = vec3_from_vec4(vec4_sub(vertices[i1].position, vertices[i0].position));
		vec3 edge2 = vec3_from_vec4(vec4_sub(vertices[i2].position, vertices[i0].position));

		f32 deltaU1 = vertices[i1].position.w - vertices[i0].position.w; // texcoord x
		f32 deltaV1 = vertices[i1].normal.w - vertices[i0].normal.w;	 // texcoord y

		f32 deltaU2 = vertices[i2].position.w - vertices[i0].position.w; // texcoord x
		f32 deltaV2 = vertices[i2].normal.w - vertices[i0].normal.w;	 // texcoord y

		f32 dividend = (deltaU1 * deltaV2 - deltaU2 * deltaV1);
		f32 fc = 1.0f / dividend;

		vec3 tangent = (vec3){(fc * (deltaV2 * edge1.x - deltaV1 * edge2.x)),
							  (fc * (deltaV2 * edge1.y - deltaV1 * edge2.y)),
							  (fc * (deltaV2 * edge1.z - deltaV1 * edge2.z))};

		tangent = vec3_normalized(tangent);

		f32 sx = deltaU1, sy = deltaU2;
		f32 tx = deltaV1, ty = deltaV2;
		f32 handedness = ((tx * sy - ty * sx) < 0.0f) ? -1.0f : 1.0f;

		vec3 t4 = vec3_mul_scalar(tangent, handedness);
		// Encode handedness into w.
		vertices[i0].tangent = vec4_from_vec3(t4, handedness);
		vertices[i1].tangent = vec4_from_vec3(t4, handedness);
		vertices[i2].tangent = vec4_from_vec3(t4, handedness);
	}
}

void generate_chunk(hf_terrain* t, hf_chunk* chunk, u16 chunk_x, u16 chunk_z, u16 block_x, u16 block_z, u16 z_dim) {
	chunk->x = chunk_x;
	chunk->z = chunk_z;
	chunk->index = (chunk_z * HF_BLOCK_CHUNK_DIM) + chunk_x;

	// Get the offset into the array in elements.
	u64 block_offset = (block_z * HF_BLOCK_VERTEX_COUNT * z_dim) + (block_x * HF_BLOCK_VERTEX_COUNT);
	u64 chunk_offset = block_offset + (chunk_z * HF_CHUNK_VERTEX_COUNT * HF_BLOCK_CHUNK_DIM) + (HF_CHUNK_VERTEX_COUNT * chunk_x);
	chunk->vertex_buffer_offset = t->base_vertex_buffer_offset + (chunk_offset * sizeof(hf_vertex_3d));

	// Determines the size modification of each quad relative to a world unit.
	f32 quad_size_mod = 1.0f;

	f32 block_base_z = block_z * HF_BLOCK_QUAD_COUNT * quad_size_mod;
	f32 block_base_x = block_x * HF_BLOCK_QUAD_COUNT * quad_size_mod;
	f32 chunk_base_z = block_base_z + (chunk_z * HF_CHUNK_QUAD_COUNT * quad_size_mod);
	f32 chunk_base_x = block_base_x + (chunk_x * HF_CHUNK_QUAD_COUNT * quad_size_mod);

	extents_3d extents = {
		.max = vec3_create(-999999.0f, -999999.0f, -999999.0f),
		.min = vec3_create(999999.0f, 999999.0f, 999999.0f)};

	for (u8 z = 0; z < HF_VERTEX_STRIDE; ++z) {
		for (u8 x = 0; x < HF_VERTEX_STRIDE; ++x) {
			u32 index = chunk_offset + (z * HF_VERTEX_STRIDE) + x;

			hf_vertex_3d* v = &t->vertices[index];

			v->position.x = chunk_base_x + (x * quad_size_mod);
			v->position.z = chunk_base_z + (z * quad_size_mod);
			// HACK: Should start at 0, but doing this in the meantime.
			// TODO: This should also be using the Y from the previous chunk.
			v->position.y = (ksin(v->position.x / 8.0) + kcos(v->position.z / 8.0f)) * 2.0f;

			// Tex coords are encoded in normal/position.
			v->position.w = x;
			v->normal.w = z;

			// These will be calculated later.
			v->normal.x = 0;
			v->normal.y = 1.0f;
			v->normal.z = 0;

			extents.min = vec3_min(vec3_from_vec4(v->position), extents.min);
			extents.max = vec3_max(vec3_from_vec4(v->position), extents.max);
		}
	}

	generate_normals(HF_CHUNK_VERTEX_COUNT, t->vertices + chunk_offset, HF_INDEX_COUNT, t->indices);
	generate_tangents(HF_CHUNK_VERTEX_COUNT, t->vertices + chunk_offset, HF_INDEX_COUNT, t->indices);

	chunk->aabb = extents;

	// Acquire shader resources.
	chunk->shader_instance_id = kshader_acquire_binding_set_instance(t->hf_terrain_shader, 1);

	// Create a splatmap for the chunk.
	kname name = kname_format("hf_terrain_splatmap_block_%u_%u_chunk_%u_%u", block_z, block_x, chunk_z, chunk_x);

	// FIXME: This pixel array will need to be freed somehow...
	u64 pixel_array_size = HF_TERRAIN_SPLATMAP_RESOLUTION * HF_TERRAIN_SPLATMAP_RESOLUTION * 4;
	u8* pixels = KALLOC_TYPE_CARRAY(u8, pixel_array_size);
	kzero_memory(pixels, sizeof(u8) * pixel_array_size);

	// HACK: Testing material mappings
	if (block_x == 0 && block_z == 0) {
		if (chunk_x == 0 && chunk_z == 0) {
			pixels[(64 + 1) * 4 + 1] = 255;
			pixels[(128 + 1) * 4 + 1] = 255;
			pixels[(64 + 2) * 4 + 1] = 255;
			pixels[(128 + 2) * 4 + 1] = 255;
			pixels[(64 + 3) * 4 + 1] = 255;
			pixels[(128 + 3) * 4 + 1] = 255;
			pixels[(64 + 4) * 4 + 1] = 255;
			pixels[(128 + 4) * 4 + 1] = 255;

			for (u32 i = 0; i < 64; ++i) {
				pixels[(64 * i) * 4 + 3] = 255;
			}
		} else if (chunk_x == 0 && chunk_z == 1) {
			for (u32 i = 0; i < 64 * 64; ++i) {
				if (i < 64) {
					pixels[i * 4 + 0] = 0;
				} else {
					pixels[i * 4 + 0] = 255;
				}
			}
		} else if (chunk_x == 0 && chunk_z == 2) {
			for (u32 i = 0; i < 64 * 64; ++i) {
				pixels[i * 4 + 1] = 255;
			}
		} else if (chunk_x == 0 && chunk_z == 3) {
			for (u32 i = 0; i < 64 * 64; ++i) {
				pixels[i * 4 + 2] = 255;
			}
		} else if (chunk_x == 0 && chunk_z == 4) {
			for (u32 i = 0; i < 64 * 64; ++i) {
				pixels[i * 4 + 3] = 255;
			}
		}
	}

	chunk->splatmap = texture_acquire_from_pixel_data(KPIXEL_FORMAT_RGBA8, pixel_array_size, pixels, HF_TERRAIN_SPLATMAP_RESOLUTION, HF_TERRAIN_SPLATMAP_RESOLUTION, name);

	// HACK: Hardcoding material textures until HF terrain materials are setup.
	chunk->albedo_textures[0] = texture_acquire_sync(kname_create("dirtRocks_grass_mix"));
	chunk->normal_textures[0] = texture_acquire_sync(kname_create("dirtRocks_grass_mix_NORM"));
	chunk->albedo_textures[1] = texture_acquire_sync(kname_create("dirtRocks_dark"));
	chunk->normal_textures[1] = texture_acquire_sync(kname_create(DEFAULT_NORMAL_TEXTURE_NAME));
	chunk->albedo_textures[2] = texture_acquire_sync(kname_create("paverPath"));
	chunk->normal_textures[2] = texture_acquire_sync(kname_create(DEFAULT_NORMAL_TEXTURE_NAME));
	chunk->albedo_textures[3] = texture_acquire_sync(kname_create("lushGrass"));
	chunk->normal_textures[3] = texture_acquire_sync(kname_create(DEFAULT_NORMAL_TEXTURE_NAME));
	chunk->albedo_textures[4] = texture_acquire_sync(kname_create("yellowFlowers"));
	chunk->normal_textures[4] = texture_acquire_sync(kname_create(DEFAULT_NORMAL_TEXTURE_NAME));
}

void generate_block(hf_terrain* t, hf_block* block, u16 block_x, u16 block_z, u16 z_dim) {

	extents_3d extents = {
		.max = vec3_create(-999999.0f, -999999.0f, -999999.0f),
		.min = vec3_create(999999.0f, 999999.0f, 999999.0f)};

	block->x = block_x;
	block->z = block_z;
	block->index = (block_z * z_dim) + block_x;

	for (u8 z = 0; z < HF_BLOCK_CHUNK_DIM; ++z) {
		for (u8 x = 0; x < HF_BLOCK_CHUNK_DIM; ++x) {
			u32 index = (z * HF_BLOCK_CHUNK_DIM) + x;
			generate_chunk(t, &block->chunks[index], x, z, block_x, block_z, z_dim);

			extents.min = vec3_min(block->chunks[index].aabb.min, extents.min);
			extents.max = vec3_max(block->chunks[index].aabb.max, extents.max);
		}
	}

	block->aabb = extents;
}

hf_terrain hf_terrain_generate(u16 blocks_x, u16 blocks_z) {
	hf_terrain t = {0};

	t.block_count_x = blocks_x;
	t.block_count_z = blocks_z;

	KALLOC_TYPE_CARRAY(hf_block, blocks_x * blocks_z);

	// Each chunk uses the same indices.
	// Surface indices. Generate 1 set of 6 per tile.
	for (u32 row = 0, i = 0; row < HF_CHUNK_QUAD_COUNT; row += 1) {
		for (u32 col = 0; col < HF_CHUNK_QUAD_COUNT; col += 1, i += 6) {
			u32 next_row = row + 1;
			u32 next_col = col + 1;
			u32 v0 = (row * HF_VERTEX_STRIDE) + col;
			u32 v1 = (row * HF_VERTEX_STRIDE) + next_col;
			u32 v2 = (next_row * HF_VERTEX_STRIDE) + col;
			u32 v3 = (next_row * HF_VERTEX_STRIDE) + next_col;

			t.indices[i + 0] = v2;
			t.indices[i + 1] = v1;
			t.indices[i + 2] = v0;
			t.indices[i + 3] = v3;
			t.indices[i + 4] = v1;
			t.indices[i + 5] = v2;
		}
	}

	// Acquire the shader.
	t.hf_terrain_shader = kshader_system_get(kname_create(SHADER_NAME_RUNTIME_HF_TERRAIN), kname_create(PACKAGE_NAME_RUNTIME));
	KASSERT_DEBUG(t.hf_terrain_shader != KSHADER_INVALID);

	// Acquire the vertex/index buffers.
	struct renderer_system_state* renderer_system = engine_systems_get()->renderer_system;
	krenderbuffer index_buffer = renderer_renderbuffer_get(renderer_system, kname_create(KRENDERBUFFER_NAME_INDEX_STANDARD));
	krenderbuffer vertex_buffer = renderer_renderbuffer_get(renderer_system, kname_create(KRENDERBUFFER_NAME_VERTEX_STANDARD));

	// Allocate index buffer space.
	u64 index_buffer_size = sizeof(u32) * HF_INDEX_COUNT;
	KASSERT(renderer_renderbuffer_allocate(renderer_system, index_buffer, index_buffer_size, &t.index_buffer_offset));

	// Allocate vertexchunk_base_x buffer space.
	u32 block_count = (blocks_x * blocks_z);
	t.vertex_count = (HF_VERTEX_STRIDE * HF_VERTEX_STRIDE) * HF_BLOCK_CHUNK_COUNT * block_count;
	t.vertices = KALLOC_TYPE_CARRAY(hf_vertex_3d, t.vertex_count);
	u64 vertex_buffer_size = sizeof(hf_vertex_3d) * t.vertex_count;
	KASSERT(renderer_renderbuffer_allocate(renderer_system, vertex_buffer, vertex_buffer_size, &t.base_vertex_buffer_offset));

	t.blocks = KALLOC_TYPE_CARRAY(hf_block, blocks_z * blocks_x);

	// Generate blocks.
	for (u8 z = 0; z < blocks_z; ++z) {
		for (u8 x = 0; x < blocks_x; ++x) {
			u32 index = (z * blocks_z) + x;
			generate_block(&t, &t.blocks[index], x, z, blocks_z);
		}
	}

	// Upload vertices
	KASSERT(renderer_renderbuffer_load_range(renderer_system, vertex_buffer, t.base_vertex_buffer_offset, vertex_buffer_size, t.vertices, false));

	// Upload indices
	KASSERT(renderer_renderbuffer_load_range(renderer_system, index_buffer, t.index_buffer_offset, index_buffer_size, t.indices, false));

	return t;
}

void hf_terrain_destroy(hf_terrain* t) {
	if (t) {
		u32 block_count = t->block_count_z * t->block_count_x;
		for (u32 b = 0; b < block_count; ++b) {
			hf_block* block = &t->blocks[b];
			for (u32 c = 0; c < 256; ++c) {
				hf_chunk* chunk = &block->chunks[c];

				texture_release(chunk->splatmap);

				kshader_release_binding_set_instance(t->hf_terrain_shader, 1, chunk->shader_instance_id);

				for (u8 m = 0; m < HF_TERRAIN_CHUNK_MAX_MATERIALS; ++m) {
					texture_release(chunk->albedo_textures[m]);
				}
			}
		}
		KFREE_TYPE_CARRAY(t->blocks, hf_block, block_count);

		struct renderer_system_state* renderer_system = engine_systems_get()->renderer_system;
		krenderbuffer index_buffer = renderer_renderbuffer_get(renderer_system, kname_create(KRENDERBUFFER_NAME_INDEX_STANDARD));
		krenderbuffer vertex_buffer = renderer_renderbuffer_get(renderer_system, kname_create(KRENDERBUFFER_NAME_VERTEX_STANDARD));

		// Free vertices
		u64 vertex_buffer_size = sizeof(hf_vertex_3d) * t->vertex_count;
		KFREE_TYPE_CARRAY(t->vertices, hf_vertex_3d, t->vertex_count);
		renderer_renderbuffer_free(renderer_system, vertex_buffer, vertex_buffer_size, t->base_vertex_buffer_offset);

		// Free indices
		u64 index_buffer_size = sizeof(u32) * HF_INDEX_COUNT;
		renderer_renderbuffer_free(renderer_system, index_buffer, index_buffer_size, t->index_buffer_offset);

		kzero_memory(t, sizeof(hf_terrain));
	}
}

void hf_terrain_get_render_data(const hf_terrain* t, frame_data* p_frame_data, hf_terrain_render_data* render_data) {
	KASSERT(t);

	if (t->blocks) {
		render_data->block_count = t->block_count_z * t->block_count_x;
		render_data->blocks = p_frame_data->allocator.allocate(sizeof(hf_terrain_block_render_data) * render_data->block_count);
		render_data->index_count = HF_INDEX_COUNT;
		render_data->index_buffer_offset = t->index_buffer_offset;

		for (u32 z = 0; z < t->block_count_z; ++z) {
			for (u32 x = 0; x < t->block_count_z; ++x) {
				u32 block_index = (t->block_count_z * z) + x;
				hf_block* block = &t->blocks[block_index];
				hf_terrain_block_render_data* brd = &render_data->blocks[block_index];

				// TODO: frustum culling within this block
				brd->chunk_count = HF_BLOCK_CHUNK_COUNT;
				brd->chunks = p_frame_data->allocator.allocate(sizeof(hf_terrain_chunk_render_data) * brd->chunk_count);
				for (u32 i = 0; i < brd->chunk_count; ++i) {
					hf_chunk* chunk = &block->chunks[i];
					hf_terrain_chunk_render_data* crd = &brd->chunks[i];
					crd->vertex_count = HF_CHUNK_VERTEX_COUNT;
					crd->vertex_buffer_offset = chunk->vertex_buffer_offset;
					crd->splatmap = chunk->splatmap;
					for (u8 a = 0; a < HF_TERRAIN_CHUNK_MAX_MATERIALS; ++a) {
						crd->albedo_textures[a] = chunk->albedo_textures[a];
					}
				}
			}
		}
	}
}

hf_block* hf_terrain_get_block_relative_to(hf_terrain* t, hf_block* block, hf_terrain_direction direction) {
	switch (direction) {
	case HF_TERRAIN_DIRECTION_N:
		if (block->z >= t->block_count_z - 1) {
			return KNULL;
		}
		return &t->blocks[t->block_count_x * block->z + block->x];

	case HF_TERRAIN_DIRECTION_NE:
		if (block->z >= t->block_count_z - 1 || block->x == 0) {
			return KNULL;
		}
		return &t->blocks[(t->block_count_x * block->z + block->x) - 1];

	case HF_TERRAIN_DIRECTION_NW:
		if (block->z >= t->block_count_z - 1 || block->x >= t->block_count_x - 1) {
			return KNULL;
		}
		return &t->blocks[(t->block_count_x * block->z + block->x) + 1];

	case HF_TERRAIN_DIRECTION_E:
		if (block->x == 0) {
			return KNULL;
		}
		return &t->blocks[block->index - 1];

	case HF_TERRAIN_DIRECTION_S:
		if (block->z == 0) {
			return KNULL;
		}
		return &t->blocks[t->block_count_x * (block->z - 1) + block->x];

	case HF_TERRAIN_DIRECTION_SE:
		if (block->z == 0 || block->x == 0) {
			return KNULL;
		}
		return &t->blocks[(t->block_count_x * (block->z - 1) + block->x) - 1];

	case HF_TERRAIN_DIRECTION_SW:
		if (block->z == 0 || block->x >= t->block_count_x - 1) {
			return KNULL;
		}
		return &t->blocks[(t->block_count_x * (block->z - 1) + block->x) + 1];

	case HF_TERRAIN_DIRECTION_W:
		if (block->x >= t->block_count_x - 1) {
			return KNULL;
		}
		return &t->blocks[block->index + 1];
	}

	return KNULL;
}

hf_chunk* hf_terrain_block_get_chunk_at(hf_block* block, u8 x, u8 z) {
	return &block->chunks[(z * HF_BLOCK_CHUNK_DIM) + x];
}

hf_chunk* hf_terrain_get_chunk_relative_to(hf_terrain* t, hf_block* block, hf_chunk* chunk, hf_terrain_direction direction) {

	switch (direction) {
	case HF_TERRAIN_DIRECTION_N:
		if (chunk->z >= HF_BLOCK_CHUNK_DIM - 1) {
			// Try getting from the next block.
			hf_block* next_block = hf_terrain_get_block_relative_to(t, block, direction);
			if (next_block) {
				return hf_terrain_block_get_chunk_at(next_block, chunk->x, 0);
			}
			return KNULL;
		}
		return &block->chunks[HF_BLOCK_CHUNK_DIM * chunk->z + chunk->x];

	case HF_TERRAIN_DIRECTION_NE: {
		b8 n_needed = false;
		b8 e_needed = false;
		if (chunk->z >= HF_BLOCK_CHUNK_DIM - 1) {
			n_needed = true;
		}
		if (chunk->x == 0) {
			e_needed = true;
		}
		if (!n_needed && !e_needed) {
			return &block->chunks[(HF_BLOCK_CHUNK_DIM * chunk->z + chunk->x) - 1];
		}

		hf_terrain_direction next_dir;
		u8 x = 0;
		u8 z = 0;
		if (n_needed && e_needed) {
			next_dir = HF_TERRAIN_DIRECTION_NE;
			x = HF_BLOCK_CHUNK_DIM - 1;
			z = 0;
		} else if (n_needed && !e_needed) {
			next_dir = HF_TERRAIN_DIRECTION_N;
			x = chunk->x - 1;
			z = 0;
		} else {
			next_dir = HF_TERRAIN_DIRECTION_E;
			x = HF_BLOCK_CHUNK_DIM - 1;
			z = chunk->z + 1;
		}
		// Try getting from the next block.
		hf_block* next_block = hf_terrain_get_block_relative_to(t, block, next_dir);
		if (next_block) {
			return hf_terrain_block_get_chunk_at(next_block, x, z);
		}
	}
	case HF_TERRAIN_DIRECTION_NW: {
		b8 n_needed = false;
		b8 w_needed = false;
		if (chunk->z >= HF_BLOCK_CHUNK_DIM - 1) {
			n_needed = true;
		}
		if (chunk->x >= HF_BLOCK_CHUNK_DIM - 1) {
			w_needed = true;
		}
		if (!n_needed && !w_needed) {
			return &block->chunks[(HF_BLOCK_CHUNK_DIM * chunk->z + chunk->x) + 1];
		}

		hf_terrain_direction next_dir;
		u8 x = 0;
		u8 z = 0;
		if (n_needed && w_needed) {
			next_dir = HF_TERRAIN_DIRECTION_NW;
			x = HF_BLOCK_CHUNK_DIM - 1;
			z = 0;
		} else if (n_needed && !w_needed) {
			next_dir = HF_TERRAIN_DIRECTION_N;
			x = chunk->x - 1;
			z = 0;
		} else {
			next_dir = HF_TERRAIN_DIRECTION_W;
			x = chunk->x + 1;
			z = chunk->z + 1;
		}
		// Try getting from the next block.
		hf_block* next_block = hf_terrain_get_block_relative_to(t, block, next_dir);
		if (next_block) {
			return hf_terrain_block_get_chunk_at(next_block, x, z);
		}
	}

	case HF_TERRAIN_DIRECTION_E:
		if (chunk->x == 0) {
			// Try getting from the next block.
			hf_block* next_block = hf_terrain_get_block_relative_to(t, block, direction);
			if (next_block) {
				return hf_terrain_block_get_chunk_at(next_block, HF_BLOCK_CHUNK_DIM - 1, chunk->z);
			}
			return KNULL;
		}
		return &block->chunks[chunk->index - 1];

	case HF_TERRAIN_DIRECTION_S:
		if (block->z == 0) {
			// Try getting from the next block.
			hf_block* next_block = hf_terrain_get_block_relative_to(t, block, direction);
			if (next_block) {
				return hf_terrain_block_get_chunk_at(next_block, chunk->x, HF_BLOCK_CHUNK_DIM - 1);
			}
			return KNULL;
		}
		return &block->chunks[HF_BLOCK_CHUNK_DIM * (chunk->z - 1) + chunk->x];

	case HF_TERRAIN_DIRECTION_SE: {
		b8 n_needed = false;
		b8 e_needed = false;
		if (chunk->z == 0) {
			n_needed = true;
		}
		if (chunk->x == 0) {
			e_needed = true;
		}
		if (!n_needed && !e_needed) {
			return &block->chunks[(HF_BLOCK_CHUNK_DIM * (chunk->z - 1) + chunk->x) - 1];
		}

		hf_terrain_direction next_dir;
		u8 x = 0;
		u8 z = 0;
		if (n_needed && e_needed) {
			next_dir = HF_TERRAIN_DIRECTION_NE;
			x = HF_BLOCK_CHUNK_DIM - 1;
			z = HF_BLOCK_CHUNK_DIM - 1;
		} else if (n_needed && !e_needed) {
			next_dir = HF_TERRAIN_DIRECTION_N;
			x = chunk->x - 1;
			z = HF_BLOCK_CHUNK_DIM - 1;
		} else {
			next_dir = HF_TERRAIN_DIRECTION_E;
			x = HF_BLOCK_CHUNK_DIM - 1;
			z = chunk->z - 1;
		}
		// Try getting from the next block.
		hf_block* next_block = hf_terrain_get_block_relative_to(t, block, next_dir);
		if (next_block) {
			return hf_terrain_block_get_chunk_at(next_block, x, z);
		}
	}
	case HF_TERRAIN_DIRECTION_SW: {
		b8 n_needed = false;
		b8 w_needed = false;
		if (chunk->z == 0) {
			n_needed = true;
		}
		if (chunk->x >= HF_BLOCK_CHUNK_DIM - 1) {
			w_needed = true;
		}
		if (!n_needed && !w_needed) {
			return &block->chunks[(HF_BLOCK_CHUNK_DIM * (chunk->z - 1) + chunk->x) + 1];
		}

		hf_terrain_direction next_dir;
		u8 x = 0;
		u8 z = 0;
		if (n_needed && w_needed) {
			next_dir = HF_TERRAIN_DIRECTION_NW;
			x = HF_BLOCK_CHUNK_DIM - 1;
			z = HF_BLOCK_CHUNK_DIM - 1;
		} else if (n_needed && !w_needed) {
			next_dir = HF_TERRAIN_DIRECTION_N;
			x = chunk->x - 1;
			z = HF_BLOCK_CHUNK_DIM - 1;
		} else {
			next_dir = HF_TERRAIN_DIRECTION_W;
			x = chunk->x + 1;
			z = chunk->z - 1;
		}
		// Try getting from the next block.
		hf_block* next_block = hf_terrain_get_block_relative_to(t, block, next_dir);
		if (next_block) {
			return hf_terrain_block_get_chunk_at(next_block, x, z);
		}
	}
	case HF_TERRAIN_DIRECTION_W:
		if (chunk->x >= HF_BLOCK_CHUNK_DIM - 1) {
			// Try getting from the next block.
			hf_block* next_block = hf_terrain_get_block_relative_to(t, block, direction);
			if (next_block) {
				return hf_terrain_block_get_chunk_at(next_block, 0, chunk->z);
			}
			return KNULL;
		}
		return &block->chunks[chunk->index + 1];
	}
}
