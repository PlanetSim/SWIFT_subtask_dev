/*******************************************************************************
 * This file is part of SWIFT.
 * Coypright (c) 2022 Matthieu Schaller (schaller@strw.leidenuniv.nl)
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
#ifndef SWIFT_MHD_STRUCT_H
#define SWIFT_MHD_STRUCT_H

/* Config parameters. */
#include "../config.h"

/* Local headers. */
#include "part.h"

/* Import the right functions */
#if defined(NONE_MHD)
#include "./mhd/None/mhd_struct.h"
#elif defined(DIRECT_INDUCTION_MHD)
#include "./mhd/DirectInduction/mhd_struct.h"
#elif defined(DIRECT_INDUCTION_FEDE_MHD)
#include "./mhd/DInduction/mhd_struct.h"
#elif defined(VECTOR_POTENTIAL_MHD)
#include "./mhd/VPotential/mhd_struct.h"
#else
#error "Invalid choice of MHD variant"
#endif

#endif /* SWIFT_MHD_STRUCT_H */