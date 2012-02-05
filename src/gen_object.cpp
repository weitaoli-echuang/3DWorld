// 3D World - OpenGL CS184 Computer Graphics Project
// by Frank Gennari
// 3/10/02

#include "3DWorld.h"
#include "mesh.h"
#include "physics_objects.h"


float    const SMOKE_ZVEL      = 3.0;
unsigned const NUM_STARS       = 4000;
unsigned const MAX_BUBBLES     = 2000;
unsigned const MAX_PART_CLOUDS = 200;
unsigned const MAX_FIRES       = 50;
unsigned const MAX_DECALS      = 2500;


// Global Variables
unsigned num_stars(0);
vector<star> stars(NUM_STARS);
obj_vector_t<bubble> bubbles(MAX_BUBBLES);
obj_vector_t<particle_cloud> part_clouds(MAX_PART_CLOUDS);
obj_vector_t<fire> fires(MAX_FIRES);
obj_vector_t<decal_obj> decals(MAX_DECALS);
float gauss_rand_arr[N_RAND_DIST+2];
rand_gen_t global_rand_gen;


extern int star_init, begin_motion, animate2, show_fog;
extern float zmax_est, zmax, ztop;
extern int coll_id[];
extern point leaf_points[];
extern obj_group obj_groups[];
extern obj_type object_types[];



void gen_stars(float alpha, int half_sphere) {

	if (show_fog) return;
	unsigned const cur_num_stars(stars.size());

	if (!star_init) {
		num_stars = cur_num_stars - rand()%max(1U, cur_num_stars/4);
		star_init = 1;

		for (unsigned i = 0; i < num_stars; ++i) {
			gen_star(stars[i], half_sphere);
		}
	}
	else {
		num_stars += rand()%max(1U, cur_num_stars/200);
		int const old_num(num_stars);
		num_stars  = min(num_stars, cur_num_stars);

		for (unsigned i = old_num; i < num_stars; ++i) {
			gen_star(stars[i], half_sphere);
		}
		if ((rand()&15) == 0) {
			float const cmin[3] = {0.7, 0.9, 0.6};

			for (unsigned i = 0; i < num_stars; ++i) {
				int const rnum(rand());

				if (rnum%10 == 0) { // change intensity
					stars[i].intensity *= 1.0 + 0.1*signed_rand_float();
					stars[i].intensity  = min(stars[i].intensity, 1.0f);
				}
				if (rnum%20 == 0) { // change color
					for (unsigned j = 0; j < 3; ++j) {
						stars[i].color[j] *= 1.0 + 0.1*signed_rand_float();
						stars[i].color[j]  = min(max(stars[i].color[j], cmin[j]), 1.0f);
					}
				}
				if (rnum%2000 == 0) gen_star(stars[i], half_sphere); // create a new star and destroy the old
			}
		}
	}
	draw_stars(alpha);
}


void gen_star(star &star1, int half_sphere) {

	float const radius(0.7*(FAR_CLIP+X_SCENE_SIZE)), theta(rand_uniform(0.0, TWO_PI));
	float phi(gen_rand_phi<rand_uniform>());
	if (!half_sphere && (rand()&1) == 0) phi = PI - phi;
	star1.pos       = rtp_to_xyz(radius, theta, phi);
	star1.intensity = rand_uniform(0.1, 1.0);
	star1.color.assign(rand_uniform(0.7, 1.0), rand_uniform(0.9, 1.0), rand_uniform(0.6, 1.0));
}


void rand_xy_point(float zval, point &pt, unsigned flags) {

	for (unsigned i = 0; i < 2; ++i) {
		pt[i] = ((flags & FALL_EVERYWHERE) ? 1.5 : 1.0)*SCENE_SIZE[i]*signed_rand_float();
	}
	pt.z = zval;
}


void gen_object_pos(point &position, unsigned flags) {

	rand_xy_point((CLOUD_CEILING + ztop)*(1.0 + rand_uniform(-0.1, 0.1)), position, flags);
}


void basic_physics_obj::init(point const &p) {

	status = 1;
	time   = 0;
	pos    = p;
}


void basic_physics_obj::init_gen_rand(point const &p, float rxy, float rz) {

	init(p);
	pos += point(rand_uniform(-rxy, rxy), rand_uniform(-rxy, rxy), rand_uniform(-rz, rz));
}


void dwobject::print_and_terminate() const { // only called when there is an error

	cout << "pos = "; pos.print();
	cout << ", vel = "; velocity.print();
	cout << ", type = " << int(type) << ", status = " << int(status) << endl;
	assert(0);
}


void bubble::gen(point const &p, float r, colorRGBA const &c) {

	init_gen_rand(p, 0.005, 0.01);
	radius   = ((r == 0.0) ? rand_uniform(0.001, 0.004) : r);
	velocity = rand_uniform(0.15, 0.2);
	pos.z   -= 0.01;
	color    = c;
}


void particle_cloud::gen(point const &p, colorRGBA const &bc, vector3d const &iv, float r,
						 float den, float dark, float dam, int src, int dt, bool as, bool use_parts, bool nl)
{
	init_gen_rand(p, 0.005, 0.025);
	acc_smoke  = as;
	source     = src;
	damage_type= dt;
	base_color = bc;
	init_vel   = iv;
	radius     = r;
	init_radius= r;
	darkness   = dark;
	density    = den;
	damage     = dam;
	no_lighting= nl;
	red_only   = 0;

	if (use_parts) {
		parts.resize((rand()&3) + 2); // 2-5 parts

		for (unsigned i = 0; i < parts.size(); ++i) {
			parts[i].pos    = signed_rand_vector(1.0);
			parts[i].pos.z  = fabs(parts[i].pos.z); // always +z
			parts[i].radius = rand_uniform(0.5, 1.0);
			parts[i].status = 1; // always 1
		}
	}
}


void fire::gen(point const &p, float size, float intensity, int src, bool is_static_, float light_bw) {

	if (is_static_) {
		init(p);
		radius = 0.04*size;
		heat   = intensity;
		cval   = 0.5;
		inten  = 0.5;
	}
	else {
		init_gen_rand(p, 0.07, 0.01);
		radius = size*rand_uniform(0.02, 0.05);
		heat   = intensity*rand_uniform(0.5, 0.8);
		cval   = rand_uniform(0.3, 0.7);
		inten  = rand_uniform(0.3, 0.7);
	}
	is_static    = is_static_;
	light_bwidth = light_bw;
	velocity     = zero_vector;
	source       = src;
	float const zval(interpolate_mesh_zval(pos.x, pos.y, radius, 0, 0));
	if (fabs(pos.z - zval) > 2.0*radius) status = 2; // above the ground
}


void decal_obj::gen(point const &p, float r, vector3d const &o, int cid_, float init_alpha, colorRGBA const &color_, bool is_glass_) {

	assert(r > 0.0 && init_alpha > 0.0);
	cid    = cid_; // must be set first
	ipos   = p;
	ipos  -= get_platform_delta(); // make relative to the at-rest platform pos
	init_gen_rand(ipos, 0.0, 0.0);
	radius = r;
	alpha  = init_alpha;
	color  = color_;
	orient = o; // normal of attached surface at collision/anchor point
	orient.normalize();
	pos   += orient*rand_uniform(0.001, 0.002); // move away from the object it's attached to
	is_glass = is_glass_;
}


void gen_bubble(point const &pos, float r, colorRGBA const &c) {

	bubbles[bubbles.choose_element()].gen(pos, r, c);
}


void gen_line_of_bubbles(point const &p1, point const &p2, float r, colorRGBA const &c) {

	//RESET_TIME;
	point cur(p1);
	vector3d const dir(p2 - p1);
	float const step_sz(0.02), dist(dir.mag());
	vector3d const step(dir*(step_sz/dist));
	unsigned const nsteps(min((unsigned)bubbles.size(), unsigned(dist/step_sz)));
	vector<unsigned> ixs;
	bubbles.choose_elements(ixs, nsteps);
	assert(ixs.size() == nsteps);

	for (unsigned i = 0, cur_ix = 0; i < nsteps; ++i) {
		cur += step;
		if (!is_over_mesh(cur)) break;
		int xpos(get_xpos(cur.x)), ypos(get_ypos(cur.y));
		if (point_outside_mesh(xpos, ypos) || cur.z < mesh_height[ypos][xpos]) break;
		if (is_underwater(cur)) bubbles[ixs[cur_ix++]].gen(cur, r, c);
	}
	//PRINT_TIME("Bubble Gen");
}


bool gen_arb_smoke(point const &pos, colorRGBA const &bc, vector3d const &iv,
				   float r, float den, float dark, float dam, int src, int dt, bool as)
{
	if (!animate2 || is_underwater(pos) || is_under_mesh(pos)) return 0;
	unsigned const chosen();
	part_clouds[part_clouds.choose_element()].gen(pos, bc, iv, r, den, dark, dam, src, dt, as);
	return 1;
}


void gen_smoke(point const &pos) {

	gen_arb_smoke(pos, WHITE, vector3d(0.0, 0.0, SMOKE_ZVEL),
		rand_uniform(0.01, 0.025), rand_uniform(0.7, 0.9), rand_uniform(0.75, 0.95), 0.0, -2, SMOKE, 1);
}


bool gen_fire(point const &pos, float size, int source, bool allow_close, bool is_static, float light_bwidth, float intensity) {

	assert(size > 0.0);
	if (!is_over_mesh(pos) || is_underwater(pos)) return 0; // off the mesh or under water/ice

	if (!allow_close) { // check if there are any existing fires near this location and if so, skip this one
		for (unsigned i = 0; i < fires.size(); ++i) {
			if (fires[i].status != 0 && dist_less_than(pos, fires[i].pos, 2.0*fires[i].radius)) {
				fires[i].radius = max(fires[i].radius, size*rand_uniform(0.02, 0.05)); // make it larger
				return 0;
			}
		}
	}
	fires[fires.choose_element()].gen(pos, size, intensity, source, is_static, light_bwidth);
	return 1;
}


void gen_decal(point const &pos, float radius, vector3d const &orient, int cid, float init_alpha, colorRGBA const &color, bool is_glass_) {

	decal_obj decal;
	decal.gen(pos, radius, orient, cid, init_alpha, color, is_glass_);
	if (decal.is_on_cobj(cid)) decals[decals.choose_element()] = decal;
}


void gen_particles(point const &pos, unsigned num, float lt_scale, bool fade) { // lt_scale: 0.0 = full lt, 1.0 = no lt

	obj_group &objg(obj_groups[coll_id[PARTICLE]]);

	for (unsigned o = 0; o < num; ++o) {
		int const i(objg.choose_object());
		objg.create_object_at(i, pos);
		objg.get_obj(i).velocity = gen_rand_vector((fade ? 1.5 : 1.0)*rand_uniform(3.0, 5.0), (fade ? 1.5 : 1.8), 0.75*PI);
		if (lt_scale != 1.0) objg.get_obj(i).time = lt_scale*object_types[PARTICLE].lifetime;
		if (fade) objg.get_obj(i).flags |= TYPE_FLAG;
	}
}


int gen_fragment(point const &pos, vector3d const &velocity, float size_mult, float time_mult,
	colorRGBA const &color, int tid, float tscale, int source, bool tri_fragment)
{
	obj_group &objg(obj_groups[coll_id[FRAGMENT]]);
	int const ix(objg.choose_object());
	objg.create_object_at(ix, pos);
	dwobject &obj(objg.get_obj(ix));
	UNROLL_3X(obj.init_dir[i_] = color[i_];)
	obj.coll_id     = -(tid + 2); // < 0;
	assert(obj.coll_id < 0);
	obj.velocity    = (velocity + gen_rand_vector(rand_uniform(0.3, 0.7), 1.0, PI))*rand_uniform(10.0, 15.0);
	obj.angle       = 360.0*rand_float();
	obj.orientation = signed_rand_vector_norm();
	obj.vdeform.x   = size_mult*(0.5 + rand_float()); // size
	obj.vdeform.y   = color.alpha;
	obj.vdeform.z   = fabs(tscale)*(tri_fragment ? 1.0 : -1.0);
	obj.time        = int(time_mult*object_types[FRAGMENT].lifetime);
	obj.source      = source;
	return ix;
}


void gen_leaf_at(point const *const points, vector3d const &normal, int type, colorRGB const &color) {

	if (!begin_motion) return;
	int const cid(coll_id[LEAF]);
	if (cid < 0) return;
	obj_group &objg(obj_groups[cid]);
	if (objg.max_objs == 0) return;
	int const max_t_i(objg.choose_object());
	point const pos(get_center(points, 4));
	vector3d const delta(points[1] - points[0]), ldelta(leaf_points[1] - leaf_points[0]);
	float const leaf_size(delta.mag()/ldelta.mag());
	assert(leaf_size > 0.0);
	objg.create_object_at(max_t_i, pos);
	dwobject &obj(objg.get_obj(max_t_i));
	obj.init_dir.z = leaf_size; // sets leaf size
	obj.init_dir.x = 2.0*PI*rand_float(); // angle rotated about z-axis
	//obj.init_dir.x = get_angle(delta.get_norm(), ldelta.get_norm());
	obj.source     = (short)type;

	for (unsigned i = 0; i < 3; ++i) {
		obj.vdeform[i] = color[i]; // stuff color into vdeform
	}
	obj.set_orient_for_coll(&normal);
}


void gen_gauss_rand_arr() {

	float const mconst(2.0E-4*RG_NORM), aconst(((float)N_RAND_GAUSS)*RG_NORM);

	for (int i = 0; i < N_RAND_DIST+2; ++i) {
		float val(0.0);

		for (int j = 0; j < N_RAND_GAUSS; ++j) {
			val += rand()%10000;
		}
		gauss_rand_arr[i] = mconst*val - aconst;
	}
}







