// Net/Client/Game/Rendering/BlockRenderer.cs - Renders placed blocks as 3D models
using System;
using System.Collections.Generic;
using OpenTK.Graphics.OpenGL4;
using OpenTK.Mathematics;
using AetherisClient.Rendering;

// Type alias for clarity
using ClientBlockType = AetherisClient.Rendering.BlockType;

namespace Aetheris
{
    /// <summary>
    /// Renders placed blocks as individual 3D cube models
    /// </summary>
    public class BlockRenderer : IDisposable
    {
        private int vao, vbo;
        private int shaderProgram;
        
        private const float BLOCK_SIZE = 1f;
        
        // Cube vertices: Position(3) + Normal(3) + UV(2) = 8 floats per vertex
        private static readonly float[] cubeVertices = GenerateCubeVertices();
        
        public BlockRenderer()
        {
            InitializeCubeMesh();
            InitializeShader();
        }
        
        private void InitializeCubeMesh()
        {
            vao = GL.GenVertexArray();
            vbo = GL.GenBuffer();
            
            GL.BindVertexArray(vao);
            GL.BindBuffer(BufferTarget.ArrayBuffer, vbo);
            
            GL.BufferData(BufferTarget.ArrayBuffer, 
                cubeVertices.Length * sizeof(float), 
                cubeVertices, 
                BufferUsageHint.StaticDraw);
            
            int stride = 8 * sizeof(float);
            
            // Position
            GL.EnableVertexAttribArray(0);
            GL.VertexAttribPointer(0, 3, VertexAttribPointerType.Float, false, stride, 0);
            
            // Normal
            GL.EnableVertexAttribArray(1);
            GL.VertexAttribPointer(1, 3, VertexAttribPointerType.Float, false, stride, 3 * sizeof(float));
            
            // UV
            GL.EnableVertexAttribArray(2);
            GL.VertexAttribPointer(2, 2, VertexAttribPointerType.Float, false, stride, 6 * sizeof(float));
            
            GL.BindVertexArray(0);
            
            Console.WriteLine($"[BlockRenderer] Initialized with {cubeVertices.Length / 8} vertices");
        }
        
        private void InitializeShader()
        {
            string vertexShader = @"
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;

out vec3 vNormal;
out vec3 vWorldPos;
out vec3 vViewPos;
out vec2 vUV;

uniform mat4 uProjection;
uniform mat4 uView;
uniform mat4 uModel;
uniform mat3 uNormalMatrix;

void main()
{
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vec4 viewPos4 = uView * worldPos;
    vViewPos = viewPos4.xyz;
    vNormal = normalize(uNormalMatrix * aNormal);
    vUV = aUV;
    gl_Position = uProjection * viewPos4;
}";

            string fragmentShader = @"
#version 330 core
in vec3 vNormal;
in vec3 vWorldPos;
in vec3 vViewPos;
in vec2 vUV;

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

    // Two directional lights
    vec3 light1Dir = normalize(vec3(0.5, 1.0, 0.3));
    vec3 light2Dir = normalize(vec3(-0.3, 0.5, -0.8));

    float diff1 = max(dot(N, light1Dir), 0.0);
    float diff2 = max(dot(N, light2Dir), 0.0) * 0.45;

    // Hemispheric ambient
    float hemi = N.y * 0.5 + 0.5;
    vec3 hemiAmbient = mix(uGroundColor, uSkyColor, hemi) * 0.28;

    vec3 diffuse = (diff1 + diff2) * baseTex;

    // Specular
    vec3 viewDir = normalize(uCameraPos - vWorldPos);
    vec3 half1 = normalize(light1Dir + viewDir);
    float spec = pow(max(dot(N, half1), 0.0), 32.0) * 0.6;

    vec3 lit = hemiAmbient + diffuse + vec3(spec);

    // Tonemapping
    vec3 tone = lit / (lit + vec3(1.0));
    float gray = dot(tone, vec3(0.299, 0.587, 0.114));
    tone = mix(vec3(gray), tone, 0.88);
    tone = pow(tone, vec3(1.0 / 2.2));

    // Fog
    float fogDist = length(vViewPos);
    float f = exp(-(fogDist * uFogDecay) * (fogDist * uFogDecay));
    f = clamp(f, 0.0, 1.0);

    vec3 final = mix(uFogColor, tone * baseTex, f);

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
        
        /// <summary>
        /// Render all visible placed blocks
        /// </summary>
        public void RenderBlocks(
            PlacedBlockManager blockManager, 
            Vector3 cameraPos, 
            Matrix4 view, 
            Matrix4 projection,
            int atlasTexture,
            float fogDecay,
            float maxRenderDistance)
        {
            GL.UseProgram(shaderProgram);
            
            // Set global uniforms
            GL.UniformMatrix4(GL.GetUniformLocation(shaderProgram, "uProjection"), false, ref projection);
            GL.UniformMatrix4(GL.GetUniformLocation(shaderProgram, "uView"), false, ref view);
            GL.Uniform1(GL.GetUniformLocation(shaderProgram, "uFogDecay"), fogDecay);
            GL.Uniform3(GL.GetUniformLocation(shaderProgram, "uCameraPos"), cameraPos);
            GL.Uniform3(GL.GetUniformLocation(shaderProgram, "uFogColor"), 0.5f, 0.6f, 0.7f);
            GL.Uniform3(GL.GetUniformLocation(shaderProgram, "uSkyColor"), 0.6f, 0.7f, 0.95f);
            GL.Uniform3(GL.GetUniformLocation(shaderProgram, "uGroundColor"), 0.28f, 0.22f, 0.16f);
            
            // Bind texture
            GL.ActiveTexture(TextureUnit.Texture0);
            GL.BindTexture(TextureTarget.Texture2D, atlasTexture);
            GL.Uniform1(GL.GetUniformLocation(shaderProgram, "uAtlasTexture"), 0);
            
            GL.BindVertexArray(vao);
            
            int rendered = 0;
            
            // Render all blocks in range
            foreach (var block in blockManager.GetBlocksInRange(cameraPos, maxRenderDistance))
            {
                // Frustum culling would go here (optional)
                
                RenderSingleBlock(block, cameraPos);
                rendered++;
            }
            
            GL.BindVertexArray(0);
            
            if (rendered > 0)
            {
                //Console.WriteLine($"[BlockRenderer] Rendered {rendered} blocks");
            }
        }
        
        private void RenderSingleBlock(PlacedBlockManager.PlacedBlock block, Vector3 cameraPos)
        {
            // Create model matrix (translate to block position)
            Matrix4 model = Matrix4.CreateTranslation(
                block.Position.X + 0.5f, 
                block.Position.Y + 0.5f, 
                block.Position.Z + 0.5f
            );
            
            GL.UniformMatrix4(GL.GetUniformLocation(shaderProgram, "uModel"), false, ref model);
            
            // Normal matrix
            Matrix3 normalMat = new Matrix3(model);
            normalMat = Matrix3.Transpose(normalMat.Inverted());
            GL.UniformMatrix3(GL.GetUniformLocation(shaderProgram, "uNormalMatrix"), false, ref normalMat);
            
            // Draw the cube (36 vertices = 6 faces * 2 triangles * 3 vertices)
            GL.DrawArrays(PrimitiveType.Triangles, 0, 36);
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
                Console.WriteLine($"[BlockRenderer] Shader compilation failed: {info}");
            }
            
            return shader;
        }
        
        /// <summary>
        /// Generate cube vertices with proper UVs for each face
        /// </summary>
        private static float[] GenerateCubeVertices()
        {
            List<float> vertices = new List<float>();
            float s = BLOCK_SIZE / 2f; // Half size
            
            // FIXED: Use fully qualified namespace to avoid ambiguity
            // Get UV coordinates for each face
            var topUV = AtlasManager.GetAtlasUV(ClientBlockType.Grass, BlockFace.Top);
            var bottomUV = AtlasManager.GetAtlasUV(ClientBlockType.Grass, BlockFace.Bottom);
            var sideUV = AtlasManager.GetAtlasUV(ClientBlockType.Grass, BlockFace.Side);
            
            // Front face (+Z)
            AddQuad(vertices, 
                new Vector3(-s, -s, s), new Vector3(s, -s, s), 
                new Vector3(s, s, s), new Vector3(-s, s, s),
                new Vector3(0, 0, 1), sideUV);
            
            // Back face (-Z)
            AddQuad(vertices,
                new Vector3(s, -s, -s), new Vector3(-s, -s, -s),
                new Vector3(-s, s, -s), new Vector3(s, s, -s),
                new Vector3(0, 0, -1), sideUV);
            
            // Top face (+Y)
            AddQuad(vertices,
                new Vector3(-s, s, s), new Vector3(s, s, s),
                new Vector3(s, s, -s), new Vector3(-s, s, -s),
                new Vector3(0, 1, 0), topUV);
            
            // Bottom face (-Y)
            AddQuad(vertices,
                new Vector3(-s, -s, -s), new Vector3(s, -s, -s),
                new Vector3(s, -s, s), new Vector3(-s, -s, s),
                new Vector3(0, -1, 0), bottomUV);
            
            // Right face (+X)
            AddQuad(vertices,
                new Vector3(s, -s, s), new Vector3(s, -s, -s),
                new Vector3(s, s, -s), new Vector3(s, s, s),
                new Vector3(1, 0, 0), sideUV);
            
            // Left face (-X)
            AddQuad(vertices,
                new Vector3(-s, -s, -s), new Vector3(-s, -s, s),
                new Vector3(-s, s, s), new Vector3(-s, s, -s),
                new Vector3(-1, 0, 0), sideUV);
            
            return vertices.ToArray();
        }
        
        private static void AddQuad(List<float> vertices, 
            Vector3 v0, Vector3 v1, Vector3 v2, Vector3 v3, 
            Vector3 normal, 
            (float uMin, float vMin, float uMax, float vMax) uv)
        {
            // Triangle 1: v0, v1, v2
            AddVertex(vertices, v0, normal, uv.uMin, uv.vMin);
            AddVertex(vertices, v1, normal, uv.uMax, uv.vMin);
            AddVertex(vertices, v2, normal, uv.uMax, uv.vMax);
            
            // Triangle 2: v0, v2, v3
            AddVertex(vertices, v0, normal, uv.uMin, uv.vMin);
            AddVertex(vertices, v2, normal, uv.uMax, uv.vMax);
            AddVertex(vertices, v3, normal, uv.uMin, uv.vMax);
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
            GL.DeleteProgram(shaderProgram);
        }
    }
}
