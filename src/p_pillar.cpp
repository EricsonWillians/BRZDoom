/*
** p_pillar.cpp
** Handles pillars
**
**---------------------------------------------------------------------------
** Copyright 1998-2001 Randy Heit
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/

#include "doomdef.h"
#include "p_local.h"
#include "p_spec.h"
#include "g_level.h"
#include "s_sndseq.h"

IMPLEMENT_CLASS (DPillar)

DPillar::DPillar ()
{
}

void DPillar::Serialize (FArchive &arc)
{
	Super::Serialize (arc);
	arc << m_Type
		<< m_FloorSpeed
		<< m_CeilingSpeed
		<< m_FloorTarget
		<< m_CeilingTarget
		<< m_Crush;
}

void DPillar::Tick ()
{
	int r, s;
	fixed_t oldfloor, oldceiling;

	oldfloor = m_Sector->floorplane.d;
	oldceiling = m_Sector->ceilingplane.d;

	if (m_Type == pillarBuild)
	{
		r = MoveFloor (m_FloorSpeed, m_FloorTarget, m_Crush, 1);
		s = MoveCeiling (m_CeilingSpeed, m_CeilingTarget, m_Crush, -1);
	}
	else
	{
		r = MoveFloor (m_FloorSpeed, m_FloorTarget, m_Crush, -1);
		s = MoveCeiling (m_CeilingSpeed, m_CeilingTarget, m_Crush, 1);
	}

	if (r == pastdest && s == pastdest)
	{
		SN_StopSequence (m_Sector);
		Destroy ();
	}
	else
	{
		if (r == crushed)
		{
			MoveFloor (m_FloorSpeed, oldfloor, -1, -1);
		}
		if (s == crushed)
		{
			MoveCeiling (m_CeilingSpeed, oldceiling, -1, 1);
		}
	}
}

DPillar::DPillar (sector_t *sector, EPillar type, fixed_t speed,
				  fixed_t floordist, fixed_t ceilingdist, int crush)
	: DMover (sector)
{
	fixed_t newheight;
	vertex_t *spot;

	sector->floordata = sector->ceilingdata = this;
	setinterpolation (&sector->floorplane.d);
	setinterpolation (&sector->ceilingplane.d);
	setinterpolation (&sector->floortexz);
	setinterpolation (&sector->ceilingtexz);

	m_Type = type;
	m_Crush = crush;

	if (type == pillarBuild)
	{
		// If the pillar height is 0, have the floor and ceiling meet halfway
		if (floordist == 0)
		{
			newheight = (sector->CenterFloor () + sector->CenterCeiling ()) / 2;
			m_FloorTarget = sector->floorplane.PointToDist (sector->soundorg[0], sector->soundorg[1], newheight);
			m_CeilingTarget = sector->ceilingplane.PointToDist (sector->soundorg[0], sector->soundorg[1], newheight);
			floordist = newheight - sector->CenterFloor ();
		}
		else
		{
			newheight = sector->CenterFloor () + floordist;
			m_FloorTarget = sector->floorplane.PointToDist (sector->soundorg[0], sector->soundorg[1], newheight);
			m_CeilingTarget = sector->ceilingplane.PointToDist (sector->soundorg[0], sector->soundorg[1], newheight);
		}
		ceilingdist = sector->CenterCeiling () - newheight;
	}
	else
	{
		// If one of the heights is 0, figure it out based on the
		// surrounding sectors
		if (floordist == 0)
		{
			newheight = sector->FindLowestFloorSurrounding (&spot);
			m_FloorTarget = sector->floorplane.PointToDist (spot, newheight);
			floordist = sector->floorplane.ZatPoint (spot) - newheight;
		}
		else
		{
			newheight = sector->floorplane.ZatPoint (0, 0) - floordist;
			m_FloorTarget = sector->floorplane.PointToDist (0, 0, newheight);
		}
		if (ceilingdist == 0)
		{
			newheight = sector->FindHighestCeilingSurrounding (&spot);
			m_CeilingTarget = sector->ceilingplane.PointToDist (spot, newheight);
			ceilingdist = newheight - sector->ceilingplane.ZatPoint (spot);
		}
		else
		{
			newheight = sector->ceilingplane.ZatPoint (0, 0) + ceilingdist;
			m_CeilingTarget = sector->ceilingplane.PointToDist (0, 0, newheight);
		}
	}

	// The speed parameter applies to whichever part of the pillar
	// travels the farthest. The other part's speed is then set so
	// that it arrives at its destination at the same time.
	if (floordist > ceilingdist)
	{
		m_FloorSpeed = speed;
		m_CeilingSpeed = Scale (speed, ceilingdist, floordist);
	}
	else
	{
		m_CeilingSpeed = speed;
		m_FloorSpeed = Scale (speed, floordist, ceilingdist);
	}

	if (sector->seqType >= 0)
		SN_StartSequence (sector, sector->seqType, SEQ_PLATFORM);
	else
		SN_StartSequence (sector, "Floor");
}

bool EV_DoPillar (DPillar::EPillar type, int tag, fixed_t speed, fixed_t height,
				  fixed_t height2, int crush)
{
	bool rtn = false;
	int secnum = -1;

	while ((secnum = P_FindSectorFromTag (tag, secnum)) >= 0)
	{
		sector_t *sec = &sectors[secnum];

		if (sec->floordata || sec->ceilingdata)
			continue;

		fixed_t flor, ceil;

		flor = sec->CenterFloor ();
		ceil = sec->CenterCeiling ();

		if (type == DPillar::pillarBuild && flor == ceil)
			continue;

		if (type == DPillar::pillarOpen && flor != ceil)
			continue;

		rtn = true;
		new DPillar (sec, type, speed, height, height2, crush);
	}
	return rtn;
}
