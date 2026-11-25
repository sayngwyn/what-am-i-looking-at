// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// Which mesh file does a part of a picked object come from? A weapon is built from its own model plus one model per object
// modification, and the game keeps no file name on the nodes it builds from them. The header of each candidate mesh lists every
// part name it uses, so a part whose name is in exactly one candidate's list is from that file. Pure logic over plain data:
// the game-reading code gathers the candidates, the offline tests build them by hand.
#pragma once

#include "PickData.h"

#include <string>
#include <unordered_set>
#include <vector>

namespace ti
{
	/** One mesh file that may have contributed parts to the object. */
	struct MeshSource
	{
		std::string                     path;      ///< Data-relative path of the file
		std::string                     owner;     ///< "Name (Plugin.esm)" of the item or modification that brings it
		bool                            isMod = false;
		FileOrigin                      origin;    ///< where the file lives
		std::unordered_set<std::string> names;     ///< the header's names, lower case
	};

	/** Adds the header names (any case) to a_source, stored lower case. */
	void SetMeshNames(MeshSource& a_source, const std::vector<std::string>& a_names);

	/**
	 * Fills sourceMesh, sourceOrigin, sourceOwner and sourceIsMod of every part whose name is in exactly one source (two sources
	 * with the same path count as one); a part named by several different files gets sourceChoices (their paths) and nothing else;
	 * a part no source names, or without a name, is left as it is.
	 */
	void AttributeParts(std::vector<ShapeInfo>& a_shapes, const std::vector<MeshSource>& a_sources);
}
