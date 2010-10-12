/**
 * @brief Milliseconds Timer
 *
 * ---------------------------------------------------------------------- 
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * ---------------------------------------------------------------------- 
 *
 * http://msdn.microsoft.com/en-us/library/ms644904%28v=VS.85%29.aspx
 *
 * @author Davide Anastasia, <davide.anastasia@gmail.com>
 *
 */

#ifndef __TIMING_C_H__
#define __TIMING_C_H__

#if defined(_WIN32) || defined(__CYGWIN__)
#define WIN_TIMER
#endif

/*
 * this define enables the timing profile
 * You should comment this define if you want to disable this feature
 */
#define TIMER_PROFILING 

#ifdef TIMER_PROFILING

#include <stdio.h>

// TIMER -----
#ifdef WIN_TIMER
#include <windows.h>
#elif __APPLE__
#include <stdint.h>
#include <mach/mach_time.h>
#else
//#include <ctime>
#include <sys/time.h> 
#endif

class msec_timer {
private:
#ifdef WIN_TIMER
  LARGE_INTEGER start_t;
  LARGE_INTEGER stop_t;
  LARGE_INTEGER freq;
  double        wrk_time;
#elif __APPLE__
  uint64_t start_t;
  uint64_t stop_t;
  uint64_t wrk_time;
  double   conversion;
#else
  timeval start_t;
  timeval stop_t;
  double  wrk_time;
#endif
  
public:
  msec_timer();
  ~msec_timer();
  void start();
  void stop();
  void update();
  void stop_and_update();
  void reset();
  double get_time();
  
  void get_timer_type();
};

double convert_to_gigaflops(double, double);

#endif // TIMER_PROFILING 
#endif // __TIMING_C_H__