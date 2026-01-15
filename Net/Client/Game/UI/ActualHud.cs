using System;
using SkiaSharp;
using OpenTK.Graphics.OpenGL4;
using OpenTK.Mathematics;
using Aetheris.GameLogic;

namespace Aetheris.UI
{
    public class SkiaGameHUD : IDisposable
    {
        private int width;
        private int height;
        private int textureId;
        private SKBitmap bitmap;
        private SKCanvas canvas;
        private SKPaint paint;
        private SKFont font;
        
        // Shader for rendering
        private int shaderProgram;
        private int vao;
        private int vbo;

        public SkiaGameHUD(int width, int height)
        {
            this.width = width;
            this.height = height;
            
            // Create Skia bitmap and canvas
            bitmap = new SKBitmap(width, height, SKColorType.Rgba8888, SKAlphaType.Premul);
            canvas = new SKCanvas(bitmap);
            
            // Create paint and font
            paint = new SKPaint
            {
                IsAntialias = true,
                FilterQuality = SKFilterQuality.High
            };
            
            font = new SKFont
            {
                Size = 18
            };
            
            // Create OpenGL texture
            textureId = GL.GenTexture();
            GL.BindTexture(TextureTarget.Texture2D, textureId);
            GL.TexImage2D(TextureTarget.Texture2D, 0, PixelInternalFormat.Rgba,
                          width, height, 0, PixelFormat.Rgba, PixelType.UnsignedByte, IntPtr.Zero);
            GL.TexParameter(TextureTarget.Texture2D, TextureParameterName.TextureMinFilter, (int)TextureMinFilter.Linear);
            GL.TexParameter(TextureTarget.Texture2D, TextureParameterName.TextureMagFilter, (int)TextureMagFilter.Linear);
            
            CreateShaderAndGeometry();
            
            Console.WriteLine("[SkiaHUD] Initialized successfully");
        }

        private void CreateShaderAndGeometry()
        {
            string vertexShaderSource = @"
                #version 330 core
                layout (location = 0) in vec2 aPos;
                layout (location = 1) in vec2 aTexCoord;
                out vec2 TexCoord;
                void main()
                {
                    gl_Position = vec4(aPos, 0.0, 1.0);
                    TexCoord = aTexCoord;
                }
            ";

            string fragmentShaderSource = @"
                #version 330 core
                out vec4 FragColor;
                in vec2 TexCoord;
                uniform sampler2D hudTexture;
                void main()
                {
                    FragColor = texture(hudTexture, TexCoord);
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

            float[] vertices = {
                -1.0f,  1.0f,  0.0f, 0.0f,
                -1.0f, -1.0f,  0.0f, 1.0f,
                 1.0f, -1.0f,  1.0f, 1.0f,
                 1.0f,  1.0f,  1.0f, 0.0f
            };

            vao = GL.GenVertexArray();
            vbo = GL.GenBuffer();

            GL.BindVertexArray(vao);
            GL.BindBuffer(BufferTarget.ArrayBuffer, vbo);
            GL.BufferData(BufferTarget.ArrayBuffer, vertices.Length * sizeof(float), vertices, BufferUsageHint.StaticDraw);

            GL.VertexAttribPointer(0, 2, VertexAttribPointerType.Float, false, 4 * sizeof(float), 0);
            GL.EnableVertexAttribArray(0);

            GL.VertexAttribPointer(1, 2, VertexAttribPointerType.Float, false, 4 * sizeof(float), 2 * sizeof(float));
            GL.EnableVertexAttribArray(1);

            GL.BindVertexArray(0);
        }

        public void Update(float deltaTime, PlayerStats stats, Inventory inventory)
        {
            // Clear canvas with transparent background
            canvas.Clear(SKColors.Transparent);
            
            // Draw health bars
            DrawHealthBars(stats);
            
            // Draw hotbar
            DrawHotbar(inventory);
            
            // Draw crosshair
            DrawCrosshair();
            
            // Upload to texture
            UpdateTexture();
        }

        private void DrawHealthBars(PlayerStats stats)
        {
            float x = 20;
            float y = height - 180;
            float barWidth = 300;
            float barHeight = 28;
            float spacing = 10;
            
            // Health bar
            DrawBar("Health", x, y, barWidth, barHeight, 
                    stats.Health, stats.MaxHealth, 
                    new SKColor(255, 68, 68), new SKColor(204, 0, 0));
            
            // Armor bar
            DrawBar("Armor", x, y + barHeight + spacing, barWidth, barHeight,
                    stats.Armor, stats.MaxArmor,
                    new SKColor(68, 136, 255), new SKColor(0, 68, 204));
            
            // Hunger bar
            DrawBar("Hunger", x, y + (barHeight + spacing) * 2, barWidth, barHeight,
                    stats.Hunger, stats.MaxHunger,
                    new SKColor(255, 170, 68), new SKColor(204, 102, 0));
        }

        private void DrawBar(string label, float x, float y, float width, float height,
                            float current, float max, SKColor fillColor, SKColor bgColor)
        {
            // Background
            paint.Color = new SKColor(0, 0, 0, 153);
            canvas.DrawRoundRect(x, y, width, height, 4, 4, paint);
            
            // Border
            paint.Style = SKPaintStyle.Stroke;
            paint.StrokeWidth = 2;
            paint.Color = new SKColor(255, 255, 255, 76);
            canvas.DrawRoundRect(x, y, width, height, 4, 4, paint);
            paint.Style = SKPaintStyle.Fill;
            
            // Fill
            float fillWidth = (current / max) * width;
            if (fillWidth > 0)
            {
                paint.Color = fillColor;
                canvas.DrawRoundRect(x, y, fillWidth, height, 4, 4, paint);
            }
            
            // Text
            paint.Color = SKColors.White;
            string text = $"{label}: {current:F0} / {max:F0}";
            float textX = x + width / 2 - font.MeasureText(text, paint) / 2;
            float textY = y + height / 2 + font.Size / 3;
            
            // Text shadow
            paint.Color = new SKColor(0, 0, 0, 200);
            canvas.DrawText(text, textX + 2, textY + 2, font, paint);
            
            paint.Color = SKColors.White;
            canvas.DrawText(text, textX, textY, font, paint);
        }

        private void DrawHotbar(Inventory inventory)
        {
            float slotSize = 80;
            float spacing = 4;
            float totalWidth = (slotSize + spacing) * 9 - spacing;
            float x = (width - totalWidth) / 2;
            float y = height - 120;
            
            for (int i = 0; i < 9; i++)
            {
                float slotX = x + i * (slotSize + spacing);
                bool isSelected = i == inventory.SelectedHotbarSlot;
                
                // Slot background
                if (isSelected)
                {
                    paint.Color = new SKColor(255, 255, 255, 51);
                }
                else
                {
                    paint.Color = new SKColor(0, 0, 0, 153);
                }
                canvas.DrawRoundRect(slotX, y, slotSize, slotSize, 4, 4, paint);
                
                // Border
                paint.Style = SKPaintStyle.Stroke;
                paint.StrokeWidth = isSelected ? 3 : 2;
                paint.Color = isSelected ? new SKColor(255, 255, 255, 229) : new SKColor(255, 255, 255, 76);
                canvas.DrawRoundRect(slotX, y, slotSize, slotSize, 4, 4, paint);
                paint.Style = SKPaintStyle.Fill;
                
                // Slot number
                paint.Color = new SKColor(255, 255, 255, 178);
                canvas.DrawText((i + 1).ToString(), slotX + 6, y + 20, font, paint);
                
                // Item
                var item = inventory.GetSlot(i);
                if (item.ItemId > 0)
                {
                    var itemDef = ItemRegistry.Get(item.ItemId);
                    string name = itemDef?.Name ?? "???";
                    
                    // Shorten if needed
                    if (name.Length > 9)
                        name = name.Substring(0, 8) + "..";
                    
                    // Item name
                    font.Size = 14;
                    float nameWidth = font.MeasureText(name, paint);
                    float nameX = slotX + (slotSize - nameWidth) / 2;
                    float nameY = y + slotSize / 2 + 5;
                    
                    paint.Color = new SKColor(0, 0, 0, 200);
                    canvas.DrawText(name, nameX + 1, nameY + 1, font, paint);
                    paint.Color = SKColors.White;
                    canvas.DrawText(name, nameX, nameY, font, paint);
                    
                    // Count
                    if (item.Count > 1)
                    {
                        font.Size = 16;
                        string count = item.Count.ToString();
                        float countWidth = font.MeasureText(count, paint);
                        float countX = slotX + slotSize - countWidth - 6;
                        float countY = y + slotSize - 8;
                        
                        paint.Color = new SKColor(0, 0, 0, 200);
                        canvas.DrawText(count, countX + 1, countY + 1, font, paint);
                        paint.Color = SKColors.White;
                        canvas.DrawText(count, countX, countY, font, paint);
                    }
                    
                    font.Size = 18;
                }
            }
        }

        private void DrawCrosshair()
        {
            float centerX = width / 2f;
            float centerY = height / 2f;
            float size = 10f;
            
            paint.StrokeWidth = 2;
            paint.Color = new SKColor(255, 255, 255, 204);
            
            // Horizontal line
            canvas.DrawLine(centerX - size, centerY, centerX + size, centerY, paint);
            
            // Vertical line
            canvas.DrawLine(centerX, centerY - size, centerX, centerY + size, paint);
        }

        private void UpdateTexture()
        {
            GL.BindTexture(TextureTarget.Texture2D, textureId);
            
            IntPtr pixels = bitmap.GetPixels();
            GL.TexSubImage2D(TextureTarget.Texture2D, 0, 0, 0,
                width, height, PixelFormat.Rgba, PixelType.UnsignedByte, pixels);
        }

        public void Render()
        {
            GL.GetInteger(GetPName.CurrentProgram, out int oldProgram);
            GL.GetInteger(GetPName.Blend, out int blendEnabled);
            GL.GetInteger(GetPName.DepthTest, out int depthEnabled);

            GL.Enable(EnableCap.Blend);
            GL.BlendFunc(BlendingFactor.SrcAlpha, BlendingFactor.OneMinusSrcAlpha);
            GL.Disable(EnableCap.DepthTest);

            GL.UseProgram(shaderProgram);
            
            int texLocation = GL.GetUniformLocation(shaderProgram, "hudTexture");
            GL.Uniform1(texLocation, 0);
            
            GL.ActiveTexture(TextureUnit.Texture0);
            GL.BindTexture(TextureTarget.Texture2D, textureId);

            GL.BindVertexArray(vao);
            GL.DrawArrays(PrimitiveType.TriangleFan, 0, 4);
            GL.BindVertexArray(0);
            
            GL.UseProgram(oldProgram);

            if (blendEnabled == 0) GL.Disable(EnableCap.Blend);
            if (depthEnabled != 0) GL.Enable(EnableCap.DepthTest);
        }

        public void Resize(int newWidth, int newHeight)
        {
            width = newWidth;
            height = newHeight;
            
            bitmap?.Dispose();
            canvas?.Dispose();
            
            bitmap = new SKBitmap(width, height, SKColorType.Rgba8888, SKAlphaType.Premul);
            canvas = new SKCanvas(bitmap);
            
            GL.BindTexture(TextureTarget.Texture2D, textureId);
            GL.TexImage2D(TextureTarget.Texture2D, 0, PixelInternalFormat.Rgba,
                          width, height, 0, PixelFormat.Rgba, PixelType.UnsignedByte, IntPtr.Zero);
        }

        public void Dispose()
        {
            canvas?.Dispose();
            bitmap?.Dispose();
            paint?.Dispose();
            font?.Dispose();
            
            if (textureId != 0)
                GL.DeleteTexture(textureId);
            if (vao != 0)
                GL.DeleteVertexArray(vao);
            if (vbo != 0)
                GL.DeleteBuffer(vbo);
            if (shaderProgram != 0)
                GL.DeleteProgram(shaderProgram);
        }
    }
}
