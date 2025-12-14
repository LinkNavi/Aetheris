// Net/Client/UI/TooltipSystem.cs - Rich tooltips with text rendering
using System;
using System.Collections.Generic;
using OpenTK.Graphics.OpenGL4;
using OpenTK.Mathematics;
using Aetheris.UI;

namespace Aetheris
{
    public class TooltipSystem : IDisposable
    {
        private readonly ITextRenderer textRenderer;
        private int backgroundVAO, backgroundVBO;
        private int shaderProgram;
        
        private Vector2 tooltipPosition;
        private float fadeAlpha = 0f;
        private const float FADE_SPEED = 8f;
        
        public TooltipSystem(ITextRenderer textRenderer)
        {
            this.textRenderer = textRenderer ?? throw new ArgumentNullException(nameof(textRenderer));
            InitializeBackground();
            InitializeShader();
        }
        
        private void InitializeBackground()
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
        
        public void Update(float deltaTime, bool shouldShow)
        {
            float targetAlpha = shouldShow ? 1f : 0f;
            fadeAlpha = OpenTK.Mathematics.MathHelper.Lerp(fadeAlpha, targetAlpha, deltaTime * FADE_SPEED);
        }
        
        public void RenderItemTooltip(int itemId, Vector2 position, Vector2i screenSize)
        {
            if (fadeAlpha < 0.01f) return;
            
            var itemDef = ItemRegistry.Get(itemId);
            if (itemDef == null) return;
            
            tooltipPosition = position;
            
            // Measure tooltip size
            List<string> lines = BuildTooltipLines(itemDef);
            float maxWidth = 0f;
            float totalHeight = 0f;
            const float lineHeight = 20f;
            const float padding = 12f;
            
            foreach (var line in lines)
            {
                var size = textRenderer.MeasureText(line, 1f);
                maxWidth = Math.Max(maxWidth, size.X);
                totalHeight += lineHeight;
            }
            
            float tooltipWidth = maxWidth + padding * 2;
            float tooltipHeight = totalHeight + padding * 2;
            
            // Adjust position to stay on screen
            if (tooltipPosition.X + tooltipWidth > screenSize.X)
                tooltipPosition.X = screenSize.X - tooltipWidth - 10f;
            if (tooltipPosition.Y + tooltipHeight > screenSize.Y)
                tooltipPosition.Y = screenSize.Y - tooltipHeight - 10f;
            
            // Render background
            RenderBackground(tooltipPosition, tooltipWidth, tooltipHeight, screenSize);
            
            // Render text
            Vector2 textPos = tooltipPosition + new Vector2(padding, padding);
            
            for (int i = 0; i < lines.Count; i++)
            {
                Vector4 color = GetLineColor(i, itemDef);
                color.W *= fadeAlpha;
                
                textRenderer.DrawText(lines[i], textPos, 1f, color);
                textPos.Y += lineHeight;
            }
        }
        
        private List<string> BuildTooltipLines(ItemDefinition itemDef)
        {
            var lines = new List<string>();
            
            // Title (item name with rarity)
            lines.Add(itemDef.Name);
            
            // Category and rarity
            lines.Add($"{itemDef.GetRarityName()} {itemDef.Category}");
            
            // Stats
            if (itemDef.AttackDamage > 0)
                lines.Add($"Damage: {itemDef.AttackDamage:F1}");
            
            if (itemDef.MiningSpeed > 1f)
                lines.Add($"Mining Speed: {itemDef.MiningSpeed:F1}x");
            
            if (itemDef.Durability > 0)
                lines.Add($"Durability: {itemDef.Durability:F0}");
            
            if (itemDef.HungerRestore > 0)
                lines.Add($"Restores {itemDef.HungerRestore} Hunger");
            
            if (itemDef.HealthRestore > 0)
                lines.Add($"Restores {itemDef.HealthRestore:F1} Health");
            
            // Description
            if (!string.IsNullOrEmpty(itemDef.Description))
            {
                lines.Add("");
                lines.Add(itemDef.Description);
            }
            
            return lines;
        }
        
        private Vector4 GetLineColor(int lineIndex, ItemDefinition itemDef)
        {
            if (lineIndex == 0)
                return itemDef.GetRarityColor(); // Item name in rarity color
            
            if (lineIndex == 1)
                return new Vector4(0.7f, 0.7f, 0.7f, 1f); // Category/rarity
            
            return new Vector4(0.9f, 0.9f, 0.9f, 1f); // Default text
        }
        
        private void RenderBackground(Vector2 position, float width, float height, Vector2i screenSize)
        {
            GL.UseProgram(shaderProgram);
            
            var projection = Matrix4.CreateOrthographicOffCenter(0, screenSize.X, screenSize.Y, 0, -1, 1);
            GL.UniformMatrix4(GL.GetUniformLocation(shaderProgram, "projection"), false, ref projection);
            
            Vector4 bgColor = new Vector4(0.1f, 0.1f, 0.15f, 0.95f * fadeAlpha);
            GL.Uniform4(GL.GetUniformLocation(shaderProgram, "bgColor"), ref bgColor);
            
            float[] vertices = {
                position.X, position.Y, 0, 0,
                position.X + width, position.Y, 1, 0,
                position.X + width, position.Y + height, 1, 1,
                
                position.X, position.Y, 0, 0,
                position.X + width, position.Y + height, 1, 1,
                position.X, position.Y + height, 0, 1
            };
            
            GL.Enable(EnableCap.Blend);
            GL.BlendFunc(BlendingFactor.SrcAlpha, BlendingFactor.OneMinusSrcAlpha);
            
            GL.BindVertexArray(backgroundVAO);
            GL.BindBuffer(BufferTarget.ArrayBuffer, backgroundVBO);
            GL.BufferData(BufferTarget.ArrayBuffer, vertices.Length * sizeof(float), vertices, BufferUsageHint.DynamicDraw);
            
            GL.DrawArrays(PrimitiveType.Triangles, 0, 6);
            
            // Border
            RenderBorder(position, width, height, screenSize);
            
            GL.BindVertexArray(0);
        }
        
        private void RenderBorder(Vector2 position, float width, float height, Vector2i screenSize)
        {
            Vector4 borderColor = new Vector4(0.5f, 0.5f, 0.6f, 0.8f * fadeAlpha);
            GL.Uniform4(GL.GetUniformLocation(shaderProgram, "bgColor"), ref borderColor);
            
            float thickness = 2f;
            
            // Top
            float[] top = {
                position.X, position.Y, 0, 0,
                position.X + width, position.Y, 1, 0,
                position.X + width, position.Y + thickness, 1, 1,
                position.X, position.Y, 0, 0,
                position.X + width, position.Y + thickness, 1, 1,
                position.X, position.Y + thickness, 0, 1
            };
            
            GL.BindBuffer(BufferTarget.ArrayBuffer, backgroundVBO);
            GL.BufferData(BufferTarget.ArrayBuffer, top.Length * sizeof(float), top, BufferUsageHint.DynamicDraw);
            GL.DrawArrays(PrimitiveType.Triangles, 0, 6);
            
            // Bottom
            float[] bottom = {
                position.X, position.Y + height - thickness, 0, 0,
                position.X + width, position.Y + height - thickness, 1, 0,
                position.X + width, position.Y + height, 1, 1,
                position.X, position.Y + height - thickness, 0, 0,
                position.X + width, position.Y + height, 1, 1,
                position.X, position.Y + height, 0, 1
            };
            
            GL.BufferData(BufferTarget.ArrayBuffer, bottom.Length * sizeof(float), bottom, BufferUsageHint.DynamicDraw);
            GL.DrawArrays(PrimitiveType.Triangles, 0, 6);
            
            // Left
            float[] left = {
                position.X, position.Y, 0, 0,
                position.X + thickness, position.Y, 1, 0,
                position.X + thickness, position.Y + height, 1, 1,
                position.X, position.Y, 0, 0,
                position.X + thickness, position.Y + height, 1, 1,
                position.X, position.Y + height, 0, 1
            };
            
            GL.BufferData(BufferTarget.ArrayBuffer, left.Length * sizeof(float), left, BufferUsageHint.DynamicDraw);
            GL.DrawArrays(PrimitiveType.Triangles, 0, 6);
            
            // Right
            float[] right = {
                position.X + width - thickness, position.Y, 0, 0,
                position.X + width, position.Y, 1, 0,
                position.X + width, position.Y + height, 1, 1,
                position.X + width - thickness, position.Y, 0, 0,
                position.X + width, position.Y + height, 1, 1,
                position.X + width - thickness, position.Y + height, 0, 1
            };
            
            GL.BufferData(BufferTarget.ArrayBuffer, right.Length * sizeof(float), right, BufferUsageHint.DynamicDraw);
            GL.DrawArrays(PrimitiveType.Triangles, 0, 6);
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
                Console.WriteLine($"[TooltipSystem] Shader compilation failed: {info}");
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
    
    // Add text renderer interface if not exists
    public interface ITextRenderer
    {
        void DrawText(string text, Vector2 position, float scale = 1f, Vector4? color = null);
        Vector2 MeasureText(string text, float scale = 1f);
        void SetProjection(Matrix4 projection);
    }
}
