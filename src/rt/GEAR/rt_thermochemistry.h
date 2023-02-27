/*******************************************************************************
 * This file is part of SWIFT.
 * Copyright (c) 2020 Mladen Ivkovic (mladen.ivkovic@hotmail.com)
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
#ifndef SWIFT_RT_GEAR_THERMOCHEMISTRY_H
#define SWIFT_RT_GEAR_THERMOCHEMISTRY_H

#include "rt_grackle_utils.h"
#include "rt_interaction_cross_sections.h"
#include "rt_interaction_rates.h"
#include "rt_ionization_equilibrium.h"

/**
 * @file src/rt/GEAR/rt_thermochemistry.h
 * @brief Main header file for the GEAR M1 closure radiative transfer scheme
 * thermochemistry related functions.
 */

/**
 * @brief initialize particle quantities relevant for the thermochemistry.
 *
 * @param p part to work with
 * @param rt_props rt_properties struct
 * @param hydro_props hydro properties struct
 * @param phys_const physical constants struct
 * @param us unit system struct
 * @param cosmo cosmology struct
 */
__attribute__((always_inline)) INLINE static void rt_tchem_first_init_part(
    struct part* restrict p, const struct rt_props* rt_props,
    const struct hydro_props* hydro_props,
    const struct phys_const* restrict phys_const,
    const struct unit_system* restrict us,
    const struct cosmology* restrict cosmo) {

  if (rt_props->set_equilibrium_initial_ionization_mass_fractions) {
    float XHI, XHII, XHeI, XHeII, XHeIII;
    rt_ion_equil_get_mass_fractions(&XHI, &XHII, &XHeI, &XHeII, &XHeIII, p,
                                    rt_props, hydro_props, phys_const, us,
                                    cosmo);
    p->rt_data.tchem.mass_fraction_HI = XHI;
    p->rt_data.tchem.mass_fraction_HII = XHII;
    p->rt_data.tchem.mass_fraction_HeI = XHeI;
    p->rt_data.tchem.mass_fraction_HeII = XHeII;
    p->rt_data.tchem.mass_fraction_HeIII = XHeIII;
  } else if (rt_props->set_initial_ionization_mass_fractions) {
    p->rt_data.tchem.mass_fraction_HI = rt_props->mass_fraction_HI_init;
    p->rt_data.tchem.mass_fraction_HII = rt_props->mass_fraction_HII_init;
    p->rt_data.tchem.mass_fraction_HeI = rt_props->mass_fraction_HeI_init;
    p->rt_data.tchem.mass_fraction_HeII = rt_props->mass_fraction_HeII_init;
    p->rt_data.tchem.mass_fraction_HeIII = rt_props->mass_fraction_HeIII_init;
  }

  /* Check that we didn't do something stupid */
  rt_check_unphysical_mass_fractions(p);

  /* Check that the Hydrogen and Helium mass fractions correspond to those
   * provided by the user in the parameter file. This mass fraction is also
   * passed down to grackle internally, so it is error-prone if left
   * unchecked. */
  const float mH =
      p->rt_data.tchem.mass_fraction_HI + p->rt_data.tchem.mass_fraction_HII;
  if (fabsf(mH - rt_props->hydrogen_mass_fraction) > 1e-4)
    error("Got wrong Hydrogen mass fraction: Got =%.6f provided in yml =%.6f",
          mH, rt_props->hydrogen_mass_fraction);
  const float mHe = p->rt_data.tchem.mass_fraction_HeI +
                    p->rt_data.tchem.mass_fraction_HeII +
                    p->rt_data.tchem.mass_fraction_HeIII;
  if (fabsf(mHe - rt_props->helium_mass_fraction) > 1e-4)
    error("Got wrong Helium mass fraction: Got =%.6f provided in yml =%.6f",
          mHe, rt_props->helium_mass_fraction);
}

/**
 * @brief compute the heating, ionization, and dissassociation rates
 * for the particle radiation field as needed by grackle, and the
 * net absorption/emission rates for each photon group
 *
 * @param rates (return) Interaction rates for grackle. [0]: heating rate.
 * [1]: HI ionization. [2]: HeI ionization. [3]: HeII ionization.
 * [4]: H2 dissociation.
 * @param heating_rates_by_group (return) net absorption/emission rates of each
 * photon frequency group in internal units.
 * @param p particle to work on
 * @param species_densities the physical densities of all traced species
 * @param rt_props rt_properties struct
 * @param phys_const physical constants struct
 * @param us internal units struct
 * @param cosmo cosmology struct
 **/
__attribute__((always_inline)) INLINE static void
rt_tchem_get_interaction_rates(gr_float rates[5],
                               float heating_rates_by_group[RT_NGROUPS],
                               const struct part* restrict p,
                               gr_float species_densities[6],
                               const struct rt_props* restrict rt_props,
                               const struct phys_const* restrict phys_const,
                               const struct unit_system* restrict us,
                               const struct cosmology* restrict cosmo) {

  rates[0] = 0.; /* Needs to be in [erg / s / cm^3 / nHI] for grackle. */
  rates[1] = 0.; /* [1 / time_units] */
  rates[2] = 0.; /* [1 / time_units] */
  rates[3] = 0.; /* [1 / time_units] */
  rates[4] = 0.; /* [1 / time_units] */
  for (int group = 0; group < RT_NGROUPS; group++) {
    heating_rates_by_group[group] = 0.;
  }

  /* "copy" ionization energies from cross section parameters */
  struct rt_photoion_cs_parameters cs_params_cgs =
      rt_init_photoion_cs_params_cgs();
  const double* E_ion_cgs = cs_params_cgs.E_ion;

  /* Integrate energy spectra and cross sections assuming blackbody spectra
   * to obtain estimate for effective cross sections, then use the actual
   * energies present to get the rates */
  /* TODO: check whether we shouldn't be using actual speed of light here */
  const double c_cgs = rt_params.reduced_speed_of_light *
                       units_cgs_conversion_factor(us, UNIT_CONV_VELOCITY);
  const double to_erg = units_cgs_conversion_factor(us, UNIT_CONV_ENERGY);

  /* First, get species number densities and number densities
   * in units of neutral hydrogen number density. */
  double m_p = phys_const->const_proton_mass;
  double species_number_densities_cgs[RT_NIONIZING_SPECIES]; /* in cm^-3 */
  double species_number_densities_nHI[RT_NIONIZING_SPECIES]; /* in nHI^-1 */
  const double to_inv_volume =
      units_cgs_conversion_factor(us, UNIT_CONV_INV_VOLUME);
  const double mass_to_number_density_cgs = to_inv_volume / m_p;
  /* neutral hydrogen */
  species_number_densities_cgs[0] =
      species_densities[0] * mass_to_number_density_cgs;
  species_number_densities_nHI[0] = 1.;
  /* neutral helium */
  species_number_densities_cgs[1] =
      0.25 * species_densities[2] * mass_to_number_density_cgs;
  species_number_densities_nHI[1] =
      0.25 * species_densities[2] / species_densities[0];
  /* singly ionized helium */
  species_number_densities_cgs[2] =
      0.25 * species_densities[3] * mass_to_number_density_cgs;
  species_number_densities_nHI[2] =
      0.25 * species_densities[3] / species_densities[0];

  const double inv_time_cgs =
      units_cgs_conversion_factor(us, UNIT_CONV_INV_TIME);

  /* For the grackle photoionization, we need to
   * keep track of the rates for each species.
   * For the heating rate, we need to sum up all species.
   * To remove the correct amount of energy from the
   * radiation fields, we additionally need to keep track
   * of rates from each photon group. */

  /* store photoionization rate for each species here */
  double ionization_rates_by_species[RT_NIONIZING_SPECIES];
  for (int spec = 0; spec < RT_NIONIZING_SPECIES; spec++)
    ionization_rates_by_species[spec] = 0.;

  for (int group = 0; group < RT_NGROUPS; group++) {

    /* Sum results for this group over all species */
    double heating_rate_group_nHI = 0.;
    double heating_rate_group_cgs = 0.;
    float energy_density_i_cgs =
        p->rt_data.radiation[group].energy_density * to_erg * to_inv_volume;

    for (int spec = 0; spec < RT_NIONIZING_SPECIES; spec++) {
      /* Note: the cross sections are in cgs. */
      const double cse = rt_props->energy_weighted_cross_sections[group][spec];
      const double csn = rt_props->number_weighted_cross_sections[group][spec];

      heating_rate_group_nHI +=
          (cse - E_ion_cgs[spec] * csn) * species_number_densities_nHI[spec];
      heating_rate_group_cgs +=
          (cse - E_ion_cgs[spec] * csn) * species_number_densities_cgs[spec];
      ionization_rates_by_species[spec] +=
          energy_density_i_cgs * cse * species_number_densities_cgs[spec] *
          c_cgs / inv_time_cgs; /* internal units T^-1 */
    }

    /* Store total heating rate for grackle */
    rates[0] += heating_rate_group_nHI * c_cgs * energy_density_i_cgs;
    /* Store rates for each group in internal units WITHOUT THE ENERGY DENSITY
     * TERM */
    heating_rates_by_group[group] +=
        heating_rate_group_cgs * c_cgs / inv_time_cgs;
  }

  /* We're done. Write the results in correct place */
  rates[1] = ionization_rates_by_species[0];
  rates[2] = ionization_rates_by_species[1];
  rates[3] = ionization_rates_by_species[2];
}

/**
 * @brief Main function for the thermochemistry step.
 *
 * @param p Particle to work on.
 * @param xp Pointer to the particle' extended data.
 * @param rt_props RT properties struct
 * @param cosmo The current cosmological model.
 * @param hydro_props The #hydro_props.
 * @param phys_const The physical constants in internal units.
 * @param us The internal system of units.
 * @param dt The time-step of this particle.
 * @depth recursion depth
 */
INLINE static void rt_do_thermochemistry(
    struct part* restrict p, struct xpart* restrict xp,
    struct rt_props* rt_props, const struct cosmology* restrict cosmo,
    const struct hydro_props* hydro_props,
    const struct phys_const* restrict phys_const,
    const struct unit_system* restrict us, const double dt, int depth) {
  /* Note: Can't pass rt_props as const struct because of grackle
   * accessinging its properties there */

  /* Nothing to do here? */
  if (rt_props->skip_thermochemistry) return;
  if (dt == 0.) return;

  /* This is where the fun begins */
  /* ---------------------------- */

  /* initialize data so it'll be in scope */
  grackle_field_data particle_grackle_data;

  gr_float density = hydro_get_physical_density(p, cosmo);

  /* In rare cases, unphysical solutions can arise with negative densities
   * which won't be fixed in the hydro part until further down the dependency
   * graph. Also, we can have vacuum, in which case we have nothing to do here.
   * So exit early if that is the case. */
  if (density <= 0.) return;

  const float u_minimal = hydro_props->minimal_internal_energy;
  gr_float internal_energy =
      max(hydro_get_physical_internal_energy(p, xp, cosmo), u_minimal);
  const float u_old = internal_energy;

  gr_float species_densities[6];
  rt_tchem_get_species_densities(p, density, species_densities);

  float radiation_energy_density[RT_NGROUPS];
  rt_part_get_radiation_energy_density(p, radiation_energy_density);

  gr_float iact_rates[5];
  rt_get_interaction_rates_for_grackle(
      iact_rates, radiation_energy_density, species_densities,
      rt_props->average_photon_energy, rt_props->energy_weighted_cross_sections,
      rt_props->number_weighted_cross_sections, phys_const, us);

  /* Put all the data into a grackle field struct */
  rt_get_grackle_particle_fields(&particle_grackle_data, density,
                                 internal_energy, species_densities,
                                 iact_rates);

  /* solve chemistry */
  /* Note: `grackle_rates` is a global variable defined by grackle itself.
   * Using a manually allocd and initialized variable here fails with MPI
   * for some reason. */
  if (local_solve_chemistry(&rt_props->grackle_chemistry_data, &grackle_rates,
                            &rt_props->grackle_units, &particle_grackle_data,
                            dt) == 0)
    error("Error in solve_chemistry.");

  /* copy updated grackle data to particle */
  /* update particle internal energy. Grackle had access by reference
   * to internal_energy */
  internal_energy = particle_grackle_data.internal_energy[0];
  const float u_new = max(internal_energy, u_minimal);

  /* Re-do thermochemistry? */
  if ((rt_props->max_tchem_recursion > depth) &&
      (fabsf(u_old - u_new) > 0.1 * u_old)) {
    /* Note that grackle already has internal "10% rules". But sometimes, they
     * may not suffice. */
    rt_clean_grackle_fields(&particle_grackle_data);
    rt_do_thermochemistry(p, xp, rt_props, cosmo, hydro_props, phys_const, us,
                          0.5 * dt, depth + 1);
    rt_do_thermochemistry(p, xp, rt_props, cosmo, hydro_props, phys_const, us,
                          0.5 * dt, depth + 1);
    return;
  }

  /* If we're good, update the particle data from grackle results */
  hydro_set_internal_energy(p, u_new);

  /* Update mass fractions */
  const gr_float one_over_rho = 1. / density;
  p->rt_data.tchem.mass_fraction_HI =
      particle_grackle_data.HI_density[0] * one_over_rho;
  p->rt_data.tchem.mass_fraction_HII =
      particle_grackle_data.HII_density[0] * one_over_rho;
  p->rt_data.tchem.mass_fraction_HeI =
      particle_grackle_data.HeI_density[0] * one_over_rho;
  p->rt_data.tchem.mass_fraction_HeII =
      particle_grackle_data.HeII_density[0] * one_over_rho;
  p->rt_data.tchem.mass_fraction_HeIII =
      particle_grackle_data.HeIII_density[0] * one_over_rho;

  rt_check_unphysical_mass_fractions(p);

  /* Update radiation fields */
  /* First get absorption rates at the start and the end of the step */
  double absorption_rates[RT_NGROUPS];
  rt_get_absorption_rates(
      absorption_rates, species_densities, rt_props->average_photon_energy,
      rt_props->number_weighted_cross_sections, phys_const, us);

  gr_float species_densities_new[6];
  species_densities_new[0] = particle_grackle_data.HI_density[0];
  species_densities_new[1] = particle_grackle_data.HII_density[0];
  species_densities_new[2] = particle_grackle_data.HeI_density[0];
  species_densities_new[3] = particle_grackle_data.HeII_density[0];
  species_densities_new[4] = particle_grackle_data.HeIII_density[0];
  species_densities_new[5] = particle_grackle_data.e_density[0];
  double absorption_rates_new[RT_NGROUPS];
  rt_get_absorption_rates(absorption_rates_new, species_densities_new,
                          rt_props->average_photon_energy,
                          rt_props->number_weighted_cross_sections, phys_const,
                          us);

  /* Now remove absorbed radiation */
  for (int g = 0; g < RT_NGROUPS; g++) {
    const float E_old = p->rt_data.radiation[g].energy_density;
    double f = dt * 0.5 * (absorption_rates[g] + absorption_rates_new[g]);
    f = min(1., f);
    f = max(0., f);
    p->rt_data.radiation[g].energy_density *= (1. - f);
    for (int i = 0; i < 3; i++) {
      p->rt_data.radiation[g].flux[i] *= (1. - f);
    }

    rt_check_unphysical_state(&p->rt_data.radiation[g].energy_density,
                              p->rt_data.radiation[g].flux, E_old,
                              /*callloc=*/2);
  }

  /* Clean up after yourself. */
  rt_clean_grackle_fields(&particle_grackle_data);
}

/**
 * @brief Main function for the thermochemistry step.
 *
 * @param p Particle to work on.
 * @param xp Pointer to the particle' extended data.
 * @param rt_props RT properties struct
 * @param cosmo The current cosmological model.
 * @param hydro_props The #hydro_props.
 * @param phys_const The physical constants in internal units.
 * @param us The internal system of units.
 */
__attribute__((always_inline)) INLINE static float rt_tchem_get_tchem_time(
    const struct part* restrict p, const struct xpart* restrict xp,
    struct rt_props* rt_props, const struct cosmology* restrict cosmo,
    const struct hydro_props* hydro_props,
    const struct phys_const* restrict phys_const,
    const struct unit_system* restrict us) {
  /* Note: Can't pass rt_props as const struct because of grackle
   * accessinging its properties there */

  /* initialize data so it'll be in scope */
  grackle_field_data particle_grackle_data;

  gr_float density = hydro_get_physical_density(p, cosmo);
  const float u_minimal = hydro_props->minimal_internal_energy;
  gr_float internal_energy =
      max(hydro_get_physical_internal_energy(p, xp, cosmo), u_minimal);

  gr_float species_densities[6];
  rt_tchem_get_species_densities(p, density, species_densities);

  float radiation_energy_density[RT_NGROUPS];
  rt_part_get_radiation_energy_density(p, radiation_energy_density);

  gr_float iact_rates[5];
  rt_get_interaction_rates_for_grackle(
      iact_rates, radiation_energy_density, species_densities,
      rt_props->average_photon_energy, rt_props->energy_weighted_cross_sections,
      rt_props->number_weighted_cross_sections, phys_const, us);

  rt_get_grackle_particle_fields(&particle_grackle_data, density,
                                 internal_energy, species_densities,
                                 iact_rates);

  /* Compute 'cooling' time */
  /* Note: grackle_rates is a global variable defined by grackle itself.
   * Using a manually allocd and initialized variable here fails with MPI
   * for some reason. */
  gr_float tchem_time;
  if (local_calculate_cooling_time(&rt_props->grackle_chemistry_data,
                                   &grackle_rates, &rt_props->grackle_units,
                                   &particle_grackle_data, &tchem_time) == 0)
    error("Error in calculate_cooling_time.");

  /* Clean up after yourself. */
  rt_clean_grackle_fields(&particle_grackle_data);

  return (float)tchem_time;
}
#endif /* SWIFT_RT_GEAR_THERMOCHEMISTRY_H */
