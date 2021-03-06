// echo_character.cpp

/*
    This file is part of L-Echo.

    L-Echo is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    L-Echo is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with L-Echo.  If not, see <http://www.gnu.org/licenses/>.
*/

/** Your standard libraries
 * typeinfo is used to find if the grid is a hole, so the character can fall
 * through the hole if the first grid is one.
 */
#include <cstdlib>
#include <iostream>
#include <typeinfo>
#include <cmath>
#include <cfloat>

/** L-Echo libraries (not grids)
 */
#include "echo_platform.h"
#include "echo_sys.h"
#include "echo_gfx.h"
#include "echo_debug.h"
#include "echo_math.h"
#include "echo_ns.h"
#include "echo_character.h"
#include "echo_char_joints.h"
#include "echo_stage.h"

/** Grids; just launchers and holes are discriminated against, because
 * the character is responsible for flying and falling
 */
#include "launcher.h"
#include "grid.h"
#include "hole.h"

/// Need to measure the body sizes in order to do IK correctly
#include "gen/gen.h"

/// Convenience macro to draw at a particular vector3f
#define DRAW_VEC(vec)		draw((vec)->x, (vec)->y, (vec)->z)

/// The acceleration constant (Units / s^2)
#define ACCEL					15.0f

/// How high above the start grid does the character start?
#define STARTY					10

const float CHARACTER_SPEEDS[] = { 0.07f, 0.25f, 0.00f, -0.50f, 14.4913767f, 0.00f };
	
/// Launching initial horizontal velocity (see echo_char#initialize_launching)
const float LAUNCH_INIT_X = CHARACTER_SPEEDS[LAUNCH] / 7;

/** Initialize, and prepare to fall to that grid.
 * @param g1 The initial grid on which to spawn
 */
echo_char::echo_char(grid* g1)
{
	/// num_goals is the total number of goals this character has passed; shouldn't be reset
	num_goals = 0;
	/// Set fall_position to NULL, or else init/land will attempt to delete it.
	fall_position = NULL;
	/// Set fly_direction to NULL, or else next_grid will attempt to delete it.
	fly_direction = NULL;
	/// initialize
	init(g1);
}
/// Destructor
echo_char::~echo_char()
{
	/// fall_position is the only dynamically constructed member
	if(fall_position != NULL)
	{
		delete fall_position;
		fall_position = NULL;
	}
	if(fly_direction != NULL)
	{
		delete fly_direction;
		fly_direction = NULL;
	}
}

/** @return If the character is paused; should actually relegate to echo_ns.
 */
int echo_char::is_paused()
{
	return(paused);
} 

/** @return How many goals (echos, in echochrome-speak) the character has reached.
 */
int echo_char::num_goals_reached()
{
	return(num_goals);
}

/** Start falling from the given position, or where grid1 is.
 * @param pos An arbitrary position to fall from.  If this is NULL, then grid1's position will be used
 */
void echo_char::initialize_falling(vector3f* pos)
{
	//ECHO_PRINT("initialize falling...%p, %p\n", grid1, grid2);
	if(pos != NULL)
	{
		/// fall_position is used as the position if the camera angle is (0, 0, 0)
		fall_position = pos->neg_rotate_yx(echo_ns::angle);
	}
	else if(grid1 != NULL)
	{
		/// Get the info
		grid_info_t* i1 = grid1->get_info(echo_ns::angle);
		if(i1 != NULL)
		{
			/// fall_position is used as the position if the camera angle is (0, 0, 0)
			fall_position = i1->pos->neg_rotate_yx(echo_ns::angle);
		}
	}
	else
		echo_error("Cannot find where the character is falling from; quitting\n");
	/// Set the mode to fall
	mode = FALL;
	/// If the character was already falling (speed != 0), don't change the actual speed
	if(speed == 0)
		speed = CHARACTER_SPEEDS[mode];
	initialize_falling_mode();
}

/** Start launching from the given position and direction, or where grid1 and grid2 are.
 * The launching works like this: If the character is launched from and lands on the same 
 * level, then he'll go up 7 units and right 4 units (tested in the real game).
 * So we can derive some constants from that...
 * 
 * When y = 7, v_fy = 0:
 * {v_oy}^2 + 2{a_y}x = {v_fy}^2
 * {v_oy}^2 + 14{a_y} = 0
 * {v_oy} = sqrt(-14{a_y})  (a is already negative)
 * 
 * When y = 0 (when he launches and lands):
 * y = {v_oy}t + a{t^2} / 2
 * 0 = t * sqrt(-14{a_y}) + a{t^2} / 2
 * 
 * Since a_y = -sqrt(-a_y) * sqrt(-a_y):
 * 0 = t * sqrt(-a_y) * ( sqrt(14) - t * sqrt(-a_y) / 2 )
 * 
 * So: 
 * t * sqrt(-a_y) / 2 = sqrt(14)
 * t = 2 * sqrt(14) / sqrt(-a_y)   (when he lands)
 * 
 * Plug it into the x equation:
 * x = v_ox * t
 * 4 = v_ox * 2 * sqrt(14) / sqrt(-a_y)
 * 2 * sqrt(-a_y) / sqrt(14) = v_ox
 * sqrt(-14{a_y}) / 7 = v_ox
 * v_oy / 7 = v_ox
 * 
 * @param pos An arbitrary position to launch from.  If this is NULL, then grid1's position will be used
 * @param direction Direction to launch towards laterally (on the xz-plane).  If this is NULL, then get_direction will be used.  WILL BE DELETED!!!
 */
void echo_char::initialize_launching(vector3f* pos, vector3f* direction)
{
	if(pos != NULL)
	{
		/// fall_position is used as the position if the camera angle is (0, 0, 0)
		fall_position = pos->neg_rotate_yx(echo_ns::angle);
	}
	else if(grid1 != NULL)
	{
		/// Get the info
		grid_info_t* i1 = grid1->get_info(echo_ns::angle);
		if(i1 != NULL)
		{
			/// fall_position is used as the position if the camera angle is (0, 0, 0)
			fall_position = i1->pos->neg_rotate_yx(echo_ns::angle);
		}
	}
	else
		echo_error("Cannot find where the character is falling from; quitting\n");
	
	/// Initialize direction if it isn't already
	if(direction == NULL)
	{
		direction = new vector3f(fly_direction->x, fly_direction->y, fly_direction->z);
		
	}
	/// Get the length of the direction...
	const float dir_length = sqrt(direction->x * direction->x + direction->z * direction->z);
	/// And normalize the 
	x_speed = direction->x / dir_length * LAUNCH_INIT_X;
	z_speed = direction->z / dir_length * LAUNCH_INIT_X;
	ECHO_PRINT("x_speed: %f\n", x_speed);
	ECHO_PRINT("y_speed: %f\n", CHARACTER_SPEEDS[LAUNCH]);
	ECHO_PRINT("z_speed: %f\n", z_speed);
	delete direction;
	
	/// Set the mode to launch
	mode = LAUNCH;
	/// If the character was already falling (speed != 0), don't change the actual speed
	if(speed == 0)
		speed = CHARACTER_SPEEDS[mode];
	initialize_falling_mode();
}

/** Falling from the sky, at the start of the stage.
 */
void echo_char::initialize_fall_from_sky()
{
	if(grid1 != NULL)
	{
		/// Get the info
		grid_info_t* i1 = grid1->get_info(echo_ns::angle);
		if(i1 != NULL)
		{
			/// Set the target level to grid1's y-coordinate
			target_y = i1->pos->y;
			/// Clear fall_position first
			if(fall_position != NULL)
				delete fall_position;
			/// Copy the position of grid1
			fall_position = new vector3f();
			fall_position->set(i1->pos);
			/// But also put fall_position above grid1
			fall_position->y += STARTY;
		}
	}
	else
		echo_error("Cannot find where the character is falling from; quitting\n");
	mode = FALL_FROM_SKY;
	speed = CHARACTER_SPEEDS[mode];
	initialize_falling_mode();
}
/// Changes the mode and speed of the character according to the grids it's at.
void echo_char::change_speed()
{
	/// Need to have grid1 as non-null to check the type
	if(grid1 != NULL)
	{
		/// First grid is an hole, and there is no second grid (no esc) -> Fall into hole
		if(typeid(*grid1) == typeid(hole) && grid2 == NULL)
		{
			ECHO_PRINT("falling into hole...\n");
			initialize_falling(NULL);
		}
		/// First grid is a launcher, and there is no second grid (no esc) -> Launched
		else if(typeid(*grid1) == typeid(launcher) && grid2 == NULL)
		{
			ECHO_PRINT("being launched!\n");
			initialize_launching(NULL, NULL);
		}
		/// If the character isn't in Grid Mode, it should be.
		else if(mode == FALL || mode == LAUNCH || mode == FALL_FROM_SKY)
		{
			ECHO_PRINT("normal mode\n");
			mode = is_running ? RUN : STEP;
			speed = CHARACTER_SPEEDS[mode];
		}
	}
}
/// If the character is walking, start running
void echo_char::start_run()
{
	if(mode == STEP)
	{
		is_running = true;
		mode = RUN;
		speed = CHARACTER_SPEEDS[mode];
	}
}
/// If the character is running, start walking
void echo_char::start_step()
{
	if(mode == RUN)
	{
		is_running = false;
		mode = STEP;
		speed = CHARACTER_SPEEDS[mode];
	}
}
/// Start running if walking, or start walking if running
void echo_char::toggle_run()
{
	if(mode == RUN)
		start_step();
	else
		start_run();
}
/** Reinitializes the grid; spawns on g1
 * @param g1 The new initial grid on which to spawn
 */
void echo_char::init(grid* g1)
{
	/// Set our spawn position
	start = g1;
	
	/// The character starts by walking, not running.
	is_running = 0;
	/// The character is unpaused when started
	paused = 0;
	
	/** Prime the character to land on g1, but not actually change_speed,
	 * because that'll result in the character walking on that grid without
	 * landing there first.	
	 */
	land(g1, false);
	/// Initialize the landing sequence
	initialize_fall_from_sky();
}
/** Makes the character land on the grid
 * @param g1 Where to land on
 * @param do_change_speed Should this grid change the 
 */
void echo_char::land(grid* g1, int do_change_speed)
{
	/// Is the place this character has just landed on a goal?
	check_goal(g1);
	/// Set its first grid to g1
	grid1 = g1;
	/// Set its next grid to g1's next if we can, or just NULL
	grid2 = g1 ? grid1->get_next(echo_ns::angle, grid1) : NULL;
	/// Assume that the character is in Grid Mode, and clear fall_position
	if(fall_position != NULL)
		delete fall_position;
	fall_position = NULL;
	
	/// The character hasn't started walking yet
	grid1per = 1;
	
	/// Default distance between grids; the character will calculate this if needed.
	dist = 1;
	
	/// Reset the walking distances
	dist_traveled = 0;
	dist_traveled_cyclic = 0;
	
	/// Initialize the joint values by setting them to zero.
	reset_joints(&joints);
	
	/// Only change the speed if this function is told so
	if(do_change_speed == true)
	{
		change_speed();
	}
}
/// Pause if running, or unpause if paused
void echo_char::toggle_pause()
{
	paused = !paused;
}
/// Respawns; same as init(start); 
void echo_char::reset()
{
	init(start);
}
/** Checks if the grid given is a goal, and if it is, the character
 * will toggle the goal, set it as its new spawn spot, and add to the goal count
 * @param g Grid to check
 */
void echo_char::check_goal(grid* g)
{
	/// Is the grid given a goal?
	if(g->is_goal(echo_ns::angle))
	{
		/// Set our next spawn spot at that goal
		start = g;
		/// Toggle the goal
		g->toggle_goal(echo_ns::angle);
		/// Increment the character's goal count
		num_goals++;
	}
}

/// Forces the character to go the next grid (and trigger the goal there, if any)
void echo_char::next_grid()
{
	///Only works if there is a grid2 that can tell us our next grid
	if(grid2 != NULL)
	{
		/// Check if the grid this character just arrived at is a goal
		check_goal(grid2);
		
		/// Clear fly_direction
		if(fly_direction != NULL)
			delete fly_direction;
		/// Save the direction in case grid2 is a launcher
		fly_direction = get_direction();
		
		/// Save the pointer to grid2
		grid* temp = grid2;
		/// Get the next-next grid, and store that into grid2
		grid2 = grid2->get_next(echo_ns::angle, grid1);
		/// Store the next grid (was grid2) into grid1
		grid1 = temp;
		/// Adjust the speed/mode as needed
		change_speed();
	}
	
	/// Reset the percentage
	grid1per = 1;
}

/// Take one step in animation and movement; call each frame
void echo_char::step()
{
	/// Set the color to white
	gfx_color3f(1, 1, 1);
	/// If the character is (re)spawning...
	if(mode == FALL_FROM_SKY)
	{
		/// Draw the fall_position (which is absolute in this case), even if the character is paused
		DRAW_VEC(fall_position);
		if(!paused)
		{
			/// Fall by decreasing the y
			fall_position->y += speed * WAIT / 1000;
			/// The character is accelerating
			speed -= ACCEL * WAIT / 1000;
			/// If the character is below the target...
			if(fall_position->y < target_y)
			{
				/// ...and if the character's target is a hole...
				if(typeid(*grid1) == typeid(hole))
					/// ...fall into the hole so that there isn't an weird and unnecessary shift between fall_position and grid1's position.
					initialize_falling(fall_position);
				
				/// ...and if the character's target is not a hole...
				else
					/// ...just land on it.
					land(grid1, true);
			}
		}
	}
	/// If the character fell through a hole...
	else if(mode == FALL)
	{
		/// Get the abolute position from the relative position stored inside fall_position
		vector3f* absolute_pos = fall_position->rotate_xy(echo_ns::angle);
		/// Draw it
		DRAW_VEC(absolute_pos);
		if(!paused)
		{
			/// If the character fell off the stage (defined as 5 units lower than the lowest level)...
			if(absolute_pos->y < echo_ns::get_lowest_level() - 5)
			{
				/// Reset
				reset();
			}
			/// If not...
			else
			{
				/// Get the character's next position; same as the current absolute position, but moved downwards
				vector3f* next_absolute_pos = new vector3f(absolute_pos->x,
									absolute_pos->y + speed * WAIT / 1000,
									absolute_pos->z);
				
				
				/// Checking for grids to fall on
				
				/** Get the projected equivalents of the absolute grids;
				 * it's neg_rotate_xy because that's what the display function in main does
				 */
				vector3f* p1 = absolute_pos->neg_rotate_xy(echo_ns::angle);
				vector3f* p2 = next_absolute_pos->neg_rotate_xy(echo_ns::angle);
				/// Get the fall_grid, if any
				grid* fall_grid = echo_ns::current_stage->get_grid_intersection(p1, p2, echo_ns::angle);
				/// Clean up
				delete p1;
				delete p2;
				
				/** If there is a grid to fall on and it isn't a hole
				 * (otherwise, the character will keep falling through the same hole)
				 */
				if(fall_grid != NULL && typeid(*fall_grid) != typeid(hole))
				{
					/// Clear fall_position (don't need to check for NULL, because it'll be too slow, and it won't happen)
					delete fall_position;
					fall_position = NULL;
					/// Land on that grid
					land(fall_grid, true);
				}
				else 
				{
					/// Accelerate
					speed -= ACCEL * WAIT / 1000;
					/// Clear fall_position (don't need to check for NULL, because it'll be too slow, and it won't happen)
					delete fall_position;
					/// Get the next fall_position by rotating the next absolute position back
					fall_position = next_absolute_pos->neg_rotate_yx(echo_ns::angle);
				}
				/// Clean up
				delete next_absolute_pos;
			}
		}
		/// Clean up
		delete absolute_pos;
	}
	/// If the character was launched...
	else if(mode == LAUNCH)
	{
		/// Get the abolute position from the relative position stored inside fall_position
		vector3f* absolute_pos = fall_position->rotate_xy(echo_ns::angle);
		/// Draw it
		DRAW_VEC(absolute_pos);
		if(!paused)
		{
			/// If the character fell off the stage (defined as 5 units lower than the lowest level)...
			if(absolute_pos->y < echo_ns::get_lowest_level() - 5)
			{
				/// Reset
				reset();
			}
			/// If not...
			else
			{
				/// Get the character's next position; same as the current absolute position, but moved downwards
				vector3f* next_absolute_pos = new vector3f(absolute_pos->x + x_speed * WAIT / 1000,
									absolute_pos->y + speed * WAIT / 1000,
									absolute_pos->z + z_speed * WAIT / 1000);
				
				
				grid* fall_grid = NULL;
				
				/// Check only if we're falling
				if(speed < 0)
				{
					/// Checking for grids to fall on
					
					/** Get the projected equivalents of the absolute grids;
					 * it's neg_rotate_xy because that's what the display function in main does
					 */
					vector3f* p1 = absolute_pos->neg_rotate_xy(echo_ns::angle);
					vector3f* p2 = next_absolute_pos->neg_rotate_xy(echo_ns::angle);
					/// Get the fall_grid, if any
					fall_grid = echo_ns::current_stage->get_grid_intersection(p1, p2, echo_ns::angle);
					/// Clean up
					delete p1;
					delete p2;
				}
				
				/** If there is a grid to fall on and it isn't a hole
				 * (otherwise, the character will keep falling through the same hole)
				 */
				if(fall_grid != NULL && typeid(*fall_grid) != typeid(hole))
				{
					/// Clear fall_position (don't need to check for NULL, because it'll be too slow, and it won't happen)
					delete fall_position;
					fall_position = NULL;
					/// Land on that grid
					land(fall_grid, true);
				}
				else 
				{
					/// Accelerate
					speed -= ACCEL * WAIT / 1000;
					/// Clear fall_position (don't need to check for NULL, because it'll be too slow, and it won't happen)
					delete fall_position;
					/// Get the next fall_position by rotating the next absolute position back
					fall_position = next_absolute_pos->neg_rotate_yx(echo_ns::angle);
				}
				/// Clean up
				delete next_absolute_pos;
			}
		}
		/// Clean up
		delete absolute_pos;
	}
	/// Else, it's just Grid Mode (requires both grids)
	else if(grid1 != NULL)
	{
		/// If we also have a second grid to walk/run across...
		if(grid2 != NULL)
		{
			/// Get the positions of the grids...
			grid_info_t* i1 = grid1->get_info(echo_ns::angle);
			if(i1 != NULL)
			{
				vector3f* pos1 = i1->pos;
				grid_info_t* i2 = grid2->get_info(echo_ns::angle);
				if(i2 != NULL)
				{
					vector3f* pos2 = i2->pos;
					/// If the character is not paused...
					if(!paused)
					{
						/// Step through the animation cycle
						dist_traveled += speed * 2;		/// Slightly inflated
						dist_traveled_cyclic += speed * 180;
						/// Cycle back if the variables have reached the end
						if(dist_traveled_cyclic > 360)
						{
							dist_traveled -= 4;	
							dist_traveled_cyclic -= 360;
						}
						/// Cache the distance between grids
						dist = pos1->dist(pos2);
						/** Make the walking slightly more realistic by having the character accelerate/decelerate 
						 * This particular walk cycle is similar to one here:
						 * http://www.idleworm.com/how/anm/02w/walk1.shtml
						 */
						if(dist_traveled > 0.5f && dist_traveled <= 1)
							grid1per -= (1 + 1 * echo_cos(90 * dist_traveled - 22.5f)) * speed / dist;
						else if(dist_traveled > 2.5f && dist_traveled <= 3)
							grid1per -= (1 + 1 * echo_cos(90 * dist_traveled + 67.5f)) * speed / dist;
						else
							grid1per -= speed / dist;
						
						/// If the character reached the end of its walk cycle
						if(grid1per <= 0)
						{
							/// Go on to the next grid
							next_grid();
							/// Shift the positions over
							pos1 = pos2;
							/// Get the new grid2's position
							if(grid2 != NULL)
							{
								i2 = grid2->get_info(echo_ns::angle);
								if(i2 != NULL)
								{
									pos2 = i2->pos;
								}
							}
						}
					}
					/// Draw the character at a weighted average of the positions
					draw(pos1->x * grid1per + pos2->x * (1 - grid1per),
							pos1->y * grid1per + pos2->y * (1 - grid1per),
							pos1->z * grid1per + pos2->z * (1 - grid1per));
				}
				/// If grid2's position could not be acquired...
				else
					/// Just draw the character at grid1
					DRAW_VEC(i1->pos);
			}
		}
		/// If there isn't a second grid...
		else
		{
			/// Attempt to acquired one (perhaps grid1 shifted an esc over?)
			grid2 = grid1->get_next(echo_ns::angle, grid1);
			/// If there still isn't a second grid...
			if(grid2 == NULL)
			{
				/// Just draw the character at grid1
				grid_info_t* i1 = grid1->get_info(echo_ns::angle);
				if(i1 != NULL)
					DRAW_VEC(i1->pos);
			}
			/// Else, we need to change speed and step again (hopefully no recursion stuff...?)
			else
			{
				change_speed();
				step();
			}
		}
	}
}
/** Get the current direction of the character.
 * @return The current direction of the character, or grid2's position - grid1's
 */
vector3f* echo_char::get_direction()
{
	/// If the character has both grids...
	if(grid1 != NULL && grid2 != NULL)
	{
		/// Get grid1's position
		grid_info_t* i1 = grid1->get_info(echo_ns::angle);
		if(i1 != NULL)
		{
			/// Get grid2's position
			grid_info_t* i2 = grid2->get_info(echo_ns::angle);
			if(i2 != NULL)
				/// Return their subtraction
				return(*(i2->pos) - i1->pos);
		}
	}
	/// Failed
	return(NULL);
}
/** Draws the character at (x,y,z)
 * @param x X-coordinate of the character
 * @param y Y-coordinate of the character
 * @param z Z-coordinate of the character
 */
void echo_char::draw(float x, float y, float z)
{
	/// Push a matrix so the following operations won't screw up the rotation matrix
	gfx_push_matrix();
	{
		if(mode == RUN || mode == STEP)
		{
			grid_mode_joints(y);
		}
		else if(mode == LANDING)
		{
			landing_mode_joints();
		}
		else if(mode == STANDING_UP)
		{
			standing_up_joints();
		}
		else
		{
			falling_mode_joints();
		}
		/// Actually translate the character to the position...
		gfx_translatef(x, y, z);
		/// But before actually drawing the character, rotate the character so that it faces where it goes
		if(mode == LAUNCH)
		{
			gfx_rotatef(90 - TO_DEG(atan2(fly_direction->z, fly_direction->x))
				, 0, 1, 0);
		}
		else if(grid1 != NULL && grid2 != NULL)
		{
			grid_info_t* i1 = grid1->get_info(echo_ns::angle);
			if(i1 != NULL)
			{
				grid_info_t* i2 = grid2->get_info(echo_ns::angle);
				if(i2 != NULL)
				{
					gfx_rotatef(90 - TO_DEG(atan2(i2->pos->z - i1->pos->z, i2->pos->x - i1->pos->x))
						, 0, 1, 0);
				}
			}
		}
#ifndef ECHO_NDS
		/// Need to draw the character twice for the outline
		gfx_outline_start();
		draw_character(&joints);
		gfx_outline_mid();
		draw_character(&joints);
		gfx_outline_end();
#else
		/// draw_character already sets the polyIDs, so no need to draw twice
		draw_character(&joints);
#endif
	}
	/// Pop the "tainted" matrix
	gfx_pop_matrix();
}
/** Returns the "speed" variable (see speed attribute)
 * @return "speed" (see speed attribute)
 */
float echo_char::get_speed()
{
	return(speed);
}
/// Initializes the joints for falling mode
void echo_char::initialize_falling_mode()
{
	reset_joints(&joints);
	
	joints.body_pitch = 70;
	joints.waist_bow = 10;
	joints.rshoulder_flap = -75;
	joints.lshoulder_flap = 75;
	joints.rshoulder_push = -45;
	joints.lshoulder_push = 45;
	joints.rarm_twist = 45;
	joints.larm_twist = 45;
}
/// Calculate joint values for a character in the air (Falling Mode)
void echo_char::falling_mode_joints()
{
	static float rotation = 0;
	
	joints.body_turn = 30 * echo_sin(rotation);
	joints.lshoulder_swing = rotation;
	joints.rshoulder_swing = -rotation;
	joints.rarm_bend = 45 * echo_sin(rotation / 2);
	joints.larm_bend = joints.rarm_bend;
	joints.lthigh_lift = 45 * echo_sin(rotation);
	joints.rthigh_lift = -joints.lthigh_lift;
	joints.lleg_bend = 30 * echo_sin(rotation) + 30;
	joints.rleg_bend = 30 * echo_sin(rotation + 90) + 30;
	
	rotation += 10;
	if(rotation > 360)
		rotation = 0;
}
/// Joint calculation for a character just landing
void echo_char::landing_mode_joints()
{
}
/// Joint calculation for a character standing up right after a landing
void echo_char::standing_up_joints()
{
}
/// Step through joint calculations for walking (used in Grid Mode)
void echo_char::grid_mode_joints(float y)
{
	/// Shift the joints (should probably LERP these suckers)
	joints.rshoulder_swing = -20 * echo_sin(dist_traveled_cyclic);
	joints.lshoulder_swing = 20 * echo_sin(dist_traveled_cyclic);
	joints.rarm_bend = -10 * echo_sin(dist_traveled_cyclic) - 20;
	joints.larm_bend = 10 * echo_sin(dist_traveled_cyclic) - 20;
	joints.rthigh_lift = 35 * echo_sin(dist_traveled_cyclic) - 15;
	joints.lthigh_lift = -35 * echo_sin(dist_traveled_cyclic) - 15;
	
	/// main_grid is the grid the character is on right now (if it's just two normal grids) 
	grid* main_grid = NULL;
	/// main_per is the percentage into the grid on the interval of [0.5, 1)
	float main_per = 0;
	if(grid1 != NULL && grid1per >= 0.5f)
	{
		main_per = grid1per;
		main_grid = grid1;
	}
	else if(grid2 != NULL && grid1per <= 0.5f)
	{
		main_per = 1 - grid1per;
		main_grid = grid2;
	}
	/// If there is a main_grid...
	if(main_grid != NULL)
	{
		/// ...then there is a vertical shift, which is a cos function
		float vshift = 0.05f * echo_cos(360 * main_per) - 0.05f;
		/// Shift y up
		y += vshift;
		/// rdf is the distance from hip to right leg
		float right_dist_foot = 0;
		/// ldf is the distance from hip to left leg
		float left_dist_foot = 0;
		/// Need to know which direction the character is moving in...
		vector3f* foot_vec = get_direction();
		if(foot_vec != NULL)
		{
			/// Get the angle the direction vector has with the up vector <0, 1, 0>
			const float dir_angle = foot_vec->scalar_angle_with_up();
			/// Get the approximate distance the right leg has with the ground (0.825f is arbitrary)
			right_dist_foot = (vshift + 0.825f) * echo_sin(abs(joints.rthigh_lift)) 
										/ echo_sin(dir_angle);
			/// Do the same things with the right leg
			left_dist_foot = (vshift + 0.825f) * echo_sin(abs(joints.lthigh_lift)) 
										/ echo_sin(dir_angle);
		}
		/// If the character can't figure out the direction it's going, just use assume that the ground is flat
		else
		{
			right_dist_foot = (vshift + 1.175f) / echo_cos(joints.rthigh_lift);
			left_dist_foot = (vshift + 1.175f) / echo_cos(joints.lthigh_lift);
		}
		
		/// Get the results, mod it under 90
		const float rtemp = fmod(IK_angle(0.5f, 0.65f, right_dist_foot), 90);
		/// If the result is not 0 and not NaN, save it
		if(rtemp != 0 && rtemp == rtemp)
			joints.rleg_bend = rtemp;
		
		/// Do the same thing with left leg
		const float ltemp = fmod(IK_angle(0.5f, 0.65f, left_dist_foot), 90);
		if(ltemp != 0 && ltemp == ltemp)
			joints.lleg_bend = ltemp;
		delete foot_vec;
	}
}
