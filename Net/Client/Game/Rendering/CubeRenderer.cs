// Net/Client/Game/Rendering/VoxelCubeRenderer.cs
using System;
using System.Collections.Generic;
using OpenTK.Graphics.OpenGL4;
using OpenTK.Mathematics;

namespace Aetheris
{
    public class VoxelCubeRenderer : IDisposable
    {
        private int vao, vbo, ebo, instanceVBO;
        private int shader;
        
        public VoxelCubeRenderer()
        {
            InitMesh();
            InitShader();
        }
        
        private void InitMesh()
        {
            float[] verts = {
                // Pos (3) + Normal (3) + UV (2)
                // Front
                -0.5f,-0.5f, 0.5f, 0,0,1, 0,0,
                 0.5f,-0.5f, 0.5f, 0,0,1, 1,0,
                 0.5f, 0.5f, 0.5f, 0,0,1, 1,1,
                -0.5f, 0.5f, 0.5f, 0,0,1, 0,1,
                // Back
                 0.5f,-0.5f,-0.5f, 0,0,-1, 0,0,
                -0.5f,-0.5f,-0.5f, 0,0,-1, 1,0,
                -0.5f, 0.5f,-0.5f, 0,0,-1, 1,1,
                 0.5f, 0.5f,-0.5f, 0,0,-1, 0,1,
                // Top
                -0.5f, 0.5f, 0.5f, 0,1,0, 0,0,
                 0.5f, 0.5f, 0.5f, 0,1,0, 1,0,
                 0.5f, 0.5f,-0.5f, 0,1,0, 1,1,
                -0.5f, 0.5f,-0.5f, 0,1,0, 0,1,
                // Bottom
                -0.5f,-0.5f,-0.5f, 0,-1,0, 0,0,
                 0.5f,-0.5f,-0.5f, 0,-1,0, 1,0,
                 0.5f,-0.5f, 0.5f, 0,-1,0, 1,1,
                -0.5f,-0.5f, 0.5f, 0,-1,0, 0,1,
                // Right
                 0.5f,-0.5f, 0.5f, 1,0,0, 0,0,
                 0.5f,-0.5f,-0.5f, 1,0,0, 1,0,
                 0.5f, 0.5f,-0.5f, 1,0,0, 1,1,
                 0.5f, 0.5f, 0.5f, 1,0,0, 0,1,
                // Left
                -0.5f,-0.5f,-0.5f, -1,0,0, 0,0,
                -0.5f,-0.5f, 0.5f, -1,0,0, 1,0,
                -0.5f, 0.5f, 0.5f, -1,0,0, 1,1,
                -0.5f, 0.5f,-0.5f, -1,0,0, 0,1,
            };
            
            uint[] indices = {
                0,1,2, 0,2,3,
                4,5,6, 4,6,7,
                8,9,10, 8,10,11,
                12,13,14, 12,14,15,
                16,17,18, 16,18,19,
                20,21,22, 20,22,23
            };
            
            vao = GL.GenVertexArray();
            vbo = GL.GenBuffer();
            ebo = GL.GenBuffer();
            instanceVBO = GL.GenBuffer();
            
            GL.BindVertexArray(vao);
            
            GL.BindBuffer(BufferTarget.ArrayBuffer, vbo);
            GL.BufferData(BufferTarget.ArrayBuffer, verts.Length * 4, verts, BufferUsageHint.StaticDraw);
            
            GL.BindBuffer(BufferTarget.ElementArrayBuffer, ebo);
            GL.BufferData(BufferTarget.ElementArrayBuffer, indices.Length * 4, indices, BufferUsageHint.StaticDraw);
            
            GL.EnableVertexAttribArray(0);
            GL.VertexAttribPointer(0, 3, VertexAttribPointerType.Float, false, 32, 0);
            
            GL.EnableVertexAttribArray(1);
            GL.VertexAttribPointer(1, 3, VertexAttribPointerType.Float, false, 32, 12);
            
            GL.EnableVertexAttribArray(2);
            GL.VertexAttribPointer(2, 2, VertexAttribPointerType.Float, false, 32, 24);
            
            GL.BindBuffer(BufferTarget.ArrayBuffer, instanceVBO);
            GL.BufferData(BufferTarget.ArrayBuffer, 10000 * 12, IntPtr.Zero, BufferUsageHint.DynamicDraw);
            
            GL.EnableVertexAttribArray(3);
            GL.VertexAttribPointer(3, 3, VertexAttribPointerType.Float, false, 12, 0);
            GL.VertexAttribDivisor(3, 1);
            
            GL.BindVertexArray(0);
        }
        
        private void InitShader()
        {
            string vert = @"#version 330
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNorm;
layout(location=2) in vec2 aUV;
layout(location=3) in vec3 aInstancePos;

uniform mat4 uProjection;
uniform mat4 uView;

out vec3 vNormal;
out vec2 vUV;
out vec3 vWorldPos;

void main() {
    vec3 worldPos = aPos + aInstancePos;
    gl_Position = uProjection * uView * vec4(worldPos, 1.0);
    vNormal = aNorm;
    vUV = aUV;
    vWorldPos = worldPos;
}";
            
            string frag = @"#version 330
in vec3 vNormal;
in vec2 vUV;
in vec3 vWorldPos;

uniform sampler2D uAtlas;
uniform vec3 uLightDir;

out vec4 FragColor;

void main() {
    float light = max(dot(vNormal, normalize(uLightDir)), 0.3);
    vec4 texColor = texture(uAtlas, vUV);
    FragColor = vec4(texColor.rgb * light, texColor.a);
}";
            
            int vs = GL.CreateShader(ShaderType.VertexShader);
            GL.ShaderSource(vs, vert);
            GL.CompileShader(vs);
            
            int fs = GL.CreateShader(ShaderType.FragmentShader);
            GL.ShaderSource(fs, frag);
            GL.CompileShader(fs);
            
            shader = GL.CreateProgram();
            GL.AttachShader(shader, vs);
            GL.AttachShader(shader, fs);
            GL.LinkProgram(shader);
            
            GL.DeleteShader(vs);
            GL.DeleteShader(fs);
        }
        
        public void Render(Dictionary<Vector3i, VoxelCube> cubes, Matrix4 proj, Matrix4 view, int atlas)
        {
            if (cubes.Count == 0) return;
            
            float[] positions = new float[cubes.Count * 3];
            int i = 0;
            foreach (var cube in cubes.Values)
            {
                positions[i++] = cube.Position.X;
                positions[i++] = cube.Position.Y;
                positions[i++] = cube.Position.Z;
            }
            
            GL.BindBuffer(BufferTarget.ArrayBuffer, instanceVBO);
            GL.BufferSubData(BufferTarget.ArrayBuffer, IntPtr.Zero, positions.Length * 4, positions);
            
            GL.UseProgram(shader);
            GL.UniformMatrix4(GL.GetUniformLocation(shader, "uProjection"), false, ref proj);
            GL.UniformMatrix4(GL.GetUniformLocation(shader, "uView"), false, ref view);
            GL.Uniform3(GL.GetUniformLocation(shader, "uLightDir"), new Vector3(0.5f, 1f, 0.3f).Normalized());
            
            GL.ActiveTexture(TextureUnit.Texture0);
            GL.BindTexture(TextureTarget.Texture2D, atlas);
            GL.Uniform1(GL.GetUniformLocation(shader, "uAtlas"), 0);
            
            GL.BindVertexArray(vao);
            GL.DrawElementsInstanced(PrimitiveType.Triangles, 36, DrawElementsType.UnsignedInt, IntPtr.Zero, cubes.Count);
            GL.BindVertexArray(0);
        }
        
        public void Dispose()
        {
            GL.DeleteVertexArray(vao);
            GL.DeleteBuffer(vbo);
            GL.DeleteBuffer(ebo);
            GL.DeleteBuffer(instanceVBO);
            GL.DeleteProgram(shader);
        }
    }
}
