/*
 * spsolver.cpp - S-parameter solver class implementation
 *
 * Copyright (C) 2003, 2004 Stefan Jahn <stefan@lkcc.org>
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
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.  
 *
 * $Id: spsolver.cpp,v 1.16 2004-07-04 11:16:16 ela Exp $
 *
 */

#if HAVE_CONFIG_H
# include <config.h>
#endif

using namespace std;

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>

#include "logging.h"
#include "complex.h"
#include "object.h"
#include "node.h"
#include "circuit.h"
#include "strlist.h"
#include "vector.h"
#include "dataset.h"
#include "net.h"
#include "tee.h"
#include "open.h"
#include "itrafo.h"
#include "analysis.h"
#include "sweep.h"
#include "spsolver.h"
#include "components/constants.h"
#include "components/component_id.h"

// Constructor creates an unnamed instance of the spsolver class.
spsolver::spsolver () : analysis () {
  type = ANALYSIS_SPARAMETER;
  swp = NULL;
  noise = 0;
}

// Constructor creates a named instance of the spsolver class.
spsolver::spsolver (char * n) : analysis (n) {
  type = ANALYSIS_SPARAMETER;
  swp = NULL;
  noise = 0;
}

// Destructor deletes the spsolver class object.
spsolver::~spsolver () {
  if (swp) delete swp;
}

/* The copy constructor creates a new instance of the spsolver class
   based on the given spsolver object. */
spsolver::spsolver (spsolver & n) : analysis (n) {
  noise = n.noise;
  if (n.swp) swp = new sweep (*n.swp);
}

/* This function joins two nodes of a single circuit (interconnected
   nodes) and returns the resulting circuit. */
circuit * spsolver::interconnectJoin (node * n1, node * n2) {

  circuit * s = n1->getCircuit ();
  circuit * result = new circuit (s->getSize () - 2);
  complex p;

  // interconnected port numbers
  int k = n1->getPort (), l = n2->getPort ();

  int j2; // column index for resulting matrix
  int i2; // row index for resulting matrix
  int j1; // column index for S matrix
  int i1; // row index for S matrix

  // handle single S block only
  i2 = j2 = 1;
  for (j1 = 1; j1 <= s->getSize (); j1++) {

    // skip connected node
    if (j1 == k || j1 == l) continue;

    // assign node name of resulting circuit
    result->setNode (j2, s->getNode(j1)->getName());

    // inside S only
    for (i1 = 1; i1 <= s->getSize (); i1++) {

      // skip connected node
      if (i1 == k || i1 == l) continue;

      // compute S'ij
      p = s->getS (i1, j1);
      p += 
	(s->getS (k, j1) * s->getS (i1, l) * (1.0 - s->getS (l, k)) + 
	 s->getS (l, j1) * s->getS (i1, k) * (1.0 - s->getS (k, l)) + 
	 s->getS (k, j1) * s->getS (l, l) * s->getS (i1, k) +
	 s->getS (l, j1) * s->getS (k, k) * s->getS (i1, l)) / 
	((1.0 - s->getS (k, l)) * (1.0 - s->getS (l, k)) -
	 s->getS (k, k) * s->getS (l, l));
      result->setS (i2++, j2, p);
    }

    // next column
    j2++; i2 = 1;
  }
  return result;
}

/* This function joins two nodes of two different circuits (connected
   nodes) and returns the resulting circuit. */
circuit * spsolver::connectedJoin (node * n1, node * n2) {

  circuit * s = n1->getCircuit ();
  circuit * t = n2->getCircuit ();
  circuit * result = new circuit (s->getSize () + t->getSize () - 2);
  complex p;

  // connected port numbers
  int k = n1->getPort (), l = n2->getPort ();

  int j2; // column index for resulting matrix
  int i2; // row index for resulting matrix
  int j1; // column index for S matrix
  int i1; // row index for S matrix

  // handle S block
  i2 = j2 = 1;
  for (j1 = 1; j1 <= s->getSize (); j1++) {

    // skip connected node
    if (j1 == k) continue;

    // assign node name of resulting circuit
    result->setNode (j2, s->getNode(j1)->getName());

    // inside S
    for (i1 = 1; i1 <= s->getSize (); i1++) {

      // skip connected node
      if (i1 == k) continue;

      // compute S'ij
      p = s->getS (i1, j1);
      p +=
	(s->getS (k, j1) * t->getS (l, l) * s->getS (i1, k)) /
	(1.0 - s->getS (k, k) * t->getS (l, l));
      result->setS (i2++, j2, p);
    }

    // across S and T
    for (i1 = 1; i1 <= t->getSize (); i1++) {

      // skip connected node
      if (i1 == l) continue;

      // compute S'mj
      p =
	(s->getS (k, j1) * t->getS (i1, l)) /
	(1.0 - s->getS (k, k) * t->getS (l, l));
      result->setS (i2++, j2, p);
    }
    // next column
    j2++; i2 = 1;
  }

  // handle T block
  for (j1 = 1; j1 <= t->getSize (); j1++) {

    // skip connected node
    if (j1 == l) continue;

    // assign node name of resulting circuit
    result->setNode (j2, t->getNode(j1)->getName());

    // across T and S
    for (i1 = 1; i1 <= s->getSize (); i1++) {

      // skip connected node
      if (i1 == k) continue;

      // compute S'mj
      p =
	(t->getS (l, j1) * s->getS (i1, k)) /
	(1.0 - t->getS (l, l) * s->getS (k, k));
      result->setS (i2++, j2, p);
    }

    // inside T
    for (i1 = 1; i1 <= t->getSize (); i1++) {

      // skip connected node
      if (i1 == l) continue;

      // compute S'ij
      p = t->getS (i1, j1);
      p +=
	(t->getS (l, j1) * s->getS (k, k) * t->getS (i1, l)) /
	(1.0 - t->getS (l, l) * s->getS (k, k));
      result->setS (i2++, j2, p);
    }

    // next column
    j2++; i2 = 1;
  }

  return result;
}

/* This function joins two nodes of a single circuit (interconnected
   nodes) and returns the resulting circuit. */
void spsolver::noiseInterconnect (circuit * result, node * n1, node * n2) {

  circuit * c = n1->getCircuit ();
  complex p, K1, K2, K3, K4;

  // interconnected port numbers
  int k = n1->getPort (), l = n2->getPort ();

  // denuminator needs only be calculated one time
  complex t = (1.0 - c->getS (k, l)) * (1.0 - c->getS (l, k))
	      - c->getS (k, k) * c->getS (l, l);

  int j2; // column index for resulting matrix
  int i2; // row index for resulting matrix
  int j1; // column index for S matrix
  int i1; // row index for S matrix

/*  fprintf (stderr, "interJoin: C(%d)-%d and -%d\n", c->getSize(), k, l);
  for (j1 = 1; j1 <= c->getSize (); j1++)
    for (i1 = 1; i1 <= c->getSize (); i1++) {
      fprintf (stderr, "S%d%d=%f, ", i1, j1, real(c->getS (i1, j1)));
    }
  fprintf(stderr, "\n");
  fprintf (stderr, "Nenner=%g\n", real(t));
*/
  // handle single C block only
  i2 = j2 = 1;
  for (j1 = 1; j1 <= c->getSize (); j1++) {

    // skip connected node
    if (j1 == k || j1 == l) continue;

    // inside C only
    for (i1 = 1; i1 <= c->getSize (); i1++) {

      // skip connected node
      if (i1 == k || i1 == l) continue;

      K1 = (c->getS (i1, l) * (1.0 - c->getS (l, k)) +
	    c->getS (l, l) * c->getS (i1, k)) / t;
      K2 = (c->getS (i1, k) * (1.0 - c->getS (k, l)) +
	    c->getS (k, k) * c->getS (i1, l)) / t;
      K3 = (c->getS (j1, l) * (1.0 - c->getS (l, k)) +
	    c->getS (l, l) * c->getS (j1, k)) / t;
      K4 = (c->getS (j1, k) * (1.0 - c->getS (k, l)) +
	    c->getS (k, k) * c->getS (j1, l)) / t;
//fprintf (stderr, "K1 =  %g, %g\n", real(K1), imag(K1));
//fprintf (stderr, "K2 =  %g, %g\n", real(K2), imag(K2));
//fprintf (stderr, "K3 =  %g, %g\n", real(K3), imag(K3));
//fprintf (stderr, "K4 =  %g, %g\n", real(K4), imag(K4));

      p = c->getN (i1, j1) + c->getN (k, j1) * K1 + c->getN (l, j1) * K2 +
	  conj(K3) * (c->getN (i1, k) + c->getN (k, k) * K1 +
				        c->getN (l, k) * K2) +
	  conj(K4) * (c->getN (i1, l) + c->getN (k, l) * K1 +
					c->getN (l, l) * K2);
      result->setN (i2, j2, p);
      fprintf (stderr, "I: Cnl(%d,%d) -> %e\n", i2, j2, real(p));
      if (i2 >= j2) break;   // the other half need not be computed
      result->setN (j2, i2, conj (p));
      i2++;
    }

    // next column
    j2++; i2 = 1;
  }
}


/* The following function joins two nodes of two different circuits and
   save the the noise wave correlation matrix in the resulting circuit. */
void spsolver::noiseConnect (circuit * result, node * n1, node * n2) {
  circuit * c = n1->getCircuit ();
  circuit * d = n2->getCircuit ();
  complex p;

  // connected port numbers
  int k = n1->getPort (), l = n2->getPort ();

  // denuminator needs only be calculated one time
  complex denum = 1.0 - c->getS (k, k) * d->getS (l, l);


  int j2; // column index for resulting matrix
  int i2; // row index for resulting matrix
  int j1; // column index for S matrix
  int i1; // row index for S matrix

  fprintf (stderr, "join: C(%d)-%d and D(%d)-%d -> N(%d)\n", c->getSize(), k,
	   d->getSize(), l, c->getSize() + d->getSize() - 2);
/*  for (j1 = 1; j1 <= c->getSize (); j1++)
    for (i1 = 1; i1 <= c->getSize (); i1++) {
      fprintf (stderr, "S%d%d=%f, ", i1, j1, real(c->getS (i1, j1)));
    }
  fprintf(stderr, "\n");
  for (j1 = 1; j1 <= d->getSize (); j1++)
    for (i1 = 1; i1 <= d->getSize (); i1++) {
      fprintf (stderr, "T%d%d=%f, ", i1, j1, real(d->getS (i1, j1)));
    }
  fprintf(stderr, "\n");*/

  // handle C block
  i2 = j2 = 1;
  for (j1 = 1; j1 <= c->getSize (); j1++) {

    // skip connected node
    if (j1 == k) continue;

    // inside C
    for (i1 = 1; i1 <= c->getSize (); i1++) {

      // skip connected node
      if (i1 == k) continue;

      // compute C'nl
      p = c->getN (i1, j1) +
	  c->getN (k, j1) * d->getS (l, l) * c->getS (i1, k) / denum +
	  c->getN (i1, k) * conj (d->getS (l, l) * c->getS (j1, k) / denum) +
	 (c->getN (k, k) * sqr (abs (d->getS (l, l))) + d->getN (l, l)) *
	  c->getS (i1, k) * conj (c->getS (j1, k)) / sqr (abs (denum));

      result->setN (i2, j2, p);
      fprintf (stderr, "C: Cnl(%d,%d) -> %e\n", i2, j2, real(p));
      if (i2 >= j2)  break;   // the other half need not be computed
      result->setN (j2, i2, conj (p));
      i2++;
    }

    // The formulas "across C and D" are calculated elsewhere by the other
    // half of the matrix (conjugate complex). Therefore, they are missing
    // here.

    // next column
    j2++; i2 = 1;
  }

  // handle D block
  for (j1 = 1; j1 <= d->getSize (); j1++) {

    // skip connected node
    if (j1 == l) continue;

    // across D and C
    for (i1 = 1; i1 <= c->getSize (); i1++) {

      // skip connected node
      if (i1 == k) continue;

      // compute C'nl
      p = (c->getN (k, k) * d->getS (l, l) +
	   d->getN (l, l) * conj(c->getS (k, k))) *
	   c->getS (i1, k) * conj(d->getS (j1, l)) / sqr (abs (denum)) +
	   d->getN (l, j1) * c->getS (i1, k) / denum +
	   c->getN (i1, k) * conj(d->getS (j1, l) / denum);
      result->setN (i2, j2, p);
      result->setN (j2, i2, conj (p));
      i2++;
      fprintf (stderr, "DC: Cnl(%d,%d) -> %e\n", i2-1, j2, real(p));
    }

    // inside D
    for (i1 = 1; i1 <= d->getSize (); i1++) {

      // skip connected node
      if (i1 == l) continue;

      // compute C'nl
      p = d->getN (i1, j1) +
	 (d->getN (l, l) * sqr (abs (c->getS (k, k))) + c->getN (k, k)) *
	  d->getS (i1, l) * conj (d->getS (j1, l)) / sqr (abs (denum)) +
	  d->getN (i1, l) * conj (c->getS (k, k) * d->getS (j1, l) / denum) +
	  d->getN (l, j1) * c->getS (k, k) * d->getS (i1, l) / denum;
      result->setN (i2, j2, p);
      fprintf (stderr, "D: Cnl(%d,%d) -> %e\n", i2, j2, real(p));
      if (i2 >= j2)  break;   // the other half need not be computed
      result->setN (j2, i2, conj (p));
      i2++;
    }

    // next column
    j2++; i2 = 1;
  }

  fprintf (stderr,"--------------------------------------------------------\n");
}

/* Goes through the list of circuit objects and runs its frequency
   dependent calcSP() function. */
void spsolver::calc (nr_double_t freq) {
  circuit * root = subnet->getRoot ();
  for (circuit * c = root; c != NULL; c = (circuit *) c->getNext ()) {
    c->calcSP (freq);
  }
}

/* Go through each registered circuit object in the list and find the
   connection which results in a new subnetwork with the smallest
   number of s-parameters to calculate. */
void spsolver::reduce (void) {

  node * n1, * n2, * cand;
  circuit * result, * c1, * c2, * cand1, * cand2;
  int ports;
  circuit * root = subnet->getRoot ();

  // initialize local variables
  result = c1 = c2 = cand1 = cand2 = NULL;
  n1 = n2 = cand = NULL;
  ports = 10000; // huge

  // go through the circuit list
  for (circuit * c = root; c != NULL; c = (circuit *) c->getNext ()) {

    // skip signal ports
    if (c->isPort ()) continue;

    // and each node in the circuit
    for (int i = 1; i <= c->getSize (); i++) {

      // find duplicate node
      if ((cand = subnet->findConnectedCircuitNode (c->getNode (i))) != NULL) {

	// save both candidates
	c1 = c; c2 = cand->getCircuit ();
	// connected
	if (c1 != c2) {
	  if (c1->getSize () + c2->getSize () - 2 < ports) {
	    ports = c1->getSize () + c2->getSize () - 2;
	    cand1 = c1; cand2 = c2; n1 = c1->getNode (i); n2 = cand;
	  }
	}
	// interconnect
	else {
	  if (c1->getSize () - 2 < ports) {
	    ports = c1->getSize () - 2;
	    cand1 = c1; cand2 = c2; n1 = c1->getNode (i); n2 = cand;
	  }
	}
      }
    }
  }

  // found a connection ?
  if (cand1 != NULL && cand2 != NULL) {
    // connected
    if (cand1 != cand2) {
#if DEBUG
      logprint (LOG_STATUS, "DEBUG: connected node (%s): %s - %s\n",
		n1->getName (), cand1->getName (), cand2->getName ());
#endif /* DEBUG */
      result = connectedJoin (n1, n2);
      if (noise) noiseConnect (result, n1, n2);
      subnet->reducedCircuit (result);
      subnet->removeCircuit (cand1);
      subnet->removeCircuit (cand2);
      subnet->insertCircuit (result);
      result->setOriginal (0);
    }
    // interconnect
    else {
#if DEBUG
      logprint (LOG_STATUS, "DEBUG: interconnected node (%s): %s\n",
		n1->getName (), cand1->getName ());
#endif
      result = interconnectJoin (n1, n2);
      if (noise) noiseInterconnect (result, n1, n2);
      subnet->reducedCircuit (result);
      subnet->removeCircuit (cand1);
      subnet->insertCircuit (result);
      result->setOriginal (0);
    }
  }
}

/* Goes through the list of circuit objects and runs initializing
   functions if necessary. */
void spsolver::init (void) {
  circuit * root = subnet->getRoot ();
  for (circuit * c = root; c != NULL; c = (circuit *) c->getNext ()) {
    if (c->isNonLinear ())
      c->calcOperatingPoints ();
  }
}

/* This is the netlist solver.  It prepares the circuit list for each
   requested frequency and solves it then. */
void spsolver::solve (void) {

  nr_double_t freq;
  int ports;
  circuit * root;

  runs++;

  // run additional noise analysis ?
  noise = !strcmp (getPropertyString ("Noise"), "yes") ? 1 : 0;

  // create frequency sweep if necessary
  if (swp == NULL) {
    char * type = getPropertyString ("Type");
    nr_double_t start = getPropertyDouble ("Start");
    nr_double_t stop = getPropertyDouble ("Stop");
    nr_double_t points = getPropertyDouble ("Points");

    if (!strcmp (type, "lin")) {
      swp = new linsweep ("frequency");
      ((linsweep *) swp)->create (start, stop, points);
    }
    else if (!strcmp (type, "log")) {
      swp = new logsweep ("frequency");
      ((logsweep *) swp)->create (start, stop, points);
    }
  }

  insertConnections ();
  init ();

  swp->reset ();
  for (int i = 0; i < swp->getSize (); i++) {
    freq = swp->next ();

    root = subnet->getRoot ();
    ports = root->countNodes ();
    subnet->setReduced (0);
    calc (freq);

#if DEBUG
    logprint (LOG_STATUS, "NOTIFY: solving netlist for f = %e\n", 
	      (double) freq);
#endif
    
    while (ports > subnet->getPorts ()) {
      reduce ();
      ports -= 2;
    }

    saveResults (freq);
    subnet->getDroppedCircuits ();
    subnet->deleteUnusedCircuits ();
  }
  dropConnections ();
}

/* The function goes through the list of circuit objects and creates T
   circuits if necessary.  It looks for nodes in the circuit list
   connected to the given node. */
void spsolver::insertTee (node * n) {

  int count = 0;
  node * nodes[4], * _node;
  tee * result;
  char * _name = n->getName ();
  circuit * root = subnet->getRoot ();

  nodes[0] = n;

  // go through list of circuit objects
  for (circuit * c = root; c != NULL; c = (circuit *) c->getNext ()) {
    // and each node in a circuit
    for (int i = 1; i <= c->getSize (); i++) {
      _node = c->getNode (i);
      if (!strcmp (_node->getName (), _name)) {
	if (_node != n) {

	  // found a connected node
	  nodes[++count] = _node;
	  if (count == 2) {

	    // create a T and assign its node names
	    result = new tee ();
	    subnet->insertedCircuit (result);
	    result->setNode (1, _name);
	    subnet->insertedNode (result->getNode (2));
	    subnet->insertedNode (result->getNode (3));

	    // rename the nodes connected to the T
	    nodes[1]->setName (result->getNode(2)->getName ());
	    nodes[2]->setName (result->getNode(3)->getName ());

	    // complete the nodes of the T
	    result->getNode(2)->setCircuit (result);
	    result->getNode(3)->setCircuit (result);
	    result->getNode(2)->setPort (2);
	    result->getNode(3)->setPort (3);

	    // put the T in the circuit list
	    subnet->insertCircuit (result);
	    nodes[1] = result->getNode (1);
	    count = 1;
	  }
	}
      }
    }
  }
}

/* This function removes an inserted tee from the netlist and restores
   the original node names. */
void spsolver::dropTee (circuit * c) {
  node * n;
  char * name;
  if (c->getType () == CIR_TEE) {
    name = c->getNode(1)->getName ();
    n = subnet->findConnectedNode (c->getNode (2));
    n->setName (name);
    n = subnet->findConnectedNode (c->getNode (3));
    n->setName (name);
    c->setOriginal (0);
    subnet->removeCircuit (c);
  }
}

/* The function adds an open to the circuit list if the given node is
   unconnected. */
void spsolver::insertOpen (node * n) {

  circuit * result;

  if (subnet->findConnectedNode (n) == NULL) {
    result = new open ();
    subnet->insertedCircuit (result);
    result->setNode (1, n->getName());
    subnet->insertCircuit (result);
  }
}

// This function removes an inserted open from the netlist.
void spsolver::dropOpen (circuit * c) {
  if (c->getType () == CIR_OPEN) {
    c->setOriginal (0);
    subnet->removeCircuit (c);
  }
}

/* This function prepares the circuit list by adding Ts and opens to
   the circuit list.  With this adjustments the solver is able to
   solve the circuit. */
void spsolver::insertConnections (void) {

  circuit * root = subnet->getRoot ();
#if DEBUG
  logprint (LOG_STATUS, "NOTIFY: preparing circuit for analysis\n");
#endif /* DEBUG */
  for (circuit * c = root; c != NULL; c = (circuit *) c->getNext ()) {
    for (int i = 1; i <= c->getSize (); i++) {
      insertTee (c->getNode (i));
      insertOpen (c->getNode (i));
    }
  }
  insertDifferentialPorts ();
}

/* The function is the counterpart of insertConnections().  It removes
   all additional circuits from the netlist which were necessary to
   run the analysis algorithm. */
void spsolver::dropConnections (void) {
  circuit * next, * cand;
  int inserted;

  // drop all additional inserted circuits in correct order
  do {
    // find last inserted circuit
    inserted = -1;
    cand = NULL;
    for (circuit * c = subnet->getRoot (); c != NULL; c = next) {
      next = (circuit *) c->getNext ();
      if (c->getInserted () > inserted) {
	inserted = c->getInserted ();
	cand = c;
      }
    }
    // if found, then drop that circuit
    if (cand != NULL) {
      switch (cand->getType ()) {
      case CIR_OPEN: dropOpen (cand); break;
      case CIR_TEE: dropTee (cand); break;
      case CIR_ITRAFO: dropDifferentialPort (cand); break;
      }
    }
  } while (cand != NULL);
}

/* This function inserts an ideal transformator before an AC power
   source in order to allow differential S parameter ports.  */
void spsolver::insertDifferentialPorts (void) {
  circuit * root = subnet->getRoot ();
  for (circuit * c = root; c != NULL; c = (circuit *) c->getNext ()) {
    if (c->isPort ()) {

      // create an ideal transformator and assign its node names
      circuit * result = new itrafo ();
      subnet->insertedCircuit (result);
      subnet->insertedNode (result->getNode (1));
      result->setNode (2, c->getNode(1)->getName ());
      result->setNode (3, c->getNode(2)->getName ());

      // rename the nodes connected to the trafo
      c->getNode(1)->setName (result->getNode(1)->getName ());
      c->getNode(2)->setName ("PacGround");

      // complete the nodes of the trafo
      result->getNode(1)->setCircuit (result);
      result->getNode(1)->setPort (1);

      // pass the port impedance to the ideal trafo
      result->addProperty ("Z", c->getPropertyDouble ("Z"));

      // put the trafo in the circuit list
      subnet->insertCircuit (result);
    }
  }
}

/* This function removes an ideal transformer which was necessary to
   be placed in front of a s-parameter port in order to allow
   differential s-parameters.  It also restores the original node
   names. */
void spsolver::dropDifferentialPort (circuit * c) {
  circuit * pac;
  node * n;
  if (c->getType () == CIR_ITRAFO) {
    n = subnet->findConnectedNode (c->getNode (1));
    pac = n->getCircuit ();
    pac->getNode(1)->setName (c->getNode(2)->getName ());
    pac->getNode(2)->setName (c->getNode(3)->getName ());
    c->setOriginal (0);
    subnet->removeCircuit (c);
  }
}

/* This function saves the results of a single solve() functionality
   (for the given frequency) into the output dataset. */
void spsolver::saveResults (nr_double_t freq) {

  vector * f;
  node * sig_i, * sig_j;
  char * n;
  int res_i, res_j;
  circuit * root = subnet->getRoot ();

  // temporary noise matrices
  complex noise_c[4], noise_s[4];

  // add current frequency to the dependency of the output dataset
  if ((f = data->findDependency ("frequency")) == NULL) {
    f = new vector ("frequency");
    data->addDependency (f);
  }
  if (runs == 1) f->add (freq);

  // go through the list of remaining circuits
  for (circuit * c = root; c != NULL; c = (circuit *) c->getNext ()) {
    // skip signals
    if (!c->isPort ()) {
      // handle each s-parameter
      for (int i = 1; i <= c->getSize (); i++) {
	for (int j = 1; j <= c->getSize (); j++) {

	  // generate the appropriate variable name
	  sig_i = subnet->findConnectedNode (c->getNode (i));
	  sig_j = subnet->findConnectedNode (c->getNode (j));
	  res_i = sig_i->getCircuit()->getPropertyInteger ("Num");
	  res_j = sig_j->getCircuit()->getPropertyInteger ("Num");
	  n = createSP (res_i, res_j);

	  // add variable data item to dataset
	  saveVariable (n, c->getS (i, j), f);

	  // if noise analysis is requested
	  if (noise) {
	    int ro, co;
	    int ni = getPropertyInteger ("NoiseIP");
	    int no = getPropertyInteger ("NoiseOP");
	    if ((res_i == ni || res_i == no) && (res_j == ni || res_j == no)) {
	      ro = (res_i == ni) ? 0 : 1;
	      co = (res_j == ni) ? 0 : 1;
	      fprintf (stderr, "C[%d,%d] = %g,%g\n", ro, co, 
		       real(c->getN (i, j)), imag(c->getN (i, j))); 
	      // save results in temporary data items
	      noise_c[co + ro * 2] = c->getN (i, j);
	      noise_s[co + ro * 2] = c->getS (i, j);
	    }
	  }
	}
      }
    }
  }

  // finally compute and save noise parameters
  if (noise) {
    saveNoiseResults (noise_s, noise_c, f);
  }
}

/* This function takes the s-parameter matrix and noise wave
   correlation matrix and computes the noise parameters based upon
   these values.  Then it save the results into the dataset. */
void spsolver::saveNoiseResults (complex s[4], complex c[4], vector * f) {
  complex c22 = c[3], c11 = c[0], c12 = c[1];
  complex s11 = s[0], s21 = s[2];
  complex n1, n2, F, Ropt, Fmin, Rn;

  // linear noise figure
  F    = real(1.0 + c22 / sqr (abs (s21)));
  n1   =
    c11 * sqr (abs (s21)) - 2.0 * real (c12 * s21 * conj (s11)) +
    c22 * sqr (abs (s11));
  n2   = 2.0 * (c22 * s11 - c12 * s21) / (c22 + n1);

  // optimal source reflection coefficient
  Ropt = 1.0 - sqr (abs (n2));
  if (real (Ropt) < 0.0)
    Ropt = 1.0;  // avoid a negative radicant
  else
    Ropt = (1.0 - sqrt (Ropt)) / n2;

  // minimum noise figure
  Fmin = real(
    1.0 + (c22 - n1 * sqr (abs (Ropt))) /
    (sqr (abs (s21)) * (1.0 + sqr (abs (Ropt)))));

  // equivalent noise resistance
  Rn   = real(circuit::z0 * (c11 -
			2.0 * real (c12 * conj ((1.0 + s11) / s21)) +
			c22 * sqr (abs ((1.0 + s11) / s21))) / 4.0);

  // add variable data items to dataset
  saveVariable ("F", 10*log10(abs(F)), f);
  saveVariable ("Gopt", Ropt, f);
  saveVariable ("Fmin", 10*log10(abs(Fmin)), f);
  saveVariable ("Rn", Rn, f);
}

/* Saves the given variable into dataset.  Creates the dataset vector
   if necessary. */
void spsolver::saveVariable (char * n, complex z, vector * f) {
  vector * d;
  if ((d = data->findVariable (n)) == NULL) {
    d = new vector (n);
    d->setDependencies (new strlist ());
    d->getDependencies()->add (f->getName ());
    d->setOrigin (getName ());
    data->addVariable (d);
  }
  d->add (z);
}

// Create an appropriate variable name.
char * spsolver::createSP (int i, int j) {
  static char text[16];
  sprintf (text, "S[%d,%d]", i, j);
  return text;
}
