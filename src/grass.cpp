// 3D World - Grass Generation and Rendering Code
// by Frank Gennari
// 9/28/10

#include "3DWorld.h"
#include "grass.h"
#include "mesh.h"
#include "physics_objects.h"
#include "textures_3dw.h"
#include "lightmap.h"
#include "shaders.h"
#include "draw_utils.h"


bool grass_enabled(1);
unsigned grass_density(0);
float grass_length(0.02), grass_width(0.002), flower_density(0.0);

extern int default_ground_tex, read_landscape, display_mode, animate2, frame_counter, draw_model;
extern unsigned create_voxel_landscape;
extern float vegetation, zmin, zmax, fticks, tfticks, h_dirt[], leaf_color_coherence, tree_deadness, relh_adj_tex, zmax_est;
extern colorRGBA leaf_base_color, flower_color;
extern vector3d wind;
extern obj_type object_types[];
extern coll_obj_group coll_objects;

bool is_grass_enabled();


// *** detail scenery (shared with grass and flowers)

void detail_scenery_t::setup_shaders_pre(shader_t &s) { // used for grass and flowers
	s.setup_enabled_lights(2, 2); // FS; L0-L1: static directional
	if (set_dlights_booleans(s, 1, 1)) {s.set_prefix("#define NO_DL_SPECULAR", 1);} // FS
	s.check_for_fog_disabled();
	s.set_bool_prefix("use_shadow_map", shadow_map_enabled(), 1); // FS
}

void detail_scenery_t::setup_shaders_post(shader_t &s) { // used for grass and flowers
	s.begin_shader();
	s.setup_scene_bounds();
	setup_dlight_textures(s);
	if (shadow_map_enabled()) {set_smap_shader_for_all_lights(s);}
	setup_wind_for_shader(s, 1);
	s.add_uniform_int("tex0", 0);
	s.setup_fog_scale();
}


// *** grass ***

void grass_manager_t::grass_t::merge(grass_t const &g) {

	p   = (p + g.p)*0.5; // average locations
	float const dmag1(dir.mag()), dmag2(g.dir.mag());
	dir = (dir/dmag1 + g.dir/dmag2).get_norm() * (0.5*(dmag1 + dmag2)); // average directions and lengths independently
	n   = (n + g.n).get_norm(); // average normals
	w  += g.w; // add widths to preserve surface area
	//UNROLL_3X(c[i_] = (unsigned char)(unsigned(c[i_]) + unsigned(g.c[i_]))/2;) // don't average colors because they're used for the density filtering hash
}

void grass_manager_t::clear() {
	clear_vbo();
	grass.clear();
}

vector3d grass_manager_t::interpolate_mesh_normal(point const &pos) const {

	float const xp((pos.x + X_SCENE_SIZE)*DX_VAL_INV), yp((pos.y + Y_SCENE_SIZE)*DY_VAL_INV);
	int const x0((int)xp), y0((int)yp);
	if (point_outside_mesh(x0, y0) || point_outside_mesh(x0+1, y0+1)) return plus_z; // shouldn't get here
	float const xpi(fabs(xp - (float)x0)), ypi(fabs(yp - (float)y0)); // cubic_interpolate()?
	return vertex_normals[y0+0][x0+0]*((1.0 - xpi)*(1.0 - ypi))
			+ vertex_normals[y0+1][x0+0]*((1.0 - xpi)*ypi)
			+ vertex_normals[y0+0][x0+1]*(xpi*(1.0 - ypi))
		    + vertex_normals[y0+1][x0+1]*(xpi*ypi);
}

void grass_manager_t::add_grass_blade(point const &pos, float cscale, bool on_mesh) {

	vector3d const base_dir(on_mesh ? 0.5*(plus_z + interpolate_mesh_normal(pos)) : plus_z); // average mesh normal and +z for grass on mesh
	vector3d const dir((base_dir + rgen.signed_rand_vector(0.3)).get_norm());
	vector3d const norm(cross_product(dir, rgen.signed_rand_vector()).get_norm());
	float const ilch(1.0 - leaf_color_coherence), dead_scale(CLIP_TO_01(tree_deadness));
	float const base_color[3] = {0.25, 0.6, 0.08};
	float const mod_color [3] = {0.3,  0.3, 0.12};
	float const lbc_mult  [3] = {0.2,  0.4, 0.0 };
	float const dead_color[3] = {0.75, 0.6, 0.0 };
	unsigned char color[3];

	for (unsigned i = 0; i < 3; ++i) {
		float const ccomp(CLIP_TO_01(cscale*(base_color[i] + lbc_mult[i]*leaf_base_color[i] + ilch*mod_color[i]*rgen.rand_float())));
		color[i] = (unsigned char)(255.0*(dead_scale*dead_color[i] + (1.0 - dead_scale)*ccomp));
	}
	float const length(grass_length*rgen.rand_uniform(0.7, 1.3));
	float const width( grass_width *rgen.rand_uniform(0.7, 1.3));
	grass.push_back(grass_t(pos, dir*length, norm, color, width, on_mesh));
}

void grass_manager_t::create_new_vbo() {

	delete_vbo(vbo); // unnecessary since vbo is always 0 here?
	vbo        = create_vbo();
	data_valid = 0;
}

void grass_manager_t::add_to_vbo_data(grass_t const &g, vector<grass_data_t> &data, unsigned &ix, vector3d &norm) const {

	point const p1(g.p), p2(p1 + g.dir + point(0.0, 0.0, 0.05*grass_length));
	vector3d const binorm(cross_product(g.dir, g.n).get_norm());
	vector3d const delta(binorm*(0.5*g.w));
	data[ix++].assign(p1-delta, norm, g.c);
	data[ix++].assign(p1+delta, norm, g.c);
	data[ix++].assign(p2,       norm, g.c);
	assert(ix <= data.size());
}

void grass_manager_t::scale_grass(float lscale, float wscale) {

	for (auto i = grass.begin(); i != grass.end(); ++i) {
		i->dir *= lscale;
		i->w   *= wscale;
	}
	clear_vbo();
}

void grass_manager_t::begin_draw() const {
	pre_render();
	grass_data_t::set_vbo_arrays();
	select_texture(GRASS_BLADE_TEX);
}

void grass_manager_t::end_draw() const {
	post_render();
	check_gl_error(40);
}


void grass_tile_manager_t::gen_block(unsigned bix) {

	for (unsigned y = 0; y < GRASS_BLOCK_SZ; ++y) {
		for (unsigned x = 0; x < GRASS_BLOCK_SZ; ++x) {
			float const xval(x*DX_VAL), yval(y*DY_VAL);

			for (unsigned n = 0; n < grass_density; ++n) {
				add_grass_blade(point(rgen.rand_uniform(xval, xval+DX_VAL), rgen.rand_uniform(yval, yval+DY_VAL), 0.0), TT_GRASS_COLOR_SCALE, 0); // no mesh normal
			}
		}
	}
	assert(bix+1 < vbo_offsets[0].size());
	vbo_offsets[0][bix+1] = grass.size(); // end of block/beginning of next block
}


void grass_tile_manager_t::gen_lod_block(unsigned bix, unsigned lod) {

	if (lod == 0) {
		gen_block(bix);
		return;
	}
	assert(bix+1 < vbo_offsets[lod].size());
	unsigned const search_dist(1*grass_density); // enough for one cell
	unsigned const start_ix(vbo_offsets[lod-1][bix]), end_ix(vbo_offsets[lod-1][bix+1]); // from previous LOD
	float const dmax(2.5*grass_width*(1 << lod));
	vector<unsigned char> used((end_ix - start_ix), 0); // initially all unused
	
	for (unsigned i = start_ix; i < end_ix; ++i) {
		if (used[i-start_ix]) continue; // already used
		grass.push_back(grass[i]); // seed with an existing grass blade
		float dmin_sq(dmax*dmax); // start at max allowed dist
		unsigned merge_ix(i); // start at ourself (invalid)
		unsigned const end_val(min(i+search_dist, end_ix));

		for (unsigned cur = i+1; cur < end_val; ++cur) {
			float const dist_sq(p2p_dist_sq(grass[i].p, grass[cur].p));
					
			if (dist_sq < dmin_sq) {
				dmin_sq  = dist_sq;
				merge_ix = cur;
			}
		}
		if (merge_ix > i) {
			assert(merge_ix < grass.size());
			assert(merge_ix-start_ix < used.size());
			grass.back().merge(grass[merge_ix]);
			used[merge_ix-start_ix] = 1;
		}
	} // for i
	//cout << "level " << lod << " num: " << (grass.size() - vbo_offsets[lod][bix]) << endl;
	vbo_offsets[lod][bix+1] = grass.size(); // end of current LOD block/beginning of next LOD block
}


void grass_tile_manager_t::clear() {

	grass_manager_t::clear();
	for (unsigned lod = 0; lod < NUM_GRASS_LODS; ++lod) {vbo_offsets[lod].clear();}
}


void grass_tile_manager_t::upload_data() {

	if (empty()) return;
	RESET_TIME;
	vector<grass_data_t> data(3*size()); // 3 vertices per grass blade

	for (unsigned i = 0, ix = 0; i < size(); ++i) {
		vector3d norm(plus_z); // use grass normal? 2-sided lighting? generate normals in vertex shader?
		//vector3d norm(grass[i].n);
		add_to_vbo_data(grass[i], data, ix, norm);
	}
	upload_to_vbo(vbo, data, 0, 1);
	data_valid = 1;
	PRINT_TIME("Grass Tile Upload");
}


void grass_tile_manager_t::gen_grass() {

	RESET_TIME;
	assert(NUM_GRASS_LODS > 0);
	assert((MESH_X_SIZE % GRASS_BLOCK_SZ) == 0 && (MESH_Y_SIZE % GRASS_BLOCK_SZ) == 0);

	for (unsigned lod = 0; lod < NUM_GRASS_LODS; ++lod) {
		vbo_offsets[lod].resize(NUM_RND_GRASS_BLOCKS+1);
		vbo_offsets[lod][0] = grass.size(); // start
		for (unsigned i = 0; i < NUM_RND_GRASS_BLOCKS; ++i) {gen_lod_block(i, lod);}
	}
	PRINT_TIME("Grass Tile Gen");
}


void grass_tile_manager_t::update() { // to be called once per frame

	if (!is_grass_enabled()) {clear(); return;}
	if (empty()    ) {gen_grass();}
	if (vbo == 0   ) {create_new_vbo();}
	if (!data_valid) {upload_data();}
}


// Note: density won't work if grass is spatially sorted, which is currently the case for tiled terrain grass
void grass_tile_manager_t::render_block(unsigned block_ix, unsigned lod, float density, unsigned num_instances) {

	assert(density > 0.0 && density <= 1.0);
	assert(lod < NUM_GRASS_LODS);
	assert(block_ix+1 < vbo_offsets[lod].size());
	unsigned const start_ix(vbo_offsets[lod][block_ix]), end_ix(vbo_offsets[lod][block_ix+1]);
	assert(start_ix < end_ix && end_ix <= grass.size());
	unsigned const num_tris(ceil(density*(end_ix - start_ix)));
	if (num_tris == 0) return;
	bind_vbo(vbo); // needed because incoming vbo is 0 (so that instance attrib array isn't bound to a vbo)
	glDrawArraysInstanced(GL_TRIANGLES, 3*start_ix, 3*num_tris, num_instances);
}


class grass_manager_dynamic_t : public grass_manager_t {
	
	vector<unsigned> mesh_to_grass_map; // maps mesh x,y index to starting index in grass vector
	vector<int> last_occluder;
	mutable vector<grass_data_t> vertex_data_buffer;
	bool has_voxel_grass;
	int last_light;
	point last_lpos;

	bool hcm_chk(int x, int y) const {
		return (!point_outside_mesh(x, y) && (mesh_height[y][x] + SMALL_NUMBER < h_collision_matrix[y][x]));
	}
	void check_and_update_grass(unsigned ix, unsigned min_up, unsigned max_up) {
		if (min_up > max_up) return; // nothing updated
		//modified[ix] = 1; // usually few duplicates each frame, except for cluster grenade explosions
		if (vbo > 0) {upload_data_to_vbo(min_up, max_up+1, 0);}
		//data_valid = 0;
	}

public:
	grass_manager_dynamic_t() : has_voxel_grass(0), last_light(-1), last_lpos(all_zeros) {}
	
	void clear() {
		grass_manager_t::clear();
		mesh_to_grass_map.clear();
	}

	bool ao_lighting_too_low(point const &pos) {
		float const keep_prob(5.0*(get_voxel_terrain_ao_lighting_val(pos) - 0.8));
		return (keep_prob < 0.0 || (keep_prob < 1.0 && rgen.randd() > keep_prob)); // too dark
	}

	void gen_grass() {
		RESET_TIME;
		float const dz_inv(1.0/(zmax - zmin));
		mesh_to_grass_map.resize(XY_MULT_SIZE+1, 0);
		object_types[GRASS].radius = 0.0;
		rgen.pregen_floats(10000);
		unsigned num_voxel_polys(0), num_voxel_blades(0);
		bool const grass_tex_enabled(default_ground_tex < 0 || default_ground_tex == GROUND_TEX);
		unsigned const om_stride(MESH_X_SIZE+1);
		vector<unsigned char> occ_map;
		unsigned const SAMPLES_PER_TILE(min(grass_density, 16U));

		if (grass_tex_enabled) {
			occ_map.resize(om_stride*(MESH_Y_SIZE+1), 0);
		
			#pragma omp parallel for schedule(dynamic,1)
			for (int y = 0; y <= MESH_Y_SIZE; ++y) {
				rand_gen_t occ_rgen;
				occ_rgen.set_state(y, 123);

				for (int x = 0; x <= MESH_X_SIZE; ++x) {
					if (is_mesh_disabled(x, y)) continue;
					point const start_pt(get_xval(x), get_yval(y), mesh_height[min(y, MESH_Y_SIZE-1)][min(x, MESH_X_SIZE-1)]);
					unsigned char &val(occ_map[y*om_stride + x]);

					for (unsigned n = 0; n < SAMPLES_PER_TILE; ++n) {
						point const end_pt(start_pt + Z_SCENE_SIZE*vector3d(0.5*occ_rgen.signed_rand_float(), 0.5*occ_rgen.signed_rand_float(), 1.0));
						int cindex(-1);
						if (check_coll_line(start_pt, end_pt, cindex, -1, 1, 0, 0)) {++val;} // ignore alpha value (even for leaves, to incrase their influence)
					}
				}
			}
			//PRINT_TIME("Grass Occlusion");
		}
		for (int y = 0; y < MESH_Y_SIZE; ++y) {
			for (int x = 0; x < MESH_X_SIZE; ++x) {
				mesh_to_grass_map[y*MESH_X_SIZE+x] = (unsigned)grass.size();
				float const xval(get_xval(x)), yval(get_yval(y));

				//if (create_voxel_landscape) {
				if (coll_objects.has_voxel_cobjs) {
					float const blades_per_area(grass_density/dxdy);
					coll_cell const &cell(v_collision_matrix[y][x]);
					cube_t const test_cube(xval-0.5*DX_VAL, xval+0.5*DX_VAL, yval-0.5*DY_VAL, yval+0.5*DY_VAL, mesh_height[y][x], czmax+grass_length);
					float const nz_thresh = 0.4;

					for (unsigned k = 0; k < cell.cvals.size(); ++k) {
						int const index(cell.cvals[k]);
						if (index < 0) continue;
						coll_obj const &cobj(coll_objects.get_cobj(index));
						if (cobj.type != COLL_POLYGON || cobj.cp.cobj_type != COBJ_TYPE_VOX_TERRAIN) continue;
						if (cobj.norm.z < nz_thresh)     continue; // not oriented upward
						if (!cobj.intersects(test_cube)) continue;
						assert(cobj.npoints == 3 || cobj.npoints == 4); // triangles and quads
						float const density_scale((cobj.norm.z - nz_thresh)/(1.0 - nz_thresh)); // FIXME: better to use vertex normals and interpolate?
						unsigned const num_blades(blades_per_area*density_scale*polygon_area(cobj.points, cobj.npoints) + 0.5);
						++num_voxel_polys;

						for (unsigned n = 0; n < num_blades; ++n) {
							float const r1(rgen.randd()), r2(rgen.randd()), sqrt_r1(sqrt(r1));
							unsigned const ptix((cobj.npoints == 4 && n < num_blades/2) ? 3 : 1); // handle both triangles and quads
							point const pos((1 - sqrt_r1)*cobj.points[0] + (sqrt_r1*(1 - r2))*cobj.points[ptix] + (sqrt_r1*r2)*cobj.points[2]);
							if (!test_cube.contains_pt(pos)) continue; // bbox test
							if (ao_lighting_too_low(pos))    continue; // too dark
							add_grass_blade(pos, 0.8, 0); // use cobj.norm instead of mesh normal?
							++num_voxel_blades;
							has_voxel_grass = 1;
						}
					} // for k
				}

				// create mesh grass
				if (!grass_tex_enabled) continue; // no grass
				if (x == MESH_X_SIZE-1 || y == MESH_Y_SIZE-1) continue; // mesh not drawn
				if (is_mesh_disabled(x, y) || is_mesh_disabled(x+1, y) || is_mesh_disabled(x, y+1) || is_mesh_disabled(x+1, y+1)) continue; // mesh disabled
				if (mesh_height[y][x] < water_matrix[y][x])   continue; // underwater (make this dynamically update?)
				bool const do_cobj_check(hcm_chk(x, y) || hcm_chk(x+1, y) || hcm_chk(x, y+1) || hcm_chk(x+1, y+1));
				float const vnz(vertex_normals[y][x].z);
				float const *const sti(sthresh[0]);
				float slope_scale(1.0);
				if (vnz < sti[1]) {slope_scale = CLIP_TO_01((vnz - sti[0])/(sti[1] - sti[0]));} // handle steep slopes (dirt/rock texture replaces grass texture)
				if (slope_scale == 0.0) continue; // no grass
				assert(vnz > 0.0);
				float mod_den(grass_density/vnz); // slightly more grass on steep slopes so that we have equal density over the surface, not just the XY projection
				
				if (!occ_map.empty()) { // check 4 corners of occlusion map
					unsigned const occ_cnt(occ_map[y*om_stride + x] + occ_map[y*om_stride + x+1] + occ_map[(y+1)*om_stride + x] + occ_map[(y+1)*om_stride + x+1]);
					float const sunlight(1.0 - occ_cnt/(4.0*SAMPLES_PER_TILE));
					mod_den *= min(1.0f, 2.0f*sunlight); // more than half occluded reduces grass density
				}
				unsigned const tile_density(round_fp(mod_den));

				for (unsigned n = 0; n < tile_density; ++n) {
					float const xv(rgen.rand_uniform(xval, xval + DX_VAL));
					float const yv(rgen.rand_uniform(yval, yval + DY_VAL));
					float const mh(interpolate_mesh_zval(xv, yv, 0.0, 0, 1));
					point const pos(xv, yv, mh);

					if (default_ground_tex < 0 && zmin < zmax) {
						float const relh(relh_adj_tex + (mh - zmin)*dz_inv);
						int k1, k2;
						float t(0.0);
						get_tids(relh, k1, k2, &t); // t==0 => use k1, t==1 => use k2
						int const id1(lttex_dirt[k1].id), id2(lttex_dirt[k2].id);
						if (id1 != GROUND_TEX && id2 != GROUND_TEX) continue; // not ground texture
						float density(1.0);
						if (id1 != GROUND_TEX) {density = t;}
						if (id2 != GROUND_TEX) {density = 1.0 - t;}
						density *= slope_scale;
						if (density < 1.0 && rgen.randd() >= density) continue; // skip - density too low
					}
					// skip grass intersecting cobjs
					if (do_cobj_check && dwobject(GRASS, pos).check_vert_collision(0, 0, 0)) continue; // make a GRASS object for collision detection

					if (create_voxel_landscape) {
						if (point_inside_voxel_terrain(pos)) continue; // inside voxel volume
						if (ao_lighting_too_low(pos))        continue; // too dark
					}
					add_grass_blade(pos, 0.8, 1);
				} // for n
			} // for x
		} // for y
		if (has_voxel_grass) {cout << "voxel_polys: " << num_voxel_polys << ", voxel_blades: " << num_voxel_blades << endl;}
		mesh_to_grass_map[XY_MULT_SIZE] = (unsigned)grass.size();
		remove_excess_cap(grass);
		PRINT_TIME("Grass Generation");
	}

	void upload_data_to_vbo(unsigned start, unsigned end, bool alloc_data) const {
		if (start == end) return; // nothing to update
		assert(start < end && end <= grass.size());
		unsigned const num_verts(3*(end - start)), block_size(3*4096); // must be a multiple of 3
		unsigned const vntc_sz(sizeof(grass_data_t));
		unsigned offset(3*start);
		vertex_data_buffer.resize(min(num_verts, block_size));
		bind_vbo(vbo);
		if (alloc_data) {upload_vbo_data(NULL, 3*grass.size()*vntc_sz);} // initial upload (setup, no data)
		
		for (unsigned i = start, ix = 0; i < end; ++i) {
			//vector3d norm(grass[i].n); // use grass normal? 2-sided lighting?
			//vector3d norm(surface_normals[get_ypos(p1.y)][get_xpos(p1.x)]);
			vector3d norm(grass[i].on_mesh ? interpolate_mesh_normal(grass[i].p) : plus_z); // use +z normal for voxels
			add_to_vbo_data(grass[i], vertex_data_buffer, ix, norm);

			if (ix == block_size || i+1 == end) { // filled block or last entry
				upload_vbo_sub_data(&vertex_data_buffer.front(), offset*vntc_sz, ix*vntc_sz); // upload part or all of the data
				offset += ix;
				ix = 0; // reset to the beginning of the buffer
			}
		}
		assert(offset == 3*end);
		bind_vbo(0);
	}

	float get_xy_bounds(point const &pos, float radius, int &x1, int &y1, int &x2, int &y2) const {
		if (empty() || !is_over_mesh(pos)) return 0.0;
		assert(radius > 0.0);

		if (!has_voxel_grass) { // determine radius at grass height
			float const mh(interpolate_mesh_zval(pos.x, pos.y, 0.0, 0, 1));
			if ((pos.z - radius) > (mh + grass_length)) return 0.0; // above grass
			if ((pos.z + radius) < mh)                  return 0.0; // below the mesh
			float const height(pos.z - (mh + grass_length));
			if (height > 0.0) {radius = sqrt(max(1.0E-6f, (radius*radius - height*height)));}
		}
		x1 = get_xpos(pos.x - radius) - 1;
		x2 = get_xpos(pos.x + radius);
		y1 = get_ypos(pos.y - radius) - 1;
		y2 = get_ypos(pos.y + radius);
		return radius;
	}

	unsigned get_start_and_end(int x, int y, unsigned &start, unsigned &end) const {
		unsigned const ix(y*MESH_X_SIZE + x);
		assert(ix+1 < mesh_to_grass_map.size());
		start = mesh_to_grass_map[ix];
		end   = mesh_to_grass_map[ix+1];
		assert(start <= end && end <= grass.size());
		return ix;
	}

	bool place_obj_on_grass(point &pos, float radius) const {
		int x1, y1, x2, y2;
		float const rad(get_xy_bounds(pos, radius, x1, y1, x2, y2));
		if (rad == 0.0) return 0;
		bool updated(0);

		for (int y = y1; y <= y2; ++y) {
			for (int x = x1; x <= x2; ++x) {
				if (point_outside_mesh(x, y)) continue;
				unsigned start, end;
				get_start_and_end(x, y, start, end);
				if (start == end) continue; // no grass at this location

				for (unsigned i = start; i < end; ++i) {
					if (grass[i].dir == zero_vector) continue; // removed
					float const dsq(p2p_dist_xy_sq(pos, grass[i].p));
					if (dsq > rad*rad) continue; // too far away
					pos.z   = max(pos.z, (grass[i].p.z + grass[i].dir.z + radius));
					updated = 1;
				}
			}
		}
		return updated;
	}

	float get_grass_density(point const &pos) {
		if (empty() || !is_over_mesh(pos)) return 0.0;
		int const x(get_xpos(pos.x)), y(get_ypos(pos.y));
		if (point_outside_mesh(x, y))      return 0.0;
		unsigned const ix(y*MESH_X_SIZE + x);
		assert(ix+1 < mesh_to_grass_map.size());
		unsigned const num_grass(mesh_to_grass_map[ix+1] - mesh_to_grass_map[ix]); // Note: ignores crushed/cut/removed grass
		return ((float)num_grass)/((float)grass_density);
	}

	void mesh_height_change(int x, int y) {
		assert(!point_outside_mesh(x, y));
		unsigned start, end;
		unsigned const ix(get_start_and_end(x, y, start, end));
		unsigned min_up(end+1), max_up(start);
		int last_cobj(-1);

		for (unsigned i = start; i < end; ++i) { // will do nothing if there's no grass here
			grass_t &g(grass[i]);
			if (!g.on_mesh || g.dir == zero_vector) continue; // not on mesh, or already "removed"
			float const mh(interpolate_mesh_zval(g.p.x, g.p.y, 0.0, 0, 1));

			if (fabs(g.p.z - mh) > 0.01*grass_width) { // is there any way we can check the ground texture to see if we sill have grass texture here?
				g.p.z  = mh;
				min_up = min(min_up, i);
				max_up = max(max_up, i);
			}
		} // for i
		check_and_update_grass(ix, min_up, max_up);
	}

	void modify_grass(point const &pos, float radius, bool crush, bool burn, bool cut, bool check_uw, bool add_color, bool remove, colorRGBA const &color) {
		if (!burn && !crush && !cut && !check_uw && !add_color && !remove) return; // nothing left to do
		int x1, y1, x2, y2;
		float const rad(get_xy_bounds(pos, radius, x1, y1, x2, y2));
		if (rad == 0.0) return;
		color_wrapper cw;
		if (add_color) {cw.set_c3(color);}

		// modify grass within radius of pos
		for (int y = y1; y <= y2; ++y) {
			for (int x = x1; x <= x2; ++x) {
				if (point_outside_mesh(x, y)) continue;
				bool const maybe_underwater((burn || check_uw) && has_water(x, y) && mesh_height[y][x] <= water_matrix[y][x]);
				unsigned start, end;
				unsigned const ix(get_start_and_end(x, y, start, end));
				unsigned min_up(end+1), max_up(start);

				for (unsigned i = start; i < end; ++i) { // will do nothing if there's no grass here
					grass_t &g(grass[i]);
					if (g.dir == zero_vector) continue; // already "removed"
					float const dsq(p2p_dist_xy_sq(pos, g.p));
					if (dsq > rad*rad) continue; // too far away
					float const reld(sqrt(dsq)/rad);
					bool const underwater(maybe_underwater && g.on_mesh);
					bool updated(0);

					if (cut) {
						float const length(g.dir.mag());

						if (length > 0.25*grass_length) {
							g.dir  *= reld;
							updated = 1;
						}
					}
					if (crush) {
						vector3d const &sn(surface_normals[y][x]);
						float const length(g.dir.mag());

						if (fabs(dot_product(g.dir, sn)) > 0.1*length) { // update if not flat against the mesh
							float const dx(g.p.x - pos.x), dy(g.p.y - pos.y), atten_val(1.0 - (1.0 - reld)*(1.0 - reld));
							vector3d const new_dir(vector3d(dx, dy, -(sn.x*dx + sn.y*dy)/sn.z).get_norm()); // point away from crushing point

							if (dot_product(g.dir, new_dir) < 0.95*length) { // update if not already aligned
								g.dir   = (g.dir*(atten_val/length) + new_dir*(1.0 - atten_val)).get_norm()*length;
								g.n     = (g.n*atten_val + sn*(1.0 - atten_val)).get_norm();
								updated = 1;
							}
						}
					}
					if (add_color && !underwater) {
						UNROLL_3X(updated |= (g.c[i_] != cw.c[i_]);) // not already this color
						
						if (updated) {
							float const atten_val(1.0 - color.alpha*(1.0 - reld)*(1.0 - reld));
							UNROLL_3X(g.c[i_] = (unsigned char)(atten_val*g.c[i_] + (1.0 - atten_val)*cw.c[i_]);)
						}
					}
					if (burn && !underwater) {
						float const atten_val(1.0 - (1.0 - reld)*(1.0 - reld));
						UNROLL_3X(updated |= (g.c[i_] > 0);)
						if (updated) {UNROLL_3X(g.c[i_] = (unsigned char)(atten_val*g.c[i_]);)}
					}
					if (check_uw && underwater && (g.p.z + g.dir.mag()) <= water_matrix[y][x]) {
						unsigned char uwc[3] = {120,  100, 50};
						UNROLL_3X(updated |= (g.c[i_] != uwc[i_]);)
						if (updated) {UNROLL_3X(g.c[i_] = (unsigned char)(0.9*g.c[i_] + 0.1*uwc[i_]);)}
					}
					if (remove) {
						// Note: if we're removing, it doesn't make sense to do any other operations since they won't have any effect
						g.dir   = zero_vector; // make zero length (can't actually remove it)
						updated = 1;
					}
					if (updated) {
						min_up = min(min_up, i);
						max_up = max(max_up, i);
					}
				} // for i
				check_and_update_grass(ix, min_up, max_up);
			} // for x
		} // for y
	}

	void upload_data(bool alloc_data) {
		if (empty()) return;
		RESET_TIME;
		upload_data_to_vbo(0, (unsigned)grass.size(), alloc_data);
		data_valid = 1;
		PRINT_TIME("Grass Upload VBO");
		cout << "mem used: " << grass.size()*sizeof(grass_t) << ", vmem used: " << 3*grass.size()*sizeof(grass_data_t) << endl;
	}

	void check_for_updates() {
		bool const vbo_invalid(vbo == 0);
		if (vbo_invalid) {create_new_vbo();}
		if (!data_valid) {upload_data(vbo_invalid);}
	}

	void draw_range(unsigned beg_ix, unsigned end_ix) const {
		assert(beg_ix <= end_ix && end_ix <= grass.size());
		if (beg_ix < end_ix) {glDrawArrays(GL_TRIANGLES, 3*beg_ix, 3*(end_ix - beg_ix));} // nonempty segment
	}

	static void setup_shaders(shader_t &s, bool distant) { // per-pixel dynamic lighting
		setup_shaders_pre(s);
		if (distant) {s.set_prefix("#define NO_GRASS_TEXTURE", 1);} // FS
		s.set_vert_shader("wind.part*+grass_texture.part+grass_pp_dl");
		s.set_frag_shader("linear_fog.part+ads_lighting.part*+shadow_map.part*+dynamic_lighting.part*+grass_with_dlights");
		//s.set_vert_shader("ads_lighting.part*+shadow_map.part*+wind.part*+grass_texture.part+grass");
		//s.set_frag_shader("linear_fog.part+textured_with_fog");
		setup_shaders_post(s);
		s.add_uniform_color("color_scale", (distant ? texture_color(GRASS_BLADE_TEX) : WHITE));
		s.add_uniform_float("height", grass_length);
		s.set_specular(0.2, 20.0);
	}

	// texture units used: 0: grass texture, 1: wind texture
	void draw() {
		if (empty()) return;
		//RESET_TIME;
		check_for_updates();
		shader_t s;
		setup_shaders(s, 1); // enables lighting and shadows as well
		begin_draw();

		// draw the grass
		bool last_visible(0);
		unsigned beg_ix(0);
		point const camera(get_camera_pos()), adj_camera(camera + point(0.0, 0.0, 2.0*grass_length));
		last_occluder.resize(XY_MULT_SIZE, -1);
		int last_occ_used(-1);
		vector<unsigned> nearby_ixs;

		for (int y = 0; y < MESH_Y_SIZE; ++y) {
			for (int x = 0; x < MESH_X_SIZE; ++x) {
				unsigned const ix(y*MESH_X_SIZE + x);
				if (mesh_to_grass_map[ix] == mesh_to_grass_map[ix+1]) continue; // empty section
				point const mpos(get_mesh_xyz_pos(x, y));
				bool visible(1);

				if (x+1 < MESH_X_SIZE && y+1 < MESH_Y_SIZE &&
					dot_product(surface_normals[y  ][x  ], (adj_camera - mpos                      )) < 0.0 &&
					dot_product(surface_normals[y  ][x+1], (adj_camera - get_mesh_xyz_pos(x+1, y  ))) < 0.0 &&
					dot_product(surface_normals[y+1][x+1], (adj_camera - get_mesh_xyz_pos(x+1, y+1))) < 0.0 &&
					dot_product(surface_normals[y+1][x  ], (adj_camera - get_mesh_xyz_pos(x,   y+1))) < 0.0) // back_facing
				{
					visible = 0;
				}
				else {
					float const grass_zmax((has_voxel_grass ? max(mpos.z, czmax) : mpos.z) + grass_length);
					cube_t const cube(mpos.x-grass_length, mpos.x+DX_VAL+grass_length,
									  mpos.y-grass_length, mpos.y+DY_VAL+grass_length, z_min_matrix[y][x], grass_zmax);
					visible = camera_pdu.cube_visible(cube); // could use camera_pdu.sphere_and_cube_visible_test()
				
					if (visible && (display_mode & 0x08)) {
						int &last_occ_cobj(last_occluder[y*MESH_X_SIZE + x]);

						if (last_occ_cobj >= 0 || ((frame_counter + y) & 7) == 0) { // only sometimes update if not previously occluded
							point pts[8];
							cube.get_points(pts);

							if (x > 0 && last_occ_cobj >= 0 && last_occluder[y*MESH_X_SIZE + x-1] == last_occ_cobj &&
								!coll_objects[last_occ_cobj].disabled() && coll_objects[last_occ_cobj].intersects_all_pts(camera, (pts+4), 4))
							{
								visible = 0; // check last 4 points (first 4 should already be checked from the last grass block)
							}
							else {
								if (last_occ_cobj < 0) {last_occ_cobj = last_occ_used;}
								visible &= !cobj_contained_ref(camera, cube.get_cube_center(), pts, 8, -1, last_occ_cobj);
								if (visible) {last_occ_cobj = -1;}
							}
							if (last_occ_cobj >= 0) {last_occ_used = last_occ_cobj;}
						}
					}
				}
				if (visible && dist_less_than(camera, mpos, 1000.0*grass_width)) { // nearby grass
					nearby_ixs.push_back(ix);
					visible = 0; // drawn in the second pass
				}
				if (visible && !last_visible) { // start a segment
					beg_ix = mesh_to_grass_map[ix];
				}
				else if (!visible && last_visible) { // end a segment
					draw_range(beg_ix, mesh_to_grass_map[ix]);
				}
				last_visible = visible;
			}
		}
		if (last_visible) {draw_range(beg_ix, (unsigned)grass.size());}
		end_draw();
		s.end_shader();

		if (!nearby_ixs.empty()) {
			setup_shaders(s, 0);
			begin_draw();

			for (vector<unsigned>::const_iterator i = nearby_ixs.begin(); i != nearby_ixs.end(); ++i) {
				draw_range(mesh_to_grass_map[*i], mesh_to_grass_map[(*i)+1]);
			}
			end_draw();
			s.end_shader();
		}
		//PRINT_TIME("Draw Grass");
	}
};

grass_manager_dynamic_t grass_manager;


// *** flowers ***

float const FLOWER_DIST_THRESH = 0.5;

bool flower_manager_t::skip_generate() const {
	return (generated || no_grass() || flower_density == 0.0); // no grass, no flowers
}

void flower_manager_t::check_vbo() {

	if (vbo != 0 || empty()) return; // nothing to update
	//RESET_TIME;

	if (0 && world_mode == WMODE_INF_TERRAIN) {
		vector<sized_vert_t<vert_norm_color> > verts(flowers.size());

		for (auto i = flowers.begin(); i != flowers.end(); ++i) {
			verts[i-flowers.begin()] = sized_vert_t<vert_norm_color>(vert_norm_color(i->pos, i->normal, i->color), i->radius);
		}
		create_vbo_and_upload(vbo, verts);
	}
	else {
		vector<vert_norm_comp_color> verts;
		create_verts_range(verts, 0, size());
		create_vbo_and_upload(vbo, verts);
	}
	//PRINT_TIME("Flowers VBO");
}

void flower_manager_t::create_verts_range(vector<vert_norm_comp_color> &verts, unsigned start, unsigned end) const {

	assert(start < end && end <= size());
	verts.resize(4*(end - start));
	unsigned ix(0);

	for (auto i = flowers.begin()+start; i != flowers.begin()+end; ++i) {
		vector3d v1(zero_vector), v2;
		v1[get_min_dim(i->normal)] = 1.0;
		v2 = i->radius*cross_product(i->normal, v1).get_norm();
		v1 = i->radius*cross_product(i->normal, v2).get_norm();
		color_wrapper cw;
		cw.set_c4(i->color);
		norm_comp const n(i->normal);
		point const pts[4] = {(i->pos - v1 - v2), (i->pos + v1 - v2), (i->pos + v1 + v2), (i->pos - v1 + v2)};
		UNROLL_4X(verts[ix++] = vert_norm_comp_color(vert_norm_comp(pts[i_], n), cw);)
	}
	assert(ix == verts.size());
}

void flower_manager_t::upload_range(unsigned start, unsigned end) const {

	if (!vbo) return; // can we ever get here? assert vbo?
	vector<vert_norm_comp_color> verts;
	create_verts_range(verts, start, end);
	pre_render();
	unsigned const flower_sz(4*sizeof(vert_norm_comp_color));
	upload_vbo_sub_data(&verts.front(), start*flower_sz, (end - start)*flower_sz);
	post_render();
}

void flower_manager_t::setup_flower_shader_post(shader_t &shader) {

	shader.add_uniform_float("height", grass_length);
	shader.add_uniform_float("min_alpha", 0.9);
	shader.clear_specular();
}

void flower_manager_t::draw_triangles(shader_t &shader) const {

	pre_render();
	select_texture((draw_model == 1) ? WHITE_TEX : DAISY_TEX);

	if (0 && world_mode == WMODE_INF_TERRAIN) {
		sized_vert_t<vert_norm_color>::set_vbo_arrays();
		glDrawArrays(GL_POINTS, 0, flowers.size());
	}
	else {
		vert_norm_comp_color::set_vbo_arrays();
		draw_quads_as_tris(4*flowers.size());
	}
	post_render();
}

void flower_manager_t::add_flowers(mesh_xy_grid_cache_t const density_gen[2],
	float grass_den, float hthresh, float dx, float dy, int xpos, int ypos, bool gen_zval) 
{
	if (grass_den < 0.5) return; // no grass
	unsigned const NUM_COLORS(3), start_eval_sine(50);
	colorRGBA const colors[NUM_COLORS] = {WHITE, YELLOW, LT_BLUE};
	float const dval(density_gen[0].eval_index(xpos, ypos, 0, start_eval_sine));
	float const cval(density_gen[1].eval_index(xpos, ypos, 0, start_eval_sine));
	unsigned const num_per_bin(unsigned(flower_density*grass_den + 0.5));

	for (unsigned n = 0; n < num_per_bin; ++n) {
		if ((dval + 0.2*zmax_est*rgen.signed_rand_float()) > hthresh) continue; // density function test
		float const height(grass_length*rgen.rand_uniform(0.85, 1.0));
		point pos((dx + DX_VAL*(xpos + rgen.rand_float())), (dy + DY_VAL*(ypos + rgen.rand_float())), height);
		if (gen_zval) {pos.z += interpolate_mesh_zval(pos.x, pos.y, 0.0, 0, 1);}
		vector3d const normal((plus_z + rgen.signed_rand_vector(0.2)).get_norm()); // facing mostly up (or face toward sun?)
		float const radius(grass_width*rgen.rand_uniform(1.5, 2.5));
		colorRGBA color;

		if (flower_color.alpha > 0.0) {
			color = flower_color;
		}
		else {
			float const color_val(cval + 0.25*rgen.signed_rand_float());
			color = colors[int(0.5*NUM_COLORS*color_val)%NUM_COLORS];
		}
		flowers.push_back(flower_t(pos, normal, radius, height, color));
	}
}

void flower_manager_t::gen_density_cache(mesh_xy_grid_cache_t density_gen[2], int x1, int y1) {
	for (unsigned i = 0; i < 2; ++i) {
		float const fds(500.0*(1.0 + 0.3*i)), xscale(fds*DX_VAL*DX_VAL), yscale(fds*DY_VAL*DY_VAL);
		density_gen[i].build_arrays(xscale*(x1 + xoff2), yscale*(y1 + yoff2), xscale, yscale, MESH_X_SIZE, MESH_Y_SIZE, 0, 1); // force_sine_mode=1
	}
}

void flower_manager_t::scale_flowers(float lscale, float wscale) {

	for (auto i = flowers.begin(); i != flowers.end(); ++i) {
		float const old_height(i->height);
		i->height *= lscale;
		i->radius *= wscale;
		i->pos.z  += i->height - old_height;
	}
	clear_vbo();
}


void flower_tile_manager_t::gen_flowers(vector<unsigned char> const &weight_data, unsigned wd_stride, int x1, int y1) {

	if (skip_generate()) return;
	//RESET_TIME;
	assert(empty()); // or call clear()?
	rgen.set_state(x1+xoff2+123, y1+yoff2+456); // deterministic for each tile
	mesh_xy_grid_cache_t density_gen[2]; // density thresh, color selection
	gen_density_cache(density_gen, x1, y1);
	assert(wd_stride >= (unsigned)MESH_X_SIZE && wd_stride >= (unsigned)MESH_Y_SIZE);
	float const hthresh(get_median_height(FLOWER_DIST_THRESH));

	for (unsigned y = 0; y < (unsigned)MESH_Y_SIZE; ++y) {
		for (unsigned x = 0; x < (unsigned)MESH_X_SIZE; ++x) {
			unsigned const wd_ix(4*(y*wd_stride + x) + 2);
			assert(wd_ix < weight_data.size());
			add_flowers(density_gen, weight_data[wd_ix]/255.0, hthresh, 0.0, 0.0, x, y, 0);
		}
	}
	generated = 1;
	//PRINT_TIME("Gen Flowers TT");
}


class flower_manager_dynamic_t : public flower_manager_t {
public:
	void gen_flowers() {
		if (skip_generate()) return;
		assert(empty()); // or call clear()?
		mesh_xy_grid_cache_t density_gen[2]; // density thresh, color selection
		gen_density_cache(density_gen, 0, 0);
		float const hthresh(get_median_height(FLOWER_DIST_THRESH));

		for (unsigned y = 0; y < (unsigned)MESH_Y_SIZE; ++y) {
			for (unsigned x = 0; x < (unsigned)MESH_X_SIZE; ++x) {
				float density(1.0);
				if (flower_weight != nullptr) {density *= flower_weight[y][x]/255.0;}
				if (density == 0.0) continue;
				density *= get_grass_density(point(get_xval(x), get_yval(y), 0.0));
				add_flowers(density_gen, density, hthresh, -X_SCENE_SIZE, -Y_SCENE_SIZE, x, y, 1);
			}
		}
		generated = 1;
	}

	void flower_height_change(int x, int y, int rad) {
		if (empty()) return;
		unsigned mod_start(flowers.size()), mod_end(0);
		
		for (unsigned i = 0; i < flowers.size(); ++i) {
			point &pos(flowers[i].pos);
			int const xpos(get_xpos(pos.x)), ypos(get_ypos(pos.y));
			if ((y - ypos)*(y - ypos) + (x - xpos)*(x - xpos) > rad*rad) continue;
			pos.z     = interpolate_mesh_zval(pos.x, pos.y, 0.0, 0, 1) + flowers[i].height;
			mod_start = min(mod_start, i);
			mod_end   = max(mod_end,   i+1); // one past the end
		}
		if (mod_start < mod_end) {upload_range(mod_start, mod_end);}
	}

	void modify_flowers(point const &pos, float radius, bool crush, bool burn, bool remove) {
		if (!(crush || burn || remove) || empty()) return; // nothing to modify
		if (get_grass_density(pos) == 0.0) return; // optimization - if there's no grass, there are no flowers
		float const radius_sq(radius*radius), y_end(pos.y + radius + DY_VAL);
		unsigned mod_start(flowers.size()), mod_end(0);

		for (unsigned i = 0; i < flowers.size(); ++i) {
			flower_t &flower(flowers[i]);
			if (flower.pos.y > y_end) break; // since flowers are created in blocks increasing in y, we can early terminate when y is large enough
			float const dsq(p2p_dist_sq(flower.pos, pos));
			if (dsq > radius_sq) continue;
			bool modified(0);

			if (remove) {
				if (flower.radius == 0.0) continue; // already removed
				flower.radius = 0.0;
				modified      = 1;
			}
			if (crush) { // less wind effect as well?
				if (flower.height > 0.05*grass_length) { // not already crushed
					float const reld(sqrt(dsq)/radius), delta(flower.height*min(1.0, 2.0*(1.0 - reld)*(1.0 - reld)));
					flower.pos.z  -= delta;
					flower.height -= delta;

					if (flower.height < 0.1*grass_length) { // when flattened, flowers conform to the mesh surface normal
						int const xpos(get_xpos(flower.pos.x)), ypos(get_ypos(flower.pos.y));
						if (!point_outside_mesh(xpos, ypos)) {flower.normal = surface_normals[ypos][xpos];}
					}
					modified = 1;
				}
			}
			if (burn) {
				if (flower.color != BLACK) { // not already black (max burned)
					float const reld(sqrt(dsq)/radius);
					flower.color *= 1.0 - (1.0 - reld)*(1.0 - reld);
					if (flower.color.get_luminance() < 0.05) {flower.color = BLACK;}
					modified = 1;
				}
			}
			if (modified) {
				mod_start = min(mod_start, i);
				mod_end   = max(mod_end,   i+1); // one past the end
			}
		}
		if (mod_start < mod_end) {upload_range(mod_start, mod_end);}
	}

	void draw() const {
		if (empty()) return; // nothing to draw
		shader_t s;
		setup_shaders_pre(s);
		s.set_vert_shader("texture_gen.part+wind.part*+flowers_pp_dl");
		s.set_frag_shader("linear_fog.part+ads_lighting.part*+shadow_map.part*+dynamic_lighting.part*+flowers");
		setup_shaders_post(s);
		setup_flower_shader_post(s);
		draw_triangles(s);
		s.end_shader();
	}
};

flower_manager_dynamic_t flower_manager;


// *** global functions ***

void setup_wind_for_shader(shader_t &s, unsigned tu_id) {

	static float wind_time(0.0);
	if (animate2 && ((display_mode & 0x0100) != 0)) {wind_time = tfticks;}
	s.add_uniform_float("time", 0.5*wind_time/TICKS_PER_SECOND);
	s.add_uniform_float("wind_x", wind.x);
	s.add_uniform_float("wind_y", wind.y);
	s.add_uniform_int("wind_noise_tex", tu_id);
	select_multitex(WIND_TEX, tu_id);
}


bool no_grass() {
	return (grass_density == 0 || !grass_enabled || snow_enabled() || vegetation == 0.0 || read_landscape);
}


void gen_grass() { // and flowers

	grass_manager.clear();
	flower_manager.clear();
	if (no_grass() || world_mode != WMODE_GROUND) return;
	grass_manager.gen_grass();
	flower_manager.gen_flowers();
	cout << "grass: " << grass_manager.size() << " out of " << XY_MULT_SIZE*grass_density;
	if (!flower_manager.empty()) {cout << ", flowers: " << flower_manager.size();}
	cout << endl;
}


void update_tiled_grass_length_width(float lscale, float wscale);

void update_grass_length_width(float new_grass_length, float new_grass_width) {

	assert(new_grass_length > 0.0 && new_grass_width > 0.0);
	float const lscale(new_grass_length/grass_length), wscale(new_grass_width/grass_width);
	grass_length = new_grass_length;
	grass_width  = new_grass_width;

	if (world_mode == WMODE_GROUND) {
		grass_manager.scale_grass(lscale, wscale);
		flower_manager.scale_flowers(lscale, wscale);
	}
	else if (world_mode == WMODE_INF_TERRAIN) {
		update_tiled_grass_length_width(lscale, wscale);
	}
}


void update_grass_vbos() {
	grass_manager.clear_vbo();
	flower_manager.clear_vbo();
}

void draw_grass() { // and flowers
	if (!no_grass() && (display_mode & 0x02)) {
		grass_manager.draw();
		flower_manager.check_vbo();
		flower_manager.draw();
	}
}

void modify_grass_at(point const &pos, float radius, bool crush, bool burn, bool cut, bool check_uw, bool add_color, bool remove, colorRGBA const &color) {
	if (no_grass()) return;
	if (burn && is_underwater(pos)) {burn = 0;}
	grass_manager.modify_grass(pos, radius, crush, burn, cut, check_uw, add_color, remove, color);
	flower_manager.modify_flowers(pos, radius, crush, burn, (cut || remove));
}

void grass_mesh_height_change(int xpos, int ypos) {
	if (!no_grass()) {grass_manager.mesh_height_change(xpos, ypos);}
}

void flower_mesh_height_change(int xpos, int ypos, int rad) {
	flower_manager.flower_height_change(xpos, ypos, rad);
}

bool place_obj_on_grass(point &pos, float radius) {
	return (!no_grass() && grass_manager.place_obj_on_grass(pos, radius));
}

float get_grass_density(point const &pos) {
	return (no_grass() ? 0.0 : grass_manager.get_grass_density(pos));
}



