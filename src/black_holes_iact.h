/*******************************************************************************
 * This file is part of SWIFT.
 * Copyright (c) 2022 Matthieu Schaller (schaller@strw.leidenuniv.nl)
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
#ifndef SWIFT_BLACK_HOLES_IACT_H
#define SWIFT_BLACK_HOLES_IACT_H

/* Config parameters. */
#include <config.h>

/* Select the correct BH model */
#if defined(BLACK_HOLES_NONE)
#include "./black_holes/Default/black_holes_iact.h"
#elif defined(BLACK_HOLES_EAGLE)
#include "./black_holes/EAGLE/black_holes_iact.h"
#elif defined(BLACK_HOLES_SPIN_JET)
#include "./black_holes/SPIN_JET/black_holes_iact.h"
#else
#error "Invalid choice of black hole model"
#endif

#endif /* SWIFT_BLACK_HOLES_IACT_H */