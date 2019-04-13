/*
 * r_efrag.c -- entity fragments
 * $Id: gl_refrag.c,v 1.9 2008-04-22 13:06:06 sezero Exp $
 *
 * Copyright (C) 1996-1997  Id Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "quakedef.h"


static mnode_t	*r_pefragtopnode;

/*
===============================================================================

ENTITY FRAGMENT FUNCTIONS

===============================================================================
*/

static efrag_t		**lastlink;
static entity_t		*r_addent;
static vec3_t		r_emins, r_emaxs;

#define EXTRA_EFRAGS	512

// based on RMQEngine
static efrag_t *R_GetEfrag(void)
{
	// we could just Hunk_Alloc a single efrag_t and return it, but since
	// the struct is so small (2 pointers) allocate groups of them
	// to avoid wasting too much space on the hunk allocation headers.

	if (cl.free_efrags)
	{
		efrag_t *ef = cl.free_efrags;
		cl.free_efrags = ef->leafnext;
		ef->leafnext = NULL;

		cl.num_efrags++;

		return ef;
	}
	else
	{
		int i;

		cl.free_efrags = (efrag_t *)Hunk_AllocName(EXTRA_EFRAGS * sizeof(efrag_t), "efrags");

		for (i = 0; i < EXTRA_EFRAGS - 1; i++)
			cl.free_efrags[i].leafnext = &cl.free_efrags[i + 1];

		cl.free_efrags[i].leafnext = NULL;

		// call recursively to get a newly allocated free efrag
		return R_GetEfrag();
	}
}

/*
================
R_RemoveEfrags

Call when removing an object from the world or moving it to another position
================
*/
void R_RemoveEfrags(entity_t *ent)
{
	efrag_t		*ef, *old, *walk, **prev;

	ef = ent->efrag;

	while (ef)
	{
		prev = &ef->leaf->efrags;
		while (1)
		{
			walk = *prev;
			if (!walk)
				break;
			if (walk == ef)
			{	// remove this fragment
				*prev = ef->leafnext;
				break;
			}
			else
				prev = &walk->leafnext;
		}

		old = ef;
		ef = ef->entnext;

		// put it on the free list
		old->entnext = cl.free_efrags;
		cl.free_efrags = old;
	}

	ent->efrag = NULL;
}

/*
===================
R_SplitEntityOnNode
===================
*/
static void R_SplitEntityOnNode(mnode_t *node)
{
	efrag_t		*ef;
	mplane_t	*splitplane;
	mleaf_t		*leaf;
	int			sides;

	if (node->contents == CONTENTS_SOLID)
	{
		return;
	}

	// add an efrag if the node is a leaf

	if (node->contents < 0)
	{
		if (!r_pefragtopnode)
			r_pefragtopnode = node;

		leaf = (mleaf_t *)node;

		// grab an efrag off the free list
		ef = R_GetEfrag(); //shan
		ef->entity = r_addent;

		// set the leaf links
		ef->leafnext = leaf->efrags;
		leaf->efrags = ef;

		return;
	}

	// NODE_MIXED

	splitplane = node->plane;
	sides = BOX_ON_PLANE_SIDE(r_emins, r_emaxs, splitplane);

	if (sides == 3)
	{
		// split on this plane
		// if this is the first splitter of this bmodel, remember it
		if (!r_pefragtopnode)
			r_pefragtopnode = node;
	}

	// recurse down the contacted sides
	if (sides & 1)
		R_SplitEntityOnNode(node->children[0]);

	if (sides & 2)
		R_SplitEntityOnNode(node->children[1]);
}

/*
===========
R_AddEfrags
===========
*/
void R_AddEfrags(entity_t *ent)
{
	qmodel_t	*entmodel;
	int			i;

	if (!ent->model)
		return;

	r_addent = ent;

	r_pefragtopnode = NULL;

	entmodel = ent->model;

	for (i = 0; i < 3; i++)
	{
		r_emins[i] = ent->origin[i] + entmodel->mins[i];
		r_emaxs[i] = ent->origin[i] + entmodel->maxs[i];
	}

	R_SplitEntityOnNode(cl.worldmodel->nodes);

	ent->topnode = r_pefragtopnode;

	//R_CheckEfrags(); //johnfitz
}


/*
================
R_StoreEfrags -- johnfitz -- pointless switch statement removed.
================
*/
void R_StoreEfrags(efrag_t **ppefrag)
{
	entity_t	*pent;
	efrag_t		*pefrag;

	while ((pefrag = *ppefrag) != NULL)
	{
		pent = pefrag->entity;

		if ((pent->visframe != r_framecount) && (cl_numvisedicts < MAX_VISEDICTS))
		{
			cl_visedicts[cl_numvisedicts++] = pent;
			pent->visframe = r_framecount;
		}

		ppefrag = &pefrag->leafnext;
	}
}
