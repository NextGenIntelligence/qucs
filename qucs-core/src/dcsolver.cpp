/*
 * dcsolver.cpp - DC solver class implementation
 *
 * Copyright (C) 2003, 2004, 2005, 2006 Stefan Jahn <stefan@lkcc.org>
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
 * $Id: dcsolver.cpp,v 1.39 2006-01-30 07:45:34 raimi Exp $
 *
 */

#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>

#include "object.h"
#include "complex.h"
#include "circuit.h"
#include "net.h"
#include "analysis.h"
#include "nasolver.h"
#include "dcsolver.h"

// Constructor creates an unnamed instance of the dcsolver class.
dcsolver::dcsolver () : nasolver<nr_double_t> () {
  saveOPs = 0;
  type = ANALYSIS_DC;
  setDescription ("DC");
}

// Constructor creates a named instance of the dcsolver class.
dcsolver::dcsolver (char * n) : nasolver<nr_double_t> (n) {
  saveOPs = 0;
  type = ANALYSIS_DC;
  setDescription ("DC");
}

// Destructor deletes the dcsolver class object.
dcsolver::~dcsolver () {
}

/* The copy constructor creates a new instance of the dcsolver class
   based on the given dcsolver object. */
dcsolver::dcsolver (dcsolver & o) : nasolver<nr_double_t> (o) {
  saveOPs = o.saveOPs;
}

/* This is the DC netlist solver.  It prepares the circuit list and
   solves it then. */
void dcsolver::solve (void) {
  // fetch simulation properties
  saveOPs |= !strcmp (getPropertyString ("saveOPs"), "yes") ? SAVE_OPS : 0;
  saveOPs |= !strcmp (getPropertyString ("saveAll"), "yes") ? SAVE_ALL : 0;
  char * solver = getPropertyString ("Solver");

  // initialize node voltages, first guess for non-linear circuits and
  // generate extra circuits if necessary
  init ();
  setCalculation ((calculate_func_t) &calc);

  // start the iterative solver
  solve_pre ();

  // choose a solver
  if (!strcmp (solver, "CroutLU"))
    eqnAlgo = ALGO_LU_DECOMPOSITION_CROUT;
  else if (!strcmp (solver, "DoolittleLU"))
    eqnAlgo = ALGO_LU_DECOMPOSITION_DOOLITTLE;
  else if (!strcmp (solver, "HouseholderQR"))
    eqnAlgo = ALGO_QR_DECOMPOSITION;
  else if (!strcmp (solver, "HouseholderLQ"))
    eqnAlgo = ALGO_QR_DECOMPOSITION_LS;

  // local variables for the fallback thingies
  int retry = -1, error, fallback = 0, preferred;
  int helpers[] = {
    CONV_SourceStepping,
    CONV_GMinStepping,
    CONV_SteepestDescent,
    CONV_LineSearch,
    CONV_Attenuation,
    -1 };

  // is a certain convergence helper requested?
  char * helper = getPropertyString ("convHelper");
  convHelper = CONV_None;
  if (!strcmp (helper, "LineSearch")) {
    convHelper = CONV_LineSearch;
  } else if (!strcmp (helper, "SteepestDescent")) {
    convHelper = CONV_SteepestDescent;
  } else if (!strcmp (helper, "Attenuation")) {
    convHelper = CONV_Attenuation;
  } else if (!strcmp (helper, "gMinStepping")) {
    convHelper = CONV_GMinStepping;
  } else if (!strcmp (helper, "SourceStepping")) {
    convHelper = CONV_SourceStepping;
  }
  preferred = convHelper;

  if (!subnet->isNonLinear ()) {
    // Start the linear solver.
    convHelper = CONV_None;
    error = solve_linear ();
  }
  else do {
    // Run the DC solver once.
    try_running () {
      applyNodeset ();
      error = solve_nonlinear ();
#if DEBUG
      if (!error) {
	logprint (LOG_STATUS,
		  "NOTIFY: %s: convergence reached after %d iterations\n",
		  getName (), iterations);
      }
#endif /* DEBUG */
      if (!error) retry = -1;
    }
    // Appropriate exception handling.
    catch_exception () {
    case EXCEPTION_NO_CONVERGENCE:
      pop_exception ();
      if (preferred == helpers[fallback] && preferred) fallback++;
      convHelper = helpers[fallback++];
      if (convHelper != -1) {
	logprint (LOG_ERROR, "WARNING: %s: %s analysis failed, using fallback "
		  "#%d (%s)\n", getName (), getDescription (), fallback,
		  getHelperDescription ());
	retry++;
      }
      else {
	retry = -1;
      }
      break;
    default:
      // Otherwise return.
      estack.print ();
      error++;
      break;
    }
  } while (retry != -1);

  // save results and cleanup the solver
  saveOperatingPoints ();
  saveResults ("V", "I", saveOPs);

  solve_post ();
}

/* Goes through the list of circuit objects and runs its calcDC()
   function. */
void dcsolver::calc (dcsolver * self) {
  circuit * root = self->getNet()->getRoot ();
  for (circuit * c = root; c != NULL; c = (circuit *) c->getNext ()) {
    c->calcDC ();
  }
}

/* Goes through the list of circuit objects and runs its initDC()
   function. */
void dcsolver::init (void) {
  circuit * root = subnet->getRoot ();
  for (circuit * c = root; c != NULL; c = (circuit *) c->getNext ()) {
    c->initDC ();
  }
}

/* Goes through the list of non-linear circuit objects and runs its
   saveOperatingPoints() function. */
void dcsolver::saveOperatingPoints (void) {
  circuit * root = subnet->getRoot ();
  for (circuit * c = root; c != NULL; c = (circuit *) c->getNext ()) {
    if (c->isNonLinear ()) c->saveOperatingPoints ();
  }
}
