// Net/Client/Game/Rendering/HUDRenderer.cs - HUD for health, armor, hunger
using System;
using OpenTK.Graphics.OpenGL4;
using OpenTK.Mathematics;

namespace Aetheris
{
    /// <summary>
    /// Renders player HUD elements (health, armor, hunger bars)
    /// </summary>
    public class HUDRenderer : IDisposable
    {
        private int shaderProgram;
        private int vao, vbo;

        // Bar configurations
        private const float BAR_WIDTH = 200f;
        private const float BAR_HEIGHT = 20f;
        private const float BAR_SPACING = 8f;
        private const float BAR_Y_OFFSET = 100f; // From bottom

        // Animation
        private float healthBarWidth = BAR_WIDTH;
        private float armorBarWidth = 0f;
        private float hungerBarWidth = BAR_WIDTH;
        private const float LERP_SPEED = 8f;

        // Flash effects
        private float damageFlashTimer = 0f;
        private float healFlashTimer = 0f;

        public HUDRenderer()
        {
            InitializeShaders();
            InitializeBuffers();
        }

        private void InitializeShaders()
        {
            string vertexShader = @"
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec4 aColor;

out vec2 TexCoord;
out vec4 Color;

uniform mat4 projection;

void main()
{
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
    Color = aColor;
}";

            string fragmentShader = @"
#version 330 core
in vec2 TexCoord;
in vec4 Color;

out vec4 FragColor;

void main()
{
    FragColor = Color;
}";

            int vs = CompileShader(ShaderType.VertexShader, vertexShader);
            int fs = CompileShader(ShaderType.FragmentShader, fragmentShader);

            shaderProgram = GL.CreateProgram();
            GL.AttachShader(shaderProgram, vs);
            GL.AttachShader(shaderProgram, fs);
            GL.LinkProgram(shaderProgram);

            GL.DeleteShader(vs);
            GL.DeleteShader(fs);
        }

        private void InitializeBuffers()
        {
            vao = GL.GenVertexArray();
            vbo = GL.GenBuffer();

            GL.BindVertexArray(vao);
            GL.BindBuffer(BufferTarget.ArrayBuffer, vbo);

            int stride = 8 * sizeof(float);

            GL.VertexAttribPointer(0, 2, VertexAttribPointerType.Float, false, stride, 0);
            GL.EnableVertexAttribArray(0);

            GL.VertexAttribPointer(1, 2, VertexAttribPointerType.Float, false, stride, 2 * sizeof(float));
            GL.EnableVertexAttribArray(1);

            GL.VertexAttribPointer(2, 4, VertexAttribPointerType.Float, false, stride, 4 * sizeof(float));
            GL.EnableVertexAttribArray(2);

            GL.BindVertexArray(0);
        }

        public void Update(PlayerStats stats, float deltaTime)
        {
            // Smooth bar animations
            float targetHealth = (stats.Health / stats.MaxHealth) * BAR_WIDTH;
            float targetArmor = (stats.Armor / stats.MaxArmor) * BAR_WIDTH;
            float targetHunger = (stats.Hunger / stats.MaxHunger) * BAR_WIDTH;

            healthBarWidth = Lerp(healthBarWidth, targetHealth, deltaTime * LERP_SPEED);
            armorBarWidth = Lerp(armorBarWidth, targetArmor, deltaTime * LERP_SPEED);
            hungerBarWidth = Lerp(hungerBarWidth, targetHunger, deltaTime * LERP_SPEED);

            // Update flash timers
            if (damageFlashTimer > 0f) damageFlashTimer -= deltaTime;
            if (healFlashTimer > 0f) healFlashTimer -= deltaTime;
        }

        public void OnDamageTaken() => damageFlashTimer = 0.3f;
        public void OnHealed() => healFlashTimer = 0.3f;

        public void Render(PlayerStats stats, Vector2i windowSize)
        {
            bool depthTestEnabled = GL.IsEnabled(EnableCap.DepthTest);
            bool blendEnabled = GL.IsEnabled(EnableCap.Blend);

            GL.Disable(EnableCap.DepthTest);
            GL.Enable(EnableCap.Blend);
            GL.BlendFunc(BlendingFactor.SrcAlpha, BlendingFactor.OneMinusSrcAlpha);

            GL.UseProgram(shaderProgram);

            var projection = Matrix4.CreateOrthographicOffCenter(0, windowSize.X, 0, windowSize.Y, -1, 1);
            int projLoc = GL.GetUniformLocation(shaderProgram, "projection");
            GL.UniformMatrix4(projLoc, false, ref projection);

            GL.BindVertexArray(vao);

            float centerX = windowSize.X / 2f;
            float startY = windowSize.Y - BAR_Y_OFFSET;

            // Health bar
            RenderBar(centerX - BAR_WIDTH / 2f, startY, BAR_WIDTH, BAR_HEIGHT,
            healthBarWidth, new Vector4(0.8f, 0.2f, 0.2f, 1f),
            new Vector4(0.3f, 0.1f, 0.1f, 0.8f), "HEALTH");

            startY += BAR_HEIGHT + BAR_SPACING;

            // Armor bar (only if armor > 0)
            if (stats.Armor > 1f || armorBarWidth > 1f)
            {
                RenderBar(centerX - BAR_WIDTH / 2f, startY, BAR_WIDTH, BAR_HEIGHT,
                         armorBarWidth, new Vector4(0.5f, 0.5f, 0.8f, 1f),
                         new Vector4(0.2f, 0.2f, 0.3f, 0.8f), "ARMOR");

                startY += BAR_HEIGHT + BAR_SPACING;
            }

            // Hunger bar
            Vector4 hungerColor = stats.IsStarving
                ? new Vector4(0.6f, 0.1f, 0.1f, 1f)
                : new Vector4(0.9f, 0.7f, 0.2f, 1f);

            RenderBar(centerX - BAR_WIDTH / 2f, startY, BAR_WIDTH, BAR_HEIGHT,
                     hungerBarWidth, hungerColor,
                     new Vector4(0.3f, 0.25f, 0.1f, 0.8f), "HUNGER");

            // Damage flash overlay
            if (damageFlashTimer > 0f)
            {
                float alpha = damageFlashTimer / 0.3f * 0.3f;
                DrawFullScreenOverlay(new Vector4(1f, 0f, 0f, alpha), windowSize);
            }

            // Heal flash overlay
            if (healFlashTimer > 0f)
            {
                float alpha = healFlashTimer / 0.3f * 0.2f;
                DrawFullScreenOverlay(new Vector4(0f, 1f, 0f, alpha), windowSize);
            }

            GL.BindVertexArray(0);
            GL.UseProgram(0);

            if (depthTestEnabled) GL.Enable(EnableCap.DepthTest);
            if (!blendEnabled) GL.Disable(EnableCap.Blend);
        }

        private void RenderBar(float x, float y, float width, float height,
                               float fillWidth, Vector4 fillColor, Vector4 bgColor, string label)
        {
            // Background
            DrawRect(x, y, width, height, bgColor);

            // Fill
            if (fillWidth > 0.5f)
            {
                DrawRect(x, y, fillWidth, height, fillColor);
            }

            // Border
            DrawRectOutline(x, y, width, height, 2f, new Vector4(0.1f, 0.1f, 0.1f, 1f));

            // Inner highlight
            DrawRect(x + 2, y + height - 4, width - 4, 2, new Vector4(1f, 1f, 1f, 0.2f));
        }

        private void DrawRect(float x, float y, float w, float h, Vector4 color)
        {
            float[] vertices = new float[]
            {
                x,     y,       0, 0,        color.X, color.Y, color.Z, color.W,
                x + w, y,       1, 0,        color.X, color.Y, color.Z, color.W,
                x + w, y + h,   1, 1,        color.X, color.Y, color.Z, color.W,

                x,     y,       0, 0,        color.X, color.Y, color.Z, color.W,
                x + w, y + h,   1, 1,        color.X, color.Y, color.Z, color.W,
                x,     y + h,   0, 1,        color.X, color.Y, color.Z, color.W,
            };

            GL.BindBuffer(BufferTarget.ArrayBuffer, vbo);
            GL.BufferData(BufferTarget.ArrayBuffer, vertices.Length * sizeof(float), vertices, BufferUsageHint.DynamicDraw);
            GL.DrawArrays(PrimitiveType.Triangles, 0, 6);
        }

        private void DrawRectOutline(float x, float y, float w, float h, float thickness, Vector4 color)
        {
            DrawRect(x, y + h - thickness, w, thickness, color);
            DrawRect(x, y, w, thickness, color);
            DrawRect(x, y, thickness, h, color);
            DrawRect(x + w - thickness, y, thickness, h, color);
        }

        private void DrawFullScreenOverlay(Vector4 color, Vector2i windowSize)
        {
            DrawRect(0, 0, windowSize.X, windowSize.Y, color);
        }

        private float Lerp(float a, float b, float t) => a + (b - a) * Math.Clamp(t, 0f, 1f);

        private int CompileShader(ShaderType type, string source)
        {
            int shader = GL.CreateShader(type);
            GL.ShaderSource(shader, source);
            GL.CompileShader(shader);

            GL.GetShader(shader, ShaderParameter.CompileStatus, out int success);
            if (success == 0)
            {
                string info = GL.GetShaderInfoLog(shader);
                Console.WriteLine($"[HUDRenderer] Shader compilation failed: {info}");
            }

            return shader;
        }

        public void Dispose()
        {
            GL.DeleteVertexArray(vao);
            GL.DeleteBuffer(vbo);
            GL.DeleteProgram(shaderProgram);
        }
    }
}
