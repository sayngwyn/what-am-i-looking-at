// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
#include "CardState.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>

namespace ti
{
	float ClampUiScale(float a_scale)
	{
		if (!std::isfinite(a_scale))
			return 1.0f;
		const float snapped = std::round(a_scale / kUiScaleStep) * kUiScaleStep;
		return std::clamp(snapped, kMinUiScale, kMaxUiScale);
	}

	float StepUiScale(float a_scale, int a_steps, float a_stepSize)
	{
		return ClampUiScale(ClampUiScale(a_scale) + static_cast<float>(a_steps) * a_stepSize);
	}

	PxRect ClampToScreen(PxRect a_rect, float a_displayW, float a_displayH, float a_minW, float a_minH, float a_margin)
	{
		const float maxW = std::max(1.0f, a_displayW - 2.0f * a_margin);
		const float maxH = std::max(1.0f, a_displayH - 2.0f * a_margin);
		a_rect.w = std::clamp(a_rect.w, std::min(a_minW, maxW), maxW);
		if (a_rect.h > 0.0f)
			a_rect.h = std::clamp(a_rect.h, std::min(a_minH, maxH), maxH);
		// a fit-the-content height is not known here: keep at least the top edge inside, and the card's header visible
		const float keepH = a_rect.h > 0.0f ? a_rect.h : std::min(a_minH, maxH);
		a_rect.x = std::clamp(a_rect.x, a_margin, std::max(a_margin, a_displayW - a_margin - a_rect.w));
		a_rect.y = std::clamp(a_rect.y, a_margin, std::max(a_margin, a_displayH - a_margin - keepH));
		return a_rect;
	}

	std::string StateToJson(const CardState& a_state)
	{
		std::string out = "{\n  \"version\": 1";
		if (a_state.hasPos)
			out += std::format(",\n  \"x\": {:.5f},\n  \"y\": {:.5f}", a_state.x, a_state.y);
		if (a_state.hasSize)
			out += std::format(",\n  \"w\": {:.1f},\n  \"h\": {:.1f}", a_state.w, a_state.h);
		if (a_state.hasScale)
			out += std::format(",\n  \"scale\": {:.2f}", a_state.scale);
		out += "\n}\n";
		return out;
	}

	namespace
	{
		/** The number after "key": in a flat JSON object; false when absent or not a finite number. */
		bool Find(std::string_view a_json, std::string_view a_key, float& a_value)
		{
			const std::string needle = std::format("\"{}\"", a_key);
			auto              at = a_json.find(needle);
			if (at == std::string_view::npos)
				return false;
			at += needle.size();
			while (at < a_json.size() && (a_json[at] == ' ' || a_json[at] == '\t' || a_json[at] == ':' || a_json[at] == '\r' || a_json[at] == '\n'))
				++at;
			float      value = 0.0f;
			const auto result = std::from_chars(a_json.data() + at, a_json.data() + a_json.size(), value);
			if (result.ec != std::errc() || !std::isfinite(value))
				return false;
			a_value = value;
			return true;
		}
	}

	bool StateFromJson(std::string_view a_json, CardState& a_state)
	{
		CardState parsed;
		float     x = 0, y = 0, w = 0, h = 0, scale = 0;
		if (Find(a_json, "x", x) && Find(a_json, "y", y) && x >= -1.0f && x <= 2.0f && y >= -1.0f && y <= 2.0f) {
			parsed.hasPos = true;
			parsed.x = x;
			parsed.y = y;
		}
		if (Find(a_json, "w", w) && w >= 100.0f && w <= 8000.0f) {
			parsed.hasSize = true;
			parsed.w = w;
			parsed.h = (Find(a_json, "h", h) && h >= 100.0f && h <= 8000.0f) ? h : 0.0f;
		}
		if (Find(a_json, "scale", scale)) {
			parsed.hasScale = true;
			parsed.scale = ClampUiScale(scale);
		}
		if (!parsed.hasPos && !parsed.hasSize && !parsed.hasScale)
			return false;
		a_state = parsed;
		return true;
	}
}
