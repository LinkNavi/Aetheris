// Net/Client/Rendering/GLBModelLoader.cs - Load and render GLB/GLTF models
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using OpenTK.Graphics.OpenGL4;
using OpenTK.Mathematics;
using SharpGLTF.Schema2;
using SharpGLTF.Runtime;

namespace AetherisClient.Rendering
{
    public class GLBModel : IDisposable
    {
        public int VAO { get; private set; }
        public int VBO { get; private set; }
        public int EBO { get; private set; }
        public int VertexCount { get; private set; }
        public int IndexCount { get; private set; }
        public bool UseIndices { get; private set; }

        public Vector3 BoundsMin { get; set; }
        public Vector3 BoundsMax { get; set; }

        public GLBModel(int vao, int vbo, int ebo, int vertexCount, int indexCount, bool useIndices)
        {
            VAO = vao;
            VBO = vbo;
            EBO = ebo;
            VertexCount = vertexCount;
            IndexCount = indexCount;
            UseIndices = useIndices;
        }

        public void Dispose()
        {
            if (VAO != 0) GL.DeleteVertexArray(VAO);
            if (VBO != 0) GL.DeleteBuffer(VBO);
            if (EBO != 0) GL.DeleteBuffer(EBO);
        }
    }

    public class GLBModelLoader
    {
        private readonly Dictionary<string, GLBModel> modelCache = new();

        public GLBModel? LoadModel(string path)
        {
            if (modelCache.TryGetValue(path, out var cached))
                return cached;

            if (!File.Exists(path))
            {
                Console.WriteLine($"[GLB] Model not found: {path}");
                return CreateFallbackModel();
            }

            try
            {
                var model = ModelRoot.Load(path);
                var glbModel = ConvertToGLBModel(model);
                modelCache[path] = glbModel;
                return glbModel;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[GLB] Error loading {path}: {ex.Message}");
                return CreateFallbackModel();
            }
        }

        private GLBModel ConvertToGLBModel(ModelRoot model)
        {
            var vertices = new List<float>();
            var indices = new List<uint>();

            OpenTK.Mathematics.Vector3 boundsMin = new OpenTK.Mathematics.Vector3(float.MaxValue);
            OpenTK.Mathematics.Vector3 boundsMax = new OpenTK.Mathematics.Vector3(float.MinValue);

            foreach (var mesh in model.LogicalMeshes)
            {
                foreach (var primitive in mesh.Primitives)
                {
                    var accessor = primitive.GetVertexAccessor("POSITION");
                    if (accessor == null) continue;

                    var positions = accessor.AsVector3Array();
                    var normalAccessor = primitive.GetVertexAccessor("NORMAL");
                    var texCoordAccessor = primitive.GetVertexAccessor("TEXCOORD_0");

                    uint baseIndex = (uint)(vertices.Count / 8);

                    for (int i = 0; i < positions.Count; i++)
                    {
                        var pos = positions[i];

                        // Convert System.Numerics.Vector3 to OpenTK.Mathematics.Vector3
                        var posOTK = new OpenTK.Mathematics.Vector3(pos.X, pos.Y, pos.Z);

                        // Get normal (or default)
                        System.Numerics.Vector3 normal = System.Numerics.Vector3.UnitY;
                        if (normalAccessor != null)
                        {
                            var normals = normalAccessor.AsVector3Array();
                            if (i < normals.Count)
                                normal = normals[i];
                        }

                        // Get UV (or default)
                        System.Numerics.Vector2 uv = System.Numerics.Vector2.Zero;
                        if (texCoordAccessor != null)
                        {
                            var texCoords = texCoordAccessor.AsVector2Array();
                            if (i < texCoords.Count)
                                uv = texCoords[i];
                        }

                        // Update bounds
                        boundsMin = OpenTK.Mathematics.Vector3.ComponentMin(boundsMin, posOTK);
                        boundsMax = OpenTK.Mathematics.Vector3.ComponentMax(boundsMax, posOTK);

                        // Add vertex: Position(3) + Normal(3) + UV(2)
                        vertices.Add(pos.X);
                        vertices.Add(pos.Y);
                        vertices.Add(pos.Z);
                        vertices.Add(normal.X);
                        vertices.Add(normal.Y);
                        vertices.Add(normal.Z);
                        vertices.Add(uv.X);
                        vertices.Add(uv.Y);
                    }

                    // Add indices
                    var primitiveIndices = primitive.GetIndices();
                    foreach (var idx in primitiveIndices)
                    {
                        indices.Add(baseIndex + (uint)idx);
                    }
                }
            }

            // Upload to GPU
            int vao = GL.GenVertexArray();
            int vbo = GL.GenBuffer();
            int ebo = GL.GenBuffer();

            GL.BindVertexArray(vao);

            // VBO
            GL.BindBuffer(BufferTarget.ArrayBuffer, vbo);
            GL.BufferData(BufferTarget.ArrayBuffer, vertices.Count * sizeof(float),
                vertices.ToArray(), BufferUsageHint.StaticDraw);

            // EBO
            GL.BindBuffer(BufferTarget.ElementArrayBuffer, ebo);
            GL.BufferData(BufferTarget.ElementArrayBuffer, indices.Count * sizeof(uint),
                indices.ToArray(), BufferUsageHint.StaticDraw);

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

            var glbModel = new GLBModel(vao, vbo, ebo, vertices.Count / 8, indices.Count, true)
            {
                BoundsMin = boundsMin,
                BoundsMax = boundsMax
            };

            Console.WriteLine($"[GLB] Loaded model: {vertices.Count / 8} vertices, {indices.Count} indices");

            return glbModel;
        }
        private GLBModel CreateFallbackModel()
        {
            // Simple cube fallback
            float[] vertices = {
                // Positions         Normals          UVs
                -0.5f, -0.5f, -0.5f,  0, 0, -1,  0, 0,
                 0.5f, -0.5f, -0.5f,  0, 0, -1,  1, 0,
                 0.5f,  0.5f, -0.5f,  0, 0, -1,  1, 1,
                -0.5f,  0.5f, -0.5f,  0, 0, -1,  0, 1,
            };

            uint[] indices = { 0, 1, 2, 0, 2, 3 };

            int vao = GL.GenVertexArray();
            int vbo = GL.GenBuffer();
            int ebo = GL.GenBuffer();

            GL.BindVertexArray(vao);

            GL.BindBuffer(BufferTarget.ArrayBuffer, vbo);
            GL.BufferData(BufferTarget.ArrayBuffer, vertices.Length * sizeof(float),
                vertices, BufferUsageHint.StaticDraw);

            GL.BindBuffer(BufferTarget.ElementArrayBuffer, ebo);
            GL.BufferData(BufferTarget.ElementArrayBuffer, indices.Length * sizeof(uint),
                indices, BufferUsageHint.StaticDraw);

            int stride = 8 * sizeof(float);
            GL.EnableVertexAttribArray(0);
            GL.VertexAttribPointer(0, 3, VertexAttribPointerType.Float, false, stride, 0);
            GL.EnableVertexAttribArray(1);
            GL.VertexAttribPointer(1, 3, VertexAttribPointerType.Float, false, stride, 3 * sizeof(float));
            GL.EnableVertexAttribArray(2);
            GL.VertexAttribPointer(2, 2, VertexAttribPointerType.Float, false, stride, 6 * sizeof(float));

            GL.BindVertexArray(0);

            return new GLBModel(vao, vbo, ebo, 4, 6, true);
        }

        public void ClearCache()
        {
            foreach (var model in modelCache.Values)
            {
                model.Dispose();
            }
            modelCache.Clear();
        }
    }

    public class HeldItemRenderer : IDisposable
    {
        private readonly GLBModelLoader modelLoader = new();
        private int shaderProgram;

        public HeldItemRenderer()
        {
            InitializeShader();
        }

        private void InitializeShader()
        {
            string vertexShader = @"
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;

out vec3 vNormal;
out vec2 vUV;
out vec3 vFragPos;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

void main()
{
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vFragPos = worldPos.xyz;
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    vUV = aUV;
    gl_Position = uProjection * uView * worldPos;
}";

            string fragmentShader = @"
#version 330 core
in vec3 vNormal;
in vec2 vUV;
in vec3 vFragPos;

out vec4 FragColor;

uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform vec3 uAmbient;
uniform vec4 uBaseColor;

void main()
{
    vec3 normal = normalize(vNormal);
    vec3 lightDir = normalize(uLightDir);
    
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = diff * uLightColor;
    
    vec3 lighting = uAmbient + diffuse;
    vec3 result = lighting * uBaseColor.rgb;
    
    FragColor = vec4(result, uBaseColor.a);
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

        public void RenderHeldItem(int itemId, Matrix4 viewMatrix, Matrix4 projectionMatrix,
            float swingProgress = 0f, float time = 0f)
        {
            var itemDef = Aetheris.ItemRegistry.Get(itemId);
            if (itemDef == null || string.IsNullOrEmpty(itemDef.ModelPath))
                return;

            var model = modelLoader.LoadModel(itemDef.ModelPath);
            if (model == null) return;

            GL.UseProgram(shaderProgram);

            // Calculate held item transform
            Matrix4 itemTransform = CalculateHeldTransform(itemDef, swingProgress, time);

            // Set uniforms
            int locModel = GL.GetUniformLocation(shaderProgram, "uModel");
            int locView = GL.GetUniformLocation(shaderProgram, "uView");
            int locProj = GL.GetUniformLocation(shaderProgram, "uProjection");

            GL.UniformMatrix4(locModel, false, ref itemTransform);
            GL.UniformMatrix4(locView, false, ref viewMatrix);
            GL.UniformMatrix4(locProj, false, ref projectionMatrix);

            // Lighting
            Vector3 lightDir = new Vector3(0.5f, 1f, 0.3f);
            Vector3 lightColor = new Vector3(1f, 1f, 1f);
            Vector3 ambient = new Vector3(0.3f, 0.3f, 0.3f);
            Vector4 baseColor = itemDef.GetRarityColor();

            GL.Uniform3(GL.GetUniformLocation(shaderProgram, "uLightDir"), ref lightDir);
            GL.Uniform3(GL.GetUniformLocation(shaderProgram, "uLightColor"), ref lightColor);
            GL.Uniform3(GL.GetUniformLocation(shaderProgram, "uAmbient"), ref ambient);
            GL.Uniform4(GL.GetUniformLocation(shaderProgram, "uBaseColor"), ref baseColor);

            // Render
            GL.BindVertexArray(model.VAO);

            if (model.UseIndices)
            {
                GL.DrawElements(OpenTK.Graphics.OpenGL4.PrimitiveType.Triangles, model.IndexCount, DrawElementsType.UnsignedInt, 0);
            }
            else
            {
                GL.DrawArrays(OpenTK.Graphics.OpenGL4.PrimitiveType.Triangles, 0, model.VertexCount);
            }

            GL.BindVertexArray(0);
        }

        private Matrix4 CalculateHeldTransform(Aetheris.ItemDefinition itemDef,
            float swingProgress, float time)
        {
            // Base position (right hand position relative to camera)
            Matrix4 transform = Matrix4.CreateTranslation(0.6f, -0.5f, -1.2f);

            // Apply item-specific offset
            transform *= Matrix4.CreateTranslation(itemDef.HeldOffset);

            // Apply item-specific rotation
            transform *= Matrix4.CreateRotationX(MathHelper.DegreesToRadians(itemDef.HeldRotation.X));
            transform *= Matrix4.CreateRotationY(MathHelper.DegreesToRadians(itemDef.HeldRotation.Y));
            transform *= Matrix4.CreateRotationZ(MathHelper.DegreesToRadians(itemDef.HeldRotation.Z));

            // Apply swing animation
            if (swingProgress > 0f)
            {
                float swingAngle = MathF.Sin(swingProgress * MathF.PI) * 45f;
                transform *= Matrix4.CreateRotationX(MathHelper.DegreesToRadians(swingAngle));
            }

            // Apply idle sway
            float sway = MathF.Sin(time * 2f) * 0.02f;
            float bob = MathF.Cos(time * 2f) * 0.01f;
            transform *= Matrix4.CreateTranslation(sway, bob, 0f);

            // Apply scale
            transform *= Matrix4.CreateScale(itemDef.HeldScale * 0.15f);

            return transform;
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
                Console.WriteLine($"[HeldItem] Shader compilation failed: {info}");
            }

            return shader;
        }

        public void Dispose()
        {
            modelLoader.ClearCache();
            if (shaderProgram != 0)
            {
                GL.DeleteProgram(shaderProgram);
            }
        }
    }
}
