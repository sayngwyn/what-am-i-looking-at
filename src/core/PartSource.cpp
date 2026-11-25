// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
#include "PartSource.h"

#include <algorithm>
#include <cctype>

namespace ti
{
	namespace
	{
		std::string Lower(std::string_view a_text)
		{
			std::string s(a_text);
			for (auto& c : s)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return s;
		}
	}

	void SetMeshNames(MeshSource& a_source, const std::vector<std::string>& a_names)
	{
		a_source.names.clear();
		for (const auto& n : a_names)
			if (!n.empty())
				a_source.names.insert(Lower(n));
	}

	void AttributeParts(std::vector<ShapeInfo>& a_shapes, const std::vector<MeshSource>& a_sources)
	{
		for (auto& shape : a_shapes) {
			shape.sourceMesh.clear();
			shape.sourceOwner.clear();
			shape.sourceIsMod = false;
			shape.sourceOrigin = {};
			shape.sourceChoices.clear();
			if (shape.name.empty())
				continue;
			const std::string       wanted = Lower(shape.name);
			std::vector<const MeshSource*> hits;
			for (const auto& source : a_sources) {
				if (source.names.count(wanted) == 0)
					continue;
				const bool samePath = std::any_of(hits.begin(), hits.end(), [&](const MeshSource* h) { return Lower(h->path) == Lower(source.path); });
				if (!samePath)
					hits.push_back(&source);
			}
			if (hits.size() == 1) {
				shape.sourceMesh = hits[0]->path;
				shape.sourceOrigin = hits[0]->origin;
				shape.sourceOwner = hits[0]->owner;
				shape.sourceIsMod = hits[0]->isMod;
			} else if (hits.size() > 1) {
				for (const auto* h : hits)
					shape.sourceChoices.push_back(h->path);
			}
		}
	}
}
