using System;
using OpenTK.Graphics.OpenGL4;
using OpenTK.Mathematics;
using Aetheris.GameLogic;

namespace Aetheris.UI
{
    public class SimpleOpenGLHUD : IDisposable
    {
        private int shaderProgram;
        private int vao;
        private int vbo;
        private FontRenderer? fontRenderer;

        public SimpleOpenGLHUD(FontRenderer? fontRenderer)
        {
            this.fontRenderer = fontRenderer;
            CreateShaderAndBuffers();
            Console.WriteLine("[SimpleHUD] Initialized");
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
            // Nothing to update
        }

        public void Render(PlayerStats stats, Inventory inventory, Vector2i windowSize)
        {
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

            // Draw health bars
            DrawHealthBars(stats, windowSize);

            // Draw hotbar
            DrawHotbar(inventory, windowSize);

            // Draw crosshair
            DrawCrosshair(windowSize);

            GL.BindVertexArray(0);
            GL.UseProgram(oldProgram);

            if (blendEnabled == 0) GL.Disable(EnableCap.Blend);
            if (depthEnabled != 0) GL.Enable(EnableCap.DepthTest);
        }

        private void DrawHealthBars(PlayerStats stats, Vector2i windowSize)
        {
            float x = 20;
            float y = windowSize.Y - 180;
            float width = 300;
            float height = 28;
            float spacing = 10;

            // Health
            DrawBar(x, y, width, height, stats.Health / stats.MaxHealth,
                    new Vector4(0.8f, 0.2f, 0.2f, 1f));
            if (fontRenderer != null)
            {
                fontRenderer.DrawText($"Health: {stats.Health:F0}/{stats.MaxHealth:F0}",
                    (int)(x + 10), (int)(y + 5), 0.4f, new Vector3(1, 1, 1));
            }

            // Armor
            y += height + spacing;
            DrawBar(x, y, width, height, stats.Armor / stats.MaxArmor,
                    new Vector4(0.3f, 0.5f, 0.9f, 1f));
            if (fontRenderer != null)
            {
                fontRenderer.RenderText($"Armor: {stats.Armor:F0}/{stats.MaxArmor:F0}",
                    (int)(x + 10), (int)(y + 5), 0.4f, new Vector3(1, 1, 1));
            }

            // Hunger
            y += height + spacing;
            DrawBar(x, y, width, height, stats.Hunger / stats.MaxHunger,
                    new Vector4(0.9f, 0.6f, 0.2f, 1f));
            if (fontRenderer != null)
            {
                fontRenderer.RenderText($"Hunger: {stats.Hunger:F0}/{stats.MaxHunger:F0}",
                    (int)(x + 10), (int)(y + 5), 0.4f, new Vector3(1, 1, 1));
            }
        }

        private void DrawBar(float x, float y, float width, float height, float fillPercent, Vector4 color)
        {
            // Background
            DrawRect(x, y, width, height, new Vector4(0, 0, 0, 0.6f));

            // Fill
            if (fillPercent > 0)
            {
                DrawRect(x, y, width * fillPercent, height, color);
            }

            // Border
            DrawRectOutline(x, y, width, height, new Vector4(1, 1, 1, 0.3f), 2f);
        }

        private void DrawHotbar(Inventory inventory, Vector2i windowSize)
        {
            float slotSize = 80;
            float spacing = 4;
            float totalWidth = (slotSize + spacing) * 9 - spacing;
            float x = (windowSize.X - totalWidth) / 2;
            float y = windowSize.Y - 120;

            for (int i = 0; i < 9; i++)
            {
                float slotX = x + i * (slotSize + spacing);
                bool isSelected = i == inventory.SelectedHotbarSlot;

                // Background
                Vector4 bgColor = isSelected ?
                    new Vector4(0.3f, 0.3f, 0.4f, 0.8f) :
                    new Vector4(0.15f, 0.15f, 0.18f, 0.8f);
                DrawRect(slotX, y, slotSize, slotSize, bgColor);

                // Border
                Vector4 borderColor = isSelected ?
                    new Vector4(1f, 0.8f, 0.3f, 1f) :
                    new Vector4(1, 1, 1, 0.3f);
                DrawRectOutline(slotX, y, slotSize, slotSize, borderColor, isSelected ? 3f : 2f);

                // Slot number
                if (fontRenderer != null)
                {
                    fontRenderer.RenderText((i + 1).ToString(),
                        (int)(slotX + 5), (int)(y + 5), 0.4f, new Vector3(1, 1, 1));
                }

                // Item
                var item = inventory.GetSlot(i);
                if (item.ItemId > 0 && fontRenderer != null)
                {
                    var itemDef = ItemRegistry.Get(item.ItemId);
                    string name = itemDef?.Name ?? "???";

                    if (name.Length > 10)
                        name = name.Substring(0, 9) + "..";

                    fontRenderer.RenderText(name,
                        (int)(slotX + slotSize / 2 - name.Length * 4), (int)(y + slotSize / 2),
                        0.3f, new Vector3(1, 1, 1));

                    if (item.Count > 1)
                    {
                        fontRenderer.RenderText(item.Count.ToString(),
                            (int)(slotX + slotSize - 20), (int)(y + slotSize - 20),
                            0.4f, new Vector3(1, 1, 0.5f));
                    }
                }
            }
        }

        private void DrawCrosshair(Vector2i windowSize)
        {
            float centerX = windowSize.X / 2f;
            float centerY = windowSize.Y / 2f;
            float size = 10f;
            float thickness = 2f;

            Vector4 color = new Vector4(1, 1, 1, 0.8f);

            // Horizontal line
            DrawRect(centerX - size, centerY - thickness / 2, size * 2, thickness, color);

            // Vertical line
            DrawRect(centerX - thickness / 2, centerY - size, thickness, size * 2, color);
        }

        private void DrawRect(float x, float y, float width, float height, Vector4 color)
        {
            float[] vertices = {
                x, y, color.X, color.Y, color.Z, color.W,
                x + width, y, color.X, color.Y, color.Z, color.W,
                x + width, y + height, color.X, color.Y, color.Z, color.W,
                
                x, y, color.X, color.Y, color.Z, color.W,
                x + width, y + height, color.X, color.Y, color.Z, color.W,
                x, y + height, color.X, color.Y, color.Z, color.W,
            };

            GL.BindBuffer(BufferTarget.ArrayBuffer, vbo);
            GL.BufferData(BufferTarget.ArrayBuffer, vertices.Length * sizeof(float), vertices, BufferUsageHint.DynamicDraw);
            GL.DrawArrays(PrimitiveType.Triangles, 0, 6);
        }

        private void DrawRectOutline(float x, float y, float width, float height, Vector4 color, float thickness)
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

        public void Resize(int width, int height)
        {
            // Nothing to resize
        }

        public void Dispose()
        {
            if (vao != 0) GL.DeleteVertexArray(vao);
            if (vbo != 0) GL.DeleteBuffer(vbo);
            if (shaderProgram != 0) GL.DeleteProgram(shaderProgram);
        }
    }
}
