// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
#include "PCH.h"

#include "Camera.h"

#include "OverlayPlatform.h"

namespace ti
{
	namespace
	{
		struct CameraData
		{
			Mat4               matrix;
			RE::NiRect<float>  port;
			std::array<float, 4> edges{};  // left, right, top, bottom (NiRect keeps them protected)
			RE::NiCamera*      camera = nullptr;
		};

		std::optional<CameraData> ReadCamera()
		{
			auto* camera = RE::Main::WorldRootCamera();
			if (!camera)
				return std::nullopt;
			CameraData d;
			d.camera = camera;
			std::memcpy(d.matrix.m, camera->GetRuntimeData().worldToCam, sizeof(d.matrix.m));
			d.port = camera->GetRuntimeData2().port;
			static_assert(sizeof(d.port) == sizeof(d.edges));
			std::memcpy(d.edges.data(), &d.port, sizeof(d.edges));
			return d;
		}

		/** Where the ENGINE puts a world point on screen, as a fraction of the window (0,0 top-left). */
		bool EngineScreenFraction(const CameraData& a_cam, Vec3 a_point, float& a_fx, float& a_fy)
		{
			float x = 0, y = 0, z = 0;
			const RE::NiPoint3 p{ a_point.x, a_point.y, a_point.z };
			if (!RE::NiCamera::WorldPtToScreenPt3(a_cam.matrix.m, a_cam.port, p, x, y, z, 1e-5f))
				return false;
			const float w = a_cam.edges[1] - a_cam.edges[0];
			const float h = a_cam.edges[3] - a_cam.edges[2];
			if (std::fabs(w) < 1e-6f || std::fabs(h) < 1e-6f)
				return false;
			a_fx = (x - a_cam.edges[0]) / w;
			a_fy = (y - a_cam.edges[2]) / h;
			return true;
		}

		/** Builds the ray for a window fraction, choosing the y orientation the engine's own projection agrees with. */
		RayResult BuildRay(const CameraData& a_cam, float a_fracX, float a_fracY, RayResult a_result)
		{
			struct Try
			{
				bool  yUp;
				Ray   ray;
				float error;
				bool  built;
			};
			Try tries[2];
			for (int i = 0; i < 2; ++i) {
				tries[i].yUp = (i == 0);
				float nx = 0, ny = 0;
				CursorToNdc(a_fracX, a_fracY, tries[i].yUp, nx, ny);
				tries[i].built = NdcToRay(a_cam.matrix, nx, ny, tries[i].ray);
				tries[i].error = 1e9f;
				if (tries[i].built) {
					float fx = 0, fy = 0;
					const Vec3 probe = tries[i].ray.origin + tries[i].ray.dir * 200.0f;
					if (EngineScreenFraction(a_cam, probe, fx, fy))
						tries[i].error = std::max(std::fabs(fx - a_fracX), std::fabs(fy - a_fracY));
				}
			}
			if (!tries[0].built && !tries[1].built) {
				a_result.ok = false;
				a_result.check = "failed: camera matrix could not be inverted";
				return a_result;
			}
			constexpr float kTolerance = 0.02f;  // 2 % of the window
			int             pick = 0;
			if (tries[0].error <= kTolerance)
				a_result.check = "verified";
			else if (tries[1].error <= kTolerance) {
				pick = 1;
				a_result.check = "verified (y flipped)";
			} else {
				pick = tries[0].built ? 0 : 1;
				a_result.check = std::format("unverified: engine projection disagrees (error {:.3f} / {:.3f})", tries[0].error, tries[1].error);
			}
			a_result.ray = tries[pick].ray;
			a_result.ok = true;
			return a_result;
		}
	}

	RayResult CursorRay()
	{
		RayResult out;
		out.cursor.used = true;
		const auto cam = ReadCamera();
		auto*      cursor = RE::MenuCursor::GetSingleton();
		if (!cam || !cursor) {
			out.check = "failed: no camera or menu cursor";
			return out;
		}
		const auto& rt = cursor->GetRuntimeData();
		out.cursor.x = rt.cursorPosX;
		out.cursor.y = rt.cursorPosY;
		float w = rt.screenWidthX;
		float h = rt.screenWidthY;
		const auto screen = RE::BSGraphics::Renderer::GetScreenSize();
		// The cursor is reported in window pixels; if the reported area looks like something else, use the renderer's size.
		if (!(w > 1.0f && h > 1.0f) || out.cursor.x > w * 1.01f || out.cursor.y > h * 1.01f) {
			out.notes.push_back(std::format("cursor area {}x{} did not fit the cursor; using the renderer size {}x{}", w, h, screen.width, screen.height));
			w = static_cast<float>(screen.width);
			h = static_cast<float>(screen.height);
		}
		out.cursor.areaW = w;
		out.cursor.areaH = h;
		out.cursor.fracX = std::clamp(out.cursor.x / w, 0.0f, 1.0f);
		out.cursor.fracY = std::clamp(out.cursor.y / h, 0.0f, 1.0f);
		return BuildRay(*cam, out.cursor.fracX, out.cursor.fracY, std::move(out));
	}

	RayResult CentreRay()
	{
		RayResult out;
		const auto cam = ReadCamera();
		if (!cam) {
			out.check = "failed: no camera";
			return out;
		}
		return BuildRay(*cam, 0.5f, 0.5f, std::move(out));
	}

	bool CursorFraction(float& a_fractionX, float& a_fractionY)
	{
		auto* cursor = RE::MenuCursor::GetSingleton();
		if (!cursor)
			return false;
		const auto& rt = cursor->GetRuntimeData();
		float       w = rt.screenWidthX;
		float       h = rt.screenWidthY;
		if (!(w > 1.0f && h > 1.0f) || rt.cursorPosX > w * 1.01f || rt.cursorPosY > h * 1.01f) {
			const auto screen = RE::BSGraphics::Renderer::GetScreenSize();
			w = static_cast<float>(screen.width);
			h = static_cast<float>(screen.height);
		}
		if (!(w > 1.0f && h > 1.0f))
			return false;
		a_fractionX = std::clamp(rt.cursorPosX / w, 0.0f, 1.0f);
		a_fractionY = std::clamp(rt.cursorPosY / h, 0.0f, 1.0f);
		return true;
	}

	bool WorldToScreen(Vec3 a_point, float& a_fractionX, float& a_fractionY)
	{
		const auto cam = ReadCamera();
		if (!cam || !EngineScreenFraction(*cam, a_point, a_fractionX, a_fractionY))
			return false;
		return true;
	}
}
