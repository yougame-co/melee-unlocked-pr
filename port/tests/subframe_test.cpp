#include "authored_pose.h"
#include "Geometry.h"
#include "gx_shader.h"
// Sub-frame solver: fractional rigid deltas, cut detection, draw pairing.
#include "subframe.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
static void check(bool ok, const char* what) { if (!ok) { std::printf("FAIL: %s\n", what); std::fflush(stdout); std::exit(1); } }
static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
static void rot_z(float deg, float tx, float out[12]) {
  float r = deg * 3.14159265f / 180.0f, c = std::cos(r), s = std::sin(r);
  float m[12] = {c, -s, 0, tx, s, c, 0, 0, 0, 0, 1, 0};
  std::memcpy(out, m, sizeof m);
}
int main() {
  // Predict samples an animation that advanced at a new speed (a run, a dash, air drift: the game sets the
  // rate from the velocity each frame); a restart, a pause or a swap is still no continuation (player video 11-21).
  {
    gx::AuthoredJoint p, c;
    p.frame = 10; p.rate = 1.0f; p.end = 60; c = p;
    c.frame = 11.5f; c.rate = 1.5f;   // advanced by the new rate
    if (!gx::authored_retimed_clock(p, c)) { std::printf("retimed clock (new rate) not accepted\n"); return 1; }
    c.frame = 11.0f;                  // advanced by the old rate, the new one set after
    if (!gx::authored_retimed_clock(p, c)) { std::printf("retimed clock (old rate) not accepted\n"); return 1; }
    c.frame = 0.0f;                   // restarted
    if (gx::authored_retimed_clock(p, c)) { std::printf("restart taken as a retime\n"); return 1; }
    c.frame = 10.0f;                  // paused
    if (gx::authored_retimed_clock(p, c)) { std::printf("pause taken as a retime\n"); return 1; }
    c.frame = 11.5f; c.end = 40;      // another animation (its length differs)
    if (gx::authored_retimed_clock(p, c)) { std::printf("swap taken as a retime\n"); return 1; }
    c = p; c.frame = 11.0f;           // same rate: the ordinary clock, not a retime
    if (gx::authored_retimed_clock(p, c)) { std::printf("same rate taken as a retime\n"); return 1; }
    std::printf("retimed clock: speed changes sampled, restarts, pauses and swaps held\n");
  }
  // A render-to-texture camera keeps its authored projection: its consumers use
  // an authored projective texture matrix, even while the main view is widened.
  {
    gx::DrawCall camera{};
    float p[] = {3.06576f, 0, 3.73205f, 0, -0.0000061f, -0.100001f};
    std::memcpy(&camera.xf_regs[0x20], p, sizeof p);
    float viewport[] = {320, -240, 16777215, 662, 582, 16777215};
    std::memcpy(&camera.xf_regs[0x1A], viewport, sizeof viewport);
    float original[16], widened[16], reflection[16];
    gx::set_true_widescreen(false);
    gx::build_projection(camera, original);
    gx::set_true_widescreen(true);
    gx::build_projection(camera, widened);
    check(near(widened[0], original[0] * 219.0f / 320.0f), "main camera widens");
    viewport[0] = 40; viewport[1] = -30; viewport[3] = 382; viewport[4] = 372;
    std::memcpy(&camera.xf_regs[0x1A], viewport, sizeof viewport);
    gx::build_projection(camera, reflection);
    check(std::memcmp(original, reflection, sizeof original) == 0, "FoD reflection keeps authored projective mapping");
    gx::set_true_widescreen(false);
  }
  gx::DrawCall shader_draw{};
  auto uid = gx::make_vs_uid(shader_draw);
  shader_draw.xf_regs[0x0A] = 0xff00ff00;
  shader_draw.xf_regs[0x0C] = 0x12345678;
  auto recolored = gx::make_vs_uid(shader_draw);
  check(uid == recolored && uid.hash() == recolored.hash(), "color constants reuse the vertex shader");
  check(gx::generate_vertex_shader(uid) == gx::generate_vertex_shader(recolored), "recolored shader source is unchanged");
  shader_draw.xf_regs[0x0E] = 1;
  check(!(uid == gx::make_vs_uid(shader_draw)), "lighting controls remain in shader identity");
  const float ident_n[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  float prev[12], cur[12], out[12], nrm[9];
  gx::SubFrameStats st;
  // 90 degree rotation + 8 unit translation per frame: half phase gives 45 degrees and 4 units.
  rot_z(0, 0, prev); rot_z(90, 8, cur);
  gx::SubFrameSolver::fractional(prev, cur, 0.5, true, 40, 3.0f, out, nrm, ident_n, ident_n, &st);
  check(near(out[0], std::cos(3.14159265f / 4)) && near(out[4], std::sin(3.14159265f / 4)), "interpolated rotation is 45 degrees");
  // The screw axis passes through (4, 4): halfway along the arc the origin maps to (4, 4 - 4*sqrt2).
  const float s2 = std::sqrt(2.0f);
  check(near(out[3], 4.0f) && near(out[7], 4.0f - 4.0f * s2), "interpolated translation follows the screw path");
  check(near(nrm[0], out[0]) && near(nrm[3], out[4]), "normal matrix follows the rotation");
  check(st.rigid == 1, "rigid path used");
  // Extrapolation continues the same screw: t = 0.5 beyond cur gives 135 degrees at (4 + 4*sqrt2, 4).
  gx::SubFrameSolver::fractional(prev, cur, 0.5, false, 40, 3.0f, out, nrm, ident_n, ident_n, &st);
  check(near(out[0], std::cos(3 * 3.14159265f / 4)) && near(out[3], 4.0f + 4.0f * s2) && near(out[7], 4.0f), "extrapolated pose continues");
  // Pure translation halves exactly.
  float slide[12] = {1, 0, 0, 6, 0, 1, 0, -2, 0, 0, 1, 0};
  gx::SubFrameSolver::fractional(prev, slide, 0.5, true, 40, 3.0f, out, nullptr, nullptr, nullptr, &st);
  check(near(out[3], 3.0f) && near(out[7], -1.0f) && near(out[0], 1.0f), "pure translation halves");
  // Exact endpoints.
  gx::SubFrameSolver::fractional(prev, cur, 0.0, false, 40, 3.0f, out, nullptr, nullptr, nullptr, &st);
  check(std::memcmp(out, cur, sizeof out) == 0, "t = 0 extrapolation is the current pose");
  gx::SubFrameSolver::fractional(prev, cur, 1.0, true, 40, 3.0f, out, nullptr, nullptr, nullptr, &st);
  for (int i = 0; i < 12; ++i) check(near(out[i], cur[i], 1e-3f), "t = 1 interpolation reaches the current pose");
  // A camera cut (large translation) keeps the exact simulation pose.
  rot_z(0, 500, cur); st = {};
  gx::SubFrameSolver::fractional(prev, cur, 0.5, false, 40, 3.0f, out, nullptr, nullptr, nullptr, &st);
  check(std::memcmp(out, cur, sizeof out) == 0 && st.cuts == 1, "large delta treated as a cut");
  st = {};
  gx::SubFrameSolver::fractional(prev, cur, 0.5, true, 40, 3.0f, out, nullptr, nullptr, nullptr, &st);
  check(std::memcmp(out, cur, sizeof out) == 0 && st.cuts == 1, "interpolation cut retains current pose");
  // Non-rigid delta (anisotropic scale x3) retains the current pose.
  float scaled[12] = {3, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}; st = {};
  gx::SubFrameSolver::fractional(prev, scaled, 0.5, true, 40, 3.0f, out, nullptr, nullptr, nullptr, &st);
  check(st.cuts == 1 && near(out[0], 3.0f), "non-rigid delta retains current pose");
  // Draw pairing by identity through the solver.
  gx::Frame a, b; a.sequence = 1; b.sequence = 2; a.vertices.resize(3, gx::Vertex{}); b.vertices.resize(3, gx::Vertex{});
  gx::DrawCall d{}; d.identity = 7; d.object_generation = 1; d.vertex_count = 3; d.primitive = 0x90; d.components = 0; d.matrix_index_a = 0;
  d.xf_regs[0x3F] = 0;
  rot_z(0, 0, d.posMatrices); std::memcpy(d.normalMatrices, ident_n, sizeof ident_n);
  a.draws.push_back(d);
  rot_z(90, 8, d.posMatrices); b.draws.push_back(d);
  gx::DrawCall unpaired = d; unpaired.identity = 9; b.draws.push_back(unpaired);
  gx::SubFrameSolver solver; std::vector<gx::DrawMatrices> mats;
  solver.max_rotation = 3.0f;
  solver.set_frames(&a, &b);
  check(solver.stats().paired == 1, "one draw paired by identity");
  solver.build(0.5, true, mats);
  std::printf("paired row0: %g %g %g %g | %g %g %g %g (paired=%u rigid=%u blended=%u cuts=%u)\n", mats[0].pos[0], mats[0].pos[1], mats[0].pos[2], mats[0].pos[3],
              mats[0].pos[4], mats[0].pos[5], mats[0].pos[6], mats[0].pos[7], solver.stats().paired, solver.stats().rigid, solver.stats().blended, solver.stats().cuts);
  check(mats.size() == 2 && near(mats[0].pos[3], 4.0f), "paired draw gets the fractional pose");
  check(std::memcmp(mats[1].pos, unpaired.posMatrices, sizeof mats[1].pos) == 0, "unpaired draw keeps its pose");
  solver.build(0.5, false, mats, true);
  check(std::memcmp(mats[0].pos, b.draws[0].posMatrices, sizeof mats[0].pos) == 0, "unsupported authored draw holds latest pose without delay");
  b.draws[0].bp.reg[gx::BP_PE_TOKEN_ID] = 123;
  b.draws[0].bp.reg[gx::BP_EFB_ADDR] = 0x12345;
  b.draws[0].bp.reg[gx::BP_TX_SETIMAGE1] = 0x100;
  solver.set_frames(&a, &b);
  check(solver.stats().paired == 1, "FIFO token and XFB destination do not change draw identity");
  b.draws[0].bp.reg[gx::BP_BLENDMODE] = 1;
  solver.set_frames(&a, &b);
  check(solver.stats().paired == 0, "material blend changes invalidate pairing");
  b.draws[0].bp.reg[gx::BP_BLENDMODE] = 0;
  b.sequence = 4; solver.set_frames(&a, &b);
  check(solver.stats().paired == 0, "frame gaps invalidate pairing");
  b.sequence = 2; b.vertices[0].pos[0] = 10; solver.set_frames(&a, &b);
  check(solver.stats().paired == 1, "matching rewritten primitive can pair");
  solver.build(0.5, true, mats, true);
  check(mats[0].vertices && near(mats[0].vertices[0].pos[0], 5), "rewritten geometry samples between known positions");
  a.draws[0].object_generation = b.draws[0].object_generation = 0;
  solver.set_frames(&a, &b);
  solver.build(.5, false, mats, true);
  check(solver.stats().paired == 0 && !mats[0].vertices &&
        std::memcmp(mats[0].pos, b.draws[0].posMatrices, sizeof mats[0].pos) == 0,
        "unidentified rewritten glyph/primitive cannot pair by submission ordinal alone");
  a.draws[0].object_generation = b.draws[0].object_generation = 1;
  solver.set_frames(&a, &b);
  solver.build(0.5, true, mats, false);
  check(!mats[0].vertices, "switching away from authored mode clears the vertex override");
  // A stream that cannot be blended is drawn as the current stream, but its matrices still move
  // with everything else: holding them at the current frame put such a draw on its own timeline,
  // and a menu element rebuilt on some ticks and not others then flickered between the two.
  // The per-vertex matrix index only means anything when the draw is set up to read one, so the
  // draw says so here. Without that flag the index in the vertex is ignored by the hardware and by
  // the solver, which is what lets an object whose matrix moved to another slot still pair.
  a.draws[0].components = b.draws[0].components = gx::VB_HAS_POSMTXIDX;
  b.vertices[0].posmtx = 3; solver.set_frames(&a, &b);
  solver.build(0.5, true, mats, true);
  check(!mats[0].vertices && near(mats[0].pos[3], 4.0f),
        "changed matrix binding keeps the current vertices and advances the matrices");
  a.draws[0].components = b.draws[0].components = 0;
  b.vertices[0].posmtx = 0; b.vertices[0].pos[0] = solver.max_translation + 1; solver.set_frames(&a, &b);
  solver.build(0.5, true, mats, true);
  check(!mats[0].vertices && near(mats[0].pos[3], 4.0f),
        "geometry discontinuity keeps the current vertices and advances the matrices");
  b.vertices[0].pos[0] = 0; b.draws[0].xf_regs[0x26] = 1; solver.set_frames(&a, &b);
  check(solver.stats().paired == 0, "orthographic HUD draws retain exact pose");
  b.draws[0].xf_regs[0x26] = 0;
  // A rollback's discontinuous frame (see Frame::discontinuous) still holds a plain draw at its
  // exact pose, since blending stage/world geometry against the mispredicted previous frame can
  // draw a position that existed on neither timeline.
  b.discontinuous = true;
  solver.set_frames(&a, &b);
  check(solver.stats().paired == 0 && solver.stats().discontinuity == 1, "a plain draw is held across a rollback's discontinuity");
  // A fighter's authored (skinned) pose is resampled from its joint chain rather than blended
  // vertex for vertex, so it still pairs through the same discontinuous frame.
  auto envelope_pose = std::make_shared<gx::AuthoredPose>();
  envelope_pose->envelope = true;
  a.draws[0].authored_pose = b.draws[0].authored_pose = envelope_pose;
  solver.set_frames(&a, &b);
  check(solver.stats().paired == 1 && solver.stats().discontinuity == 0, "a fighter's authored pose still pairs across a rollback's discontinuity");
  // A Training Lab load or a replay seek is a hard cut: the fighter is held too, not slid back.
  b.hard_cut = true;
  solver.set_frames(&a, &b);
  check(solver.stats().paired == 0 && solver.stats().discontinuity == 1, "a fighter is held across a hard cut");
  b.hard_cut = false;
  a.draws[0].authored_pose = b.draws[0].authored_pose = nullptr;
  b.discontinuous = false;
  // Matrix selectors are discrete even when the vertex stream is unchanged.
  gx::Frame texture_previous, texture_current;
  texture_previous.sequence = 1; texture_current.sequence = 2;
  texture_previous.vertices.resize(3, gx::Vertex{}); texture_current.vertices.resize(3, gx::Vertex{});
  gx::DrawCall texture_draw{}; texture_draw.identity = 15; texture_draw.vertex_count = 3;
  texture_draw.primitive = 0x90; texture_draw.xf_regs[0x3F] = 1;
  texture_draw.matrix_index_a = 6u << 6;
  rot_z(0, 0, texture_draw.posMatrices);
  rot_z(0, 0, texture_draw.posMatrices + 24);
  texture_previous.draws.push_back(texture_draw); texture_current.draws.push_back(texture_draw);
  texture_current.draws[0].matrix_index_a |= 3;
  solver.set_frames(&texture_previous, &texture_current);
  check(solver.stats().paired == 0, "changed CP position binding invalidates subframe history");
  texture_current.draws[0].matrix_index_a = 9u << 6;
  solver.set_frames(&texture_previous, &texture_current);
  check(solver.stats().paired == 0, "changed active CP texture binding invalidates subframe history");
  texture_current.draws[0].matrix_index_a = texture_draw.matrix_index_a;
  texture_current.draws[0].matrix_index_b = 12; // inactive texture generator
  solver.set_frames(&texture_previous, &texture_current);
  check(solver.stats().paired == 1, "inactive texture bindings do not reject a draw");
  texture_current.draws[0].xf_regs[0x40] = 2;
  solver.set_frames(&texture_previous, &texture_current);
  check(solver.stats().paired == 0, "changed texture projection invalidates subframe history");
  texture_current.draws[0].xf_regs[0x40] = 0;
  texture_current.draws[0].matrix_index_b = 0;
  // All eight per-vertex selectors must drive their own texture matrices. CP
  // defaults point elsewhere, so accidentally using them is observable.
  for (unsigned generator = 0; generator < 8; ++generator) {
    texture_previous.draws[0].xf_regs[0x3F] = texture_current.draws[0].xf_regs[0x3F] = generator + 1;
    texture_previous.draws[0].components = texture_current.draws[0].components = gx::VB_HAS_TEXMTXIDX0 << generator;
    for (auto& v : texture_previous.vertices) v.texmtx[generator] = 12;
    for (auto& v : texture_current.vertices) v.texmtx[generator] = 12;
    rot_z(0, 0, texture_previous.draws[0].posMatrices + 48);
    rot_z(0, 4, texture_current.draws[0].posMatrices + 48);
    solver.set_frames(&texture_previous, &texture_current);
    solver.build(.5, false, mats, true);
    check(near(mats[0].pos[51], 6), "per-vertex texture selector advances the matrix actually used by the shader");
    solver.build(.5, true, mats, true);
    check(near(mats[0].pos[51], 2), "per-vertex texture transform shares interpolation timeline");
  }
  // A large change to one coefficient rejects the complete texture transform.
  texture_current.draws[0].posMatrices[48] = 30;
  solver.set_frames(&texture_previous, &texture_current);
  solver.build(.5, false, mats, true);
  check(std::memcmp(mats[0].pos + 48, texture_current.draws[0].posMatrices + 48, 12 * sizeof(float)) == 0,
        "texture discontinuity holds all coefficients together");
  solver.build(.5, true, mats, true);
  check(std::memcmp(mats[0].pos + 48, texture_current.draws[0].posMatrices + 48, 12 * sizeof(float)) == 0,
        "texture interpolation cut also holds the complete current transform");
  texture_previous.draws[0].components = texture_current.draws[0].components = 0;
  texture_previous.draws[0].xf_regs[0x3F] = texture_current.draws[0].xf_regs[0x3F] = 1;
  texture_previous.draws[0].matrix_index_a = texture_current.draws[0].matrix_index_a = 1u << 6;
  texture_current.draws[0].posMatrices[7] = 4;
  solver.set_frames(&texture_previous, &texture_current);
  solver.build(.5, false, mats, true);
  check(near(mats[0].pos[7], 4), "partially overlapping texture rows do not overwrite a held position matrix");
  float scale2[12] = {2,0,0,0, 0,1,0,0, 0,0,1,0};
  gx::SubFrameSolver::fractional(prev, scale2, 0.5, true, 40, 3.0f, out, nrm, ident_n, ident_n, &st);
  check(near(nrm[0], 1.0f / std::sqrt(2.0f)), "normal delta uses inverse transpose");
  // A linear authored track: forward sampling must start at the current pose,
  // and refuse to predict across its animation boundary.
  gx::AuthoredJoint joint;
  joint.generation = 1; joint.scale = {1,1,1}; joint.frame = 0; joint.rate = 1; joint.end = 2;
  joint.world = NativeMelee::Identity();
  NativeMelee::PackedTrack track; track.channel = 5; track.value_format = 128;
  track.bytes = {0x12,0,2,10}; joint.tracks.push_back(track);
  gx::AuthoredPose previous_pose, current_pose; previous_pose.joints.push_back(joint);
  joint.frame = 1; joint.translation[0] = 5; joint.world[3] = 5;
  current_pose.joints.push_back(joint);
  check(gx::sample_authored(previous_pose, current_pose, 0.5, joint.world.data(), out, nrm, ident_n), "valid forward authored track");
  check(near(out[3], 7.5f), "authored sampling advances beyond current rather than previous frame");
  // Animation-clock continuity cannot use the world-position tolerance: at
  // frame 1001 that tolerance admits a whole paused tick as forward motion.
  {
    auto paused_previous = current_pose, paused_current = current_pose;
    for (auto* pose : {&paused_previous, &paused_current}) {
      auto& j = pose->joints[0];
      j.frame = 1001; j.end = 1002; j.tracks[0].start_frame = -1000;
    }
    check(!gx::sample_authored(paused_previous, paused_current, .5, joint.world.data(), out, nrm, ident_n),
          "paused long-running animation does not predict another half tick");
    // The previous capture has to be what the track actually holds at frame 1000 (0, one frame
    // before 5): a sample is only accepted between the two captured values, so a previous pose
    // that claims frame 1000 while carrying frame 1001's translation is a contradiction.
    paused_previous.joints[0].frame = 1000; paused_previous.joints[0].translation[0] = 0; paused_previous.joints[0].world[3] = 0;
    check(gx::sample_authored(paused_previous, paused_current, .5, joint.world.data(), out, nrm, ident_n) && near(out[3], 7.5f),
          "advancing long-running animation still samples at its declared rate");
    // A speed change (the game sets a run's or a drift's rate from the velocity): the frame advanced by the new
    // rate, so Predict samples on at that rate instead of holding the joint for the frame (11-21: 60 Hz steps).
    paused_previous.joints[0].rate = .5f;
    check(gx::sample_authored(paused_previous, paused_current, .5, joint.world.data(), out, nrm, ident_n) && near(out[3], 7.5f),
          "retimed animation predicts at its new rate");
    // A frame that neither rate explains is no continuation: it waits for a continuous pair.
    paused_previous.joints[0].frame = 999.25f;
    check(!gx::sample_authored(paused_previous, paused_current, .5, joint.world.data(), out, nrm, ident_n),
          "a clock neither rate explains waits for a continuous pair before predicting");
  }
  current_pose.joints[0].end = 1;
  check(!gx::sample_authored(previous_pose, current_pose, 0.5, joint.world.data(), out, nrm, ident_n), "authored sampling holds at animation boundary");
  // Camera quakes: a panning view is carried forward, a shaking one holds its exact tick.
  {
    gx::AuthoredPose view_previous, view_current;
    view_previous.has_view = view_current.has_view = true;
    view_previous.view = NativeMelee::Identity(); view_current.view = NativeMelee::Identity();
    view_current.view[3] = 2;
    float held_pos[256]{}, held_nrm[96]{}, carried_pos[256], carried_nrm[96];
    std::memcpy(held_pos, NativeMelee::Identity().data(), 12 * sizeof(float));
    std::memcpy(carried_pos, held_pos, sizeof carried_pos);
    check(gx::carry_camera(view_previous, view_current, .5, view_current, held_pos, held_nrm, 1, carried_pos, carried_nrm) && near(carried_pos[3], 1),
          "panning camera carries held draws forward");
    // A shake: the game moves the eye and its target by quake_shift, which in this view matrix is
    // view x = -shift. The camera under it is sampled; the shake itself steps at 60 Hz.
    {
      gx::AuthoredPose shake_previous = view_previous, shake_current = view_current;
      shake_current.quake = true;
      shake_current.quake_shift = {2, 0};
      shake_current.view[3] = -2;   // the camera did not move: only the shake did
      std::memcpy(carried_pos, held_pos, sizeof carried_pos);
      check(!gx::carry_camera(shake_previous, shake_current, .5, shake_current, held_pos, held_nrm, 1, carried_pos, carried_nrm),
            "a pure shake is drawn at its exact 60 Hz offset, not extrapolated along the jolt");
      shake_current.view[3] = 2 - 1;   // the camera panned by 2 and shook by 1 in the same tick
      shake_current.quake_shift = {1, 0};
      std::memcpy(carried_pos, held_pos, sizeof carried_pos);
      check(gx::carry_camera(shake_previous, shake_current, .5, shake_current, held_pos, held_nrm, 1, carried_pos, carried_nrm) &&
                near(carried_pos[3], 1),
            "a panning camera keeps moving through a shake");
      // Another camera (the HUD's) does not move while the stage shakes: drawn as it is.
      gx::AuthoredPose hud_previous = view_previous, hud_current = view_previous;
      hud_previous.quake = hud_current.quake = true;
      hud_previous.quake_shift = {1, 0}; hud_current.quake_shift = {-1, 0};
      check(!gx::carry_camera(hud_previous, hud_current, .5, hud_current, held_pos, held_nrm, 1, carried_pos, carried_nrm),
            "a camera that did not move is not moved by the stage's shake");
      shake_previous.quake = true; shake_previous.quake_shift = {2, 0}; shake_previous.view[3] = -2;
      shake_current.quake = false; shake_current.quake_shift = {0, 0}; shake_current.view[3] = 0;
      check(!gx::carry_camera(shake_previous, shake_current, .5, shake_current, held_pos, held_nrm, 1, carried_pos, carried_nrm),
            "the tick a shake ends does not extrapolate its return");
      // Interpolate: the camera under the shake is blended, the previous frame's shake is shown
      // until the phase reaches the current frame, and blended rows get the same shake.
      gx::set_authored_interpolate(true);
      shake_previous = view_previous; shake_current = view_current;
      shake_previous.quake = shake_current.quake = true;
      shake_previous.quake_shift = {1, 0}; shake_previous.view[3] = -1;
      shake_current.quake_shift = {-1, 0}; shake_current.view[3] = 2 + 1;   // panned by 2
      check(gx::carry_camera(shake_previous, shake_current, .5, shake_previous, held_pos, held_nrm, 1, carried_pos, carried_nrm) &&
                near(carried_pos[3], 1 - 1 - (-1)),
            "interpolated camera under a shake shows the previous frame's shake");
      float fix[3];
      check(gx::quake_row_correction(shake_previous, shake_current, .5, fix) && near(fix[0], -1),
            "blended rows get the shown shake instead of a blend of two");
      gx::set_authored_interpolate(false);
    }
    // The carry has to cancel the view that is actually in the matrices it is given. An object
    // standing still in the world is at view * world, so at half a frame it must be drawn at the
    // half-way view, whichever frame the caller held: a paired draw holds the previous frame's
    // matrices, an unpaired one has only the current frame's. Taking the base from the mode instead
    // left a whole camera step in every unpaired draw (here x = 3 rather than 1).
    view_previous.quake = false;
    gx::set_authored_interpolate(true);
    float from_current[256]{}, from_previous[256]{};
    std::memcpy(from_current, NativeMelee::Identity().data(), 12 * sizeof(float));
    from_current[3] = 2;   // current frame: view x = 2 times a world position of 0
    std::memcpy(from_previous, NativeMelee::Identity().data(), 12 * sizeof(float));
    check(gx::carry_camera(view_previous, view_current, .5, view_current, from_current, held_nrm, 1, carried_pos, carried_nrm) &&
              near(carried_pos[3], 1),
          "a draw with no pair is carried off the current frame's view");
    check(gx::carry_camera(view_previous, view_current, .5, view_previous, from_previous, held_nrm, 1, carried_pos, carried_nrm) &&
              near(carried_pos[3], 1),
          "a paired draw holding the previous frame is carried off that view");
    gx::set_authored_interpolate(false);
  }
  // Menus interpolate, so every menu draw is shown between the previous frame and this one. A
  // draw whose vertex stream cannot be blended (here a leftover texture-matrix selector differs, as
  // on most menu ticks) must still advance its matrices with the rest: holding it at the current
  // frame drew those pieces of a sliding menu row a tick ahead of the pieces that blended, which
  // is the doubled Online menu rows at 180 Hz. A draw whose matrix binding really changed holds.
  {
    // assign, not resize: frame vertices use an allocator that leaves new elements unset.
    gx::Frame ma, mb; ma.sequence = 10; mb.sequence = 11; ma.vertices.assign(6, gx::Vertex{}); mb.vertices.assign(6, gx::Vertex{});
    gx::DrawCall row{}; row.identity = 21; row.object_generation = 3; row.vertex_count = 3; row.primitive = 0x90;
    row.components = 0; row.matrix_index_a = 0; row.xf_regs[0x3F] = 0;
    std::memcpy(row.normalMatrices, ident_n, sizeof ident_n);
    rot_z(0, 0, row.posMatrices); ma.draws.push_back(row);
    gx::DrawCall cap = row; cap.identity = 22; cap.first_vertex = 3; cap.components = gx::VB_HAS_POSMTXIDX;
    ma.draws.push_back(cap);
    rot_z(0, 6, row.posMatrices); mb.draws.push_back(row);   // the row slides 6 units this tick
    rot_z(0, 6, cap.posMatrices); mb.draws.push_back(cap);
    mb.vertices[0].pos[0] = 0.5f;          // rewritten text: the stream changed
    mb.vertices[1].texmtx[3] = 5;          // leftover selector: the stream cannot blend
    mb.vertices[3].pos[0] = 0.5f;          // the end cap is rewritten too, and its binding moved
    mb.vertices[3].posmtx = 3;
    gx::SubFrameSolver menu; menu.max_rotation = 3.0f; menu.set_menu_mode(true);
    menu.set_frames(&ma, &mb);
    check(menu.stats().paired == 2, "menu row and end cap pair");
    std::vector<gx::DrawMatrices> mm;
    menu.build(0.5, true, mm, true);
    check(mm.size() == 2 && !mm[0].vertices && near(mm[0].pos[3], 3.0f),
          "a menu draw that cannot blend its stream still advances with the rest of the menu");
    check(near(mm[1].pos[3], 6.0f), "a menu draw whose matrix binding changed holds its current frame");
    check(menu.stats().menu_advanced == 1, "unchanged menu route-6 bindings count as advanced");
    check(menu.stats().menu_held == 1, "changed menu route-6 posmtx counts as held");
    menu.build(0.75, true, mm, true);
    check(menu.stats().menu_advanced == 1 && menu.stats().menu_held == 1,
          "menu route-6 counters reset on each build");
  }
  // Three shaken fighter draws must not outvote the stage's actual camera.
  // An unpaired piece of stage art should follow the camera pan alone.
  {
    gx::Frame before, after; before.sequence = 1; after.sequence = 2;
    before.vertices.resize(3, gx::Vertex{}); after.vertices.resize(3, gx::Vertex{});
    auto old_view = std::make_shared<gx::AuthoredPose>(), new_view = std::make_shared<gx::AuthoredPose>();
    old_view->has_view = new_view->has_view = true;
    old_view->view = new_view->view = NativeMelee::Identity(); new_view->view[3] = 2;
    gx::AuthoredJoint root; root.generation = 1; root.scale = {1,1,1}; root.world = NativeMelee::Identity();
    old_view->joints.push_back(root); new_view->joints.push_back(root);
    auto old_model = std::make_shared<gx::AuthoredPose>(*old_view), new_model = std::make_shared<gx::AuthoredPose>(*new_view);
    old_model->model_shift = new_model->model_shift = old_model->quake = new_model->quake = true;
    old_model->view[3] += 2; old_model->quake_shift = {-2,0};
    new_model->view[3] -= 2; new_model->quake_shift = {2,0};
    for (int i = 0; i < 4; ++i) {
      gx::DrawCall draw{}; draw.identity = i + 100; draw.object_generation = 1;
      draw.vertex_count = 3; draw.primitive = 0x90;
      draw.authored_pose = i ? old_model : old_view;
      rot_z(0, draw.authored_pose->view[3], draw.posMatrices);
      std::memcpy(draw.normalMatrices, ident_n, sizeof ident_n); before.draws.push_back(draw);
      draw.authored_pose = i ? new_model : new_view;
      rot_z(0, draw.authored_pose->view[3], draw.posMatrices); after.draws.push_back(draw);
    }
    auto scenery = after.draws[0]; scenery.identity = 999; after.draws.push_back(scenery);
    solver.set_frames(&before, &after); solver.build(.5, false, mats, true);
    std::printf("model-shake stage carry: %g (paired=%u carried=%u)\n", mats.back().pos[3], solver.stats().paired, solver.stats().carried);
    check(near(mats.back().pos[3], 3), "fighter hit shake cannot become the stage camera");
  }
  // Predict ahead, a slow pan (0.1 units, inside the near-static tolerance). A piece of the world with
  // no authored pose moves by exactly the camera step and is carried with it; game text on the HUD's own
  // perspective camera (the online name tags under the percents) repeats its matrices exactly and must be
  // drawn where the game put it, not carried by a share of the world camera's step on every sub-frame.
  {
    gx::Frame before, after; before.sequence = 1; after.sequence = 2;
    before.vertices.resize(3, gx::Vertex{}); after.vertices.resize(3, gx::Vertex{});
    auto old_view = std::make_shared<gx::AuthoredPose>(), new_view = std::make_shared<gx::AuthoredPose>();
    old_view->has_view = new_view->has_view = true;
    old_view->view = new_view->view = NativeMelee::Identity(); new_view->view[3] = 0.1f;
    gx::AuthoredJoint root; root.generation = 1; root.scale = {1,1,1}; root.world = NativeMelee::Identity();
    old_view->joints.push_back(root); new_view->joints.push_back(root);
    const float world_proj[7] = {1.5f, 0, 2.0f, 0, -1.0f, -2.0f, 0}, hud_proj[7] = {2.5f, 0, 3.3f, 0, -1.0f, -2.0f, 0};
    gx::DrawCall camera{}; camera.identity = 300; camera.object_generation = 1; camera.vertex_count = 3; camera.primitive = 0x90;
    std::memcpy(&camera.xf_regs[0x20], world_proj, sizeof world_proj);
    std::memcpy(camera.normalMatrices, ident_n, sizeof ident_n);
    gx::DrawCall world = camera; world.identity = 301; world.object_generation = 0;
    gx::DrawCall hud = world; hud.identity = 302;
    std::memcpy(&hud.xf_regs[0x20], hud_proj, sizeof hud_proj);
    rot_z(0, 5, hud.posMatrices);
    camera.authored_pose = old_view; rot_z(0, 0, camera.posMatrices); rot_z(0, 0, world.posMatrices);
    before.draws = {camera, world, hud};
    camera.authored_pose = new_view; rot_z(0, 0.1f, camera.posMatrices); rot_z(0, 0.1f, world.posMatrices);
    after.draws = {camera, world, hud};
    gx::SubFrameSolver pan; pan.max_rotation = 3.0f;
    pan.set_frames(&before, &after);
    pan.build(.5, false, mats, true);
    std::printf("slow pan: world %g hud %g (paired=%u carried=%u)\n", mats[1].pos[3], mats[2].pos[3], pan.stats().paired, pan.stats().carried);
    check(mats.size() == 3 && mats[1].pos[3] > 0.101f, "a world draw with no pose follows a slow pan ahead of the tick");
    check(std::memcmp(mats[2].pos, hud.posMatrices, sizeof mats[2].pos) == 0, "HUD text on its own camera stays where the game drew it during a pan");
  }
  // Predict ahead, then the motion stops (a dash dance turning, a float starting). The last presented
  // frame of the moving tick showed the world ahead of where the game ended up; the next tick glides
  // it back over half a tick instead of snapping in one presented frame. A draw that starts moving
  // from rest was never shown ahead, so it is not held back.
  {
    auto view_at = [](float x) {
      auto v = std::make_shared<gx::AuthoredPose>(); v->has_view = true; v->view = NativeMelee::Identity(); v->view[3] = x;
      gx::AuthoredJoint root; root.generation = 1; root.scale = {1,1,1}; root.world = NativeMelee::Identity(); v->joints.push_back(root);
      return v;
    };
    const float world_proj[7] = {1.5f, 0, 2.0f, 0, -1.0f, -2.0f, 0};
    auto frame_at = [&](uint64_t seq, float x) {
      gx::Frame f; f.sequence = seq; f.vertices.resize(6, gx::Vertex{});
      gx::DrawCall camera{}; camera.identity = 400; camera.object_generation = 1; camera.vertex_count = 3; camera.primitive = 0x90;
      std::memcpy(&camera.xf_regs[0x20], world_proj, sizeof world_proj);
      std::memcpy(camera.normalMatrices, ident_n, sizeof ident_n);
      camera.authored_pose = view_at(x); rot_z(0, x, camera.posMatrices);
      gx::DrawCall world = camera; world.identity = 401; world.object_generation = 0; world.authored_pose = nullptr; world.first_vertex = 3;
      rot_z(0, x, world.posMatrices);
      f.draws = {camera, world};
      return f;
    };
    gx::Frame f1 = frame_at(1, 0), f2 = frame_at(2, 0.1f), f3 = frame_at(3, 0.1f);
    gx::SubFrameSolver glide; glide.max_rotation = 3.0f;
    glide.set_frames(&f1, &f2); glide.build(.9, false, mats, true); glide.present_shown();
    const float shown = mats[1].pos[3];
    glide.set_frames(&f2, &f3); glide.build(.1, false, mats, true); glide.present_shown();
    const float first = mats[1].pos[3];
    glide.build(.35, false, mats, true); glide.present_shown();
    const float middle = mats[1].pos[3];
    glide.build(.7, false, mats, true);
    const float settled = mats[1].pos[3];
    std::printf("predict glide: shown %g, next tick %g -> %g -> %g\n", shown, first, middle, settled);
    check(shown > 0.15f, "the moving tick is drawn ahead of the game");
    check(near(first, shown), "the stopped tick starts where the last presented frame left the draw");
    check(middle < shown && middle > 0.1001f, "half way through the glide");
    check(near(settled, 0.1f), "the glide is over by half a tick");
    gx::Frame r1 = frame_at(1, 0), r2 = frame_at(2, 0), r3 = frame_at(3, 0.1f);
    gx::SubFrameSolver start; start.max_rotation = 3.0f;
    start.set_frames(&r1, &r2); start.build(.9, false, mats, true); start.present_shown();
    start.set_frames(&r2, &r3); start.build(.1, false, mats, true);
    check(mats[1].pos[3] >= 0.1f - 1e-4f, "motion from rest is not held back by the glide");
    // A present that never reached the screen is not what the next tick glides from: built at phase
    // .9 but cut short, so the screen still shows the phase .5 present; and a frame presented exactly
    // as drawn leaves nothing to glide.
    gx::SubFrameSolver cut; cut.max_rotation = 3.0f;
    cut.set_frames(&f1, &f2); cut.build(.5, false, mats, true); cut.present_shown();
    const float half = mats[1].pos[3];
    cut.build(.9, false, mats, true);   // cut short: no present_shown
    cut.set_frames(&f2, &f3); cut.build(.1, false, mats, true);
    check(near(mats[1].pos[3], half), "the glide starts from the last present that was shown, not one cut short");
    gx::SubFrameSolver exact; exact.max_rotation = 3.0f;
    exact.set_frames(&f1, &f2); exact.build(.9, false, mats, true); exact.present_shown();
    exact.present_as_drawn();   // the new frame went out exactly as drawn on arrival
    exact.set_frames(&f2, &f3); exact.build(.1, false, mats, true);
    check(near(mats[1].pos[3], 0.1f), "after a frame presented as drawn there is nothing to glide back");
    // Uncapped, the new frame is presented the moment it arrives, before it is paired. The glide
    // starts on that frame (found by identity in what was last shown) and this tick's builds fade it.
    gx::SubFrameSolver arrive; arrive.max_rotation = 3.0f;
    arrive.set_frames(&f1, &f2); arrive.build(.9, false, mats, true); arrive.present_shown();
    const float last_shown = mats[1].pos[3];
    std::vector<gx::DrawMatrices> on_arrival;
    const bool glided = arrive.arrival_glide(f3, on_arrival);
    const float arrived = glided && on_arrival.size() == 2 ? on_arrival[1].pos[3] : -1.0f;
    check(glided && near(arrived, last_shown), "a frame presented on arrival starts where the last present left the draw");
    check(glided && on_arrival[1].pos[0] == f3.draws[1].posMatrices[0] && on_arrival[1].nrm[0] == f3.draws[1].normalMatrices[0] &&
          !on_arrival[1].vertices && !on_arrival[1].has_projection, "the rest of an arrival frame is exactly as the game drew it");
    arrive.set_frames(&f2, &f3); arrive.build(.15, false, mats, true); arrive.present_shown();
    const float arrive_mid = mats[1].pos[3];
    arrive.build(.6, false, mats, true); arrive.present_shown();
    const float arrive_end = mats[1].pos[3];
    std::printf("arrival glide: shown %g, arrival %g -> %g -> %g\n", last_shown, arrived, arrive_mid, arrive_end);
    check(arrive_mid < arrived && arrive_mid > 0.1001f, "the builds after an arrival frame continue its glide");
    check(near(arrive_end, 0.1f), "an arrival glide is over by half a tick");
    gx::Frame m1 = frame_at(1, 0), m2 = frame_at(2, 0.1f), m3 = frame_at(3, 0.2f), m5 = frame_at(5, 0.1f);
    gx::SubFrameSolver steady; steady.max_rotation = 3.0f;
    steady.set_frames(&m1, &m2); steady.build(.9, false, mats, true); steady.present_shown();
    check(!steady.arrival_glide(m3, on_arrival), "motion that goes on leaves nothing to glide on arrival");
    check(!arrive.arrival_glide(m5, on_arrival), "a frame that is not the next one after the last shown glides nothing");
    // A fighter glides as one piece: three of its draws that overshot by different amounts (bones
    // that disagree, a slot holding another bone) all get the same offset, the median, so its mesh
    // never stretches between them; texture matrices are never moved.
    auto fighter_at = [&](uint64_t seq, float x) {
      gx::Frame f = frame_at(seq, x);
      for (auto& d : f.draws) d.owner_player = 0;
      gx::DrawCall arm = f.draws[1]; arm.identity = 402; rot_z(0, x * 3.0f, arm.posMatrices);   // swung three times as far
      arm.posMatrices[30 * 4 + 3] = 7.0f;   // a texture matrix row
      f.draws.push_back(arm);
      return f;
    };
    gx::Frame p1 = fighter_at(1, 0), p2 = fighter_at(2, 0.1f), p3 = fighter_at(3, 0.1f);
    gx::SubFrameSolver rigid; rigid.max_rotation = 3.0f;
    rigid.set_frames(&p1, &p2); rigid.build(.9, false, mats, true); rigid.present_shown();
    const bool rigid_glided = rigid.arrival_glide(p3, on_arrival) && on_arrival.size() == 3;
    const float o_world = rigid_glided ? on_arrival[1].pos[3] - p3.draws[1].posMatrices[3] : 0;
    const float o_arm = rigid_glided ? on_arrival[2].pos[3] - p3.draws[2].posMatrices[3] : 0;
    std::printf("rigid glide: world %g, arm %g\n", o_world, o_arm);
    check(rigid_glided && o_world > 0.01f && near(o_world, o_arm), "every draw of a fighter glides by the same offset");
    check(rigid_glided && on_arrival[2].pos[30 * 4 + 3] == 7.0f, "a glide never moves a texture matrix");
  }
  std::puts("sub-frame rigid fractions, cuts, blends, pairing and model-shake isolation passed");
}
