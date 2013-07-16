/*
 * tmatrix.cpp - simple matrix template class implementation
 *
 * Copyright (C) 2004, 2005, 2006, 2008 Stefan Jahn <stefan@lkcc.org>
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this package; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street - Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * $Id$
 *
 */

#include "qucs_typedefs.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cmath>

#include "compat.h"
#include "logging.h"
#include "complex.h"
#include "tmatrix.h"

namespace qucs {

// Compute inverse matrix of the given matrix by Gauss-Jordan elimination.
template <class nr_type_t>
tmatrix<nr_type_t> inverse (const tmatrix<nr_type_t> &a) {
  nr_double_t MaxPivot;
  nr_type_t f;
  tmatrix<nr_type_t> b;
  tmatrix<nr_type_t> e;
  int i, c, r, pivot, n = a.getCols ();

  // create temporary matrix and the result matrix
  b = tmatrix<nr_type_t> (a);
  e = teye<nr_type_t> (n);

  // create the eye matrix in 'b' and the result in 'e'
  for (i = 0; i < n; i++) {
    // find maximum column value for pivoting
    for (MaxPivot = 0, pivot = r = i; r < n; r++) {
      if (abs (b.get (r, i)) > MaxPivot) {
	MaxPivot = abs (b.get (r, i));
	pivot = r;
      }
    }
    // exchange rows if necessary
    assert (MaxPivot != 0); // singular matrix
    if (i != pivot) {
      b.exchangeRows (i, pivot);
      e.exchangeRows (i, pivot);
    }

    // compute current row
    f = b.get (i, i);
    for (c = 0; c < n; c++) {
      b.set (i, c, b.get (i, c) / f);
      e.set (i, c, e.get (i, c) / f);
    }

    // compute new rows and columns
    for (r = 0; r < n; r++) {
      if (r != i) {
	f = b.get (r, i);
	for (c = 0; c < n; c++) {
	  b.set (r, c, b.get (r, c) - f * b.get (i, c));
	  e.set (r, c, e.get (r, c) - f * e.get (i, c));
	}
      }
    }
  }
  return e;
}



#ifdef DEBUG
// Debug function: Prints the matrix object.
template <class nr_type_t>
void tmatrix<nr_type_t>::print (bool realonly) {
  for (int r = 0; r < this->m.rows; r++) {
    for (int c = 0; c < this->m.cols(); c++) {
      if (realonly) {
	fprintf (stderr, "%+.2e%s", (double) real (get (r, c)),
		 c != this->m.cols() - 1 ? " " : "");
      } else {
	fprintf (stderr, "%+.2e%+.2ei%s", (double) real (get (r, c)),
		 (double) imag (get (r, c)), c !=  this->m.cols() - 1 ? " " : "");
      }
    }
    fprintf (stderr, ";\n");
  }
}
#endif /* DEBUG */

} // namespace qucs
