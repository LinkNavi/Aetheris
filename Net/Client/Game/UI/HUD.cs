// Net/Client/Game/UI/HUD.cs - Gothic vampire hunter themed HUD
using System;
using OpenTK.Graphics.OpenGL4;
using OpenTK.Mathematics;

namespace Aetheris
{
    /// <summary>


    /// </summary>
    public class HUD : IDisposable
    {
        private int shaderProgram;
        private int vao, vbo;
        
        // Animation state
        private float healthBarWidth = 0f;
        private float armorBarWidth = 0f;
        private float hungerBarWidth = 0f;
        private const float LERP_SPEED = 12f;
        
        // Flash effects
        private float damageFlashTimer = 0f;
        private float healFlashTimer = 0f;
        private float heartbeatPulse = 0f;
        
        // Gothic color palette
        private static readonly Vector4 BLOOD_RED = new Vector4(0.8f, 0.1f, 0.1f, 1f);
        private static readonly Vector4 DARK_CRIMSON = new Vector4(0.5f, 0.05f, 0.05f, 1f);
        private static readonly Vector4 GOLD_ACCENT = new Vector4(0.9f, 0.7f, 0.3f, 1f);
        private static readonly Vector4 SILVER = new Vector4(0.75f, 0.75f, 0.8f, 1f);
        private static readonly Vector4 DARK_BG = new Vector4(0.08f, 0.05f, 0.05f, 0.95f);
        private static readonly Vector4 ORNATE_BORDER = new Vector4(0.4f, 0.25f, 0.15f, 1f);
        
        public HUD()
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
            float targetHealth = (stats.Health / stats.MaxHealth) * 200f;
            float targetArmor = (stats.Armor / stats.MaxArmor) * 180f;
            float targetHunger = (stats.Hunger / stats.MaxHunger) * 180f;
            
            healthBarWidth = Lerp(healthBarWidth, targetHealth, deltaTime * LERP_SPEED);
            armorBarWidth = Lerp(armorBarWidth, targetArmor, deltaTime * LERP_SPEED);
            hungerBarWidth = Lerp(hungerBarWidth, targetHunger, deltaTime * LERP_SPEED);
            
            // Heartbeat pulse effect when low health
            if (stats.HealthPercent < 0.3f)
            {
                heartbeatPulse += deltaTime * 8f;
                if (heartbeatPulse > MathF.PI * 2f) heartbeatPulse -= MathF.PI * 2f;
            }
            else
            {
                heartbeatPulse = 0f;
            }
            
            // Update flash timers
            if (damageFlashTimer > 0f) damageFlashTimer -= deltaTime;
            if (healFlashTimer > 0f) healFlashTimer -= deltaTime;
        }
        
        public void OnDamageTaken() => damageFlashTimer = 0.4f;
        public void OnHealed() => healFlashTimer = 0.4f;
        
        public void Render(PlayerStats stats, Vector2i windowSize)
        {
            bool depthTestEnabled = GL.IsEnabled(EnableCap.DepthTest);
            bool blendEnabled = GL.IsEnabled(EnableCap.Blend);
            
            GL.Disable(EnableCap.DepthTest);
            GL.Enable(EnableCap.Blend);
            GL.BlendFunc(BlendingFactor.SrcAlpha, BlendingFactor.OneMinusSrcAlpha);

            GL.UseProgram(shaderProgram);
            
            var projection = Matrix4.CreateOrthographicOffCenter(0, windowSize.X, windowSize.Y, 0, -1, 1);
            int projLoc = GL.GetUniformLocation(shaderProgram, "projection");
            GL.UniformMatrix4(projLoc, false, ref projection);

            GL.BindVertexArray(vao);
            
            // Render left side panel (health/armor)
            RenderLeftPanel(stats, windowSize);
            
            // Render right side panel (hunger/stamina)
            RenderRightPanel(stats, windowSize);
            
            // Damage flash overlay
            if (damageFlashTimer > 0f)
            {
                float alpha = damageFlashTimer / 0.4f * 0.4f;
                Vector4 flashColor = BLOOD_RED;
                flashColor.W = alpha;
                DrawFullScreenOverlay(flashColor, windowSize);
            }
            
            // Heal flash overlay
            if (healFlashTimer > 0f)
            {
                float alpha = healFlashTimer / 0.4f * 0.25f;
                Vector4 flashColor = GOLD_ACCENT;
                flashColor.W = alpha;
                DrawFullScreenOverlay(flashColor, windowSize);
            }

            GL.BindVertexArray(0);
            GL.UseProgram(0);

            if (depthTestEnabled) GL.Enable(EnableCap.DepthTest);
            if (!blendEnabled) GL.Disable(EnableCap.Blend);
        }
        
        private void RenderLeftPanel(PlayerStats stats, Vector2i windowSize)
        {
            float panelX = 20f;
            float panelY = 20f;
            float panelWidth = 280f;
            float panelHeight = 180f;
            
            // Gothic ornate background
            DrawGothicPanel(panelX, panelY, panelWidth, panelHeight);
            
            // Health bar
            float healthY = panelY + 50f;
            RenderGothicBar(
                panelX + 20f, healthY, 240f, 28f,
                healthBarWidth, stats.HealthPercent,
                BLOOD_RED, DARK_CRIMSON,
                "VITALITY",
                $"{(int)stats.Health}/{(int)stats.MaxHealth}",
                heartbeatPulse
            );
            
            // Armor bar (if player has armor)
            if (stats.Armor > 0f || armorBarWidth > 1f)
            {
                float armorY = healthY + 45f;
                RenderGothicBar(
                    panelX + 30f, armorY, 220f, 22f,
                    armorBarWidth, stats.Armor / stats.MaxArmor,
                    SILVER, new Vector4(0.3f, 0.3f, 0.35f, 1f),
                    "ARMOR",
                    $"{(int)stats.Armor}",
                    0f
                );
            }
        }
        
        private void RenderRightPanel(PlayerStats stats, Vector2i windowSize)
        {
            float panelX = windowSize.X - 300f;
            float panelY = 20f;
            float panelWidth = 280f;
            float panelHeight = 120f;
            
            // Gothic ornate background
            DrawGothicPanel(panelX, panelY, panelWidth, panelHeight);
            
            // Hunger/Stamina bar
            float hungerY = panelY + 50f;
            Vector4 hungerColor = stats.IsStarving 
                ? new Vector4(0.7f, 0.2f, 0.1f, 1f)
                : new Vector4(0.9f, 0.6f, 0.2f, 1f);
            
            RenderGothicBar(
                panelX + 20f, hungerY, 240f, 28f,
                hungerBarWidth, stats.HungerPercent,
                hungerColor, new Vector4(0.4f, 0.2f, 0.1f, 1f),
                "STAMINA",
                $"{(int)stats.Hunger}%",
                0f
            );
        }
        
        private void DrawGothicPanel(float x, float y, float w, float h)
        {
            // Dark background with vignette effect
            DrawRect(x, y, w, h, DARK_BG);
            
            // Ornate border frame (multiple layers for depth)
            float borderThickness = 3f;
            
            // Outer gold accent
            DrawRectOutline(x - 2f, y - 2f, w + 4f, h + 4f, 1f, GOLD_ACCENT);
            
            // Main ornate border
            DrawRectOutline(x, y, w, h, borderThickness, ORNATE_BORDER);
            
            // Inner highlight
            DrawRectOutline(x + borderThickness, y + borderThickness, 
                          w - borderThickness * 2, h - borderThickness * 2, 
                          1f, new Vector4(0.5f, 0.4f, 0.3f, 0.4f));
            
            // Corner decorations (small diagonal lines)
            float cornerSize = 12f;
            DrawLine(x, y, x + cornerSize, y, 2f, GOLD_ACCENT);
            DrawLine(x, y, x, y + cornerSize, 2f, GOLD_ACCENT);
            
            DrawLine(x + w, y, x + w - cornerSize, y, 2f, GOLD_ACCENT);
            DrawLine(x + w, y, x + w, y + cornerSize, 2f, GOLD_ACCENT);
            
            DrawLine(x, y + h, x + cornerSize, y + h, 2f, GOLD_ACCENT);
            DrawLine(x, y + h, x, y + h - cornerSize, 2f, GOLD_ACCENT);
            
            DrawLine(x + w, y + h, x + w - cornerSize, y + h, 2f, GOLD_ACCENT);
            DrawLine(x + w, y + h, x + w, y + h - cornerSize, 2f, GOLD_ACCENT);
        }
        
        private void RenderGothicBar(float x, float y, float w, float h,
                                     float fillWidth, float fillPercent,
                                     Vector4 fillColor, Vector4 bgColor,
                                     string label, string value, float pulse)
        {
            // Background
            DrawRect(x, y, w, h, bgColor);
            
            // Fill with gradient effect
            if (fillWidth > 0.5f)
            {
                float actualFill = Math.Min(fillWidth, w - 4f);
                
                // Pulsing effect for low health
                if (pulse > 0f)
                {
                    float pulseIntensity = MathF.Sin(pulse) * 0.3f + 0.7f;
                    fillColor.X *= pulseIntensity;
                    fillColor.Y *= pulseIntensity;
                    fillColor.Z *= pulseIntensity;
                }
                
                // Main fill
                DrawRect(x + 2f, y + 2f, actualFill, h - 4f, fillColor);
                
                // Highlight on top
                Vector4 highlight = fillColor;
                highlight.W = 0.4f;
                DrawRect(x + 2f, y + 2f, actualFill, 3f, highlight);
                
                //危険 effect when very low
                if (fillPercent < 0.2f)
                {
                    float flashAlpha = (MathF.Sin(DateTime.Now.Millisecond * 0.01f) + 1f) * 0.3f;
                    Vector4 warningFlash = new Vector4(1f, 0.2f, 0.2f, flashAlpha);
                    DrawRect(x + 2f, y + 2f, actualFill, h - 4f, warningFlash);
                }
            }
            
            // Ornate border
            DrawRectOutline(x, y, w, h, 2f, ORNATE_BORDER);
            DrawRectOutline(x + 1f, y + 1f, w - 2f, h - 2f, 1f, GOLD_ACCENT);
            
            // Notches every 20% for visual measurement
            for (int i = 1; i < 5; i++)
            {
                float notchX = x + (w * i / 5f);
                DrawLine(notchX, y, notchX, y + 4f, 1f, new Vector4(0.3f, 0.2f, 0.15f, 0.8f));
                DrawLine(notchX, y + h - 4f, notchX, y + h, 1f, new Vector4(0.3f, 0.2f, 0.15f, 0.8f));
            }
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
            DrawRect(x, y + h - thickness, w, thickness, color); // Bottom
            DrawRect(x, y, w, thickness, color); // Top
            DrawRect(x, y, thickness, h, color); // Left
            DrawRect(x + w - thickness, y, thickness, h, color); // Right
        }
        
        private void DrawLine(float x1, float y1, float x2, float y2, float thickness, Vector4 color)
        {
            float dx = x2 - x1;
            float dy = y2 - y1;
            float length = MathF.Sqrt(dx * dx + dy * dy);
            float angle = MathF.Atan2(dy, dx);
            
            float halfThickness = thickness * 0.5f;
            float perpX = -MathF.Sin(angle) * halfThickness;
            float perpY = MathF.Cos(angle) * halfThickness;
            
            float[] vertices = new float[]
            {
                x1 + perpX, y1 + perpY, 0, 0, color.X, color.Y, color.Z, color.W,
                x1 - perpX, y1 - perpY, 0, 1, color.X, color.Y, color.Z, color.W,
                x2 - perpX, y2 - perpY, 1, 1, color.X, color.Y, color.Z, color.W,
                
                x1 + perpX, y1 + perpY, 0, 0, color.X, color.Y, color.Z, color.W,
                x2 - perpX, y2 - perpY, 1, 1, color.X, color.Y, color.Z, color.W,
                x2 + perpX, y2 + perpY, 1, 0, color.X, color.Y, color.Z, color.W,
            };
            
            GL.BindBuffer(BufferTarget.ArrayBuffer, vbo);
            GL.BufferData(BufferTarget.ArrayBuffer, vertices.Length * sizeof(float), vertices, BufferUsageHint.DynamicDraw);
            GL.DrawArrays(PrimitiveType.Triangles, 0, 6);
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
                Console.WriteLine($"[HUD] Shader compilation failed: {info}");
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
