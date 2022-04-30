/*******************************************************************************
 * This file is part of SWIFT.
 * Copyright (c) 2021 Mladen Ivkovic (mladen.ivkovic@hotmail.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ******************************************************************************/
#ifndef SWIFT_RT_DEBUGGING_DEBUG_H
#define SWIFT_RT_DEBUGGING_DEBUG_H

#include "active.h"
#include "rt_properties.h"

/**
 * @file src/rt/debug/rt_debugging.h
 * @brief Main header file for the debug radiative transfer scheme
 * extra debugging functions.
 */

/**
 * @brief This resets particle carried quantities after each subcycling
 * step such that the internal checks are still consistent.
 * @param p the particle to work on
 */
__attribute__((always_inline)) INLINE static void rt_debugging_count_subcycle(
    struct part *restrict p) {
  if (p->id == PROBLEM_ID) message("Inc subcycle count %lld", p->id);
  p->rt_data.debug_nsubcycles += 1;
}

__attribute__((always_inline)) INLINE static void
rt_debugging_check_nr_subcycles(struct part *restrict p) {
  /* skip initialization */
  if (p->time_bin == 0) return;

  /* TODO: this check may fail when running with limiter/sync. */

  int bindiff = p->time_bin - p->rt_data.time_bin;
  int subcycles_expect = 1;
  for (int b = 0; b < bindiff; b++) subcycles_expect *= 2;
  if (p->rt_data.debug_nsubcycles != subcycles_expect)
    error(
        "Particle %lld didn't do the expected amount of subcycles: Expected "
        "%d, done %d; time bins %d RT: %d",
        p->id, subcycles_expect, p->rt_data.debug_nsubcycles, p->time_bin,
        p->rt_data.time_bin);
}

/**
 * @brief This resets particle carried quantities after each subcycling
 * step such that the internal checks are still consistent.
 * @param p the particle to work on
 */
__attribute__((always_inline)) INLINE static void
rt_debugging_reset_each_subcycle(struct part *restrict p) {
  p->rt_data.debug_calls_iact_gradient_interaction = 0;
  p->rt_data.debug_calls_iact_transport_interaction = 0;

  p->rt_data.debug_injection_done = 0;
  p->rt_data.debug_gradients_done = 0;
  p->rt_data.debug_transport_done = 0;
  p->rt_data.debug_thermochem_done = 0;
}

/**
 * @brief Debugging checks loop over all star particles after each time step
 */
static void rt_debugging_end_of_step_stars_mapper(void *restrict map_data,
                                                  int scount,
                                                  void *restrict extra_data) {

  struct spart *restrict sparts = (struct spart *)map_data;
  const struct engine *restrict e = (struct engine *)extra_data;

  unsigned long long emission_sum_this_step = 0ULL;
  unsigned long long emission_sum_tot = 0ULL;

  for (int k = 0; k < scount; k++) {

    struct spart *restrict sp = &sparts[k];
    emission_sum_this_step += sp->rt_data.debug_iact_hydro_inject;
    emission_sum_tot += sp->rt_data.debug_radiation_emitted_tot;
    /* Reset all values here in case stars won't be active next step */
    sp->rt_data.debug_iact_hydro_inject = 0;
    sp->rt_data.debug_iact_hydro_inject_prep = 0;
  }

  atomic_add(&e->rt_props->debug_radiation_emitted_this_step,
             emission_sum_this_step);
  atomic_add(&e->rt_props->debug_radiation_emitted_tot, emission_sum_tot);
}

/**
 * @brief Debugging checks loop over all hydro particles after each time step
 */
static void rt_debugging_end_of_step_hydro_mapper(void *restrict map_data,
                                                  int count,
                                                  void *restrict extra_data) {

  struct part *restrict parts = (struct part *)map_data;
  const struct engine *restrict e = (struct engine *)extra_data;

  unsigned long long absorption_sum_this_step = 0ULL;
  unsigned long long absorption_sum_tot = 0ULL;

  for (int k = 0; k < count; k++) {

    struct part *restrict p = &parts[k];
    absorption_sum_this_step += p->rt_data.debug_iact_stars_inject;
    absorption_sum_tot += p->rt_data.debug_radiation_absorbed_tot;

    /* Reset all values here in case particles won't be active next step */
    p->rt_data.debug_iact_stars_inject = 0;
    p->rt_data.debug_drifted = 0;
  }

  atomic_add(&e->rt_props->debug_radiation_absorbed_this_step,
             absorption_sum_this_step);
  atomic_add(&e->rt_props->debug_radiation_absorbed_tot, absorption_sum_tot);
}

/**
 * @brief Debugging checks loop over all hydro particles after each time step
 */
static void rt_debugging_start_of_step_hydro_mapper(void *restrict map_data,
                                                    int count,
                                                    void *restrict extra_data) {

  struct part *restrict parts = (struct part *)map_data;
  const struct engine *restrict e = (struct engine *)extra_data;

  for (int k = 0; k < count; k++) {

    struct part *restrict p = &parts[k];
    p->rt_data.debug_hydro_active = part_is_active(p, e);
    p->rt_data.debug_rt_active_on_main_step = part_is_rt_active(p, e);
    p->rt_data.debug_rt_zeroth_cycle_on_main_step =
        part_is_rt_active(p, e) && part_is_active(p, e);
    /* Can't check for subcycle = 0 here, it hasn't been reset yet */
    /* if (!p->rt_data.debug_rt_zeroth_cycle_on_main_step) */
    /*     message("??? %d %d %d", part_is_active(p, e), part_is_rt_active(p,
     * e), p->rt_data.debug_nsubcycles); */
    if (p->id == PROBLEM_ID)
      message("Testing part %lld - HA %d RA %d SC %d PTB %d RTTB %d", p->id,
              part_is_active(p, e), part_is_rt_active(p, e),
              p->rt_data.debug_nsubcycles, p->time_bin, p->rt_data.time_bin);
  }
}

/**
 * @brief Do some checks and set necessary flags before each (main) step is
 * taken.
 *
 * @param e The #engine.
 * @param verbose Are we talkative?
 */
__attribute__((always_inline)) INLINE static void
rt_debugging_checks_start_of_step(struct engine *e, int verbose) {

  struct space *s = e->s;
  if (!(e->policy & engine_policy_rt)) return;

  const ticks tic = getticks();

  /* hydro particle loop */
  if (s->nr_parts > 0)
    threadpool_map(&e->threadpool, rt_debugging_start_of_step_hydro_mapper,
                   s->parts, s->nr_parts, sizeof(struct part),
                   threadpool_auto_chunk_size, /*extra_data=*/e);

  /* star particle loop */
  /* if (s->nr_sparts > 0) */
  /*   threadpool_map(&e->threadpool, rt_debugging_start_of_step_stars_mapper,
   */
  /*                  s->sparts, s->nr_sparts, sizeof(struct spart), */
  /*                  threadpool_auto_chunk_size, [>extra_data=<]e); */

  if (verbose)
    message("took %.3f %s.", clocks_from_ticks(getticks() - tic),
            clocks_getunit());
}

/**
 * @brief At the end of each time step, loop over both hydro and star
 * particles and do whatever checks for this particular time step you
 * want done.
 *
 * @param e The #engine.
 * @param verbose Are we talkative?
 */
__attribute__((always_inline)) INLINE static void
rt_debugging_checks_end_of_step(struct engine *e, int verbose) {

  struct space *s = e->s;
  if (!(e->policy & engine_policy_rt)) return;

#ifdef WITH_MPI
  /* Since we aren't sending data back, none of these checks will
   * pass a run over MPI. */
  return;
#endif

  const ticks tic = getticks();

  /* reset values before the particle loops.
   * reset total counts as well. We track the totals since the beginning
   * of time in particles individually. */
  e->rt_props->debug_radiation_emitted_this_step = 0ULL;
  e->rt_props->debug_radiation_absorbed_this_step = 0ULL;
  e->rt_props->debug_radiation_emitted_tot = 0ULL;
  e->rt_props->debug_radiation_absorbed_tot = 0ULL;

  /* hydro particle loop */
  if (s->nr_parts > 0)
    threadpool_map(&e->threadpool, rt_debugging_end_of_step_hydro_mapper,
                   s->parts, s->nr_parts, sizeof(struct part),
                   threadpool_auto_chunk_size, /*extra_data=*/e);

  /* star particle loop */
  if (s->nr_sparts > 0)
    threadpool_map(&e->threadpool, rt_debugging_end_of_step_stars_mapper,
                   s->sparts, s->nr_sparts, sizeof(struct spart),
                   threadpool_auto_chunk_size, /*extra_data=*/e);

  /* Have we accidentally invented or deleted some radiation somewhere? */

  if ((e->rt_props->debug_radiation_emitted_this_step !=
       e->rt_props->debug_radiation_absorbed_this_step) ||
      (e->rt_props->debug_radiation_emitted_tot !=
       e->rt_props->debug_radiation_absorbed_tot))
    error(
        "Emitted and absorbed radiation vary.\n"
        "  This step: star emission %12lld; gas absorption %12lld\n"
        "Since start: star emission %12lld; gas absorption %12lld",
        e->rt_props->debug_radiation_emitted_this_step,
        e->rt_props->debug_radiation_absorbed_this_step,
        e->rt_props->debug_radiation_emitted_tot,
        e->rt_props->debug_radiation_absorbed_tot);

  if (verbose)
    message("took %.3f %s.", clocks_from_ticks(getticks() - tic),
            clocks_getunit());
}

/**
 * @brief Perform a series of consistency and sanity checks.
 *
 * @param p particle to check
 * @param loc location where this is called from. This determines which checks
 * will be done:
 *
 * 0: during kicks/after drifts.
 * 1: during rt_ghost1/finalise_injection / after kicks.
 * 2: during gradients / after injection.
 * 3: during transport / after gradients.
 * 4: during thermochem / after transport.
 * 5: after thermochem.
 *
 * @param function_name: Function name (or message) you want printed on error.
 */
__attribute__((always_inline)) INLINE static void rt_debug_sequence_check(
    struct part *restrict p, int loc, const char *function_name) {

  /* Have we been drifted? */
  if (p->rt_data.debug_drifted != 1 && loc != 1)
    /* The only place where we don't need to be kicked first is the
     * ghost1 (finalise injection) step, so skip the test there. */
    error("called %s on particle %lld with wrong drift count=%d", function_name,
          p->id, p->rt_data.debug_drifted);

  if (loc > 0) {
    /* Are kicks done? */

    /* For the kick check, we have following possible scenarios:
     *
     * Legend:
     *  TS: timestep task
     *  K1, K2: kick1, kick 2
     *  RT0, RT1, ... : N-th RT subcycle.
     *  H: hydro tasks. this resets the counter.
     * Top row is task execution sequence. Bottom row is how the kick counter
     * behaves.
     *
     * 1) part is hydro active, and remains hydro active after TS
     *   H -> K2 -> RT 0 -> TS -> K1 -> RT 1 -> RT 2 ...
     *   0     1       1     1     2       2       2
     * 2) part is hydro active, and becomes hydro inactive after TS.
     *    Kick1 still gets called, because part_is_starting = 1
     *   H -> K2 -> RT 0 -> TS -> K1 -> RT 1 -> RT 2 ... |
     *   0     1       1     1     2       2       2 ... |
     * 3) part is hydro inactive, and remains hydro inactive
     *    we pick up where 2 left off, and the counter doesn't change:
     *   RT X -> TS -> RT X+1 -> RT X+2 ...
     *      2     2         2         2
     * 4) part is hydro inactive, and becomes active
     *    Kick1 doesn't increase the counter because part_is_starting = 0
     *   RT X -> TS -> K1 -> RT X+1 -> RT X+2 ... | H -> K2 -> RT 0 -> ...
     *      2     2     2         2         2       0     1       1
     *            ^-- becomes active here
     * 5) Particle is hydro active, isn't radioactive after hydro, but becomes
     *    radioactive during a subcycle. I.e. the zeroth subcycle does not
     *    happen right after the kick2.
     *  H -> K2 -> TS -> K1 | -> RT0 -> RT1 -> ...
     *  0 ->  1 ->  1 ->  2 | ->   2 ->   2 -> ...
     */
    if (p->rt_data.debug_nsubcycles == 0) {
      if (p->rt_data.debug_rt_zeroth_cycle_on_main_step) {
        /* This covers case 1 & 2 */
        if (p->rt_data.debug_kicked != 1)
          error(
              "called %s on particle %lld with wrong kick count=%d (expected "
              "1) cycle=%d",
              function_name, p->id, p->rt_data.debug_kicked,
              p->rt_data.debug_nsubcycles);
      } else {
        /* This covers case 5 */
        if (p->rt_data.debug_kicked != 2)
          error(
              "called %s on particle %lld with wrong kick count=%d (expected "
              "2) cycle=%d",
              function_name, p->id, p->rt_data.debug_kicked,
              p->rt_data.debug_nsubcycles);
      }
    } else if (p->rt_data.debug_nsubcycles > 0) {
      /* This covers case 1, 2, 3, 4, 5 */
      if (p->rt_data.debug_kicked != 2)
        error(
            "called %s on particle %lld with wrong kick count=%d (expected 2) "
            "cycle=%d",
            function_name, p->id, p->rt_data.debug_kicked,
            p->rt_data.debug_nsubcycles);
    } else {
      error("Got negative subcycle???");
    }
  }

  if (loc > 1) {
    /* is injection done? */
    if (p->rt_data.debug_injection_done != 1)
      error(
          "Trying to do %s when finalise injection count is "
          "%d ID %lld",
          function_name, p->rt_data.debug_injection_done, p->id);
  }

  if (loc > 2) {
    /* are gradients done? */
    if (p->rt_data.debug_gradients_done != 1)
      error(
          "Trying to do %s on particle %lld when "
          "gradients_done count is %d",
          function_name, p->id, p->rt_data.debug_gradients_done);
  }

  if (loc > 3) {
    /* is transport done? */
    if (p->rt_data.debug_transport_done != 1)
      error(
          "Part %lld trying to do thermochemistry when transport_done != 1: %d",
          p->id, p->rt_data.debug_transport_done);
  }

  if (loc > 4) {
    /* is thermochemistry done? */
    if (p->rt_data.debug_thermochem_done != 1)
      error(
          "Trying to do rescheduling on particle %lld with with "
          "thermochem_done count=%d",
          p->id, p->rt_data.debug_thermochem_done);
  }
}

#endif /* SWIFT_RT_DEBUGGING_DEBUG_H */
