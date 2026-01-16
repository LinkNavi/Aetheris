// Net/Client/Game/Rendering/PrefabRenderer.cs - Fixed version
using System;
using System.Collections.Generic;
using OpenTK.Graphics.OpenGL4;
using OpenTK.Mathematics;
using AetherisClient.Rendering;
using Aetheris.GameLogic;

namespace Aetheris
{
    /// <summary>
    /// Renders placed prefabs (like trees) in the game world
    /// </summary>
    public class PrefabRenderer : IDisposable
    {
        private readonly GLBModelLoader modelLoader;
        private int shaderProgram;
        
        // Uniform locations
        private int locProjection, locView, locModel;
        private int locLightDir, locLightColor, locAmbient;
        private int locCameraPos, locFogDecay, locFogColor;
        private int locBaseColor;
        
        // Fallback cube mesh for when models don't load
        private int fallbackVao, fallbackVbo;
        private int fallbackVertexCount;
        
        public PrefabRenderer()
        {
            modelLoader = new GLBModelLoader();
            InitializeShader();
            InitializeFallbackMesh();
        }
        
        /// <summary>
        /// Render all prefabs in range of the camera
        /// </summary>
        public void RenderPrefabs(
            GameWorld? clientWorld, 
            Vector3 cameraPos, 
            Matrix4 view, 
            Matrix4 projection,
            float maxRenderDistance = 100f)
        {
            if (clientWorld == null) return;
            
            GL.UseProgram(shaderProgram);
            
            // Set uniforms
            GL.UniformMatrix4(locProjection, false, ref projection);
            GL.UniformMatrix4(locView, false, ref view);
            
            Vector3 lightDir = new Vector3(0.5f, 1f, 0.3f);
            Vector3 lightColor = new Vector3(1f, 1f, 1f);
            Vector3 ambient = new Vector3(0.4f, 0.4f, 0.4f);
            Vector3 fogColor = new Vector3(0.5f, 0.6f, 0.7f);
            
            GL.Uniform3(locLightDir, ref lightDir);
            GL.Uniform3(locLightColor, ref lightColor);
            GL.Uniform3(locAmbient, ref ambient);
            GL.Uniform3(locCameraPos, ref cameraPos);
            GL.Uniform1(locFogDecay, 0.003f);
            GL.Uniform3(locFogColor, ref fogColor);
            
            // Get prefabs in range
            var playerBlockPos = BlockPos.FromWorld(cameraPos.X, cameraPos.Y, cameraPos.Z);
            int searchRadius = (int)Math.Ceiling(maxRenderDistance / GridConfig.BLOCK_SIZE);
            
            var nearbyPrefabs = clientWorld.GetPrefabsInRange(playerBlockPos, searchRadius);
            
            int rendered = 0;
            foreach (var prefab in nearbyPrefabs)
            {
                var prefabDef = PrefabRegistry.Get(prefab.PrefabId);
                if (prefabDef == null) continue;
                
                // Distance culling
                var worldCenter = prefab.GetWorldCenter();
                float distance = Vector3.Distance(cameraPos, 
                    new Vector3(worldCenter.x, worldCenter.y, worldCenter.z));
                
                if (distance > maxRenderDistance) continue;
                
                // Try to load model
                var model = modelLoader.LoadModel(prefabDef.ModelPath);
                bool usingFallback = (model == null);
                
                // Calculate model matrix
                Matrix4 modelMatrix = CalculatePrefabTransform(prefab, prefabDef);
                GL.UniformMatrix4(locModel, false, ref modelMatrix);
                
                // Set color based on prefab type
                Vector4 baseColor = GetPrefabColor(prefabDef);
                GL.Uniform4(locBaseColor, ref baseColor);
                
                // Render the model or fallback
                if (usingFallback)
                {
                    RenderFallbackCube();
                }
                else
                {
                    GL.BindVertexArray(model.VAO);
                    
                    if (model.UseIndices)
                    {
                        GL.DrawElements(PrimitiveType.Triangles, model.IndexCount, 
                            DrawElementsType.UnsignedInt, 0);
                    }
                    else
                    {
                        GL.DrawArrays(PrimitiveType.Triangles, 0, model.VertexCount);
                    }
                }
                
                rendered++;
            }
            
            GL.BindVertexArray(0);
            
            if (rendered > 0)
            {
                Console.WriteLine($"[PrefabRenderer] Rendered {rendered} prefabs this frame");
            }
        }
        
        private Vector4 GetPrefabColor(PrefabDefinition def)
        {
            // Color based on prefab type
            if (def.Category == "nature" || HasTag(def, "tree"))
            {
                return new Vector4(0.3f, 0.6f, 0.2f, 1f); // Green for trees
            }
            else if (def.Category == "ores" || HasTag(def, "ore"))
            {
                // Color based on ore type
                if (HasTag(def, "ore_coal")) return new Vector4(0.2f, 0.2f, 0.2f, 1f);
                if (HasTag(def, "ore_iron")) return new Vector4(0.7f, 0.6f, 0.5f, 1f);
                if (HasTag(def, "ore_copper")) return new Vector4(0.8f, 0.5f, 0.3f, 1f);
                if (HasTag(def, "ore_diamond")) return new Vector4(0.4f, 0.8f, 1f, 1f);
                return new Vector4(0.6f, 0.6f, 0.6f, 1f); // Default gray
            }
            
            return new Vector4(0.8f, 0.8f, 0.8f, 1f); // Default
        }
        
        private bool HasTag(PrefabDefinition def, string tag)
        {
            foreach (var t in def.Tags)
            {
                if (t == tag) return true;
            }
            return false;
        }
        
        private void RenderFallbackCube()
        {
            GL.BindVertexArray(fallbackVao);
            GL.DrawArrays(PrimitiveType.Triangles, 0, fallbackVertexCount);
        }
        
        private void InitializeFallbackMesh()
        {
            // Create a simple cube mesh as fallback
            float s = 1f;
            float[] vertices = {
                // Front face
                -s, -s,  s,  0, 0, 1,  0, 0,
                 s, -s,  s,  0, 0, 1,  1, 0,
                 s,  s,  s,  0, 0, 1,  1, 1,
                -s, -s,  s,  0, 0, 1,  0, 0,
                 s,  s,  s,  0, 0, 1,  1, 1,
                -s,  s,  s,  0, 0, 1,  0, 1,
                
                // Back face
                 s, -s, -s,  0, 0, -1,  0, 0,
                -s, -s, -s,  0, 0, -1,  1, 0,
                -s,  s, -s,  0, 0, -1,  1, 1,
                 s, -s, -s,  0, 0, -1,  0, 0,
                -s,  s, -s,  0, 0, -1,  1, 1,
                 s,  s, -s,  0, 0, -1,  0, 1,
                
                // Top face
                -s,  s,  s,  0, 1, 0,  0, 0,
                 s,  s,  s,  0, 1, 0,  1, 0,
                 s,  s, -s,  0, 1, 0,  1, 1,
                -s,  s,  s,  0, 1, 0,  0, 0,
                 s,  s, -s,  0, 1, 0,  1, 1,
                -s,  s, -s,  0, 1, 0,  0, 1,
                
                // Bottom face
                -s, -s, -s,  0, -1, 0,  0, 0,
                 s, -s, -s,  0, -1, 0,  1, 0,
                 s, -s,  s,  0, -1, 0,  1, 1,
                -s, -s, -s,  0, -1, 0,  0, 0,
                 s, -s,  s,  0, -1, 0,  1, 1,
                -s, -s,  s,  0, -1, 0,  0, 1,
                
                // Right face
                 s, -s,  s,  1, 0, 0,  0, 0,
                 s, -s, -s,  1, 0, 0,  1, 0,
                 s,  s, -s,  1, 0, 0,  1, 1,
                 s, -s,  s,  1, 0, 0,  0, 0,
                 s,  s, -s,  1, 0, 0,  1, 1,
                 s,  s,  s,  1, 0, 0,  0, 1,
                
                // Left face
                -s, -s, -s,  -1, 0, 0,  0, 0,
                -s, -s,  s,  -1, 0, 0,  1, 0,
                -s,  s,  s,  -1, 0, 0,  1, 1,
                -s, -s, -s,  -1, 0, 0,  0, 0,
                -s,  s,  s,  -1, 0, 0,  1, 1,
                -s,  s, -s,  -1, 0, 0,  0, 1,
            };
            
            fallbackVertexCount = 36;
            
            fallbackVao = GL.GenVertexArray();
            fallbackVbo = GL.GenBuffer();
            
            GL.BindVertexArray(fallbackVao);
            GL.BindBuffer(BufferTarget.ArrayBuffer, fallbackVbo);
            GL.BufferData(BufferTarget.ArrayBuffer, vertices.Length * sizeof(float), 
                vertices, BufferUsageHint.StaticDraw);
            
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
            
            Console.WriteLine("[PrefabRenderer] Initialized fallback cube mesh");
        }
        
        private Matrix4 CalculatePrefabTransform(PlacedPrefab prefab, PrefabDefinition def)
        {
            // Get world position (origin of the prefab's block space)
            var (wx, wy, wz) = prefab.Position.ToWorldOrigin();
            
            // Start with translation to world position
            Matrix4 transform = Matrix4.CreateTranslation(wx, wy, wz);
            
            // Apply prefab's model offset
            transform *= Matrix4.CreateTranslation(
                def.ModelOffset.x, 
                def.ModelOffset.y, 
                def.ModelOffset.z);
            
            // Apply rotation (Y-axis rotation for prefab placement + model rotation)
            float placementRotation = prefab.Rotation * 90f; // 0-3 -> 0, 90, 180, 270
            transform *= Matrix4.CreateRotationY(MathHelper.DegreesToRadians(placementRotation));
            
            // Apply model's base rotation
            transform *= Matrix4.CreateRotationX(MathHelper.DegreesToRadians(def.ModelRotation.x));
            transform *= Matrix4.CreateRotationY(MathHelper.DegreesToRadians(def.ModelRotation.y));
            transform *= Matrix4.CreateRotationZ(MathHelper.DegreesToRadians(def.ModelRotation.z));
            
            // Apply scale
            transform *= Matrix4.CreateScale(def.ModelScale);
            
            return transform;
        }
        
        private void InitializeShader()
        {
            string vertexShader = @"
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;

out vec3 vNormal;
out vec3 vFragPos;
out vec2 vUV;
out vec3 vViewPos;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

void main()
{
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vFragPos = worldPos.xyz;
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    vUV = aUV;
    
    vec4 viewPos = uView * worldPos;
    vViewPos = viewPos.xyz;
    
    gl_Position = uProjection * viewPos;
}";

            string fragmentShader = @"
#version 330 core
in vec3 vNormal;
in vec3 vFragPos;
in vec2 vUV;
in vec3 vViewPos;

out vec4 FragColor;

uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform vec3 uAmbient;
uniform vec3 uCameraPos;
uniform float uFogDecay;
uniform vec3 uFogColor;
uniform vec4 uBaseColor;

void main()
{
    vec3 normal = normalize(vNormal);
    
    // Diffuse lighting
    float diff = max(dot(normal, normalize(uLightDir)), 0.0);
    vec3 diffuse = diff * uLightColor;
    
    // Use base color (set per prefab type)
    vec3 baseColor = uBaseColor.rgb;
    
    // Combine lighting
    vec3 result = (uAmbient + diffuse) * baseColor;
    
    // Fog
    float fogDist = length(vViewPos);
    float fogFactor = exp(-(fogDist * uFogDecay) * (fogDist * uFogDecay));
    fogFactor = clamp(fogFactor, 0.0, 1.0);
    
    vec3 final = mix(uFogColor, result, fogFactor);
    
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
            
            // Get uniform locations
            locProjection = GL.GetUniformLocation(shaderProgram, "uProjection");
            locView = GL.GetUniformLocation(shaderProgram, "uView");
            locModel = GL.GetUniformLocation(shaderProgram, "uModel");
            locLightDir = GL.GetUniformLocation(shaderProgram, "uLightDir");
            locLightColor = GL.GetUniformLocation(shaderProgram, "uLightColor");
            locAmbient = GL.GetUniformLocation(shaderProgram, "uAmbient");
            locCameraPos = GL.GetUniformLocation(shaderProgram, "uCameraPos");
            locFogDecay = GL.GetUniformLocation(shaderProgram, "uFogDecay");
            locFogColor = GL.GetUniformLocation(shaderProgram, "uFogColor");
            locBaseColor = GL.GetUniformLocation(shaderProgram, "uBaseColor");
            
            Console.WriteLine("[PrefabRenderer] Shader initialized");
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
                Console.WriteLine($"[PrefabRenderer] Shader compilation failed: {info}");
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
            if (fallbackVao != 0)
            {
                GL.DeleteVertexArray(fallbackVao);
            }
            if (fallbackVbo != 0)
            {
                GL.DeleteBuffer(fallbackVbo);
            }
        }
    }
}
