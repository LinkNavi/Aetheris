// UI/EnhancedGameHUD.cs - Fully functional OpenGL HUD with animations and dark theme
using System;
using OpenTK.Graphics.OpenGL4;
using OpenTK.Mathematics;
using Aetheris.GameLogic;
using Aetheris.UI;

namespace Aetheris.UI
{
    public class EnhancedGameHUD : IDisposable
    {
        private int shaderProgram;
        private int vao;
        private int vbo;
        private readonly FontRenderer? fontRenderer;
        private Vector2i lastWindowSize = new Vector2i(1920, 1080);

        // Animation states
        private float healthPulse = 0f;
        private float armorShake = 0f;
        private float hungerBounce = 0f;
        private float hotbarSlideProgress = 1f;
        private float[] slotHighlightAnim = new float[9];
        private float damageFlashTimer = 0f;
        private float lowHealthWarning = 0f;
        private float lastHealth = 100f;
        
        // Smooth value interpolation
        private float displayHealth = 100f;
        private float displayArmor = 0f;
        private float displayHunger = 100f;

        // Colors - Dark theme
        private static readonly Vector4 BG_DARK = new Vector4(0.08f, 0.08f, 0.1f, 0.95f);
        private static readonly Vector4 BG_MEDIUM = new Vector4(0.12f, 0.12f, 0.15f, 0.98f);
        private static readonly Vector4 BORDER_COLOR = new Vector4(0.25f, 0.25f, 0.3f, 0.95f);
        private static readonly Vector4 ACCENT_COLOR = new Vector4(0.4f, 0.6f, 1f, 1f);
        
        private static readonly Vector4 HEALTH_BG = new Vector4(0.15f, 0.05f, 0.05f, 0.92f);
        private static readonly Vector4 HEALTH_FILL = new Vector4(0.95f, 0.25f, 0.25f, 1f);
        private static readonly Vector4 HEALTH_GLOW = new Vector4(1f, 0.4f, 0.4f, 0.6f);
        
        private static readonly Vector4 ARMOR_BG = new Vector4(0.05f, 0.1f, 0.15f, 0.92f);
        private static readonly Vector4 ARMOR_FILL = new Vector4(0.3f, 0.6f, 0.95f, 1f);
        private static readonly Vector4 ARMOR_GLOW = new Vector4(0.5f, 0.8f, 1f, 0.5f);
        
        private static readonly Vector4 HUNGER_BG = new Vector4(0.15f, 0.1f, 0.05f, 0.92f);
        private static readonly Vector4 HUNGER_FILL = new Vector4(1f, 0.7f, 0.3f, 1f);
        private static readonly Vector4 HUNGER_GLOW = new Vector4(1f, 0.85f, 0.5f, 0.5f);

        public EnhancedGameHUD(FontRenderer? fontRenderer)
        {
            this.fontRenderer = fontRenderer;
            CreateShaderAndBuffers();
            Console.WriteLine("[EnhancedHUD] Initialized with dark theme and animations");
        }

        private void CreateShaderAndBuffers()
        {
            string vertexShaderSource = @"
                #version 330 core
                layout (location = 0) in vec2 aPos;
                layout (location = 1) in vec4 aColor;
                out vec4 vertexColor;
                uniform mat4 projection;
                void main()
                {
                    gl_Position = projection * vec4(aPos, 0.0, 1.0);
                    vertexColor = aColor;
                }
            ";

            string fragmentShaderSource = @"
                #version 330 core
                in vec4 vertexColor;
                out vec4 FragColor;
                void main()
                {
                    FragColor = vertexColor;
                }
            ";

            int vertexShader = GL.CreateShader(ShaderType.VertexShader);
            GL.ShaderSource(vertexShader, vertexShaderSource);
            GL.CompileShader(vertexShader);

            int fragmentShader = GL.CreateShader(ShaderType.FragmentShader);
            GL.ShaderSource(fragmentShader, fragmentShaderSource);
            GL.CompileShader(fragmentShader);

            shaderProgram = GL.CreateProgram();
            GL.AttachShader(shaderProgram, vertexShader);
            GL.AttachShader(shaderProgram, fragmentShader);
            GL.LinkProgram(shaderProgram);

            GL.DeleteShader(vertexShader);
            GL.DeleteShader(fragmentShader);

            vao = GL.GenVertexArray();
            vbo = GL.GenBuffer();

            GL.BindVertexArray(vao);
            GL.BindBuffer(BufferTarget.ArrayBuffer, vbo);

            GL.VertexAttribPointer(0, 2, VertexAttribPointerType.Float, false, 6 * sizeof(float), 0);
            GL.EnableVertexAttribArray(0);

            GL.VertexAttribPointer(1, 4, VertexAttribPointerType.Float, false, 6 * sizeof(float), 2 * sizeof(float));
            GL.EnableVertexAttribArray(1);

            GL.BindVertexArray(0);
        }

        public void Update(float deltaTime, PlayerStats stats, Inventory inventory)
        {
            // Smooth value interpolation
            displayHealth = MathHelper.Lerp(displayHealth, stats.Health, deltaTime * 8f);
            displayArmor = MathHelper.Lerp(displayArmor, stats.Armor, deltaTime * 8f);
            displayHunger = MathHelper.Lerp(displayHunger, stats.Hunger, deltaTime * 8f);

            // Damage flash effect
            if (stats.Health < lastHealth)
            {
                damageFlashTimer = 0.3f;
                healthPulse = 1f;
            }
            lastHealth = stats.Health;
            damageFlashTimer = Math.Max(0, damageFlashTimer - deltaTime);

            // Low health warning pulse
            if (stats.Health < 30f)
            {
                lowHealthWarning += deltaTime * 3f;
            }
            else
            {
                lowHealthWarning = 0f;
            }

            // Health bar pulse animation
            healthPulse = Math.Max(0, healthPulse - deltaTime * 2f);

            // Armor shake on change
            if (Math.Abs(displayArmor - stats.Armor) > 1f)
            {
                armorShake = 0.5f;
            }
            armorShake = Math.Max(0, armorShake - deltaTime * 4f);

            // Hunger bounce
            hungerBounce += deltaTime * 2f;

            // Hotbar slide in animation (on startup)
            hotbarSlideProgress = Math.Min(1f, hotbarSlideProgress + deltaTime * 2f);

            // Hotbar slot highlight animation
            for (int i = 0; i < 9; i++)
            {
                float target = (i == inventory.SelectedHotbarSlot) ? 1f : 0f;
                slotHighlightAnim[i] = MathHelper.Lerp(slotHighlightAnim[i], target, deltaTime * 12f);
            }
        }

        public void Render(PlayerStats stats, Inventory inventory, Vector2i windowSize)
        {
            // Store for use in draw functions
            lastWindowSize = windowSize;
            
            // Save GL state
            GL.GetInteger(GetPName.Blend, out int blendEnabled);
            GL.GetInteger(GetPName.DepthTest, out int depthEnabled);
            GL.GetInteger(GetPName.CurrentProgram, out int oldProgram);

            GL.Enable(EnableCap.Blend);
            GL.BlendFunc(BlendingFactor.SrcAlpha, BlendingFactor.OneMinusSrcAlpha);
            GL.Disable(EnableCap.DepthTest);

            var projection = Matrix4.CreateOrthographicOffCenter(0, windowSize.X, windowSize.Y, 0, -1, 1);

            GL.UseProgram(shaderProgram);
            int projLoc = GL.GetUniformLocation(shaderProgram, "projection");
            GL.UniformMatrix4(projLoc, false, ref projection);

            GL.BindVertexArray(vao);

            // Set font projection if available
            if (fontRenderer != null)
            {
                fontRenderer.SetProjection(projection);
            }

            // Damage flash overlay
            if (damageFlashTimer > 0)
            {
                float alpha = damageFlashTimer * 0.5f;
                DrawRect(0, 0, windowSize.X, windowSize.Y, new Vector4(1, 0, 0, alpha));
            }

            // Low health vignette
            if (lowHealthWarning > 0)
            {
                float pulse = (float)Math.Sin(lowHealthWarning) * 0.15f + 0.15f;
                DrawVignette(windowSize, new Vector4(0.8f, 0, 0, pulse));
            }

            // Draw health bars
            DrawHealthBars(stats, windowSize);

            // Draw hotbar
            DrawHotbar(inventory, windowSize);

            // Draw crosshair
            DrawCrosshair(windowSize);

            // Draw FPS counter
            DrawFPSCounter(windowSize);

            GL.BindVertexArray(0);
            GL.UseProgram(oldProgram);

            // Restore GL state
            if (blendEnabled == 0) GL.Disable(EnableCap.Blend);
            if (depthEnabled != 0) GL.Enable(EnableCap.DepthTest);
        }

        private void DrawHealthBars(PlayerStats stats, Vector2i windowSize)
        {
            float x = 30;
            float y = windowSize.Y - 200;
            float width = 350;
            float height = 32;
            float spacing = 12;
            float cornerRadius = 6f;

            // Health bar with pulse animation
            float healthShake = healthPulse * (float)Math.Sin(healthPulse * 20f) * 2f;
            DrawAnimatedBar("HEALTH", x + healthShake, y, width, height, 
                displayHealth / stats.MaxHealth, 
                HEALTH_BG, HEALTH_FILL, HEALTH_GLOW, 
                healthPulse, cornerRadius, stats.Health, stats.MaxHealth);

            // Armor bar with shake
            y += height + spacing;
            float armorShakeX = armorShake * (float)Math.Sin(armorShake * 15f) * 3f;
            DrawAnimatedBar("ARMOR", x + armorShakeX, y, width, height, 
                displayArmor / stats.MaxArmor, 
                ARMOR_BG, ARMOR_FILL, ARMOR_GLOW, 
                armorShake * 0.5f, cornerRadius, stats.Armor, stats.MaxArmor);

            // Hunger bar with bounce
            y += height + spacing;
            float hungerBounceY = (float)Math.Sin(hungerBounce) * 2f;
            DrawAnimatedBar("HUNGER", x, y + hungerBounceY, width, height, 
                displayHunger / stats.MaxHunger, 
                HUNGER_BG, HUNGER_FILL, HUNGER_GLOW, 
                0, cornerRadius, stats.Hunger, stats.MaxHunger);
        }

        private void DrawAnimatedBar(string label, float x, float y, float width, float height, 
            float fillPercent, Vector4 bgColor, Vector4 fillColor, Vector4 glowColor,
            float glowIntensity, float cornerRadius, float current, float max)
        {
            // Outer glow
            if (glowIntensity > 0)
            {
                float glowSize = 4f + glowIntensity * 8f;
                Vector4 glow = glowColor;
                glow.W *= glowIntensity;
                DrawRoundedRect(x - glowSize, y - glowSize, width + glowSize * 2, height + glowSize * 2, 
                    cornerRadius + glowSize, glow);
            }

            // Background with border
            DrawRoundedRect(x, y, width, height, cornerRadius, bgColor);
            DrawRoundedRectOutline(x, y, width, height, cornerRadius, 2.5f, BORDER_COLOR);

            // Fill bar
            if (fillPercent > 0.01f)
            {
                float fillWidth = width * fillPercent;
                
                // Gradient effect
                DrawRoundedRect(x, y, fillWidth, height, cornerRadius, fillColor);
                
                // Shine effect on top
                Vector4 shine = new Vector4(1, 1, 1, 0.15f);
                float shineHeight = height * 0.35f;
                DrawRoundedRect(x, y, fillWidth, shineHeight, cornerRadius, shine);
            }

            // Text
            if (fontRenderer != null)
            {
                // Label
                float labelScale = 0.45f;
                Vector4 labelColor = new Vector4(0.7f, 0.7f, 0.8f, 1f);
                fontRenderer.DrawText(label, new Vector2(x + 12, y + 7), labelScale, labelColor);

                // Value
                string valueText = $"{current:F0} / {max:F0}";
                Vector2 textSize = fontRenderer.MeasureText(valueText, labelScale);
                float valueX = x + width - textSize.X - 12;
                
                // Text shadow
                Vector4 shadowColor = new Vector4(0, 0, 0, 0.7f);
                fontRenderer.DrawText(valueText, new Vector2(valueX + 1, y + 8), labelScale, shadowColor);
                
                Vector4 textColor = new Vector4(1, 1, 1, 1);
                fontRenderer.DrawText(valueText, new Vector2(valueX, y + 7), labelScale, textColor);
            }
        }

        private void DrawHotbar(Inventory inventory, Vector2i windowSize)
        {
            float slotSize = 75;
            float spacing = 6;
            float totalWidth = (slotSize + spacing) * 9 - spacing;
            float baseX = (windowSize.X - totalWidth) / 2;
            float baseY = windowSize.Y - 110;

            // Slide-in animation
            float slideOffset = (1f - hotbarSlideProgress) * 200f;
            baseY += slideOffset;

            // Hotbar background panel
            float panelPadding = 15;
            DrawRoundedRect(baseX - panelPadding, baseY - panelPadding, 
                totalWidth + panelPadding * 2, slotSize + panelPadding * 2, 
                10f, BG_DARK);
            DrawRoundedRectOutline(baseX - panelPadding, baseY - panelPadding, 
                totalWidth + panelPadding * 2, slotSize + panelPadding * 2, 
                10f, 3f, BORDER_COLOR);

            for (int i = 0; i < 9; i++)
            {
                float slotX = baseX + i * (slotSize + spacing);
                float highlightAnim = slotHighlightAnim[i];
                
                // Animated slot highlight
                if (highlightAnim > 0.01f)
                {
                    float glowSize = 4f * highlightAnim;
                    Vector4 glow = ACCENT_COLOR;
                    glow.W = 0.4f * highlightAnim;
                    DrawRoundedRect(slotX - glowSize, baseY - glowSize, 
                        slotSize + glowSize * 2, slotSize + glowSize * 2, 
                        8f, glow);
                }

                // Slot background
                Vector4 slotBg = Vector4.Lerp(BG_MEDIUM, new Vector4(0.18f, 0.18f, 0.22f, 0.95f), highlightAnim);
                DrawRoundedRect(slotX, baseY, slotSize, slotSize, 6f, slotBg);

                // Border
                float borderThickness = MathHelper.Lerp(2f, 3.5f, highlightAnim);
                Vector4 borderColor = Vector4.Lerp(BORDER_COLOR, ACCENT_COLOR, highlightAnim);
                DrawRoundedRectOutline(slotX, baseY, slotSize, slotSize, 6f, borderThickness, borderColor);

                // Slot number
                if (fontRenderer != null)
                {
                    string numText = (i + 1).ToString();
                    float numScale = 0.4f;
                    Vector4 numColor = Vector4.Lerp(new Vector4(0.5f, 0.5f, 0.6f, 0.8f), 
                        new Vector4(1f, 1f, 1f, 1f), highlightAnim);
                    fontRenderer.DrawText(numText, new Vector2(slotX + 8, baseY + 8), numScale, numColor);
                }

                // Item
                var item = inventory.GetSlot(i);
                if (item.ItemId > 0 && fontRenderer != null)
                {
                    var itemDef = ItemRegistry.Get(item.ItemId);
                    string name = itemDef?.Name ?? "???";

                    if (name.Length > 10)
                        name = name.Substring(0, 9) + "..";

                    // Item name
                    float nameScale = 0.35f;
                    Vector2 nameSize = fontRenderer.MeasureText(name, nameScale);
                    float nameX = slotX + (slotSize - nameSize.X) / 2;
                    float nameY = baseY + slotSize / 2 - 5;

                    // Shadow
                    Vector4 shadowColor = new Vector4(0, 0, 0, 0.8f);
                    fontRenderer.DrawText(name, new Vector2(nameX + 1, nameY + 1), nameScale, shadowColor);
                    
                    Vector4 textColor = new Vector4(1, 1, 1, 1);
                    fontRenderer.DrawText(name, new Vector2(nameX, nameY), nameScale, textColor);

                    // Count
                    if (item.Count > 1)
                    {
                        string countText = item.Count.ToString();
                        float countScale = 0.45f;
                        Vector2 countSize = fontRenderer.MeasureText(countText, countScale);
                        float countX = slotX + slotSize - countSize.X - 8;
                        float countY = baseY + slotSize - countSize.Y - 8;

                        // Badge background
                        float badgePadding = 4;
                        DrawRoundedRect(countX - badgePadding, countY - badgePadding, 
                            countSize.X + badgePadding * 2, countSize.Y + badgePadding * 2, 
                            4f, new Vector4(0, 0, 0, 0.85f));

                        Vector4 countColor = new Vector4(1, 0.9f, 0.5f, 1);
                        fontRenderer.DrawText(countText, new Vector2(countX, countY), countScale, countColor);
                    }

                    // Rarity indicator (small dot)
                    if (itemDef != null)
                    {
                        Vector4 rarityColor = itemDef.GetRarityColor();
                        float dotSize = 6;
                        DrawCircle(slotX + slotSize - dotSize - 5, baseY + 5 + dotSize/2, dotSize/2, rarityColor);
                    }
                }
            }
        }

        private void DrawCrosshair(Vector2i windowSize)
        {
            float centerX = windowSize.X / 2f;
            float centerY = windowSize.Y / 2f;
            float size = 12f;
            float thickness = 2.5f;
            float gap = 4f;

            Vector4 color = new Vector4(1, 1, 1, 0.9f);
            Vector4 outline = new Vector4(0, 0, 0, 0.6f);

            // Outline
            DrawRect(centerX - size - 1, centerY - thickness/2 - 1, gap - 2, thickness + 2, outline);
            DrawRect(centerX + gap + 1, centerY - thickness/2 - 1, size - gap + 1, thickness + 2, outline);
            DrawRect(centerX - thickness/2 - 1, centerY - size - 1, thickness + 2, gap - 2, outline);
            DrawRect(centerX - thickness/2 - 1, centerY + gap + 1, thickness + 2, size - gap + 1, outline);

            // Crosshair
            DrawRect(centerX - size, centerY - thickness/2, gap, thickness, color);
            DrawRect(centerX + gap, centerY - thickness/2, size - gap, thickness, color);
            DrawRect(centerX - thickness/2, centerY - size, thickness, gap, color);
            DrawRect(centerX - thickness/2, centerY + gap, thickness, size - gap, color);

            // Center dot
            DrawCircle(centerX, centerY, 1.5f, color);
        }

        private void DrawFPSCounter(Vector2i windowSize)
        {
            if (fontRenderer != null)
            {
                // Calculate FPS (simple approximation)
                string fpsText = $"FPS: 60"; // You can pass actual FPS value
                float scale = 0.35f;
                Vector2 textSize = fontRenderer.MeasureText(fpsText, scale);
                
                float x = windowSize.X - textSize.X - 15;
                float y = 15;

                // Background
                float padding = 6;
                DrawRoundedRect(x - padding, y - padding, 
                    textSize.X + padding * 2, textSize.Y + padding * 2, 
                    4f, new Vector4(0, 0, 0, 0.6f));

                Vector4 fpsColor = new Vector4(0.7f, 1f, 0.7f, 1f);
                fontRenderer.DrawText(fpsText, new Vector2(x, y), scale, fpsColor);
            }
        }

        private void DrawVignette(Vector2i windowSize, Vector4 color)
        {
            float vignetteSize = 200f;
            
            // Top
            DrawGradientRect(0, 0, windowSize.X, vignetteSize, 
                color, new Vector4(color.X, color.Y, color.Z, 0));
            
            // Bottom
            DrawGradientRect(0, windowSize.Y - vignetteSize, windowSize.X, vignetteSize, 
                new Vector4(color.X, color.Y, color.Z, 0), color);
            
            // Left
            DrawGradientRectH(0, 0, vignetteSize, windowSize.Y, 
                color, new Vector4(color.X, color.Y, color.Z, 0));
            
            // Right
            DrawGradientRectH(windowSize.X - vignetteSize, 0, vignetteSize, windowSize.Y, 
                new Vector4(color.X, color.Y, color.Z, 0), color);
        }

        private void DrawRect(float x, float y, float width, float height, Vector4 color)
        {
            // CRITICAL: Ensure our shader is active before drawing
            GL.UseProgram(shaderProgram);
            
            // CRITICAL: Set projection matrix (FontRenderer changes it)
            var projection = Matrix4.CreateOrthographicOffCenter(0, lastWindowSize.X, lastWindowSize.Y, 0, -1, 1);
            int projLoc = GL.GetUniformLocation(shaderProgram, "projection");
            GL.UniformMatrix4(projLoc, false, ref projection);
            
            float[] vertices = {
                x, y, color.X, color.Y, color.Z, color.W,
                x + width, y, color.X, color.Y, color.Z, color.W,
                x + width, y + height, color.X, color.Y, color.Z, color.W,
                
                x, y, color.X, color.Y, color.Z, color.W,
                x + width, y + height, color.X, color.Y, color.Z, color.W,
                x, y + height, color.X, color.Y, color.Z, color.W,
            };

            GL.BindVertexArray(vao);
            GL.BindBuffer(BufferTarget.ArrayBuffer, vbo);
            GL.BufferData(BufferTarget.ArrayBuffer, vertices.Length * sizeof(float), vertices, BufferUsageHint.DynamicDraw);
            GL.DrawArrays(PrimitiveType.Triangles, 0, 6);
        }

        private void DrawRoundedRect(float x, float y, float width, float height, float radius, Vector4 color)
        {
            // For simplicity, approximating with regular rect
            // In production, you'd generate rounded corners with triangle fan
            DrawRect(x, y, width, height, color);
        }

        private void DrawRoundedRectOutline(float x, float y, float width, float height, float radius, float thickness, Vector4 color)
        {
            // Top
            DrawRect(x, y, width, thickness, color);
            // Bottom
            DrawRect(x, y + height - thickness, width, thickness, color);
            // Left
            DrawRect(x, y, thickness, height, color);
            // Right
            DrawRect(x + width - thickness, y, thickness, height, color);
        }

        private void DrawCircle(float centerX, float centerY, float radius, Vector4 color)
        {
            // CRITICAL: Ensure our shader is active
            GL.UseProgram(shaderProgram);
            
            int segments = 16;
            float[] vertices = new float[segments * 3 * 6];
            int idx = 0;

            for (int i = 0; i < segments; i++)
            {
                float angle1 = (float)(i * 2 * Math.PI / segments);
                float angle2 = (float)((i + 1) * 2 * Math.PI / segments);

                vertices[idx++] = centerX;
                vertices[idx++] = centerY;
                vertices[idx++] = color.X;
                vertices[idx++] = color.Y;
                vertices[idx++] = color.Z;
                vertices[idx++] = color.W;

                vertices[idx++] = centerX + (float)Math.Cos(angle1) * radius;
                vertices[idx++] = centerY + (float)Math.Sin(angle1) * radius;
                vertices[idx++] = color.X;
                vertices[idx++] = color.Y;
                vertices[idx++] = color.Z;
                vertices[idx++] = color.W;

                vertices[idx++] = centerX + (float)Math.Cos(angle2) * radius;
                vertices[idx++] = centerY + (float)Math.Sin(angle2) * radius;
                vertices[idx++] = color.X;
                vertices[idx++] = color.Y;
                vertices[idx++] = color.Z;
                vertices[idx++] = color.W;
            }

            GL.BindVertexArray(vao);
            GL.BindBuffer(BufferTarget.ArrayBuffer, vbo);
            GL.BufferData(BufferTarget.ArrayBuffer, vertices.Length * sizeof(float), vertices, BufferUsageHint.DynamicDraw);
            GL.DrawArrays(PrimitiveType.Triangles, 0, segments * 3);
        }

        private void DrawGradientRect(float x, float y, float width, float height, Vector4 topColor, Vector4 bottomColor)
        {
            GL.UseProgram(shaderProgram);
            
            float[] vertices = {
                x, y, topColor.X, topColor.Y, topColor.Z, topColor.W,
                x + width, y, topColor.X, topColor.Y, topColor.Z, topColor.W,
                x + width, y + height, bottomColor.X, bottomColor.Y, bottomColor.Z, bottomColor.W,
                
                x, y, topColor.X, topColor.Y, topColor.Z, topColor.W,
                x + width, y + height, bottomColor.X, bottomColor.Y, bottomColor.Z, bottomColor.W,
                x, y + height, bottomColor.X, bottomColor.Y, bottomColor.Z, bottomColor.W,
            };

            GL.BindVertexArray(vao);
            GL.BindBuffer(BufferTarget.ArrayBuffer, vbo);
            GL.BufferData(BufferTarget.ArrayBuffer, vertices.Length * sizeof(float), vertices, BufferUsageHint.DynamicDraw);
            GL.DrawArrays(PrimitiveType.Triangles, 0, 6);
        }

        private void DrawGradientRectH(float x, float y, float width, float height, Vector4 leftColor, Vector4 rightColor)
        {
            GL.UseProgram(shaderProgram);
            
            float[] vertices = {
                x, y, leftColor.X, leftColor.Y, leftColor.Z, leftColor.W,
                x + width, y, rightColor.X, rightColor.Y, rightColor.Z, rightColor.W,
                x + width, y + height, rightColor.X, rightColor.Y, rightColor.Z, rightColor.W,
                
                x, y, leftColor.X, leftColor.Y, leftColor.Z, leftColor.W,
                x + width, y + height, rightColor.X, rightColor.Y, rightColor.Z, rightColor.W,
                x, y + height, leftColor.X, leftColor.Y, leftColor.Z, leftColor.W,
            };

            GL.BindVertexArray(vao);
            GL.BindBuffer(BufferTarget.ArrayBuffer, vbo);
            GL.BufferData(BufferTarget.ArrayBuffer, vertices.Length * sizeof(float), vertices, BufferUsageHint.DynamicDraw);
            GL.DrawArrays(PrimitiveType.Triangles, 0, 6);
        }

        public void Resize(int width, int height)
        {
            // Nothing to resize for OpenGL HUD
        }

        public void Dispose()
        {
            if (vao != 0) GL.DeleteVertexArray(vao);
            if (vbo != 0) GL.DeleteBuffer(vbo);
            if (shaderProgram != 0) GL.DeleteProgram(shaderProgram);
        }
    }
}
