#include "editor/panels/VfxPanel.hpp"
#include "editor/Win95Widgets.hpp"

#include <imgui.h>

namespace editor
{
    void VfxPanel::draw(Context &ctx)
    {
        ImGui::Begin("VFX", &ctx.showVfx);
        auto &v = ctx.vfx;

        if (ImGui::CollapsingHeader("Sky gradient", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ui95::Checkbox("Sky enabled", &v.skyEnabled);
            ImGui::BeginDisabled(!v.skyEnabled);
            ImGui::ColorEdit3("Sky top", &v.skyTop.x);
            ImGui::ColorEdit3("Sky mid", &v.skyMid.x);
            ImGui::ColorEdit3("Sky bottom", &v.skyBottom.x);
            ImGui::EndDisabled();
        }

        if (ImGui::CollapsingHeader("Sun", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ui95::Checkbox("Sun enabled", &v.sunEnabled);
            ImGui::BeginDisabled(!v.sunEnabled);
            ui95::Checkbox("Fixed on horizon (world-anchored)", &v.sunWorldAnchored);
            if (v.sunWorldAnchored)
            {
                ui95::SliderFloat("Azimuth", &v.sunAzimuth, -180.0f, 180.0f, "%.1f\xc2\xb0");
                ui95::SliderFloat("Elevation", &v.sunElevation, -30.0f, 90.0f, "%.1f\xc2\xb0");
            }
            else
            {
                ui95::SliderFloat2("Screen position (NDC)", &v.sunPos.x, -1.0f, 1.0f);
            }
            ui95::SliderFloat("Radius", &v.sunRadius, 0.02f, 0.6f);
            ImGui::ColorEdit3("Color", &v.sunColor.x);
            ui95::SliderInt("Stripes", &v.sunStripes, 0, 16);
            ImGui::EndDisabled();
        }

        if (ImGui::CollapsingHeader("Grid floor", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ui95::Checkbox("Grid enabled", &v.gridEnabled);
            ImGui::BeginDisabled(!v.gridEnabled);
            ImGui::ColorEdit3("Grid color", &v.gridColor.x);
            ui95::SliderFloat("Cell size", &v.gridScale, 0.25f, 64.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
            ui95::SliderFloat("Fade distance", &v.gridFade, 10.0f, 1000.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
            ui95::SliderFloat("Line width", &v.gridLineWidth, 0.005f, 0.25f);
            ui95::DragFloat("Plane Y", &v.horizonY, 0.05f);
            ImGui::EndDisabled();
        }

        if (ImGui::CollapsingHeader("Fog", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ui95::Checkbox("Fog enabled", &v.fogEnabled);
            ImGui::BeginDisabled(!v.fogEnabled);
            ImGui::ColorEdit3("Fog color", &v.fogColor.x);
            ImGui::DragFloatRange2("Fog range", &v.fogStart, &v.fogEnd, 0.5f, 0.0f, 5000.0f, "%.1f");
            ImGui::EndDisabled();
        }

        if (ImGui::CollapsingHeader("Neon boost", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ui95::Checkbox("Neon enabled", &v.neonEnabled);
            ImGui::BeginDisabled(!v.neonEnabled);
            ui95::SliderFloat("Intensity", &v.neonIntensity, 1.0f, 6.0f);
            ImGui::EndDisabled();
        }

        if (ImGui::CollapsingHeader("Bloom", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ui95::Checkbox("Bloom enabled", &v.bloomEnabled);
            ImGui::BeginDisabled(!v.bloomEnabled);
            ui95::SliderFloat("Threshold", &v.bloomThreshold, 0.0f, 2.0f);
            ui95::SliderFloat("Intensity", &v.bloomIntensity, 0.0f, 4.0f);
            ui95::SliderFloat("Radius", &v.bloomRadius, 0.25f, 4.0f);
            ui95::SliderInt("Iterations", &v.bloomIterations, 1, 10);
            ImGui::EndDisabled();
        }

        if (ImGui::CollapsingHeader("CRT scanlines"))
        {
            ui95::Checkbox("Scanlines enabled", &v.scanlinesEnabled);
            ImGui::BeginDisabled(!v.scanlinesEnabled);
            ui95::SliderFloat("Strength", &v.scanlineStrength, 0.0f, 0.6f);
            ImGui::EndDisabled();
        }

        if (ImGui::CollapsingHeader("Explosion light", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ui95::Checkbox("Explosions cast light", &v.explosionLightEnabled);
            ImGui::BeginDisabled(!v.explosionLightEnabled);
            ui95::SliderFloat("Brightness", &v.explosionLightIntensity, 0.0f, 8.0f);
            ui95::SliderFloat("Reach (x blast radius)", &v.explosionLightReach, 0.5f, 6.0f);
            ImGui::ColorEdit3("Light color", &v.explosionLightColor.x);
            ImGui::EndDisabled();
            ImGui::TextDisabled("Lights nearby geometry and the grid floor.");
        }

        if (ImGui::CollapsingHeader("Wireframe fill"))
        {
            ui95::Checkbox("Solid black fill behind wireframes", &v.wireframeFill);
            ImGui::TextDisabled("Off lets the sky show through every object.");
        }

        if (ImGui::CollapsingHeader("Quality", ImGuiTreeNodeFlags_DefaultOpen))
        {
            // Mirrors the standalone game's settings menu. Changing this
            // recompiles every post-process shader and resizes the bloom chain,
            // so it stutters for a frame — that is the switch working, not a bug.
            int q = static_cast<int>(v.quality);
            if (ui95::Combo("Level", &q, "Low\0Medium\0High\0"))
                v.quality = static_cast<Quality>(q);
            ImGui::TextDisabled("Low: quarter-res bloom, fewer/cheaper passes.");
            ImGui::TextDisabled("Preview how the game runs on a phone.");
        }

        ImGui::End();
    }
}
