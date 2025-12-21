// Net/Client/Game/Rendering/BlockRenderer.cs - Fixed type conversion
using System;
using System.Collections.Generic;
using OpenTK.Graphics.OpenGL4;
using OpenTK.Mathematics;
using AetherisClient.Rendering;

namespace Aetheris
{
    public class BlockRenderer : IDisposable
    {
        private int vao, vbo, instanceVbo;
        private int shaderProgram;
        
        private const float BLOCK_SIZE = 1f;
        private const int FLOATS_PER_INSTANCE = 8; // x,y,z, blockType, uMin,vMin,uMax,vMax
        
        private static readonly float[] baseCubeVertices = GenerateBaseCubeVertices();
        
        private float[] instanceData = new float[1024 * FLOATS_PER_INSTANCE];
        private int instanceCount = 0;
        
        public BlockRenderer()
        {
            InitializeMesh();
            InitializeShader();
        }
        
        private void InitializeMesh()
        {
            vao = GL.GenVertexArray();
            vbo = GL.GenBuffer();
            instanceVbo = GL.GenBuffer();
            
            GL.BindVertexArray(vao);
            
            GL.BindBuffer(BufferTarget.ArrayBuffer, vbo);
            GL.BufferData(BufferTarget.ArrayBuffer, 
                baseCubeVertices.Length * sizeof(float), 
                baseCubeVertices, 
                BufferUsageHint.StaticDraw);
            
            int stride = 8 * sizeof(float);
            
            GL.EnableVertexAttribArray(0);
            GL.VertexAttribPointer(0, 3, VertexAttribPointerType.Float, false, stride, 0);
            
            GL.EnableVertexAttribArray(1);
            GL.VertexAttribPointer(1, 3, VertexAttribPointerType.Float, false, stride, 3 * sizeof(float));
            
            GL.EnableVertexAttribArray(2);
            GL.VertexAttribPointer(2, 2, VertexAttribPointerType.Float, false, stride, 6 * sizeof(float));
            
            GL.BindBuffer(BufferTarget.ArrayBuffer, instanceVbo);
            GL.BufferData(BufferTarget.ArrayBuffer, instanceData.Length * sizeof(float), IntPtr.Zero, BufferUsageHint.DynamicDraw);
            
            int instanceStride = FLOATS_PER_INSTANCE * sizeof(float);
            
            GL.EnableVertexAttribArray(3);
            GL.VertexAttribPointer(3, 3, VertexAttribPointerType.Float, false, instanceStride, 0);
            GL.VertexAttribDivisor(3, 1);
            
            GL.EnableVertexAttribArray(4);
            GL.VertexAttribPointer(4, 1, VertexAttribPointerType.Float, false, instanceStride, 3 * sizeof(float));
            GL.VertexAttribDivisor(4, 1);
            
            GL.EnableVertexAttribArray(5);
            GL.VertexAttribPointer(5, 4, VertexAttribPointerType.Float, false, instanceStride, 4 * sizeof(float));
            GL.VertexAttribDivisor(5, 1);
            
            GL.BindVertexArray(0);
        }
        
        private void InitializeShader()
        {
            string vertexShader = @"
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aLocalUV;
layout (location = 3) in vec3 aInstancePos;
layout (location = 4) in float aBlockType;
layout (location = 5) in vec4 aUVBounds;

out vec3 vNormal;
out vec3 vWorldPos;
out vec3 vViewPos;
out vec2 vUV;
flat out int vBlockType;

uniform mat4 uProjection;
uniform mat4 uView;

void main()
{
    vec3 worldPos = aPos * 0.5 + aInstancePos + vec3(0.5);
    vWorldPos = worldPos;
    
    vec4 viewPos4 = uView * vec4(worldPos, 1.0);
    vViewPos = viewPos4.xyz;
    vNormal = aNormal;
    
    vUV = mix(aUVBounds.xy, aUVBounds.zw, aLocalUV);
    vBlockType = int(aBlockType);
    
    gl_Position = uProjection * viewPos4;
}";

            string fragmentShader = @"
#version 330 core
in vec3 vNormal;
in vec3 vWorldPos;
in vec3 vViewPos;
in vec2 vUV;
flat in int vBlockType;

out vec4 FragColor;

uniform sampler2D uAtlasTexture;
uniform float uFogDecay;
uniform vec3 uCameraPos;
uniform vec3 uFogColor;
uniform vec3 uSkyColor;
uniform vec3 uGroundColor;

void main()
{
    vec3 N = normalize(vNormal);
    vec3 baseTex = texture(uAtlasTexture, vUV).rgb;

    vec3 light1Dir = normalize(vec3(0.5, 1.0, 0.3));
    vec3 light2Dir = normalize(vec3(-0.3, 0.5, -0.8));

    float diff1 = max(dot(N, light1Dir), 0.0);
    float diff2 = max(dot(N, light2Dir), 0.0) * 0.45;

    float hemi = N.y * 0.5 + 0.5;
    vec3 hemiAmbient = mix(uGroundColor, uSkyColor, hemi) * 0.28;

    vec3 diffuse = (diff1 + diff2) * baseTex;

    vec3 viewDir = normalize(uCameraPos - vWorldPos);
    vec3 half1 = normalize(light1Dir + viewDir);
    float spec = pow(max(dot(N, half1), 0.0), 32.0) * 0.4;

    vec3 lit = hemiAmbient + diffuse + vec3(spec);

    vec3 tone = lit / (lit + vec3(1.0));
    float gray = dot(tone, vec3(0.299, 0.587, 0.114));
    tone = mix(vec3(gray), tone, 0.88);
    tone = pow(tone, vec3(1.0 / 2.2));

    float fogDist = length(vViewPos);
    float f = exp(-(fogDist * uFogDecay) * (fogDist * uFogDecay));
    f = clamp(f, 0.0, 1.0);

    vec3 final = mix(uFogColor, tone, f);

    FragColor = vec4(final, 1.0);
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
        
        public void RenderBlocks(
            PlacedBlockManager blockManager, 
            Vector3 cameraPos, 
            Matrix4 view, 
            Matrix4 projection,
            int atlasTexture,
            float fogDecay,
            float maxRenderDistance)
        {
            instanceCount = 0;
            float maxDistSq = maxRenderDistance * maxRenderDistance;
            
            foreach (var block in blockManager.GetBlocksInRange(cameraPos, maxRenderDistance))
            {
                float dx = block.Position.X - cameraPos.X;
                float dy = block.Position.Y - cameraPos.Y;
                float dz = block.Position.Z - cameraPos.Z;
                float distSq = dx * dx + dy * dy + dz * dz;
                
                if (distSq > maxDistSq) continue;
                
                // FIXED: Convert from Aetheris.BlockType to AetherisClient.Rendering.BlockType
                AetherisClient.Rendering.BlockType renderingBlockType = 
                    (AetherisClient.Rendering.BlockType)((int)block.BlockType);
                
                var (uMin, vMin, uMax, vMax) = AtlasManager.GetAtlasUV(renderingBlockType, BlockFace.Side);
                
                int neededSize = (instanceCount + 1) * FLOATS_PER_INSTANCE;
                if (neededSize > instanceData.Length)
                {
                    Array.Resize(ref instanceData, instanceData.Length * 2);
                }
                
                int offset = instanceCount * FLOATS_PER_INSTANCE;
                instanceData[offset + 0] = block.Position.X;
                instanceData[offset + 1] = block.Position.Y;
                instanceData[offset + 2] = block.Position.Z;
                instanceData[offset + 3] = (float)renderingBlockType;
                instanceData[offset + 4] = uMin;
                instanceData[offset + 5] = vMin;
                instanceData[offset + 6] = uMax;
                instanceData[offset + 7] = vMax;
                
                instanceCount++;
            }
            
            if (instanceCount == 0) return;
            
            GL.UseProgram(shaderProgram);
            
            GL.UniformMatrix4(GL.GetUniformLocation(shaderProgram, "uProjection"), false, ref projection);
            GL.UniformMatrix4(GL.GetUniformLocation(shaderProgram, "uView"), false, ref view);
            GL.Uniform1(GL.GetUniformLocation(shaderProgram, "uFogDecay"), fogDecay);
            GL.Uniform3(GL.GetUniformLocation(shaderProgram, "uCameraPos"), cameraPos);
            GL.Uniform3(GL.GetUniformLocation(shaderProgram, "uFogColor"), 0.5f, 0.6f, 0.7f);
            GL.Uniform3(GL.GetUniformLocation(shaderProgram, "uSkyColor"), 0.6f, 0.7f, 0.95f);
            GL.Uniform3(GL.GetUniformLocation(shaderProgram, "uGroundColor"), 0.28f, 0.22f, 0.16f);
            
            GL.ActiveTexture(TextureUnit.Texture0);
            GL.BindTexture(TextureTarget.Texture2D, atlasTexture);
            GL.Uniform1(GL.GetUniformLocation(shaderProgram, "uAtlasTexture"), 0);
            
            GL.BindBuffer(BufferTarget.ArrayBuffer, instanceVbo);
            GL.BufferSubData(BufferTarget.ArrayBuffer, IntPtr.Zero, instanceCount * FLOATS_PER_INSTANCE * sizeof(float), instanceData);
            
            GL.BindVertexArray(vao);
            GL.DrawArraysInstanced(PrimitiveType.Triangles, 0, 36, instanceCount);
            GL.BindVertexArray(0);
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
                Console.WriteLine($"[BlockRenderer] Shader error: {info}");
            }
            
            return shader;
        }
        
        private static float[] GenerateBaseCubeVertices()
        {
            var vertices = new List<float>();
            
            AddQuad(vertices, 
                new Vector3(-1, -1, 1), new Vector3(1, -1, 1), 
                new Vector3(1, 1, 1), new Vector3(-1, 1, 1),
                new Vector3(0, 0, 1));
            
            AddQuad(vertices,
                new Vector3(1, -1, -1), new Vector3(-1, -1, -1),
                new Vector3(-1, 1, -1), new Vector3(1, 1, -1),
                new Vector3(0, 0, -1));
            
            AddQuad(vertices,
                new Vector3(-1, 1, 1), new Vector3(1, 1, 1),
                new Vector3(1, 1, -1), new Vector3(-1, 1, -1),
                new Vector3(0, 1, 0));
            
            AddQuad(vertices,
                new Vector3(-1, -1, -1), new Vector3(1, -1, -1),
                new Vector3(1, -1, 1), new Vector3(-1, -1, 1),
                new Vector3(0, -1, 0));
            
            AddQuad(vertices,
                new Vector3(1, -1, 1), new Vector3(1, -1, -1),
                new Vector3(1, 1, -1), new Vector3(1, 1, 1),
                new Vector3(1, 0, 0));
            
            AddQuad(vertices,
                new Vector3(-1, -1, -1), new Vector3(-1, -1, 1),
                new Vector3(-1, 1, 1), new Vector3(-1, 1, -1),
                new Vector3(-1, 0, 0));
            
            return vertices.ToArray();
        }
        
        private static void AddQuad(List<float> vertices, 
            Vector3 v0, Vector3 v1, Vector3 v2, Vector3 v3, Vector3 normal)
        {
            AddVertex(vertices, v0, normal, 0, 0);
            AddVertex(vertices, v1, normal, 1, 0);
            AddVertex(vertices, v2, normal, 1, 1);
            
            AddVertex(vertices, v0, normal, 0, 0);
            AddVertex(vertices, v2, normal, 1, 1);
            AddVertex(vertices, v3, normal, 0, 1);
        }
        
        private static void AddVertex(List<float> vertices, Vector3 pos, Vector3 normal, float u, float v)
        {
            vertices.Add(pos.X);
            vertices.Add(pos.Y);
            vertices.Add(pos.Z);
            vertices.Add(normal.X);
            vertices.Add(normal.Y);
            vertices.Add(normal.Z);
            vertices.Add(u);
            vertices.Add(v);
        }
        
        public void Dispose()
        {
            GL.DeleteVertexArray(vao);
            GL.DeleteBuffer(vbo);
            GL.DeleteBuffer(instanceVbo);
            GL.DeleteProgram(shaderProgram);
        }
    }
}
