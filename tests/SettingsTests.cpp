// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// Offline tests for the card's remembered layout (clamping, size multiplier, the small JSON file).
#include "CardState.h"
#include "Config.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

using namespace ti;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                      \
	do {                                                                 \
		++g_checks;                                                      \
		if (!(cond)) {                                                   \
			++g_failures;                                                \
			std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);  \
		}                                                                \
	} while (0)

static bool Near(float a, float b, float eps = 0.001f) { return std::fabs(a - b) <= eps; }

int main()
{
	// ---- size multiplier ----
	CHECK(Near(ClampUiScale(1.0f), 1.0f));
	CHECK(Near(ClampUiScale(0.1f), kMinUiScale));
	CHECK(Near(ClampUiScale(0.75f), 0.75f));
	CHECK(Near(ClampUiScale(5.0f), kMaxUiScale));
	CHECK(Near(ClampUiScale(1.23f), 1.25f));  // snapped to 0.05
	CHECK(Near(ClampUiScale(std::numeric_limits<float>::quiet_NaN()), 1.0f));
	CHECK(Near(ClampUiScale(std::numeric_limits<float>::infinity()), 1.0f));
	CHECK(Near(StepUiScale(1.0f, 2, 0.05f), 1.1f));
	CHECK(Near(StepUiScale(1.0f, -1, 0.1f), 0.9f));
	CHECK(Near(StepUiScale(0.75f, -3, 0.1f), 0.75f));
	CHECK(Near(StepUiScale(2.0f, 3, 0.1f), 2.0f));
	{
		float s = 1.0f;
		for (int i = 0; i < 40; ++i)
			s = StepUiScale(s, 1, 0.1f);
		CHECK(Near(s, 2.0f));
		for (int i = 0; i < 40; ++i)
			s = StepUiScale(s, -1, 0.1f);
		CHECK(Near(s, 0.75f));
	}

	// ---- clamping to the screen ----
	{
		const auto r = ClampToScreen({ 100, 100, 460, 600 }, 1920, 1080, 300, 200, 8);
		CHECK(Near(r.x, 100) && Near(r.y, 100) && Near(r.w, 460) && Near(r.h, 600));
	}
	{
		const auto r = ClampToScreen({ -50, -20, 460, 600 }, 1920, 1080, 300, 200, 8);
		CHECK(Near(r.x, 8) && Near(r.y, 8));
	}
	{
		const auto r = ClampToScreen({ 1900, 1000, 460, 600 }, 1920, 1080, 300, 200, 8);
		CHECK(Near(r.x, 1920 - 8 - 460) && Near(r.y, 1080 - 8 - 600));
	}
	{
		const auto r = ClampToScreen({ 0, 0, 5000, 5000 }, 1920, 1080, 300, 200, 8);
		CHECK(Near(r.w, 1920 - 16) && Near(r.h, 1080 - 16) && Near(r.x, 8) && Near(r.y, 8));
	}
	{
		const auto r = ClampToScreen({ 500, 500, 100, 50 }, 1920, 1080, 300, 200, 8);
		CHECK(Near(r.w, 300) && Near(r.h, 200));
	}
	{
		// fit-the-content height stays 0, and the top edge stays on screen
		const auto r = ClampToScreen({ 1900, 1070, 460, 0 }, 1920, 1080, 300, 200, 8);
		CHECK(Near(r.h, 0) && Near(r.x, 1920 - 8 - 460) && Near(r.y, 1080 - 8 - 200));
	}
	{
		// a screen smaller than the minimum size does not break it
		const auto r = ClampToScreen({ 0, 0, 460, 600 }, 200, 150, 300, 200, 8);
		CHECK(r.w <= 200 && r.h <= 150 && r.x >= 0 && r.y >= 0);
	}

	// ---- the file ----
	{
		CardState s;
		s.hasPos = true;
		s.x = 0.625f;
		s.y = 0.0125f;
		s.hasSize = true;
		s.w = 520.0f;
		s.h = 700.5f;
		s.hasScale = true;
		s.scale = 1.25f;
		const auto json = StateToJson(s);
		CardState  back;
		CHECK(StateFromJson(json, back));
		CHECK(back.hasPos && Near(back.x, 0.625f, 0.0001f) && Near(back.y, 0.0125f, 0.0001f));
		CHECK(back.hasSize && Near(back.w, 520.0f) && Near(back.h, 700.5f));
		CHECK(back.hasScale && Near(back.scale, 1.25f));
	}
	{
		CardState s;
		s.hasScale = true;
		s.scale = 1.5f;
		CardState back;
		CHECK(StateFromJson(StateToJson(s), back));
		CHECK(!back.hasPos && !back.hasSize && back.hasScale && Near(back.scale, 1.5f));
	}
	{
		CardState s;
		s.hasSize = true;
		s.w = 480;
		s.h = 0;  // auto height survives
		CardState back;
		CHECK(StateFromJson(StateToJson(s), back));
		CHECK(back.hasSize && Near(back.w, 480) && Near(back.h, 0));
	}
	{
		CardState back;
		back.hasScale = true;
		back.scale = 1.1f;
		const CardState before = back;
		CHECK(!StateFromJson("", back));
		CHECK(!StateFromJson("not json at all", back));
		CHECK(!StateFromJson("{ \"version\": 1 }", back));
		CHECK(!StateFromJson("{ \"x\": nan, \"y\": 0.1 }", back));
		CHECK(back == before);  // a failed read leaves the state alone
	}
	{
		CardState back;
		CHECK(StateFromJson("{ \"x\": 9.0, \"y\": 0.1, \"scale\": 9.0 }", back));
		CHECK(!back.hasPos);                                  // x out of range: position ignored
		CHECK(back.hasScale && Near(back.scale, kMaxUiScale));  // scale clamped
	}
	{
		CardState back;
		CHECK(StateFromJson("{\"x\":0.5,\"y\":0.25,\"w\":10,\"h\":900}", back));  // width below the floor: size ignored
		CHECK(back.hasPos && !back.hasSize);
	}

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
