
using System;
using OpenTK.Graphics.OpenGL4;
using OpenTK.Mathematics;
using Aetheris.UI;

namespace Aetheris
{
    /// <summary>
    /// Dynamic crosshair that changes based on what you're looking at
    /// </summary>
    public class CrosshairRenderer : IDisposable
    {
        private int vao, vbo;
        private int shaderProgram;
        
        public enum CrosshairState
        {
            Normal,      // Default crosshair
            Mining,      // Mining a block
            CanPlace,    // Can place block here
            CannotPlace, // Cannot place block here
            Enemy        // Looking at enemy
        }
        
        public CrosshairState State { get; set; } = CrosshairState.Normal;
        
        public CrosshairRenderer()
        {
            InitializeMesh();
            InitializeShader();
        }
        
        private void InitializeMesh()
        {
            vao = GL.GenVertexArray();
            vbo = GL.GenBuffer();
            
            GL.BindVertexArray(vao);
            GL.BindBuffer(BufferTarget.ArrayBuffer, vbo);
            
            // Allocate for dynamic vertex data
            GL.BufferData(BufferTarget.ArrayBuffer, sizeof(float) * 2 * 12, IntPtr.Zero, BufferUsageHint.DynamicDraw);
            
            GL.EnableVertexAttribArray(0);
            GL.VertexAttribPointer(0, 2, VertexAttribPointerType.Float, false, 2 * sizeof(float), 0);
            
            GL.BindVertexArray(0);
        }
        
        private void InitializeShader()
        {
            string vertexShader = @"
#version 330 core
layout (location = 0) in vec2 aPos;

uniform mat4 projection;

void main()
{
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
}";

            string fragmentShader = @"
#version 330 core
out vec4 FragColor;

uniform vec4 color;

void main()
{
    FragColor = color;
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
        
        public void Render(Vector2i screenSize, float scale = 1f)
        {
            Vector2 center = new Vector2(screenSize.X / 2f, screenSize.Y / 2f);
            
            GL.UseProgram(shaderProgram);
            
            var projection = Matrix4.CreateOrthographicOffCenter(0, screenSize.X, screenSize.Y, 0, -1, 1);
            GL.UniformMatrix4(GL.GetUniformLocation(shaderProgram, "projection"), false, ref projection);
            
            Vector4 color = GetColorForState();
            GL.Uniform4(GL.GetUniformLocation(shaderProgram, "color"), ref color);
            
            float size = 10f * scale;
            float thickness = 2f;
            float gap = 4f;
            
            // Generate crosshair geometry based on state
            float[] vertices;
            
            if (State == CrosshairState.Mining)
            {
                // Pulsing circle for mining
                float pulseScale = 1f + MathF.Sin((float)DateTime.Now.TimeOfDay.TotalSeconds * 8f) * 0.2f;
                vertices = GenerateCircle(center, size * pulseScale, 16);
            }
            else if (State == CrosshairState.CanPlace)
            {
                // Plus sign for placement
                vertices = GeneratePlusCrosshair(center, size, thickness, gap);
            }
            else if (State == CrosshairState.CannotPlace)
            {
                // X mark for invalid placement
                vertices = GenerateXCrosshair(center, size, thickness);
            }
            else
            {
                // Normal dot crosshair
                vertices = GenerateDotCrosshair(center, size, thickness, gap);
            }
            
            GL.Disable(EnableCap.DepthTest);
            GL.Enable(EnableCap.Blend);
            GL.BlendFunc(BlendingFactor.SrcAlpha, BlendingFactor.OneMinusSrcAlpha);
            
            GL.BindVertexArray(vao);
            GL.BindBuffer(BufferTarget.ArrayBuffer, vbo);
            GL.BufferData(BufferTarget.ArrayBuffer, vertices.Length * sizeof(float), vertices, BufferUsageHint.DynamicDraw);
            
            GL.DrawArrays(PrimitiveType.Lines, 0, vertices.Length / 2);
            
            GL.BindVertexArray(0);
            GL.Enable(EnableCap.DepthTest);
        }
        
        private Vector4 GetColorForState()
        {
            return State switch
            {
                CrosshairState.Mining => new Vector4(1f, 1f, 0f, 0.9f),
                CrosshairState.CanPlace => new Vector4(0f, 1f, 0f, 0.8f),
                CrosshairState.CannotPlace => new Vector4(1f, 0f, 0f, 0.8f),
                CrosshairState.Enemy => new Vector4(1f, 0.3f, 0f, 0.9f),
                _ => new Vector4(1f, 1f, 1f, 0.7f)
            };
        }
        
        private float[] GeneratePlusCrosshair(Vector2 center, float size, float thickness, float gap)
        {
            return new float[]
            {
                // Horizontal line (left)
                center.X - size, center.Y,
                center.X - gap, center.Y,
                // Horizontal line (right)
                center.X + gap, center.Y,
                center.X + size, center.Y,
                // Vertical line (top)
                center.X, center.Y - size,
                center.X, center.Y - gap,
                // Vertical line (bottom)
                center.X, center.Y + gap,
                center.X, center.Y + size
            };
        }
        
        private float[] GenerateDotCrosshair(Vector2 center, float size, float thickness, float gap)
        {
            return new float[]
            {
                // Center dot
                center.X - thickness, center.Y,
                center.X + thickness, center.Y,
                center.X, center.Y - thickness,
                center.X, center.Y + thickness
            };
        }
        
        private float[] GenerateXCrosshair(Vector2 center, float size, float thickness)
        {
            float offset = size * 0.7f;
            return new float[]
            {
                center.X - offset, center.Y - offset,
                center.X + offset, center.Y + offset,
                center.X + offset, center.Y - offset,
                center.X - offset, center.Y + offset
            };
        }
        
        private float[] GenerateCircle(Vector2 center, float radius, int segments)
        {
            var vertices = new float[segments * 4];
            
            for (int i = 0; i < segments; i++)
            {
                float angle1 = (float)i / segments * MathF.PI * 2f;
                float angle2 = (float)(i + 1) / segments * MathF.PI * 2f;
                
                vertices[i * 4 + 0] = center.X + MathF.Cos(angle1) * radius;
                vertices[i * 4 + 1] = center.Y + MathF.Sin(angle1) * radius;
                vertices[i * 4 + 2] = center.X + MathF.Cos(angle2) * radius;
                vertices[i * 4 + 3] = center.Y + MathF.Sin(angle2) * radius;
            }
            
            return vertices;
        }
        
        private int CompileShader(ShaderType type, string source)
        {
            int shader = GL.CreateShader(type);
            GL.ShaderSource(shader, source);
            GL.CompileShader(shader);
            
            GL.GetShader(shader, ShaderParameter.CompileStatus, out int success);
            if (success == 0)
            {
                string info = GL.GetShaderInfoLog(shader);
                Console.WriteLine($"[Crosshair] Shader compilation failed: {info}");
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
    
    /// <summary>
    /// Enhanced hotbar with item names, counts, and durability bars
    /// </summary>
    public class EnhancedHotbarRenderer : IDisposable
    {
        private readonly Inventory inventory;
        private readonly ITextRenderer? textRenderer;
        private int backgroundVAO, backgroundVBO;
        private int shaderProgram;
        
        private const float SLOT_SIZE = 64f;
        private const float SLOT_SPACING = 4f;
        private const float HOTBAR_Y_OFFSET = 80f;
        
        public EnhancedHotbarRenderer(Inventory inventory, ITextRenderer? textRenderer = null)
        {
            this.inventory = inventory;
            this.textRenderer = textRenderer;
            InitializeBuffers();
            InitializeShader();
        }
        
        private void InitializeBuffers()
        {
            backgroundVAO = GL.GenVertexArray();
            backgroundVBO = GL.GenBuffer();
            
            GL.BindVertexArray(backgroundVAO);
            GL.BindBuffer(BufferTarget.ArrayBuffer, backgroundVBO);
            
            GL.BufferData(BufferTarget.ArrayBuffer, sizeof(float) * 6 * 4, IntPtr.Zero, BufferUsageHint.DynamicDraw);
            
            GL.VertexAttribPointer(0, 4, VertexAttribPointerType.Float, false, 4 * sizeof(float), 0);
            GL.EnableVertexAttribArray(0);
            
            GL.BindVertexArray(0);
        }
        
        private void InitializeShader()
        {
            string vertexShader = @"
#version 330 core
layout (location = 0) in vec4 vertex;
out vec2 TexCoords;

uniform mat4 projection;

void main()
{
    gl_Position = projection * vec4(vertex.xy, 0.0, 1.0);
    TexCoords = vertex.zw;
}";

            string fragmentShader = @"
#version 330 core
in vec2 TexCoords;
out vec4 color;

uniform vec4 bgColor;

void main()
{
    color = bgColor;
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
        
        public void Render(Vector2i screenSize)
        {
            float totalWidth = Inventory.HOTBAR_SIZE * (SLOT_SIZE + SLOT_SPACING) - SLOT_SPACING;
            float startX = (screenSize.X - totalWidth) / 2f;
            float startY = HOTBAR_Y_OFFSET;
            
            GL.Disable(EnableCap.DepthTest);
            GL.Enable(EnableCap.Blend);
            GL.BlendFunc(BlendingFactor.SrcAlpha, BlendingFactor.OneMinusSrcAlpha);
            
            var projection = Matrix4.CreateOrthographicOffCenter(0, screenSize.X, screenSize.Y, 0, -1, 1);
            
            for (int i = 0; i < Inventory.HOTBAR_SIZE; i++)
            {
                float x = startX + i * (SLOT_SIZE + SLOT_SPACING);
                bool isSelected = i == inventory.SelectedHotbarSlot;
                
                RenderSlot(x, startY, SLOT_SIZE, isSelected, i, projection, screenSize);
            }
            
            GL.Enable(EnableCap.DepthTest);
        }
        
        private void RenderSlot(float x, float y, float size, bool selected, int slotIndex, Matrix4 projection, Vector2i screenSize)
        {
            var item = inventory.GetSlot(slotIndex);
            
            // Background
            Vector4 bgColor = selected 
                ? new Vector4(0.4f, 0.4f, 0.4f, 0.9f)
                : new Vector4(0.2f, 0.2f, 0.2f, 0.8f);
            
            DrawRect(x, y, size, size, bgColor, projection);
            
            // Border
            Vector4 borderColor = selected 
                ? new Vector4(1f, 1f, 1f, 1f)
                : new Vector4(0.5f, 0.5f, 0.5f, 0.6f);
            
            float borderThickness = selected ? 3f : 2f;
            DrawRectOutline(x, y, size, size, borderThickness, borderColor, projection);
            
            if (item.ItemId > 0)
            {
                var itemDef = ItemRegistry.Get(item.ItemId);
                if (itemDef != null)
                {
                    // Item icon/representation (simple colored square for now)
                    Vector4 itemColor = itemDef.GetRarityColor();
                    float itemSize = size * 0.6f;
                    float itemX = x + (size - itemSize) / 2f;
                    float itemY = y + (size - itemSize) / 2f;
                    DrawRect(itemX, itemY, itemSize, itemSize, itemColor, projection);
                    
                    if (textRenderer != null)
                    {
                        // Item count
                        if (item.Count > 1)
                        {
                            string countText = item.Count > 99 ? "99+" : item.Count.ToString();
                            Vector2 countPos = new Vector2(x + size - 18f, y + size - 16f);
                            textRenderer.DrawText(countText, countPos, 0.7f, new Vector4(1f, 1f, 1f, 1f));
                        }
                        
                        // Slot number
                        string slotNum = (slotIndex + 1).ToString();
                        Vector2 numPos = new Vector2(x + 4f, y + 4f);
                        textRenderer.DrawText(slotNum, numPos, 0.6f, new Vector4(0.7f, 0.7f, 0.7f, 0.8f));
                        
                        // Selected item name below hotbar
                        if (selected)
                        {
                            Vector2 nameSize = textRenderer.MeasureText(itemDef.Name, 1f);
                            Vector2 namePos = new Vector2(
                                x + size / 2f - nameSize.X / 2f,
                                y + size + 8f
                            );
                            textRenderer.DrawText(itemDef.Name, namePos, 1f, itemDef.GetRarityColor());
                        }
                        
                        // Durability bar
                        if (itemDef.Durability > 0)
                        {
                            float durabilityPercent = 1f; // TODO: Track actual durability
                            float barWidth = size * 0.8f;
                            float barHeight = 3f;
                            float barX = x + (size - barWidth) / 2f;
                            float barY = y + size - barHeight - 4f;
                            
                            // Background
                            DrawRect(barX, barY, barWidth, barHeight, new Vector4(0.3f, 0.3f, 0.3f, 0.8f), projection);
                            
                            // Durability
                            Vector4 durColor = durabilityPercent > 0.5f 
                                ? new Vector4(0.2f, 1f, 0.2f, 1f)
                                : durabilityPercent > 0.25f
                                    ? new Vector4(1f, 1f, 0.2f, 1f)
                                    : new Vector4(1f, 0.2f, 0.2f, 1f);
                            
                            DrawRect(barX, barY, barWidth * durabilityPercent, barHeight, durColor, projection);
                        }
                    }
                }
            }
        }
        
        private void DrawRect(float x, float y, float w, float h, Vector4 color, Matrix4 projection)
        {
            GL.UseProgram(shaderProgram);
            GL.UniformMatrix4(GL.GetUniformLocation(shaderProgram, "projection"), false, ref projection);
            GL.Uniform4(GL.GetUniformLocation(shaderProgram, "bgColor"), ref color);
            
            float[] vertices = {
                x, y, 0, 0,
                x + w, y, 1, 0,
                x + w, y + h, 1, 1,
                
                x, y, 0, 0,
                x + w, y + h, 1, 1,
                x, y + h, 0, 1
            };
            
            GL.BindVertexArray(backgroundVAO);
            GL.BindBuffer(BufferTarget.ArrayBuffer, backgroundVBO);
            GL.BufferData(BufferTarget.ArrayBuffer, vertices.Length * sizeof(float), vertices, BufferUsageHint.DynamicDraw);
            GL.DrawArrays(PrimitiveType.Triangles, 0, 6);
        }
        
        private void DrawRectOutline(float x, float y, float w, float h, float thickness, Vector4 color, Matrix4 projection)
        {
            // Top
            DrawRect(x, y, w, thickness, color, projection);
            // Bottom
            DrawRect(x, y + h - thickness, w, thickness, color, projection);
            // Left
            DrawRect(x, y, thickness, h, color, projection);
            // Right
            DrawRect(x + w - thickness, y, thickness, h, color, projection);
        }
        
        private int CompileShader(ShaderType type, string source)
        {
            int shader = GL.CreateShader(type);
            GL.ShaderSource(shader, source);
            GL.CompileShader(shader);
            
            GL.GetShader(shader, ShaderParameter.CompileStatus, out int success);
            if (success == 0)
            {
                string info = GL.GetShaderInfoLog(shader);
                Console.WriteLine($"[EnhancedHotbar] Shader compilation failed: {info}");
            }
            
            return shader;
        }
        
        public void Dispose()
        {
            GL.DeleteVertexArray(backgroundVAO);
            GL.DeleteBuffer(backgroundVBO);
            GL.DeleteProgram(shaderProgram);
        }
    }
}
