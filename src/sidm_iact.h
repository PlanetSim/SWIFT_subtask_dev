/*******************************************************************************
 * This file is part of SWIFT.
 * Coypright (c) 2020 Camila Correa (camila.correa@uva.nl)
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
#ifndef SWIFT_SIDM_IACT_H
#define SWIFT_SIDM_IACT_H

/* Standard headers */
#include <float.h>
#include <unistd.h>
#include <math.h>

/* Local includes. */
#include "sidm.h"
#include "sidm_properties.h"

#include "cosmology.h"
#include "units.h"
#include "random.h"

/**
 * @brief Perform the 'kick' operation on both #gparts
 *
 * @param gpj #gpart
 * @param gpi #gpart
 * @param ti_current Current integer time (for random numbers).
 *
 */
__attribute__((always_inline)) INLINE static void sidm_do_kick(struct gpart *restrict gpj,
                                                               struct gpart *restrict gpi,
                                                               const integertime_t ti_current) {
    
    /* Center of Mass Velocity of interacting particles */
    const double VCM[3] = {(gpi->v_full[0] + gpj->v_full[0])/2.0, (gpi->v_full[1] + gpj->v_full[1])/2.0, (gpi->v_full[2] + gpj->v_full[2])/2.0};
    
    double dw[3] = {gpi->v_full[0] - gpj->v_full[0], gpi->v_full[1] - gpj->v_full[1], gpi->v_full[2] - gpj->v_full[2]};
    double dv2 = dw[0] * dw[0] + dw[1] * dw[1] + dw[2] * dw[2];
    double dv = sqrt(dv2) / 2.0;
    
    /* Direction of kick is randomly chosen */
    
    /* Draw a random number */
    const float rand_theta = random_unit_interval(gpi->id_or_neg_offset, ti_current, random_number_SIDM_theta);
    
    /* Transform to random number in [0, pi] */
    const float theta = 2.f * rand_theta;
    
    /* Random number for other angle */
    const float rand_phi = random_unit_interval(gpj->id_or_neg_offset, ti_current, random_number_SIDM_phi);
    
    /* Transform to random number in [-pi, pi] range */
    const float phi = 2.f * M_PI * rand_phi - M_PI;
    
    /* Randomly oriented unit vector */
    float e[3] = {sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta)};

    /* Kick of particles in momentum space */
    if (gpj->sidm_data.test_flag > 0.0f) { /* 1 indicates parts in cube */
        
        gpj->sidm_data.si_v_full[0] = VCM[0] + dv * e[0];
        gpj->sidm_data.si_v_full[1] = VCM[1] + dv * e[1];
        gpj->sidm_data.si_v_full[2] = VCM[2] + dv * e[2];

    }

    if (gpi->sidm_data.test_flag > 0.0f) { /* 1 indicates parts in cube */
        
        gpi->sidm_data.si_v_full[0] = VCM[0] - dv * e[0];
        gpi->sidm_data.si_v_full[1] = VCM[1] - dv * e[1];
        gpi->sidm_data.si_v_full[2] = VCM[2] - dv * e[2];
        
    }

    /*! change flag to indicate the particle has been scattered */
    gpj->sidm_data.sidm_flag = 1;
    gpi->sidm_data.sidm_flag = 1;
    
    /* Add counter of DM-DM collisions */
    gpj->sidm_data.num_sidm += 1.f;
    gpi->sidm_data.num_sidm += 1.f;
    
}

/**
 * @brief do self-interacting DM computation. Computes the probability of DM-DM interactions
 *
 * @param hi Comoving softening-length of particle j.
 * @param gpi First particle.
 * @param gpj Second particle.
 * @param a Current scale factor.
 * @param H Current Hubble parameter.
 * @param ti_current Current integer time (for random numbers).
 */
__attribute__((always_inline)) INLINE static void
runner_iact_sidm(float h_SI, struct gpart *gpi, struct gpart *gpj,
                 float a, float H, const double dt_cgs,
                 const integertime_t ti_current,
                 const struct sidm_props* sidm_props,
                 const struct unit_system* us) {
        
    /* Calculate probability of gparticles i & j of scattering within the next time step */
    
    /* Velocities of interacting particles */
    const double dv[3] = {gpi->v_full[0] - gpj->v_full[0], gpi->v_full[1] - gpj->v_full[1], gpi->v_full[2] - gpj->v_full[2]};
    const double v2 = dv[0] * dv[0] + dv[1] * dv[1] + dv[2] * dv[2];
    double vij = sqrt(v2) * units_cgs_conversion_factor(us, UNIT_CONV_VELOCITY);
    
    /* Scattering cross section per unit mass */
    const double sigma = sidm_props->sigma_cgs;
    
    /* DM particle mass */
    const double mass = gpj->mass * units_cgs_conversion_factor(us, UNIT_CONV_MASS);
    
    /* DM-DM distance */
    float hj_cgs = h_SI * units_cgs_conversion_factor(us, UNIT_CONV_LENGTH);
    float h_SIDM3 = hj_cgs * hj_cgs * hj_cgs;
    
    float a_inv = 1.0f / a;
    float a_inv4 = a_inv * a_inv * a_inv * a_inv;

    /* Calculate scattering rate */
    float Rate_SIDM_ij = sigma * mass * vij * a_inv4 / (4.0f * M_PI * h_SIDM3 / 3.0f);
    
    /* Calculate SIDM probability */
    float Probability_SIDM_ij = Rate_SIDM_ij * dt_cgs;
    
    /* Draw a random number */
    const float rand = random_unit_interval(gpj->id_or_neg_offset, ti_current, random_number_SIDM);
    
    /*printf("Pij %f",Probability_SIDM_ij);*/
    
    /* Are we lucky? If so we have DM-DM interactions */
    if (Probability_SIDM_ij > rand) {
        
        /* Imposing particles can interact only once */
        if (gpj->sidm_data.num_sidm < 1.f && gpi->sidm_data.num_sidm < 1.f) {
        
          /*printf("Doing kick");*/
          if (gpj->sidm_data.test_flag != gpi->sidm_data.test_flag) {
            
              sidm_do_kick(gpj, gpi, ti_current);
          }
        }
    }
}

/**
 * @brief Resets the SIDM properties of the g-particles
 *
 * @param gp Pointer to the gparticle data.
 */
__attribute__((always_inline)) INLINE static void sidm_reset(struct gpart *restrict gp) {
    
    /*! Flag to indicate the particle has been scattered yes(1)/no(0) */
    gp->sidm_data.sidm_flag = 0.0f;
    
    /*! Particle search radius */
    /*gp->sidm_data.h_sidm = 0.0f;*/
    
    /* Particle velocity */
    gp->sidm_data.si_v_full[0] = 0.0f;
    gp->sidm_data.si_v_full[1] = 0.0f;
    gp->sidm_data.si_v_full[2] = 0.0f;
}


/**
 * @brief Updates #gparts velocities
 *
 * @param gp #gpart
 *
 */
__attribute__((always_inline)) INLINE static void communicate_sidm_kick_to_gpart(struct gpart *restrict gp) {

    if (gp->sidm_data.sidm_flag > 0) {
        
    /* Rewrite gparticle's velocity */
    gp->v_full[0] = gp->sidm_data.si_v_full[0];
    gp->v_full[1] = gp->sidm_data.si_v_full[1];
    gp->v_full[2] = gp->sidm_data.si_v_full[2];
    
    /* Reset particle SIDM variables */
    sidm_reset(gp);

    }
}

#endif /* SWIFT_SIDM_IACT_H */
